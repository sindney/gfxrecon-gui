#include "gui/workspace.h"
#include "platform/dialogs.h"
#include "core/log.h"

#include <misc/cpp/imgui_stdlib.h>
#include <fstream>
#include <algorithm>

namespace gfx {
void Workspace::RequestCapture() {
    showCapture_=true;captureError_.clear();captureStep_=0;
    if(captureInProgress_){captureStep_=3;return;}
    if(!completedCaptures_.empty()){captureStep_=4;return;}
    if(liveCapture_) {
        const auto session=liveCapture_->State();
        if((session.phase!=LivePhase::Ended&&session.phase!=LivePhase::Failed)||session.gameRunning||!session.captures.empty())captureStep_=3;
    }
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
void Workspace::InspectCaptureDevice() {
    const auto tools = tools_; const auto serial = androidSerial_;
    Start("Inspecting capture device", [this, tools, serial] {
        const auto target = ResolveReplayTarget(ReplayMode::Android, true, serial, tools.at("adb"), &cancel_);
        WorkspaceResult r;
        r.deviceInfo = InspectAndroidDevice({tools.at("adb"), target.serial, &cancel_});
        r.captureSetupReady = std::find(r.deviceInfo->packages.begin(), r.deviceInfo->packages.end(), "com.lunarg.gfxreconstruct.replay") != r.deviceInfo->packages.end();
        r.message = "Device inspected: " + r.deviceInfo->model + " / Android " + r.deviceInfo->androidVersion;
        return r;
    });
}
void Workspace::InstallReplayApk(bool sample) {
    const auto tools = tools_; const auto serial = androidSerial_;
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
    captureInProgress_ = true; captureStep_ = 3; captureError_.clear();
    completedCaptures_.clear();
    SetCaptureProgress("Preparing capture");
    captureSession_ = Session(sessionsDir_);
    const auto session = captureSession_;
    const auto tools = tools_; const auto serial = androidSerial_;
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
    if (liveCapture_ && (liveCapture_->Active() || liveCapture_->State().gameRunning)) { captureStep_ = 3; return; }
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
    const auto tools = tools_; const auto serial = androidSerial_;
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
        const auto indicator=state.phase==LivePhase::Failed?PanelState::Failed:running?PanelState::Running:state.phase==LivePhase::Armed&&!state.captures.empty()?PanelState::Success:PanelState::Inactive;
        panelStatuses_["Console"]={indicator,state.detail};
    }
    if(running||Busy())panelStatuses_["Console"]={PanelState::Running,running?state.detail:jobName_};
}
} // namespace gfx
