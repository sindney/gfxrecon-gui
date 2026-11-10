#include "platform/adb.h"
#include "core/artifacts.h"
#include "core/log.h"

#include <stdexcept>
#include <sstream>
#include <thread>
#include <fstream>
#include <set>
#include <array>
#include <charconv>
#include <algorithm>
#include <mutex>
#include <map>

namespace gfx {
namespace {
std::string Checked(const ProcessResult& result, const std::string& operation) {
    if (result.cancelled) throw std::runtime_error("Operation cancelled.");
    if (result.timedOut) throw std::runtime_error(operation + " timed out. Check the selected device connection.");
    if (!result.Success()) throw std::runtime_error(operation + " failed:\n" + result.output);
    return result.output;
}
}
std::string QuoteAndroidArgument(const std::string& text) {
    if (text.find('\0') != std::string::npos) throw std::runtime_error("NUL in Android shell argument");
    std::string quoted = "'";
    for (char c : text) quoted += c == '\'' ? "'\"'\"'" : std::string(1, c);
    return quoted + "'";
}
std::string AndroidCommand(const std::vector<std::string>& words) {
    std::string command;
    for (const auto& word : words) { if (!command.empty()) command += ' '; command += QuoteAndroidArgument(word); }
    return command;
}
ProcessResult AdbConnection::Run(std::vector<std::string> args, const std::filesystem::path& input,
                                const std::filesystem::path& output, bool cleanup, std::chrono::milliseconds timeout) const {
    if (serial.empty()) throw std::runtime_error("Select an Android device first.");
    args.insert(args.begin(), {"-s", serial});
    ProcessRequest r{executable, std::move(args)};
    r.allowChildServices = true; // The shared host ADB server is not this client's child workload.
    r.timeout = std::chrono::seconds(cleanup ? 5 : 30);
    if (!cleanup && timeout.count() > 0) r.timeout = timeout;
    r.stdinFile = input; r.stdoutFile = output;
    return transport ? transport(r, cleanup ? nullptr : cancel) : RunProcess(r, cleanup ? nullptr : cancel);
}
ProcessResult AdbConnection::TryShell(const std::vector<std::string>& words, bool cleanup) const {
    return Run({"shell", AndroidCommand(words)}, {}, {}, cleanup);
}
std::string AdbConnection::Shell(const std::vector<std::string>& words, bool cleanup) const {
    return Checked(TryShell(words, cleanup), "ADB command on " + serial);
}
void AdbConnection::Put(const std::string& package, const std::filesystem::path& source, const std::string& remote) const {
    // exec-in can return while Android's cat is still draining buffered stdin.
    // Stage uniquely, acknowledge verified bytes on the receiver, then publish. A failed
    // or cancelled transfer cannot overwrite a later upload to the same path.
    const auto token=Sha256Text(remote+source.u8string()+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())).substr(0,24);
    const auto pending=remote+".upload-"+token,ack=pending+".ack",ackPending=ack+".partial";
    const auto operation="Artifact upload to "+serial;
    const auto bytes=std::filesystem::file_size(source);
    const auto digest=Sha256File(source,cancel);
    try {
        // exec-in can end stdin with EIO rather than clean EOF. Read the known
        // byte count, then validate content instead of relying on cat's status.
        const auto receive="if head -c "+std::to_string(bytes)+" > "+QuoteAndroidArgument(pending)+
            " && upload_checksum=$(sha256sum "+QuoteAndroidArgument(pending)+") && test \"${upload_checksum%% *}\" = "+QuoteAndroidArgument(digest)+
            "; then printf '%s' "+QuoteAndroidArgument(token)+" > "+QuoteAndroidArgument(ackPending)+
            "; else printf 'failed' > "+QuoteAndroidArgument(ackPending)+"; fi; mv "+QuoteAndroidArgument(ackPending)+" "+QuoteAndroidArgument(ack);
        Checked(Run({"exec-in",AndroidCommand({"run-as",package,"sh","-c",receive})},source),operation);
        const auto publish="set -e; if test -f "+QuoteAndroidArgument(ack)+"; then upload_status=$(cat "+QuoteAndroidArgument(ack)+
            "); test \"$upload_status\" = "+QuoteAndroidArgument(token)+" || { echo 'Upload receiver failed' >&2; exit 1; }; mv "+
            QuoteAndroidArgument(pending)+" "+QuoteAndroidArgument(remote)+"; rm -f "+QuoteAndroidArgument(ack)+"; printf '%s' "+QuoteAndroidArgument(token)+"; fi";
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(std::chrono::steady_clock::now()<deadline){
            if(cancel&&cancel->load())throw std::runtime_error("Operation cancelled.");
            const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());
            if(remaining.count()<=0)break;
            const auto result=Run({"shell",AndroidCommand({"run-as",package,"sh","-c",publish})},{},{},false,remaining);
            const auto output=Checked(result,operation);
            if(output==token)return;
            if(!output.empty())throw std::runtime_error(operation+" returned an unexpected receiver acknowledgement");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        throw std::runtime_error(operation+" timed out waiting for the receiver to finish writing; the uploaded file was not published");
    }catch(...){
        try{Shell({"run-as",package,"rm","-f",pending,ack,ackPending},true);}catch(...){}
        throw;
    }
}
void AdbConnection::Get(const std::string& package, const std::string& source, const std::filesystem::path& destination) const {
    auto temporary = destination; temporary += ".partial";
    Checked(Run({"exec-out", AndroidCommand({"run-as", package, "cat", source})}, {}, temporary), "Artifact retrieval from " + serial);
    if (!std::filesystem::is_regular_file(temporary) || !std::filesystem::file_size(temporary))
        throw std::runtime_error("Android produced an empty artifact: " + source);
    std::filesystem::rename(temporary, destination);
}
void AdbConnection::Stop(const std::string& package) const {
    Run({"shell", AndroidCommand({"am", "force-stop", package})}, {}, {}, true);
}
std::string AdbConnection::PrepareArtifactTransport(const std::filesystem::path& helper) const {
    if(helper.empty()||!std::filesystem::is_regular_file(helper))return {};
    std::string pending;
    try{
        const auto hash=CachedSha256File(helper,cancel);
        std::ifstream metadata(std::filesystem::u8path(helper.u8string()+".capabilities.json"));
        const auto capability=nlohmann::json::parse(metadata);
        if(capability.value("artifact_transport_protocol",0)!=1||capability.value("binary_sha256","")!=hash||capability.value("abi","")!="arm64-v8a")return {};
        const auto remote="/data/local/tmp/gfxrecon-artifacts-"+hash;
        const auto key=executable+"\n"+serial+"\n"+hash;
        static std::mutex mutex;static std::map<std::string,std::string> ready;
        {std::lock_guard<std::mutex> lock(mutex);const auto found=ready.find(key);if(found!=ready.end())return found->second;}
        const auto check=Shell({"sh","-c","if test \"$(getprop ro.product.cpu.abi)\" != arm64-v8a; then printf unsupported; elif test -x "+QuoteAndroidArgument(remote)+"; then if "+QuoteAndroidArgument(remote)+" --version >/dev/null 2>&1; then sha256sum "+QuoteAndroidArgument(remote)+"; else printf unsupported; fi; fi"});
        if(check=="unsupported"){std::lock_guard<std::mutex> lock(mutex);ready[key]={};return {};}
        if(check.rfind(hash,0)!=0){
            pending=remote+".partial-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            Checked(Run({"push",helper.u8string(),pending}),"Artifact transport helper upload");
            Shell({"sh","-c","set -e; checksum=$(sha256sum "+QuoteAndroidArgument(pending)+"); test \"${checksum%% *}\" = "+QuoteAndroidArgument(hash)+"; chmod 700 "+QuoteAndroidArgument(pending)+"; mv "+QuoteAndroidArgument(pending)+" "+QuoteAndroidArgument(remote)+"; "+QuoteAndroidArgument(remote)+" --version"});
            pending.clear();
        }
        {std::lock_guard<std::mutex> lock(mutex);if(ready.size()>=64)ready.erase(ready.begin());ready[key]=remote;}
        return remote;
    }catch(const std::exception& error){
        if(!pending.empty())try{TryShell({"rm","-f",pending},true);}catch(...){}
        if(cancel&&cancel->load())throw;
        LOGW("transfer")<<"Using raw artifact transfer: "<<error.what();return {};
    }
}
size_t AdbConnection::GetReplayFiles(const std::string& package, const std::string& directory,
                                    const std::filesystem::path& destination, bool buffers,
                                    std::chrono::milliseconds timeout, const std::string& helper,
                                    ArtifactTransferStats* stats) const {
    const auto staging=destination/"replay-artifacts.staging";
    if(!std::filesystem::create_directory(staging))throw std::runtime_error("Artifact staging directory already exists");
    struct Remove { std::filesystem::path file; ~Remove() { std::error_code error; std::filesystem::remove_all(file, error); } } remove{staging};
    const auto bundle=staging/"wire.partial",decoded=staging/"decoded.partial";
    // NUL-delimited names and lengths frame the binary payloads. The receiver
    // validates every basename and length before opening a destination file.
    const auto script = std::string("set -e; set -o pipefail; printf 'GFXRECON_ARTIFACTS_1\\000'; find ") + QuoteAndroidArgument(directory) +
        " -type f | while IFS= read -r file; do name=${file##*/}; case \"$name\" in dump-request.json) continue;; " +
        "*.bmp|*.png|*.json" + (buffers ? "|*.bin" : "") + ") ;; *) continue;; esac; "
        "size=$(stat -c %s \"$file\"); printf '%s\\000%s\\000' \"$name\" \"$size\"; cat \"$file\"; done; printf '\\000'";
    const auto producer=AndroidCommand({"run-as",package,"sh","-c",script});
    const auto command=helper.empty()?producer:AndroidCommand({"sh","-c","set -o pipefail; if test -x "+QuoteAndroidArgument(helper)+"; then "+producer+" | "+QuoteAndroidArgument(helper)+"; else "+producer+"; fi"});
    Checked(Run({"exec-out",command},{},bundle,false,timeout),"Replay artifact retrieval from "+serial);
    ArtifactTransferStats transfer;
    const auto compressed=DecodeArtifactTransfer(bundle,decoded,transfer,cancel);
    const auto content=compressed?decoded:bundle;
    std::ifstream input(content, std::ios::binary);
    const auto total = std::filesystem::file_size(content);
    auto field = [&]() {
        std::string value; char c;
        while (input.get(c)) { if (!c) return value; if (value.size() >= 255) throw std::runtime_error("Android artifact header is too long"); value += c; }
        throw std::runtime_error("Android artifact transfer was truncated");
    };
    if (field() != "GFXRECON_ARTIFACTS_1") throw std::runtime_error("Invalid Android artifact transfer header");
    std::set<std::string> received;
    std::array<char, 65536> bytes;
    for (;;) {
        if (cancel && cancel->load()) throw std::runtime_error("Artifact retrieval cancelled");
        const auto name = field();
        if (name.empty()) { if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Unexpected data after Android artifact transfer"); break; }
        const auto file = std::filesystem::u8path(name); const auto extension = file.extension().u8string();
        if (name == "." || name == ".." || name.find_first_of("/\\\r\n:") != std::string::npos || file.is_absolute() ||
            (extension != ".bmp" && extension != ".png" && extension != ".json" && !(buffers && extension == ".bin")) || !received.insert(name).second)
            throw std::runtime_error("Android returned an invalid or duplicate artifact name");
        const auto count = field(); uint64_t size = 0;
        const auto parsed = std::from_chars(count.data(), count.data() + count.size(), size);
        if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() || !size ||
            uint64_t(input.tellg()) > total || size > total - uint64_t(input.tellg()))
            throw std::runtime_error("Invalid or truncated Android artifact payload");
        const auto staged = staging / name;
        std::ofstream output(staged, std::ios::binary);
        for (uint64_t remaining = size; remaining;) {
            if (cancel && cancel->load()) throw std::runtime_error("Artifact retrieval cancelled");
            const auto chunk = static_cast<std::streamsize>(std::min<uint64_t>(remaining, bytes.size()));
            if (!input.read(bytes.data(), chunk) || !output.write(bytes.data(), chunk)) throw std::runtime_error("Cannot read or save Android artifact");
            remaining -= static_cast<uint64_t>(chunk);
        }
        output.close(); if (!output) throw std::runtime_error("Cannot save Android artifact");
    }
    // Publish only after the whole envelope and all artifact records validate.
    for(const auto& name:received){
        if(cancel&&cancel->load())throw std::runtime_error("Artifact retrieval cancelled");
        const auto completed=destination/std::filesystem::u8path(name);
        if(std::filesystem::exists(completed))std::filesystem::remove(completed);
        std::filesystem::rename(staging/std::filesystem::u8path(name),completed);
    }
    if(stats)*stats=transfer;
    return received.size();
}
AndroidUnlockRequired::AndroidUnlockRequired(AndroidLockState value,const std::string& detail)
    :std::runtime_error((value.known?"Android device "+value.serial+" is locked.":"Cannot determine the lock state of Android device "+value.serial+".")+" Unlock the phone, then retry replay."+(detail.empty()?"":"\n"+detail)),state(std::move(value)){}
AndroidLockState ParseAndroidLockState(const std::string& policy,const std::string& serial){
    AndroidLockState state;state.serial=serial;
    const auto marker=policy.find("KeyguardServiceDelegate");
    std::istringstream input(marker==std::string::npos?policy:policy.substr(marker));std::string line;
    bool primaryShowing=false;
    while(std::getline(input,line)){
        const auto first=line.find_first_not_of(" \t\r");if(first==std::string::npos)continue;line.erase(0,first);
        const auto separator=line.find('=');if(separator==std::string::npos)continue;
        const auto key=line.substr(0,separator);auto value=line.substr(separator+1);const auto end=value.find_first_of(" \t\r,");if(end!=std::string::npos)value.resize(end);
        if(value!="true"&&value!="false")continue;const bool flag=value=="true";
        if(key=="showing"&&!primaryShowing){state.known=true;state.locked=flag;primaryShowing=true;}
        else if(key=="mIsShowing"&&!primaryShowing){state.known=true;state.locked=flag;}
        // Only the delegate's actual 'secure' field authorizes automatic dismissal.
        // SIM security, input restriction and unrelated window flags are insufficient.
        if(marker!=std::string::npos&&key=="secure"&&!state.secureKnown){state.secureKnown=true;state.secure=flag;}
    }return state;
}
AndroidLockState EnsureAndroidReplayReady(const AdbConnection& adb){
    auto read=[&]{return ParseAndroidLockState(adb.Shell({"dumpsys","window","policy"}),adb.serial);};
    auto state=read();
    if(state.known&&!state.locked)return state;
    if(!state.known||!state.secureKnown||state.secure)throw AndroidUnlockRequired(state,state.secureKnown&&state.secure?"This device requires a secure unlock.":"Automatic dismissal is unavailable until an unsecured lock screen can be confirmed.");
    state.dismissAttempted=true;
    const auto wake=adb.TryShell({"input","keyevent","KEYCODE_WAKEUP"});
    if(wake.cancelled)throw std::runtime_error("Replay cancelled.");
    if(!wake.Success())throw AndroidUnlockRequired(state,"Could not wake the phone automatically.");
    const auto dismiss=adb.TryShell({"wm","dismiss-keyguard"});
    if(dismiss.cancelled)throw std::runtime_error("Replay cancelled.");
    if(!dismiss.Success())throw AndroidUnlockRequired(state,"The phone did not accept automatic lock-screen dismissal.");
    for(int attempt=0;attempt<6;++attempt){
        if(adb.cancel&&adb.cancel->load())throw std::runtime_error("Replay cancelled.");
        auto checked=read();checked.dismissAttempted=true;
        if(checked.known&&!checked.locked)return checked;
        state=checked;
        if(!checked.known||(checked.secureKnown&&checked.secure))break;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    throw AndroidUnlockRequired(state,"Automatic dismissal was attempted once; unlock the phone manually to continue.");
}
} // namespace gfx
