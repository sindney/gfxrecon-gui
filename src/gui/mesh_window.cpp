#include "gui/workspace.h"
#include "gui/refresh_overlay.h"
namespace gfx {
void Workspace::DrawMesh() {
    if (focusMesh_) {
        ImGui::SetNextWindowFocus(); focusMesh_ = false;
        if (meshGeneration_ != selectionGeneration_) {
            meshRequestGeneration_ = 0;
            if (Busy() && autoReplayRunning_ && jobExecution_<0) cancel_ = true;
        }
    }
    if (!ImGui::Begin("Mesh", nullptr, ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    if (!capture_ || selectedExecution_ < 0) { ImGui::TextDisabled("Select a submitted draw to inspect its geometry."); ImGui::End(); return; }
    if (CaptureSessionActive()) {
        ImGui::TextWrapped("End the live capture session to replay mesh inputs. The game can keep running.");
        ImGui::End(); return;
    }
    const auto& event = capture_->events[selectedEvent_];
    if (event.name != "vkCmdDraw" && event.name != "vkCmdDrawIndexed") { ImGui::TextWrapped("This draw path is not supported by the mesh preview yet."); ImGui::End(); return; }
    { RefreshOverlay refresh(Refreshing("Mesh"));
    ImGui::BeginDisabled(Busy());
    if (ImGui::Button("Reload mesh")) { meshRequestGeneration_ = meshGeneration_ = 0; previewPaused_ = false; }
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    if (ImGui::InputScalar("Instance", ImGuiDataType_U32, &meshInstance_)) meshRequestGeneration_ = meshGeneration_ = 0;
    ImGui::EndDisabled();
    if (!blockedReplay_ && !Busy() && !pendingDraw_ && !deferredJob_ && !previewPaused_ && meshRequestGeneration_ != selectionGeneration_ && ExecutionState(static_cast<size_t>(selectedExecution_))!=PanelState::Running) {
        meshRequestGeneration_ = selectionGeneration_; meshDiagnostic_.clear(); ReplayFrame(true, false, 0, false, true);
        autoReplayRunning_ = Busy(); currentReplayFrame_ = 0;
    }
    if (meshGeneration_ == selectionGeneration_) mesh_.Draw();
    else if (Busy()) ImGui::TextDisabled("Reading selected draw geometry through replay...");
    else if (previewPaused_) ImGui::TextWrapped("Mesh readback is paused. Reload mesh to replay the selected draw.");
    else ImGui::TextWrapped("%s", meshDiagnostic_.empty() ? "Select a draw or Reload mesh to read its geometry." : meshDiagnostic_.c_str());
    }
    ImGui::End();
}
}
