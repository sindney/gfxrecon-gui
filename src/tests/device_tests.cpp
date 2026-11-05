#include "capture/capture.h"
#include "core/artifacts.h"
#include "platform/android_display.h"
#include "replay/android_replay_failure.h"
#include <fstream>
#include <map>
#include <stdexcept>
#include <chrono>
#include <future>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gfx::tests {
namespace {
void Require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
Json Load(const std::filesystem::path& file) { std::ifstream in(file); return Json::parse(in); }
std::vector<std::string> ShellWords(const std::string& text) {
    std::vector<std::string> words; std::string word; char quote = 0; bool active = false;
    for (char c : text) {
        if (quote) { if (c == quote) quote = 0; else word += c; }
        else if (c == '\'' || c == '"') { quote = c; active = true; }
        else if (c == ' ') { if (active) { words.push_back(word); word.clear(); active = false; } }
        else { word += c; active = true; }
    }
    Require(!quote, "Unbalanced remote quoting"); if (active) words.push_back(word); return words;
}
struct Phone {
    std::map<std::string, std::string> settings{{"gpu_debug_app", "prior.app"}, {"gpu_debug_layers", "prior layer"}};
    std::map<std::string, std::string> properties{{"debug.gfxrecon.capture_frames", "12-14"}};
    std::string relative;
    std::filesystem::path output;
    bool offline = false, interruptTransfer = false, conflict = false;
    std::atomic_bool* cancelOnLaunch = nullptr;
    unsigned mutations = 0, stops = 0;
    ProcessResult Run(const ProcessRequest& request, const std::atomic_bool* cancel) {
        Require(request.arguments.size() >= 4 && request.arguments[0] == "-s" && request.arguments[1] == "phone-a", "ADB request escaped the selected device");
        if (cancel && cancel->load()) return {-1, "cancelled", true};
        if (offline) return {1, "device offline"};
        const auto& type = request.arguments[2]; const auto words = ShellWords(request.arguments[3]);
        if (type == "exec-out") {
            { std::ofstream data(request.stdoutFile, std::ios::binary); data << std::string(128, 'x'); }
            if (interruptTransfer) { offline = true; return {1, "device disconnected during transfer"}; }
            if (conflict) settings["gpu_debug_app"] = "outside.app";
            return {0};
        }
        Require(type == "shell" && !words.empty(), "Unexpected transport operation");
        if (words[0] == "dumpsys") return {0, "showing=false"};
        if (words[0] == "pm") return {0, "package:/data/app/replay.apk"};
        if (words[0] == "settings" || words[0] == "setprop") {
            const auto journal = output / "device-journal.json";
            if (words[0] == "setprop" || words[1] != "get") {
                Require(std::filesystem::is_regular_file(journal), "Device mutation preceded durable recovery journal");
                Require(Load(journal)["changes"].size() == 12, "Journal did not snapshot all changes before mutation");
                ++mutations;
            }
            if (words[0] == "setprop") properties[words[1]] = words[2];
            else if (words[1] == "get") return {0, (settings.count(words[3]) ? settings.at(words[3]) : "null") + "\n"};
            else if (words[1] == "put") settings[words[3]] = words[4];
            else if (words[1] == "delete") settings.erase(words[3]);
            return {0};
        }
        if (words[0] == "getprop") return {0, properties[words[1]] + "\n"};
        if (words[0] == "cmd") return {0, "test.app/Activity\n"};
        if (words[0] == "am") {
            if (words[1] == "force-stop") ++stops;
            if (words[1] == "start" && cancelOnLaunch) cancelOnLaunch->store(true);
            return {0, "Success"};
        }
        if (words[0] == "run-as") {
            if (words[2] == "test") return {1};
            if (words[2] == "mkdir") relative = words[4];
            if (words[2] == "cat") return {0, "Finished recording graphics API capture"};
            if (words[2] == "find") return {0, relative + "/c_frames_1_through_3.gfxr\n"};
            return {0};
        }
        throw std::runtime_error("Unexpected fake-device command");
    }
};
}
void RunDeviceTests(const std::filesystem::path& root) {
    // Exercise the real framing/parser with an injected transport: binary data,
    // malformed headers, path traversal, duplicate files and cancellation.
    const auto downloads = root / "batched-downloads";
    std::filesystem::create_directories(downloads);
    auto field = [](const std::string& value) { return value + '\0'; };
    const auto header = field("GFXRECON_ARTIFACTS_1");
    const std::string payload("a\0b\n", 4);
    for (int scenario = 0; scenario < 9; ++scenario) {
        std::atomic_bool cancelled{false}; unsigned requests = 0;
        auto bundle = header + field("image.png") + field("4") + payload + field("");
        if (scenario == 1) bundle.pop_back();
        if (scenario == 2) bundle = header + field("../escape.png") + field("4") + payload + field("");
        if (scenario == 3) bundle = header + field("image.png") + field("99") + payload + field("");
        if (scenario == 4) bundle = header + field("image.png") + field("4") + payload + field("image.png") + field("4") + payload + field("");
        if (scenario == 5) bundle = header + field("image.png") + field("-1") + payload + field("");
        if (scenario == 6) bundle += "trailing";
        if (scenario == 7) bundle = header + field("image.bin") + field("4") + payload + field("");
        AdbConnection adb{"fake-adb", "phone-a", &cancelled, [&](const ProcessRequest& request, const std::atomic_bool*) {
            ++requests;
            Require(request.arguments[2] == "exec-out" && request.timeout == std::chrono::seconds(91), "Batch retrieval lost its timeout or transport");
            { std::ofstream output(request.stdoutFile, std::ios::binary); output.write(bundle.data(), bundle.size()); }
            if (scenario == 8) cancelled = true;
            return ProcessResult{0};
        }};
        bool failed = false;
        try {
            Require(adb.GetReplayFiles("test.pkg", "files/replay", downloads, false, std::chrono::seconds(91)) == 1, "Binary batch lost an artifact");
            std::ifstream input(downloads / "image.png", std::ios::binary);
            Require(std::string(std::istreambuf_iterator<char>(input), {}) == payload, "Binary batch changed payload bytes");
        } catch (...) { failed = true; }
        Require(failed == (scenario != 0) && requests == 1, "Malformed batch was accepted or used multiple ADB calls");
        Require(!std::filesystem::exists(downloads / "replay-artifacts.staging"), "Failed batch left partial files");
        if(scenario)Require(!std::filesystem::exists(downloads/"image.png"),"Invalid batch published an earlier file");
        std::filesystem::remove_all(downloads); std::filesystem::create_directories(downloads);
    }

    const auto source=root/"codec-input.bin",encoded=root/"codec-wire.bin";
    std::string binary(300000,'\0');for(size_t i=0;i<binary.size();++i)binary[i]=char((i*47+(i>>3))%256);
    const auto raw=header+field("image.bin")+field(std::to_string(binary.size()))+binary+field("meta.json")+field("2")+"{}"+field("");
    auto encode=[&](const std::string& data){
        {std::ofstream file(source,std::ios::binary);file.write(data.data(),data.size());}
        ProcessRequest request{GFXRECON_ARTIFACT_TRANSPORT_TEST,{}};request.stdinFile=source;request.stdoutFile=encoded;
        Require(RunProcess(request).Success(),"Native artifact encoder failed");
        std::ifstream input(encoded,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});
    };
    const auto envelope=encode(raw);
    std::vector<std::string> fields;size_t offset=0;
    for(int i=0;i<5;++i){const auto end=envelope.find('\0',offset);Require(end!=std::string::npos,"Encoder omitted its checksum header");fields.push_back(envelope.substr(offset,end-offset));offset=end+1;}
    Require(fields[0]=="GFXRECON_TRANSFER_1"&&fields[1]=="lz4"&&fields[2]==std::to_string(raw.size())&&fields[4].size()==16,"Encoder header is not checksum-first");
    const auto body=envelope.substr(offset);
    auto wrap=[&](std::vector<std::string> f,const std::string& payload){std::string result;for(const auto& value:f)result+=field(value);return result+payload;};
    std::vector<std::string> corrupt;
    auto changed=fields;changed[4][0]=changed[4][0]=='0'?'1':'0';corrupt.push_back(wrap(changed,body));
    changed=fields;changed[2]=std::to_string(raw.size()+1);corrupt.push_back(wrap(changed,body));
    changed=fields;changed[3]=std::to_string(body.size()+1);corrupt.push_back(wrap(changed,body));
    auto damage=body;damage[damage.size()/2]^=0x55;corrupt.push_back(wrap(fields,damage));
    corrupt.push_back(envelope.substr(0,envelope.size()-1));corrupt.push_back(envelope+"trailing");
    changed=fields;changed[3]=std::to_string(body.size()-1);corrupt.push_back(wrap(changed,body.substr(0,body.size()-1)));
    changed=fields;changed[3]=std::to_string(body.size()*2);corrupt.push_back(wrap(changed,body+body));
    changed=fields;changed[4]="not-a-content-hash";corrupt.push_back(wrap(changed,body));
    changed=fields;changed[2]="9999999999999";corrupt.push_back(wrap(changed,body));
    changed=fields;changed[2]="-1";corrupt.push_back(wrap(changed,body));
    changed=fields;changed[1]="unknown";corrupt.push_back(wrap(changed,body));
    corrupt.push_back(envelope.substr(0,12));
    // A checksum-valid envelope with an invalid later record must not publish
    // the earlier valid file, either.
    corrupt.push_back(encode(raw.substr(0,raw.size()-1)+field("../escape.png")+field("1")+"x"+field("")));
    corrupt.insert(corrupt.begin(),envelope);corrupt.push_back(envelope); // last case cancels after download
    for(size_t scenario=0;scenario<corrupt.size();++scenario){
        std::filesystem::remove_all(downloads);std::filesystem::create_directory(downloads);
        {std::ofstream(downloads/"image.bin",std::ios::binary)<<"previous";}
        std::atomic_bool cancel{false};
        AdbConnection adb{"fake-adb","phone-a",&cancel,[&](const ProcessRequest& request,const std::atomic_bool*){
            {std::ofstream file(request.stdoutFile,std::ios::binary);file.write(corrupt[scenario].data(),corrupt[scenario].size());}
            if(scenario+1==corrupt.size())cancel=true;return ProcessResult{0};
        }};
        bool failed=false;ArtifactTransferStats stats;
        try{Require(adb.GetReplayFiles("pkg","files/replay",downloads,true,std::chrono::seconds(30),"/helper",&stats)==2,"Compressed batch lost files");}catch(...){failed=true;}
        Require(failed==(scenario!=0),"Corrupt, truncated or cancelled compressed data was accepted");
        std::ifstream image(downloads/"image.bin",std::ios::binary);const std::string saved(std::istreambuf_iterator<char>(image),{});
        Require(saved==(scenario==0?binary:"previous"),"Invalid transfer replaced a completed artifact");
        Require(!std::filesystem::exists(downloads/"replay-artifacts.staging"),"Transfer left its private staging data");
        if(!scenario)Require(stats.codec=="lz4"&&stats.integrity=="xxh64"&&stats.rawBytes==raw.size()&&stats.wireBytes==envelope.size(),"Verified transfer metrics are wrong");
    }
    for(int scenario=0;scenario<5;++scenario){
        const auto helper=root/("helper-"+std::to_string(scenario));{std::ofstream(helper)<<"fixture";}
        const auto hash=Sha256File(helper);
        AtomicJson(std::filesystem::u8path(helper.u8string()+".capabilities.json"),{{"artifact_transport_protocol",1},{"binary_sha256",scenario==3?"wrong":hash},{"abi","arm64-v8a"}});
        unsigned requests=0,pushes=0;std::atomic_bool cancelled{false};
        AdbConnection adb{"fake-adb","transfer-phone-"+std::to_string(scenario),&cancelled,[&](const ProcessRequest& request,const std::atomic_bool*){
            ++requests;if(request.arguments[2]=="push"){++pushes;return ProcessResult{0};}
            const auto& command=request.arguments[3];
            if(command.find("getprop")!=std::string::npos){if(scenario==4){cancelled=true;return ProcessResult{-1,"cancelled",true};}return ProcessResult{0,scenario==2?"unsupported":""};}
            if(scenario==1&&command.find("checksum=")!=std::string::npos)return ProcessResult{1,"checksum mismatch"};
            return ProcessResult{0};
        }};
        bool threw=false;std::string remote;try{remote=adb.PrepareArtifactTransport(helper);}catch(...){threw=true;}
        Require(threw==(scenario==4)&&(!remote.empty())==(scenario==0),"Helper setup did not honor fallback or cancellation");
        if(scenario==0){const auto previous=requests;Require(adb.PrepareArtifactTransport(helper)==remote&&requests==previous&&pushes==1,"Warm helper setup performed another ADB round trip");}
        if(scenario==2)Require(pushes==0,"Unsupported ABI received a native helper");
        if(scenario==3)Require(requests==0,"Unverified helper bytes were uploaded");
        std::filesystem::remove(helper);std::filesystem::remove(std::filesystem::u8path(helper.u8string()+".capabilities.json"));
    }
    std::filesystem::remove(source);std::filesystem::remove(encoded);
    std::filesystem::remove_all(downloads);

#ifdef _WIN32
    {
        const auto directory=root/"atomic-json-replacement";std::filesystem::create_directory(directory);
        const auto file=directory/"state.json";AtomicJson(file,{{"generation",1}});
        struct ReadLock{
            HANDLE value;
            explicit ReadLock(const std::filesystem::path& path):value(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)){
                Require(value!=INVALID_HANDLE_VALUE,"Cannot create the Windows replacement-lock fixture");
            }
            void Close(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}
            ~ReadLock(){Close();}
        };
        {
            ReadLock lock(file);std::promise<void> started;auto ready=started.get_future();
            auto writer=std::async(std::launch::async,[&]{started.set_value();AtomicJson(file,{{"generation",2}});});
            ready.wait();std::this_thread::sleep_for(std::chrono::milliseconds(25));
            Require(writer.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready,"Replacement did not wait for the Windows reader lock");
            Require(Load(file)["generation"]==1,"Retry discarded or changed the original artifact while locked");
            lock.Close();writer.get();Require(Load(file)["generation"]==2,"Replacement did not recover after the reader released its lock");
        }
        {
            ReadLock lock(file);bool failed=false;const auto start=std::chrono::steady_clock::now();
            try{AtomicJson(file,{{"generation",3}});}catch(const std::filesystem::filesystem_error&){failed=true;}
            Require(failed,"A permanent Windows replacement lock was silently ignored");
            Require(std::chrono::steady_clock::now()-start<std::chrono::seconds(2),"Replacement retries are not bounded");
            Require(Load(file)["generation"]==2,"A failed replacement damaged the prior artifact");
        }
        Require(std::distance(std::filesystem::directory_iterator(directory),std::filesystem::directory_iterator{})==1,"Failed replacement leaked its private staged artifact");
        std::filesystem::remove(file);std::filesystem::remove(directory);
    }
#endif
    {
        const std::string pkg = "com.lunarg.gfxreconstruct.replay";
        const std::string exit = "ApplicationExitInfo #0:\n timestamp=2026-11-03 16:24:16.001 pid=25112 reason=5 (APP CRASH(NATIVE))\n";
        const std::string crash = "*** *** ***\nTimestamp: 2026-11-03 16:24:15.999+0800\nCmdline: " + pkg +
            "\npid: 25112, tid: 29077\nAbort message: '[gfxrecon] FATAL - vkAllocateMemory: VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS'\n";
        Require(AndroidNativeCrashReason(exit, crash, pkg).find("VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS") != std::string::npos, "Matching native abort was lost");
        auto stale = crash; stale.replace(stale.find("16:24:15"), 8, "15:24:15");
        Require(AndroidNativeCrashReason(exit, stale, pkg).empty(), "Stale reused-PID crash was attributed to current replay");
        auto foreign = crash; foreign.replace(foreign.find(pkg), pkg.size(), "another.package");
        Require(AndroidNativeCrashReason(exit, foreign, pkg).empty(), "Foreign-package crash was attributed to replay");
        auto otherPid = crash; otherPid.replace(otherPid.find("pid: 25112"), 10, "pid: 25113");
        Require(AndroidNativeCrashReason(exit, otherPid, pkg).empty(), "Foreign-PID crash was attributed to replay");
        auto memoryExit = exit; memoryExit.replace(memoryExit.find("reason=5"), 8, "reason=3");
        Require(AndroidNativeCrashReason(memoryExit, crash, pkg).empty(), "Old native crash replaced a non-native exit reason");
    }
    for(int displayScenario=0;displayScenario<8;++displayScenario){
        const auto journal=root/("display-"+std::to_string(displayScenario)+".json");
        bool panelOff=displayScenario==1,offline=false,rejectReset=displayScenario==7;unsigned offCommands=0,resets=0;std::atomic_bool cancelled{false};
        AdbConnection connection{"fake-adb","phone-a",&cancelled,[&](const ProcessRequest& request,const std::atomic_bool* token){
            Require(request.allowChildServices,"ADB transport must preserve its shared host server");
            Require(request.arguments[1]=="phone-a","Display operation escaped its serial");
            if(offline)return ProcessResult{1,"offline"};if(token&&token->load())return ProcessResult{-1,"cancelled",true};
            const auto words=ShellWords(request.arguments[3]);
            if(words==std::vector<std::string>{"cmd","display","help"})return ProcessResult{1,displayScenario==6?"unsupported":"power-off DISPLAY_ID\npower-reset DISPLAY_ID"};
            if(words==std::vector<std::string>{"cmd","display","get-displays"})return ProcessResult{0,"Displays:\nDisplay id 0: uniqueId \"local:123\""};
            if(words==std::vector<std::string>{"dumpsys","display"})return ProcessResult{0,std::string("mPhysicalDisplayId=123\nmCommittedState=")+(panelOff?"OFF":"ON")};
            if(words==std::vector<std::string>{"dumpsys","window","policy"})return ProcessResult{0,"KeyguardServiceDelegate\n screenState=SCREEN_STATE_ON"};
            if(words==std::vector<std::string>{"cmd","display","power-off","0"}){Require(std::filesystem::is_regular_file(journal)&&!Load(journal).value("restored",true),"Panel changed before recovery journal");++offCommands;panelOff=true;return ProcessResult{0};}
            if(words==std::vector<std::string>{"cmd","display","power-reset","0"}){Require(!token,"Display restoration obeyed cancelled work instead of cleanup");++resets;if(!rejectReset)panelOff=false;return ProcessResult{0};}
            throw std::runtime_error("Unexpected display command");
        }};
        try{
            AndroidReplayDisplay display(connection,journal);display.TurnOff();Require(panelOff,"Display was not powered off");display.Observe();Require(display.OffObservations()>=2,"No physical-off observations");
            if(displayScenario==2)throw std::runtime_error("injected replay failure");
            if(displayScenario==3)cancelled=true;
            if(displayScenario==4)offline=true;
            if(displayScenario==5)panelOff=false;
        }catch(const std::runtime_error& error){if(displayScenario!=2&&displayScenario!=6)throw std::runtime_error("Unexpected display setup failure in scenario "+std::to_string(displayScenario)+": "+error.what());}
        if(displayScenario==6){Require(offCommands==0&&!std::filesystem::exists(journal),"Unsupported device was modified");continue;}
        if(displayScenario==4||displayScenario==7){Require(!Load(journal).value("restored",true),"Failed panel restoration was reported complete");offline=false;rejectReset=false;RestoreAndroidReplayDisplay(connection,journal);}
        Require(Load(journal).value("restored",false),"Display recovery did not complete");
        Require(panelOff==(displayScenario==1),"Initial or user-selected panel state was lost");
        Require(resets==(displayScenario==1||displayScenario==5?0u:displayScenario==7?2u:1u),"Panel reset overrode a preexisting OFF state or user intervention");
        std::filesystem::remove(journal);
    }
    const auto securePolicy=ParseAndroidLockState("KeyguardServiceDelegate\n showing=true\n secure=true\n mIsShowing=false\n mSimSecure=false","phone-a");
    Require(securePolicy.known&&securePolicy.locked&&securePolicy.secureKnown&&securePolicy.secure,"Nested status overrode the primary secure keyguard");
    Require(!ParseAndroidLockState("showing=true\n secure=false","phone-a").secureKnown,"Unscoped security field authorized dismissal");
    for(int lockScenario=0;lockScenario<8;++lockScenario){
        bool locked=lockScenario!=0;unsigned wakes=0,dismissals=0;std::atomic_bool cancelled{false};
        AdbConnection connection{"fake-adb","phone-a",&cancelled,[&](const ProcessRequest& request,const std::atomic_bool* token){
            Require(request.arguments[0]=="-s"&&request.arguments[1]=="phone-a","Readiness used the wrong device");
            if(token&&token->load())return ProcessResult{-1,"cancelled",true};
            const auto words=ShellWords(request.arguments[3]);
            if(words[0]=="dumpsys"){
                if(lockScenario==4)return ProcessResult{0,"unavailable"};
                if(lockScenario==3)return ProcessResult{0,"showing=true\n mSimSecure=false"};
                return ProcessResult{0,std::string("KeyguardServiceDelegate\n showing=")+(locked?"true":"false")+"\n secure="+(lockScenario==2?"true":"false")};
            }
            if(words==std::vector<std::string>{"input","keyevent","KEYCODE_WAKEUP"}){++wakes;if(lockScenario==7)cancelled=true;return ProcessResult{0};}
            if(words==std::vector<std::string>{"wm","dismiss-keyguard"}){++dismissals;if(lockScenario==5)return ProcessResult{1,"unsupported"};if(lockScenario!=6)locked=false;return ProcessResult{0};}
            throw std::runtime_error("Readiness issued an unexpected command");
        }};
        bool failed=false;try{const auto ready=EnsureAndroidReplayReady(connection);Require(ready.known&&!ready.locked,"Readiness returned a locked phone");}
        catch(const AndroidUnlockRequired& error){failed=true;Require(error.state.serial=="phone-a","Unlock notification lost the serial");}
        catch(const std::runtime_error&){failed=true;Require(lockScenario==7,"Unexpected readiness failure");}
        Require(failed==(lockScenario>=2),"Readiness terminal state mismatch");
        if(lockScenario==0||lockScenario==2||lockScenario==3||lockScenario==4)Require(wakes==0&&dismissals==0,"Secure/unknown/unlocked phone received input");
        else Require(wakes==1&&dismissals==(lockScenario==7?0u:1u),"Automatic dismissal was repeated or bypassed cancellation");
    }
    for (int scenario = 0; scenario < 4; ++scenario) {
        Phone phone; phone.output = root / ("device-" + std::to_string(scenario));
        phone.interruptTransfer = scenario == 2; phone.conflict = scenario == 3;
        std::atomic_bool cancel{false}; if (scenario == 1) phone.cancelOnLaunch = &cancel;
        const auto settings = phone.settings, properties = phone.properties;
        const auto transport = [&](const auto& request, const auto* token) { return phone.Run(request, token); };
        AdbConnection connection{"fake-adb", "phone-a", &cancel, transport};
        CaptureRequest request; request.package = "test.app"; request.frames = "1-3"; request.output = phone.output;
        bool failed = false; try { CaptureAndroid(connection, request); } catch (...) { failed = true; }
        Require(failed == (scenario != 0), "Capture terminal status did not match injected device outcome");
        const auto journal = phone.output / "device-journal.json";
        if (scenario >= 2) {
            Require(!Load(journal)["restored"].get<bool>(), "Disconnected/conflicting session was marked restored");
            if (scenario == 2) Require(!std::filesystem::exists(phone.output / "c_frames_1_through_3.gfxr"), "Interrupted transfer published a completed file");
            else Require(phone.settings["gpu_debug_app"] == "outside.app", "Recovery overwrote an intervening setting change");
            phone.offline = false;
            if (scenario == 3) phone.settings["gpu_debug_app"] = "test.app";
            AdbConnection restarted{"fake-adb", "phone-a", nullptr, transport};
            RestoreAndroidCapture(restarted, journal);
        }
        Require(Load(journal)["restored"].get<bool>(), "Capture/restart recovery did not finish");
        Require(phone.settings == settings, "Exact prior global setting presence/values were not restored");
        for (const auto& [name, value] : phone.properties) {
            const auto original = properties.find(name);
            Require(value == (original == properties.end() ? "" : original->second), "Prior property effective values were not restored");
        }
        const auto before = phone.mutations;
        bool isolated = false;
        try { RestoreAndroidCapture({"fake-adb", "phone-b", nullptr, transport}, journal); } catch (...) { isolated = true; }
        Require(isolated && before == phone.mutations, "Wrong-device recovery touched another phone");
        Require(phone.stops >= 1, "Capture cleanup did not stop its owned activity");
        std::filesystem::remove_all(phone.output);
    }
}
} // namespace gfx::tests
