#include "gui/workspace.h"
#include "core/log.h"
#include <imgui_internal.h>
#include <algorithm>

namespace gfx {
void Workspace::ReportProblem(const std::string& title, const std::string& message, const std::string& key) {
    if (message.empty()) return;
    const auto prefix = title + ": ";
    const auto detail = message.rfind(prefix, 0) == 0 ? message.substr(prefix.size()) : message;
    const auto identity = title + "\n" + detail;
    if (!key.empty()) {
        if (reportedProblems_[key] == identity) return;
        reportedProblems_[key] = identity;
    }
    for (const auto& problem : problems_)
        if (problem.title == title && problem.message == detail) return;
    problems_.push_back({title, detail});
    problemDialogRequested_ = true;
    LOGW("ui") << title << ": " << detail;
}
bool Workspace::ProblemDialogRequested() const {
    return problemDialogRequested_ && !problems_.empty();
}
void Workspace::OpenProblemDialog() {
    const auto* modal = ImGui::GetTopMostPopupModal();
    const bool nested = modal && (std::string_view(modal->Name) == "Settings" || std::string_view(modal->Name) == "Replay device");
    if (modal && !nested) return;
    if (modal && ImGui::GetCurrentWindow() != modal) return;
    if (!modal && ImGui::GetCurrentWindow() != ImGui::FindWindowByName("##dockspace-host")) return;
    const auto problemId = ImGui::GetID("Problem");
    if (!ImGui::IsPopupOpen(problemId, ImGuiPopupFlags_AnyPopupLevel)) ImGui::OpenPopup(problemId);
    problemDialogSettingsRequested_ = nested;
    problemDialogRequested_ = false;
}
void Workspace::DrawProblemDialog() {
    if (problems_.empty() || problemDialogFrame_ == ImGui::GetFrameCount()) return;
    const auto problemId = ImGui::GetID("Problem");
    if (!ImGui::IsPopupOpen(problemId, ImGuiPopupFlags_AnyPopupLevel)) {
        if (problemDialogSettingsRequested_) problemDialogSettingsRequested_ = false;
        else problemDialogRequested_ = true; // Recover if the popup closes without acknowledging the problem.
        return;
    }
    problemDialogFrame_ = ImGui::GetFrameCount();
    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::min(viewport->WorkSize.x * .9f, ImGui::GetFontSize() * 38);
    ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Problem", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) return;
    const auto& problem = problems_.front();
    ImGui::TextUnformatted(problem.title.c_str());
    ImGui::Separator();
    const float textWidth = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    const float height = std::min(ImGui::CalcTextSize(problem.message.c_str(), nullptr, false, textWidth).y +
                                 ImGui::GetStyle().WindowPadding.y * 2, viewport->WorkSize.y * .55f);
    if (ImGui::BeginChild("problem-message", ImVec2(0, height))) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(problem.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(100, 0))) {
        problems_.pop_front();
        problemDialogRequested_ = !problems_.empty();
        ImGui::CloseCurrentPopup();
    }
    if (problemDialogSettingsRequested_) {
        problemDialogSettingsRequested_ = false;
        problemDialogRequested_ = true;
    }
    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}
PanelState Workspace::StateFor(const std::string& panel) const {
    const auto found = panelStatuses_.find(panel);
    return found == panelStatuses_.end() ? PanelState::Inactive : found->second.state;
}
PanelState Workspace::FrameStateFor(uint32_t frame) const {
    const auto found = frameStatuses_.find(frame);
    return found == frameStatuses_.end() ? PanelState::Inactive : found->second.state;
}
void Workspace::CompleteOperation(PanelState state, const std::string& detail) {
    if(jobExecution_>=0)executionStatuses_[static_cast<size_t>(jobExecution_)]={state,detail};
    for (const auto& panel : jobPanels_) panelStatuses_[panel] = {state, detail};
    if (jobFrame_) frameStatuses_[jobFrame_] = {state, detail};
}
void Workspace::CancelJob() {
    if (!Busy()) {
        if (liveCapture_) {
            const auto phase=liveCapture_->State().phase;
            if(phase==LivePhase::Armed||phase==LivePhase::Starting)liveCapture_->End(false);else liveCapture_->StopCapture();
        }
        return;
    }
    cancel_ = true; status_ = "Cancellation requested; waiting for cleanup.";
    if (autoReplayRunning_) { previewPaused_ = true; thumbnailQueue_.clear(); pendingFrame_ = 0;pendingDraw_=false;if(selectedExecution_>=0)executionStatuses_[selectedExecution_]={PanelState::Inactive,"Cancelled"}; }
}
bool Workspace::Refreshing(const std::string& panel)const{
    if(panel=="Console"||panel=="Frames"||panel=="Events")return false;
    if(selectedExecution_>=0&&(pendingDraw_||ExecutionState(selectedExecution_)==PanelState::Running))return true;
    return Busy()&&StateFor(panel)==PanelState::Running;
}
void Workspace::DrawPanelStatuses() {
    // Validation/no-op messages also reach Console instead of requiring a Jobs pane.
    if (status_ != loggedStatus_) {
        if (jobFailed_) { LOGE("status") << status_; }
        else { LOGI("status") << status_; }
        loggedStatus_ = status_;
    }
    for (const auto* name : {"Frames", "Perf"}) {
        auto* window = ImGui::FindWindowByName(name);
        if (!window || window->LastFrameActive < ImGui::GetFrameCount() - 1) continue;
        const auto found = panelStatuses_.find(name);
        const PanelStatus status = found == panelStatuses_.end() ? PanelStatus{} : found->second;
        const bool docked = window->DockIsActive && window->DockNode && window->DockNode->HostWindow;
        const auto rect = docked ? window->DC.DockTabItemRect : window->TitleBarRect();
        if (rect.GetWidth() <= 0 || rect.GetHeight() <= 0 || (!docked && (window->Flags & ImGuiWindowFlags_NoTitleBar))) continue;
        const float padding = ImGui::GetFontSize();
        const ImVec2 center(rect.Min.x + padding * .32f, (rect.Min.y + rect.Max.y) * .5f);
        const float radius = 3.0f;
        const ImU32 colors[] = {IM_COL32(128, 136, 144, 255), IM_COL32(244, 192, 65, 255), IM_COL32(75, 196, 116, 255), IM_COL32(237, 90, 90, 255), IM_COL32(238, 163, 58, 255)};
        auto* draw = docked ? window->DockNode->HostWindow->DrawList : window->DrawList;
        draw->PushClipRect(rect.Min, rect.Max, false);
        draw->AddCircleFilled(center, radius, colors[static_cast<int>(status.state)], 16);
        draw->PopClipRect();
        if (ImGui::IsMouseHoveringRect(ImVec2(center.x - 5, rect.Min.y), ImVec2(center.x + 5, rect.Max.y), false)) {
            const char* states[] = {"Inactive / no operation", "Running", "Succeeded", "Failed", "Completed with warnings"};
            const auto summary = status.detail.substr(0, std::min<size_t>(status.detail.find('\n'), 240));
            ImGui::SetTooltip("%s: %s\n%s\nDetails are logged in Console.", name, states[static_cast<int>(status.state)], summary.c_str());
        }
    }
}
} // namespace gfx
