#include "gui/workspace.h"
#include "platform/dialogs.h"
#include "core/log.h"

#include <misc/cpp/imgui_stdlib.h>
#include <fstream>
#include <algorithm>

namespace gfx {
void Workspace::RequestCapture() {
    showCapture_=true;captureError_.clear();
    if(CaptureTargetLocked())return;
    if(!Busy())SelectCaptureDevice(true);captureSelectFirst_=true;
#ifdef IMGUI_TEMPLATE_TESTS
    if(captureUiFixture_)return;
#endif
    captureDiscoverPending_=ToolAvailable("adb");
}
bool Workspace::CaptureTargetLocked()const{
    const auto state=LiveState();
    return captureInProgress_||(state&&(state->commandPending||state->gameRunning||(state->phase!=LivePhase::Ended&&state->phase!=LivePhase::Failed)));
}
void Workspace::SelectCaptureDevice(bool first){
    if(CaptureTargetLocked()||(!first&&captureMode_==1))return;
    const auto ready=[&](const AndroidDevice& d){return d.state=="device"&&d.serial==captureSerial_;};
    if(!first&&std::any_of(devices_.begin(),devices_.end(),ready))return;
    const auto found=std::find_if(devices_.begin(),devices_.end(),[](const AndroidDevice& d){return d.state=="device";});
    const auto serial=found==devices_.end()?std::string{}:found->serial;
    if(found!=devices_.end())captureMode_=0;
    if(captureSerial_!=serial){captureSerial_=serial;deviceInfo_.reset();captureSetupReady_=false;}
}
void Workspace::PollCaptureSetup(){
#ifdef IMGUI_TEMPLATE_TESTS
    if(captureUiFixture_){captureDiscoverPending_=captureInspectPending_=false;return;}
#endif
    if(Busy()||captureInProgress_)return;
    const auto retryId=ImGui::GetID("##capture-discovery-retry");auto* storage=ImGui::GetStateStorage();
    if(!CaptureTargetLocked()&&captureMode_==0&&captureSerial_.empty()&&ToolAvailable("adb")&&
       ImGui::GetTime()>=storage->GetFloat(retryId,0))captureDiscoverPending_=true;
    if(captureDiscoverPending_){
        storage->SetFloat(retryId,static_cast<float>(ImGui::GetTime()+2));captureDiscoverPending_=false;RefreshDevices();return;
    }
    if(captureInspectPending_){captureInspectPending_=false;CheckCaptureSetup();return;}
    if(captureLaunchPending_&&captureSetupReady_){captureLaunchPending_=false;StartCapture();}
}
namespace {
std::filesystem::path Session(const std::filesystem::path& root) {
    const auto path = root / std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    if (!std::filesystem::create_directories(path)) throw std::runtime_error("Could not create capture session");
    return path;
}
Json ReadJson(const std::filesystem::path& path) { std::ifstream in(path); return Json::parse(in); }
}
void Workspace::RefreshRecoveries() {
    recoveries_.clear();displayRecoveries_.clear();
    try {
        if (!std::filesystem::is_directory(sessionsDir_)) return;
        for (const auto& directory : std::filesystem::directory_iterator(sessionsDir_)) {
            for(const auto& name:{std::filesystem::path("display-journal.json"),std::filesystem::path("baseline/display-journal.json"),std::filesystem::path("modified/display-journal.json")}){
                const auto display=directory.path()/name;
                if(std::filesystem::is_regular_file(display)&&!ReadJson(display).value("restored",false))displayRecoveries_.push_back(display);
            }
            const auto path = directory.path() / "device-journal.json";
            if (std::filesystem::is_regular_file(path) && !ReadJson(path).value("restored", false)) recoveries_.push_back(path);
        }
    } catch (const std::exception& e) { LOGW("recovery") << e.what(); }
}
void Workspace::CheckCaptureSetup(bool launchAfter) {
    if(Busy()||CaptureSessionActive())return;
    captureLaunchPending_=captureResumeLaunch_=launchAfter;captureSetupReady_=false;captureError_.clear();
    const auto fail=[&](const std::string& issue,const std::string& message){captureSetupIssue_=issue;captureError_=message;captureRepairOpen_=true;captureLaunchPending_=false;};
    if(!ToolAvailable("convert")){fail("tools","Configure the capture converter in Settings.");return;}
    if(captureMode_==0&&!ToolAvailable("adb")){fail("tools","Configure Android Platform-Tools (adb) in Settings.");return;}
    if(captureStyle_==0&&(captureLength_<1||captureLength_>1000000)){fail("options","Choose 1 to 1,000,000 frames in recording options.");return;}
    if(captureStyle_==2)try{ValidateCaptureFrames(captureFrames_);}catch(const std::exception& e){fail("options",e.what());return;}
    const auto tools=tools_;const auto serial=captureSerial_,package=capturePackage_,executable=captureExecutable_;const auto mode=captureMode_;
    Start("Checking capture setup",[this,tools,serial,package,executable,mode,launchAfter]{
        WorkspaceResult r;r.captureProbe=true;r.captureSetupIssue="tools";
        try{
            if(mode==0){
                r.captureSetupIssue="device";
                if(serial.empty())throw std::runtime_error("Connect and authorize a phone, then refresh devices.");
                const auto target=ResolveReplayTarget(ReplayMode::Android,true,serial,tools.at("adb"),&cancel_);
                AdbConnection adb{tools.at("adb"),target.serial,&cancel_};r.deviceInfo=InspectAndroidDevice(adb);
                r.captureSetupIssue="replay-apk";
                if(std::find(r.deviceInfo->packages.begin(),r.deviceInfo->packages.end(),"com.lunarg.gfxreconstruct.replay")==r.deviceInfo->packages.end())
                    throw std::runtime_error("Install the compatible replay APK to provide the capture layer.");
                if(launchAfter){
                    r.captureSetupIssue="device";RequireAndroidUnlocked(adb);r.captureSetupIssue="target";
                    if(package.empty()||package.find('.')==std::string::npos||package.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._")!=std::string::npos)
                        throw std::runtime_error("Choose a valid Android package.");
                    adb.Shell({"run-as",package,"id"});
                }
            }else{
                if(!std::filesystem::is_regular_file(std::filesystem::u8path(tools.at("layer"))/"VkLayer_gfxreconstruct.json")||
                   !std::filesystem::is_regular_file(std::filesystem::u8path(tools.at("layer"))/"capture-control.json"))
                    throw std::runtime_error("Configure the compatible Windows capture layer in Settings.");
                if(launchAfter&&!std::filesystem::is_regular_file(std::filesystem::u8path(executable))){r.captureSetupIssue="target";throw std::runtime_error("Choose an existing Vulkan executable.");}
            }
            r.captureSetupReady=true;r.captureSetupIssue.clear();r.message="Capture setup ready.";
        }catch(const std::exception& e){r.captureSetupError=e.what();r.message=r.captureSetupError;}
        return r;
    });
}
void Workspace::InstallReplayApk(bool sample) {
    const auto tools = tools_; const auto serial = captureSerial_;
    Start(sample ? "Installing Vulkan sample APK" : "Installing replay APK", [this, tools, serial, sample] {
        const auto target = ResolveReplayTarget(ReplayMode::Android, true, serial, tools.at("adb"), &cancel_);
        AdbConnection adb{tools.at("adb"), target.serial, &cancel_};
        const auto sdk = std::stoi(adb.Shell({"getprop", "ro.build.version.sdk"}));
        std::vector<std::string> args{"install"};
        if (sdk >= 30) args.push_back("--force-queryable");
        args.insert(args.end(), {"-g", "-t", "-r", tools.at(sample ? "test_apk" : "replay_apk")});
        const auto installed = adb.Run(args);
        if (!installed.Success() || installed.output.find("Failure") != std::string::npos)
            throw std::runtime_error("APK installation requires your help. Resolve the reported device prompt or signing conflict before retrying.\n" + installed.output);
        WorkspaceResult r;
        r.deviceInfo = InspectAndroidDevice(adb);
        r.captureSetupReady = std::find(r.deviceInfo->packages.begin(), r.deviceInfo->packages.end(), "com.lunarg.gfxreconstruct.replay") != r.deviceInfo->packages.end();
        r.message = std::string(sample ? "Sample APK installed on " : "Replay APK installed on ") + target.serial; return r;
    });
}
void Workspace::StartCapture() {
    if (captureStyle_ != 2) { StartLiveCapture(); return; }
    if (HasUnsavedEdits()) { status_ = "Export or revert shader edits before starting a new capture."; return; }
    if (Busy()) return;
    liveCapture_.reset();liveCaptureDetail_.clear();
    captureInProgress_ = true; captureError_.clear();
    completedCaptures_.clear();
    SetCaptureProgress("Preparing capture");
    captureSession_ = Session(sessionsDir_);
    const auto session = captureSession_;
    const auto tools = tools_; const auto serial = captureSerial_;
    const auto package = capturePackage_, arguments = captureArguments_, frames = captureFrames_;
    const auto executable = captureExecutable_, working = captureWorkingDirectory_, windowsArgs = windowsArguments_;
    const auto mode = captureMode_;
    Start("Capturing application", [this, tools, serial, package, arguments, frames, executable, working, windowsArgs, mode, session] {
        CaptureResult captured;
        if (mode == 0) {
            const auto target = ResolveReplayTarget(ReplayMode::Android, true, serial, tools.at("adb"), &cancel_);
            CaptureRequest request; request.output = session; request.package = package; request.arguments = arguments; request.frames = frames;
            request.progress = [this](const std::string& phase) { SetCaptureProgress(phase); };
            captured = CaptureAndroid({tools.at("adb"), target.serial, &cancel_}, request);
        } else {
            ProcessRequest process; process.executable = executable; process.arguments = SplitProcessArguments(windowsArgs);
            SetCaptureProgress("Launching application and recording frames");
            process.workingDirectory = working.empty() ? std::filesystem::u8path(executable).parent_path().u8string() : working;
            captured = CaptureWindows(process, tools.at("layer"), frames, session, &cancel_);
        }
        SetCaptureProgress("Opening and indexing the completed capture");
        auto result = ImportCapture(captured.files.front(), session, tools, &cancel_);
        result.completedCaptures = captured.files;
        result.message = "Capture complete; " + std::to_string(captured.files.size()) + " file(s). " + result.message;
        return result;
    }, JobKind::Capture);
}
void Workspace::StartLiveCapture() {
    if (Busy() || HasUnsavedEdits()) { status_ = "Finish the current operation or export/revert shader edits first."; return; }
    if (liveCapture_ && liveCapture_->Active())return;
    if (captureStyle_ == 0 && (captureLength_ < 1 || captureLength_ > 1000000)) { captureError_ = "Choose 1 to 1,000,000 frames per trigger."; return; }
    liveCapture_.reset(); captureSession_ = Session(sessionsDir_); captureError_.clear();
    completedCaptures_.clear();
    LiveCaptureOptions options; options.android = captureMode_ == 0;
    options.capture.output = captureSession_; options.capture.package = capturePackage_; options.capture.arguments = captureArguments_;
    options.capture.progress = [this](const std::string& phase) { SetCaptureProgress(phase); };
    options.frames = captureStyle_ == 0 ? static_cast<uint32_t>(captureLength_) : 0;
    options.layerDirectory = tools_.at("layer"); options.infoTool = tools_.at("info");
    options.process.executable = captureExecutable_; options.process.arguments = SplitProcessArguments(windowsArguments_);
    options.process.workingDirectory = captureWorkingDirectory_;
    const auto tools = tools_; const auto serial = captureSerial_;
    Start("Arming capture session", [this, options, tools, serial] {
        WorkspaceResult result;
        if (options.android) {
            const auto target = ResolveReplayTarget(ReplayMode::Android, true, serial, tools.at("adb"), &cancel_);
            result.liveSession = std::make_shared<LiveCaptureSession>(options, AdbConnection{tools.at("adb"),target.serial});
        } else result.liveSession = std::make_shared<LiveCaptureSession>(options);
        result.message = "Capture session is launching. No frames are recorded until triggered."; return result;
    }, JobKind::Capture);
}
void Workspace::PollLiveCapture() {
    if(!liveCapture_)return;const auto state=liveCapture_->State();
    const bool running=state.phase==LivePhase::Starting||state.phase==LivePhase::Triggering||state.phase==LivePhase::Recording||state.phase==LivePhase::Stopping||state.phase==LivePhase::Retrieving;
    if(state.detail!=liveCaptureDetail_){
        liveCaptureDetail_=state.detail;
        if(state.phase==LivePhase::Failed){captureError_=state.detail;captureSetupIssue_="runtime";captureResumeLaunch_=false;ReportProblem("Capture failed",state.detail);}
        const auto indicator=state.phase==LivePhase::Failed?PanelState::Failed:running?PanelState::Running:state.phase==LivePhase::Armed&&!state.captures.empty()?PanelState::Success:PanelState::Inactive;
        panelStatuses_["Console"]={indicator,state.detail};
    }
    if(running||Busy())panelStatuses_["Console"]={PanelState::Running,running?state.detail:jobName_};
}
} // namespace gfx
