#include "gui/workspace.h"
#include "platform/dialogs.h"
#include "core/log.h"

#include <misc/cpp/imgui_stdlib.h>

namespace gfx {
void Workspace::SetCaptureProgress(const std::string& phase) {
    std::lock_guard<std::mutex> lock(capturePhaseMutex_);
    if (capturePhase_ != phase) { capturePhase_ = phase; LOGI("capture") << phase; }
}
void Workspace::CheckCaptureSetup() {
    captureSetupReady_ = false; captureError_.clear();
    if (tools_["convert"].empty()) { captureError_ = "Configure the capture converter under Tool paths."; return; }
    if (captureMode_ == 0) InspectCaptureDevice();
    else {
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(captureExecutable_)))
            captureError_ = "Choose an existing Vulkan executable.";
        else if (!std::filesystem::is_regular_file(std::filesystem::u8path(tools_["layer"]) / "VkLayer_gfxreconstruct.json"))
            captureError_ = "Configure the capture layer directory under Tool paths.";
        else { captureSetupReady_ = true; status_ = "Windows capture setup is ready."; }
    }
}
void Workspace::DrawCapture() {
    if (showCapture_) { ImGui::OpenPopup("Capture application"); showCapture_ = false; }
    ImGui::SetNextWindowSize(ImVec2(800, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(800,0),ImVec2(800,ImGui::GetMainViewport()->WorkSize.y*.9f));
    if (!ImGui::BeginPopupModal("Capture application", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) return;

    const char* steps[] = {"Target", "Setup", "Options", liveCapture_ ? "Live session" : "Recording", "Save / inspect"};
    for (int i = 0; i < 5; ++i) {
        if (i) { ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine(); }
        if (i == captureStep_) ImGui::Text("%d. %s", i + 1, steps[i]);
        else ImGui::TextDisabled("%d. %s", i + 1, steps[i]);
    }
    ImGui::Separator();
    if (captureStep_ == 0) {
        ImGui::TextUnformatted("Choose where to launch and capture the Vulkan application.");
        ImGui::RadioButton("Android", &captureMode_, 0); ImGui::SameLine();
        ImGui::RadioButton("Local Windows", &captureMode_, 1);
        if (captureMode_ == 0) {
            ImGui::BeginDisabled(Busy());
            if (ImGui::Button("Refresh devices")) RefreshDevices();
            if (ImGui::BeginCombo("Device", androidSerial_.empty() ? "Automatic (single authorized phone)" : androidSerial_.c_str())) {
                if (ImGui::Selectable("Automatic (single authorized phone)", androidSerial_.empty())) { androidSerial_.clear(); deviceInfo_.reset(); }
                for (const auto& device : devices_) {
                    ImGui::BeginDisabled(device.state != "device");
                    if (ImGui::Selectable(device.serial.c_str(), androidSerial_ == device.serial)) { androidSerial_ = device.serial; deviceInfo_.reset(); }
                    ImGui::EndDisabled();
                    ImGui::SetItemTooltip("%s", device.details.c_str());
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        } else {
            ImGui::InputText("Executable", &captureExecutable_); ImGui::SameLine();
            if (ImGui::Button("Browse")) if (auto path = tpl::OpenFileDialog("Executable", "exe")) captureExecutable_ = *path;
        }
    } else if (captureStep_ == 1) {
        ImGui::TextUnformatted("Check the target and install capture support when needed.");
        ImGui::BeginDisabled(Busy());
        if (ImGui::Button("Check setup")) CheckCaptureSetup();
        if (captureMode_ == 0) {
            if (deviceInfo_) ImGui::TextWrapped("%s | Android %s | %s", deviceInfo_->model.c_str(), deviceInfo_->androidVersion.c_str(), deviceInfo_->abi.c_str());
            ImGui::BeginDisabled(tools_["replay_apk"].empty());
            if (ImGui::Button("Install replay APK")) InstallReplayApk();
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(tools_["test_apk"].empty());
            if (ImGui::Button("Install test sample")) InstallReplayApk(true);
            ImGui::EndDisabled();
            ImGui::TextWrapped("Use an authorized, unlocked phone. Capture targets must be debuggable Vulkan apps.");
            ImGui::TextWrapped("Install SDK Platform-Tools (adb) on the host and the compatible debug replay/layer APK on the phone. NDK, JDK and the remaining Android build tools are only needed to build the APKs.");
        } else ImGui::TextWrapped("The layer is enabled only for the launched process. The Vulkan loader may ignore custom layer paths in elevated processes.");
        if (ImGui::CollapsingHeader("Tool paths")) {
            for (auto key : {"adb", "convert", "layer", "replay_apk", "test_apk"}) ImGui::InputText(key, &tools_[key]);
        }
        ImGui::EndDisabled();
        ImGui::TextUnformatted(captureSetupReady_ ? "Setup is ready." : "Complete the setup check before continuing.");
    } else if (captureStep_ == 2) {
        ImGui::TextUnformatted("Select the application and when to capture.");
        if (captureMode_ == 0) {
            if (deviceInfo_ && ImGui::BeginCombo("Installed apps", capturePackage_.empty() ? "Choose an app" : capturePackage_.c_str())) {
                for (const auto& package : deviceInfo_->packages)
                    if (ImGui::Selectable(package.c_str(), capturePackage_ == package)) { capturePackage_ = package; captureArguments_.clear(); }
                ImGui::EndCombo();
            }
            ImGui::InputText("Package", &capturePackage_);
            if (ImGui::Button("Use Vulkan test sample")) {
                capturePackage_ = "com.lunarg.gfxreconstruct.test.test_apps.test_launcher"; captureArguments_ = "triangle";
            }
            ImGui::InputText("App arguments", &captureArguments_);
            ImGui::TextWrapped("Arming restarts the selected application once. Later on-demand captures keep it running.");
        } else {
            ImGui::TextWrapped("%s", captureExecutable_.c_str());
            ImGui::InputText("Arguments", &windowsArguments_);
            ImGui::InputText("Working directory", &captureWorkingDirectory_);
        }
        ImGui::Combo("Capture timing", &captureStyle_, "On demand: N frames\0On demand: start-stop\0Scheduled frames\0");
        if (captureStyle_ == 0) ImGui::InputInt("Frames per capture", &captureLength_);
        if (captureStyle_ == 2) {
            ImGui::InputText("Frames", &captureFrames_);
            ImGui::SetItemTooltip("1-based, ascending ranges: 1-3 or 10,20-25.");
        } else {
            ImGui::TextWrapped("Launch armed once, play normally, then capture when ready. Capturing does not quit the game.");
            if (captureMode_ == 1) ImGui::TextWrapped("Capture requests are queued without sending keyboard input. N is selected before launch; start/stop has no frame limit.");
        }
    } else if (captureStep_ == 3) {
      if (liveCapture_) DrawLiveCapture();
      else {
        std::string phase; { std::lock_guard<std::mutex> lock(capturePhaseMutex_); phase = capturePhase_; }
        ImGui::TextWrapped("%s", phase.c_str());
        ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()), ImVec2(-1, 0), "Capture in progress");
        ImGui::TextWrapped("Target: %s\nFrames: %s", captureMode_ == 0 ? capturePackage_.c_str() : captureExecutable_.c_str(), captureFrames_.c_str());
        if (ImGui::Button("Cancel capture")) { cancel_ = true; SetCaptureProgress("Stopping capture and restoring settings"); }
      }
    } else {
        ImGui::TextUnformatted("Capture complete and ready to inspect.");
        for(const auto& file:completedCaptures_) DrawCompletedCapture(file);
        if (ImGui::Button("Open capture")) { SelectFrame(frame_>0?static_cast<uint32_t>(frame_):1);ImGui::CloseCurrentPopup(); focusFrames_ = true; }
    }
    if (!captureError_.empty()) ImGui::TextWrapped("%s", captureError_.c_str());
    ImGui::Separator();
    if (captureStep_ < 3) {
        ImGui::BeginDisabled(Busy());
        if (captureStep_ > 0 && ImGui::Button("Back")) { --captureStep_; captureError_.clear(); }
        if (captureStep_ > 0) ImGui::SameLine();
        if (captureStep_ < 2) {
            ImGui::BeginDisabled(captureStep_ == 1 && !captureSetupReady_);
            if (ImGui::Button("Next")) {
                ++captureStep_; captureError_.clear();
                if (captureStep_ == 1) CheckCaptureSetup();
            }
            ImGui::EndDisabled();
        } else {
            ImGui::BeginDisabled(dirty_ || (captureMode_ == 0 ? capturePackage_.empty() : captureExecutable_.empty()));
            if (ImGui::Button(captureStyle_ == 2 ? "Start capture" : "Launch armed")) StartCapture();
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
    } else if (captureStep_ == 4) {
        if (ImGui::Button("New capture")) { completedCaptures_.clear(); captureStep_ = 0; captureError_.clear(); }
    }
    if (Busy() && captureStep_ != 3) ImGui::TextWrapped("%s", status_.c_str());
    ImGui::EndPopup();
}
void Workspace::DrawLiveCapture() {
    const auto session = liveCapture_->State();
    ImGui::TextWrapped("%s", session.detail.c_str());
    const bool recording = session.phase == LivePhase::Recording || session.phase == LivePhase::Triggering;
    if (session.phase == LivePhase::Armed) {
        if (ImGui::Button(captureStyle_ == 0 ? "Capture N frames" : "Start recording")) liveCapture_->Capture();
    }
    if (recording && ImGui::Button("Stop capture")) liveCapture_->StopCapture();
    if (session.phase == LivePhase::Starting || session.phase == LivePhase::Stopping || session.phase == LivePhase::Retrieving)
        ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), ImVec2(300,0), "");
    if (liveCapture_->Active()) {
        if (ImGui::Button("End session (keep game running)")) liveCapture_->End(false);
    }
    if (session.gameRunning) {
        ImGui::SameLine(); if (ImGui::Button("Close launched game")) liveCapture_->End(true);
    }
    if (!session.captures.empty()) {
        ImGui::SeparatorText("Completed captures");
        for (const auto& file : session.captures) {
            DrawCompletedCapture(file);
        }
        ImGui::TextDisabled("Preview replay is deferred while the game session is armed.");
    }
    if (!liveCapture_->Active() && !session.gameRunning && ImGui::Button("New session")) { liveCapture_.reset(); captureStep_ = 0; }
    if (ImGui::Button("Hide controls")) ImGui::CloseCurrentPopup();
}
} // namespace gfx
