#include "gui/workspace.h"
#include <algorithm>
#include <imgui_internal.h>

namespace gfx {
void Workspace::SelectFrame(uint32_t frame, bool render) {
    if (!capture_ || !frame || frame > capture_->frames) return;
    const bool changed = frame_ != static_cast<int>(frame);
    frame_ = static_cast<int>(frame); filterDirty_ = true;
    if (changed) {
        ++selectionGeneration_;
        const auto execution = std::find_if(capture_->executions.begin(), capture_->executions.end(), [&](const auto& e) { return e.frame == frame; });
        if (execution != capture_->executions.end()) SelectExecution(static_cast<size_t>(execution - capture_->executions.begin()));
        else {
            const auto event = std::find_if(capture_->events.begin(), capture_->events.end(), [&](const auto& e) { return e.frame == frame; });
            if (event != capture_->events.end()) SelectEvent(static_cast<size_t>(event - capture_->events.begin()));
        }
        hasComparison_ = false; image_.Release();
    }
    if (render && capture_->source.extension() == ".gfxr") {
        imageView_=0;resourceMode_=0;imageTab_=-1;imageTabRequest_=true;pendingDraw_=false;
        focusImage_ = true; previewPaused_ = false;
        pendingFrame_ = frame; failedFrames_.erase(frame);
        if (Busy() && autoReplayRunning_) cancel_ = true;
    }
}
void Workspace::ScheduleFrameReplay() {
    if (Busy() || blockedReplay_) return;
    if (deferredJob_) {
        auto next = std::move(*deferredJob_); deferredJob_.reset();
        Start(next.name, std::move(next.work), next.kind, next.frame); return;
    }
    if (!pendingOpen_.empty()) { const auto path = pendingOpen_; pendingOpen_.clear(); Open(path); return; }
    if (CaptureSessionActive()) return;
    if (pendingDraw_) {
        if (std::chrono::steady_clock::now() - drawRequestedAt_ < std::chrono::milliseconds(160)) return;
        pendingDraw_ = false; pendingFrame_ = 0;
        ReplayFrame(true, false, 0, false, false, true,true);
        autoReplayRunning_ = Busy(); currentReplayFrame_ = 0; return;
    }
    if (previewPaused_ || !capture_ || capture_->source.extension() != ".gfxr") return;
    const bool android = replayMode_ == ReplayMode::Android || (replayMode_ == ReplayMode::Automatic && capture_->android);
    if (!ToolAvailable(android ? "adb" : "replay")) { pendingFrame_ = 0; return; }
    uint32_t frame = pendingFrame_;
    pendingFrame_ = 0;
    if (!frame) return;
    ReplayFrame(false, false, frame);
    autoReplayRunning_ = Busy(); currentReplayFrame_ = frame;
}
void Workspace::DrawFrameActions(uint32_t frame) {
    if (ImGui::BeginMenu("Replay target", !Busy() || autoReplayRunning_)) {
        const char* labels[] = {"Automatic", "Local Windows", "Android device"};
        for (int i = 0; i < 3; ++i) if (ImGui::MenuItem(labels[i], nullptr, static_cast<int>(replayMode_) == i)) {
            SetReplayMode(static_cast<ReplayMode>(i));
            for (auto& [number, preview] : frameImages_) preview.image.Release();
            frameImages_.clear(); failedFrames_.clear(); thumbnailQueue_.clear(); SelectFrame(frame);
            frameStatuses_.clear();
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if(ImGui::MenuItem("Optimize replay copy",nullptr,optimizeReplay_,!Busy()||autoReplayRunning_)) {
        optimizeReplay_=!optimizeReplay_;InvalidateDrawResources();++selectionGeneration_;SelectFrame(frame);
    }
    ImGui::SetItemTooltip("Remove unused initialization and pipelines in a cached replay copy. Original capture and inspection data are preserved.");
    if(ImGui::MenuItem("Vulkan validation",nullptr,validationReplay_,!Busy()||autoReplayRunning_)) {
        validationReplay_=!validationReplay_;InvalidateDrawResources();++selectionGeneration_;SelectFrame(frame);
    }
    ImGui::SetItemTooltip("Replay original API calls with VK_LAYER_KHRONOS_validation. Runs fresh, without optimized captures or result/pipeline caches.");
    const bool android = replayMode_ == ReplayMode::Android || (replayMode_ == ReplayMode::Automatic && capture_->android);
    if(ImGui::MenuItem("Phone screen off while replaying",nullptr,phoneScreenOff_,android&&(!Busy()||autoReplayRunning_))){
        phoneScreenOff_=!phoneScreenOff_;InvalidateDrawResources();++selectionGeneration_;SelectFrame(frame);
    }
    ImGui::SetItemTooltip("Keep the physical panel off during Android replay. Requires supported display-power controls; prior display state is restored afterward.");
    ImGui::BeginDisabled((Busy() && !autoReplayRunning_) || capture_->source.extension() != ".gfxr" || !ToolAvailable(android ? "adb" : "replay"));
    if (ImGui::MenuItem("Render frame")) { SelectFrame(frame, false); pendingFrame_ = 0; ReplayFrame(false, false, frame); }
    if (ImGui::MenuItem("Dump selected resources")) { SelectFrame(frame, false); pendingFrame_ = 0; ReplayFrame(true, false, frame); }
    if (ImGui::MenuItem("Revert and render baseline")) { SelectFrame(frame, false); pendingFrame_ = 0; ReplayFrame(false, true, frame); }
    ImGui::EndDisabled();
    if (ImGui::MenuItem("Cancel current operation", nullptr, false, Busy())) CancelJob();
}
void Workspace::DrawFrames() {
    if (focusFrames_) { ImGui::SetNextWindowFocus(); focusFrames_ = false; }
    if (!ImGui::Begin("Frames")) { ImGui::End(); return; }
    if (!capture_) { ImGui::TextDisabled("Open a capture to browse its frames."); ImGui::End(); return; }
    const float tile = 112, height = std::clamp(ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() - 20, 36.0f, 68.0f);
    ImGui::SetNextWindowContentSize(ImVec2(capture_->frames * tile, 0));
    ImGui::BeginChild("frame-strip", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const auto first = static_cast<uint32_t>(ImGui::GetScrollX() / tile) + 1;
    const auto last = std::min(capture_->frames, first + static_cast<uint32_t>(ImGui::GetWindowWidth() / tile) + 1);
    for (auto frame = first; frame <= last; ++frame) {
        ImGui::SetCursorPos(ImVec2((frame - 1) * tile + 4, 4));
        ImGui::PushID(static_cast<int>(frame));
        ImGui::BeginGroup();
        const bool selected = frame_ == static_cast<int>(frame);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(selected ? ImGuiCol_HeaderActive : ImGuiCol_FrameBg));
        bool clicked = false;
        const auto preview = frameImages_.find(frame);
        const auto previewTarget = preview == frameImages_.end() ? "Preview has not been rendered yet." : preview->second.target;
        if (preview != frameImages_.end()) clicked = preview->second.image.DrawThumbnail("##Frame thumbnail", height);
        else {
            const auto text = currentReplayFrame_ == frame && Busy() ? "Rendering..." : failedFrames_.count(frame) ? "Unavailable" : "Click to preview";
            clicked = ImGui::Button("##Frame thumbnail", ImVec2(100, height));
            const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            const float wrap = max.x - min.x - 8;
            const auto extent = ImGui::CalcTextSize(text, nullptr, false, wrap);
            const ImVec4 clip(min.x + 4, min.y + 4, max.x - 4, max.y - 4);
            const ImVec2 origin(min.x + 4 + std::max(0.0f, (wrap - extent.x) * .5f), min.y + std::max(4.0f, (max.y - min.y - extent.y) * .5f));
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), origin, ImGui::GetColorU32(ImGuiCol_TextDisabled), text, nullptr, wrap, &clip);
        }
        const auto tileMin = ImGui::GetItemRectMin();
        const auto state = frameStatuses_.find(frame);
        const auto frameState = state == frameStatuses_.end() ? PanelState::Inactive : state->second.state;
        const ImU32 colors[] = {IM_COL32(128,136,144,255), IM_COL32(244,192,65,255), IM_COL32(75,196,116,255), IM_COL32(237,90,90,255)};
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(tileMin.x + 92, tileMin.y + 8), 3, colors[static_cast<int>(frameState)], 16);
        ImGui::PopStyleColor();
        if (clicked) SelectFrame(frame);
        if (ImGui::BeginPopupContextItem("frame-actions")) {
            DrawFrameActions(frame); ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) {
            const char* states[] = {"Inactive / pending", "Running", "Succeeded", "Failed"};
            ImGui::SetTooltip("Frame %u%s: %s\n%s\nOperation details are logged in Console.", frame, selected ? " (selected)" : "", states[static_cast<int>(frameState)], previewTarget.c_str());
        }
        ImGui::Text("Frame %u", frame);
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (ImGui::BeginPopupContextWindow("frames-context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        DrawFrameActions(std::max(1, frame_)); ImGui::EndPopup();
    }
    ImGui::EndChild(); ImGui::End();
}
} // namespace gfx
