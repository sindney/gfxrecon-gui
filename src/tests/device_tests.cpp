#include "capture/capture.h"
#include "platform/android_display.h"
#include <fstream>
#include <map>
#include <stdexcept>

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
    for(int displayScenario=0;displayScenario<8;++displayScenario){
        const auto journal=root/("display-"+std::to_string(displayScenario)+".json");
        bool panelOff=displayScenario==1,offline=false,rejectReset=displayScenario==7;unsigned offCommands=0,resets=0;std::atomic_bool cancelled{false};
        AdbConnection connection{"fake-adb","phone-a",&cancelled,[&](const ProcessRequest& request,const std::atomic_bool* token){
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
        }catch(const std::runtime_error&){Require(displayScenario==2||displayScenario==6,"Unexpected display setup failure");}
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
