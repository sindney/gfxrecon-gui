#include "platform/adb.h"

#include <stdexcept>
#include <sstream>
#include <thread>

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
                                const std::filesystem::path& output, bool cleanup) const {
    if (serial.empty()) throw std::runtime_error("Select an Android device first.");
    args.insert(args.begin(), {"-s", serial});
    ProcessRequest r{executable, std::move(args)};
    r.timeout = std::chrono::seconds(cleanup ? 5 : 30);
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
    Checked(Run({"exec-in", AndroidCommand({"run-as", package, "sh", "-c", "cat > " + QuoteAndroidArgument(remote)})}, source),
            "Artifact upload to " + serial);
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
