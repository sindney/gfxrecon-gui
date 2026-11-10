#include "gui/workspace.h"
#include "platform/dialogs.h"
#include "core/log.h"
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>

namespace gfx {
namespace {
void Tip(const char* text){
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",text);
}
bool Active(const std::optional<LiveCaptureState>& state){return state&&state->phase!=LivePhase::Ended&&state->phase!=LivePhase::Failed;}
const char* PhaseName(LivePhase phase){
    switch(phase){
    case LivePhase::Starting:return "Launching...";
    case LivePhase::Armed:return "Ready";
    case LivePhase::Triggering:return "Starting capture...";
    case LivePhase::Recording:return "Recording";
    case LivePhase::Stopping:return "Finishing...";
    case LivePhase::Retrieving:return "Retrieving...";
    case LivePhase::Ended:return "Session ended";
    case LivePhase::Failed:return "Capture failed";
    }return "";
}
}
void Workspace::SetCaptureProgress(const std::string& phase){
    std::lock_guard<std::mutex> lock(capturePhaseMutex_);
    if(capturePhase_!=phase){capturePhase_=phase;LOGI("capture")<<phase;}
}
void Workspace::DrawCaptureDevice(){
    SelectCaptureDevice();
    std::string preview=captureMode_==1?"Local Windows":captureSerial_.empty()?"No device":captureSerial_;
    if(captureMode_==0&&deviceInfo_&&deviceInfo_->serial==captureSerial_)preview=deviceInfo_->model+" ("+captureSerial_+")";
    ImGui::BeginDisabled(Busy()||CaptureTargetLocked());
    ImGui::SetNextItemWidth(std::max(140.0f,ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize("Refresh").x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x));
    if(ImGui::BeginCombo("##capture-device",preview.c_str())){
        for(const auto& device:devices_){
            ImGui::BeginDisabled(device.state!="device");
            const auto label=device.serial+(device.state=="device"?std::string{}:" ("+device.state+")");
            if(ImGui::Selectable(label.c_str(),captureMode_==0&&captureSerial_==device.serial)){
                captureMode_=0;captureSerial_=device.serial;deviceInfo_.reset();captureSetupReady_=false;captureError_.clear();captureInspectPending_=true;
            }
            ImGui::EndDisabled();Tip(device.details.c_str());
        }
#ifndef __APPLE__
        if(!devices_.empty())ImGui::Separator();
        if(ImGui::Selectable("Local Windows",captureMode_==1)){captureMode_=1;captureSetupReady_=false;captureInspectPending_=false;captureError_.clear();}
#endif
        ImGui::EndCombo();
    }
    Tip("Capture target. A new idle capture selects the first ready connected phone.\nTarget selection is locked while its capture session or launched app is running.");
    ImGui::SameLine();ImGui::BeginDisabled(!ToolAvailable("adb"));
    if(ImGui::Button("Refresh")){captureDiscoverPending_=false;RefreshDevices();}
    ImGui::EndDisabled();Tip("Refresh connected Android devices. Configure adb in Settings if unavailable.");
    ImGui::EndDisabled();
}
void Workspace::DrawCaptureSetup(){
    if(captureRepairOpen_){ImGui::OpenPopup("Capture setup");captureRepairOpen_=false;}
    if(!ImGui::IsPopupOpen("Capture setup"))return;
    const float width=std::min(ImGui::GetFontSize()*30,ImGui::GetMainViewport()->WorkSize.x*.9f);
    ImGui::SetNextWindowSize(ImVec2(width,0),ImGuiCond_Appearing);
    bool open=true;
    if(ImGui::BeginPopupModal("Capture setup",&open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDocking)){
        const char* summary=captureSetupIssue_=="replay-apk"?"Install capture support on this device.":
            captureSetupIssue_=="tools"?"Capture tools need attention.":captureSetupIssue_=="device"?"Check the selected phone's connection and unlock state.":
            captureSetupIssue_=="target"?"Check that the app is installed and debuggable.":captureSetupIssue_=="options"?"Check recording options.":"Capture could not continue.";
        if(captureSetupIssue_=="tools"||captureSetupIssue_=="options")summary=captureError_.c_str();
        ImGui::TextWrapped("%s",summary);Tip(captureError_.c_str());
        if(ImGui::CollapsingHeader("Details"))ImGui::TextWrapped("%s",captureError_.c_str());
        if(captureSetupIssue_=="replay-apk"){
            ImGui::BeginDisabled(Busy()||captureSerial_.empty()||!ToolAvailable("replay_apk"));
            if(ImGui::Button("Install replay APK"))InstallReplayApk();
            ImGui::EndDisabled();Tip("Install the compatible replay/layer APK on the selected phone. Android 11+ package visibility is configured automatically. Then Retry.");
        }
        ImGui::BeginDisabled(Busy());
        if(ImGui::Button("Retry")){CheckCaptureSetup(captureResumeLaunch_);ImGui::CloseCurrentPopup();}
        ImGui::EndDisabled();Tip("Check the environment again and continue the requested launch when ready.");
        ImGui::SameLine();
        if(ImGui::Button("Settings...")){captureLaunchPending_=captureResumeLaunch_=false;captureFocusTools_=captureSetupIssue_=="tools";captureOpenTools_=true;ImGui::CloseCurrentPopup();}
        Tip("Open Settings > Tools and devices for capture environment and tool configuration.");
        ImGui::SameLine();if(ImGui::Button("Cancel")){captureLaunchPending_=captureResumeLaunch_=false;ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
    if(!open)captureLaunchPending_=captureResumeLaunch_=false;
}
void Workspace::DrawCaptureEnvironment(){
    if(!captureSettingsVisited_){captureSettingsVisited_=true;if(devices_.empty()&&ToolAvailable("adb"))captureDiscoverPending_=true;}
    if(ImGui::CollapsingHeader("Capture environment",ImGuiTreeNodeFlags_DefaultOpen)){
        PollCaptureSetup();DrawCaptureDevice();
        ImGui::BeginDisabled(Busy()||CaptureTargetLocked());
        if(ImGui::Button("Probe"))CheckCaptureSetup();Tip("Check capture support on the selected target.");
        ImGui::SameLine();ImGui::BeginDisabled(captureMode_!=0||captureSerial_.empty()||!ToolAvailable("replay_apk"));
        if(ImGui::Button("Install replay APK"))InstallReplayApk();
        ImGui::EndDisabled();Tip("Install the capture layer and replay backend on the selected phone, including Android 11+ package visibility.");
        ImGui::SameLine();ImGui::BeginDisabled(captureMode_!=0||captureSerial_.empty()||!ToolAvailable("test_apk"));
        if(ImGui::Button("Install test sample"))InstallReplayApk(true);
        ImGui::EndDisabled();Tip("Optional Vulkan sample app for testing capture; not needed for your own app.");
        ImGui::EndDisabled();
        if(deviceInfo_)ImGui::TextDisabled("%s | Android %s",deviceInfo_->model.c_str(),deviceInfo_->androidVersion.c_str());
        else ImGui::TextUnformatted(" "); // Keep tool fields stationary when an asynchronous probe fills this row.
        DrawCaptureSetup();
        if(captureOpenTools_){captureOpenTools_=false;captureFocusTools_=true;}
    }
}
void Workspace::DrawCapture(){
    if(showCapture_){ImGui::OpenPopup("Capture application");showCapture_=false;}
    if(!ImGui::IsPopupOpen("Capture application"))return;
    const float width=std::min(ImGui::GetFontSize()*34,ImGui::GetMainViewport()->WorkSize.x*.94f);
    ImGui::SetNextWindowSize(ImVec2(width,0),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width,0),ImVec2(width,ImGui::GetMainViewport()->WorkSize.y*.9f));
    bool open=true;
    if(!ImGui::BeginPopupModal("Capture application",&open,ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_AlwaysAutoResize)){
        if(!open){captureLaunchPending_=captureResumeLaunch_=false;captureDiscoverPending_=captureInspectPending_=false;}
        return;
    }
    PollCaptureSetup();
    const float fieldX=ImGui::GetCursorPosX()+ImGui::CalcTextSize(captureMode_==1?"Executable":"Arguments").x+ImGui::GetStyle().ItemSpacing.x;
    ImGui::TextUnformatted("Device");ImGui::SameLine(fieldX);DrawCaptureDevice();
    ImGui::BeginDisabled(Busy()||CaptureTargetLocked());
    if(captureMode_==0){
        ImGui::TextUnformatted("App");ImGui::SameLine(fieldX);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()-ImGui::GetStyle().ItemSpacing.x);
        if(ImGui::InputTextWithHint("##capture-package","com.example.game",&capturePackage_)){captureSetupReady_=false;captureArguments_.clear();captureError_.clear();}
        Tip("Android package to launch. Use a debuggable app running Vulkan. Launch restarts it once; later captures keep it running.");
        ImGui::SameLine();ImGui::BeginDisabled(!deviceInfo_);
        if(ImGui::BeginCombo("##capture-apps",nullptr,ImGuiComboFlags_NoPreview)){
            for(const auto& package:deviceInfo_->packages)if(ImGui::Selectable(package.c_str(),capturePackage_==package)){capturePackage_=package;captureArguments_.clear();captureSetupReady_=false;captureError_.clear();}
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();Tip("Installed apps on the selected phone. Probe or Refresh to update the list.");
    }else{
        ImGui::TextUnformatted("Executable");ImGui::SameLine(fieldX);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize("Browse...").x-ImGui::GetStyle().FramePadding.x*2-ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##capture-executable",&captureExecutable_);ImGui::SameLine();
        if(ImGui::Button("Browse..."))if(const auto path=tpl::OpenFileDialog("Vulkan executable","exe"))captureExecutable_=*path;
    }
    ImGui::TextUnformatted("Timing");ImGui::SameLine(fieldX);
    ImGui::SetNextItemWidth(ImGui::GetFontSize()*10);ImGui::Combo("##capture-timing",&captureStyle_,"N frames\0Manual\0Scheduled\0");
    Tip("N frames: repeated bursts after launch. Manual: record until End session. Scheduled: record a startup frame range and close the launched app afterward.");
    if(captureStyle_==0){ImGui::SameLine();ImGui::TextUnformatted("Frames");ImGui::SameLine();ImGui::SetNextItemWidth(ImGui::GetFontSize()*5);ImGui::InputInt("##capture-frames",&captureLength_);Tip("Frames per capture, fixed when Launch is clicked. Choose Manual to record until End session.");}
    if(captureStyle_==2){ImGui::SameLine();ImGui::SetNextItemWidth(ImGui::GetFontSize()*7);ImGui::InputText("Frame range",&captureFrames_);Tip("Ascending 1-based frame ranges, such as 1-3 or 10,20-25.");}
    ImGui::TextUnformatted("Arguments");ImGui::SameLine(fieldX);ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##capture-arguments",captureMode_==0?&captureArguments_:&windowsArguments_);Tip("Optional launch arguments. Leave empty for the app's defaults.");
    if(captureMode_==1){ImGui::TextUnformatted("Directory");ImGui::SameLine(fieldX);ImGui::SetNextItemWidth(-1);ImGui::InputText("##capture-directory",&captureWorkingDirectory_);Tip("Working directory for the launched executable.");}
    ImGui::EndDisabled();
    ImGui::Separator();DrawLiveCapture();
    DrawCaptureSetup();
    if(captureOpenTools_){captureOpenTools_=false;RequestTools();ImGui::CloseCurrentPopup();}
    if(!open){captureLaunchPending_=captureResumeLaunch_=false;captureDiscoverPending_=captureInspectPending_=false;}
    ImGui::EndPopup();
}
void Workspace::DrawLiveCapture(){
    const auto state=LiveState();const bool active=Active(state);
    const bool pending=state&&state->commandPending;
    const bool hasTarget=captureMode_==0?!captureSerial_.empty()&&!capturePackage_.empty():!captureExecutable_.empty();
    const bool lengthValid=captureStyle_!=0||(captureLength_>=1&&captureLength_<=1000000);
    ImGui::BeginDisabled(Busy()||active||pending||captureInProgress_||!hasTarget||!lengthValid||HasUnsavedEdits());
    if(ImGui::Button("Launch"))CheckCaptureSetup(true);
    ImGui::EndDisabled();Tip("Check setup and launch with tracking armed. No frames are recorded until Capture, except in Scheduled mode.\nFinish conflicting work and select a valid target first. An ended session can be launched again.");
    ImGui::SameLine();ImGui::BeginDisabled(Busy()||pending||!state||state->phase!=LivePhase::Armed);
    if(ImGui::Button("Capture")&&liveCapture_)liveCapture_->Capture();
    ImGui::EndDisabled();Tip("Record the chosen frame count, or start manual recording. Available once the app is armed; it becomes available again after retrieval.");
    ImGui::SameLine();ImGui::BeginDisabled((state&&state->endingSession)||(!active&&(!state||!state->gameRunning)));
    if(ImGui::Button("End session"))ImGui::OpenPopup("End session");
    ImGui::EndDisabled();Tip("Finish any recording, end capture monitoring and restore settings. Choose whether to close the app or leave it running.");
    ImGui::SameLine();ImGui::BeginDisabled(Busy()||CaptureTargetLocked());
    if(ImGui::Button("Probe"))CheckCaptureSetup();
    ImGui::EndDisabled();Tip("Optional environment check. Launch always checks the current settings automatically.");
    ImGui::SameLine();if(ImGui::Button("Settings")){captureLaunchPending_=captureResumeLaunch_=false;captureRepairOpen_=false;captureOpenTools_=true;}
    Tip("Capture support, APK installation and tool paths in Settings > Tools and devices.");
    std::string detail;
    if(captureInProgress_){std::lock_guard<std::mutex> lock(capturePhaseMutex_);detail=capturePhase_;ImGui::TextDisabled("Scheduled capture...");}
    else if(Busy()&&(captureLaunchPending_||jobName_=="Checking capture setup"||jobName_=="Discovering Android devices")){
        detail=jobName_;ImGui::TextDisabled("%s",jobName_=="Discovering Android devices"?"Finding devices...":"Checking setup...");
    }
    else if(state){detail=state->detail;ImGui::TextDisabled("%s",state->endingSession?"Ending session...":state->commandPending?"Requesting capture...":PhaseName(state->phase));}
    else{detail=captureError_;ImGui::TextDisabled("%s",!captureError_.empty()?"Needs setup":!hasTarget?"Select an app and device":captureSetupReady_?"Ready to launch":"Launch to begin");}
    Tip(detail.c_str());
    if(!captureError_.empty()&&!active){ImGui::SameLine();if(ImGui::SmallButton("Setup..."))captureRepairOpen_=true;}
    const auto& captures=state&&!state->captures.empty()?state->captures:completedCaptures_;
    if(!captures.empty()){
        ImGui::Separator();
        if(ImGui::BeginChild("capture-results",ImVec2(0,std::min<float>(captures.size(),4)*ImGui::GetFrameHeightWithSpacing()+8),ImGuiChildFlags_None))
            for(const auto& file:captures)DrawCompletedCapture(file);
        ImGui::EndChild();
    }
    if(ImGui::IsPopupOpen("End session")){
        ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetFontSize()*28,ImGui::GetMainViewport()->WorkSize.x*.9f),0),ImGuiCond_Appearing);
        bool open=true;
        if(ImGui::BeginPopupModal("End session",&open,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDocking)){
            ImGui::TextWrapped("Close the app when ending this session?");
            if(ImGui::Button("Yes",ImVec2(ImGui::GetFontSize()*4,0))){if(liveCapture_)liveCapture_->End(true);ImGui::CloseCurrentPopup();}
            Tip("End the session and close its launched app.");
            ImGui::SameLine();
            if(ImGui::Button("No",ImVec2(ImGui::GetFontSize()*4,0))){if(liveCapture_)liveCapture_->End(false);ImGui::CloseCurrentPopup();}
            Tip("End the session and keep its app running.");ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
    }
}
} // namespace gfx
