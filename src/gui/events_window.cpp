#include "gui/workspace.h"
#include <algorithm>

namespace gfx {
namespace {
std::string Scope(const Execution& execution, const Event& event) {
    std::string name;
    if (!execution.primary && !execution.invocations.empty()) name = "Execute #" + std::to_string(execution.invocations.back().execute);
    else {
        const auto scope = std::find_if(execution.invocations.rbegin(), execution.invocations.rend(), [](const auto& i) { return !i.renderScope.empty(); });
        name = scope != execution.invocations.rend() ? "Render scope #" + std::to_string(scope->renderScope.front()) : "Compute / transfer";
    }
    name += " / CB " + std::to_string(event.commandBuffer);
    if (!event.label.empty()) name += " / " + event.label;
    return name;
}
}
void Workspace::DrawEvents() {
    ImGui::Begin("Events");
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputTextWithHint("##event-filter", "Find API, block, label, object...", eventFilter_.InputBuf, IM_ARRAYSIZE(eventFilter_.InputBuf))) {
        eventFilter_.Build(); filterDirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("Filters", ImGuiDir_Down)) ImGui::OpenPopup("event-filters");
    ImGui::SetItemTooltip("Event filters");
    if (ImGui::BeginPopup("event-filters")) {
        filterDirty_ |= ImGui::Checkbox("Draw / dispatch only", &onlyDraws_);
        if (ImGui::Checkbox("Submitted executions", &submittedOnly_)) { filterDirty_ = true; selectedExecution_ = -1; }
        ImGui::EndPopup();
    }
    if (!capture_) { ImGui::TextWrapped("Open a capture to inspect its Vulkan calls and submitted executions."); ImGui::End(); return; }
    if (filterDirty_) {
        filteredRows_.clear(); executionTree_.clear();
        const auto count = submittedOnly_ ? capture_->executions.size() : capture_->events.size();
        for (size_t i = 0; i < count; ++i) {
            const auto eventIndex = submittedOnly_ ? capture_->executions[i].event : i;
            const auto& event = capture_->events[eventIndex];
            const auto frame = submittedOnly_ ? capture_->executions[i].frame : event.frame;
            if (frame_ && frame != static_cast<uint32_t>(frame_)) continue;
            if (onlyDraws_ && !event.draw) continue;
            if (!eventFilter_.PassFilter(capture_->SearchText(eventIndex).c_str())) continue;
            filteredRows_.push_back(i);
            if (submittedOnly_) {
                const auto& execution = capture_->executions[i];
                executionTree_[frame][execution.submit][Scope(execution, event)].push_back(i);
            }
        }
        filterDirty_ = false;
    }
    ImGui::BeginChild("event-list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (!submittedOnly_) {
        ImGuiListClipper clipper; clipper.Begin(static_cast<int>(filteredRows_.size()));
        while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto index = filteredRows_[row]; const auto& event = capture_->events[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable((std::to_string(event.block) + "  " + event.name).c_str(), selectedEvent_ == index)) {
                int occurrence=-1;bool ambiguous=false;
                if(event.draw)for(size_t i=0;i<capture_->executions.size();++i)if(capture_->executions[i].event==index&&(!frame_||capture_->executions[i].frame==static_cast<uint32_t>(frame_))){if(occurrence>=0){ambiguous=true;break;}occurrence=static_cast<int>(i);}
                if(occurrence>=0&&!ambiguous)SelectExecution(static_cast<size_t>(occurrence),true);
                else{SelectEvent(index);if(event.draw)status_=ambiguous?"Choose a submitted occurrence to inspect this repeated command.":"This recording has no submitted occurrence in the selected frame.";}
            }
            if (!event.label.empty()) ImGui::SetItemTooltip("%s", event.label.c_str());
            ImGui::PopID();
        }
    } else {
        const auto* selected = selectedExecution_ >= 0 ? &capture_->executions.at(selectedExecution_) : nullptr;
        for (const auto& [frame, submissions] : executionTree_) {
            ImGui::PushID(static_cast<int>(frame));
            if (revealExecution_ && selected && selected->frame == frame) ImGui::SetNextItemOpen(true);
            else ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode(("Frame " + std::to_string(frame)).c_str())) {
                for (const auto& [submit, scopes] : submissions) {
                    const auto submitLabel = "Submit " + std::to_string(submit);
                    if (revealExecution_ && selected && selected->submit == submit) ImGui::SetNextItemOpen(true);
                    else ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                    if (ImGui::TreeNode(submitLabel.c_str())) {
                        for (const auto& [scope, indices] : scopes) {
                            ImGui::PushID(static_cast<int>(std::hash<std::string>{}(scope)));
                            if (revealExecution_ && std::find(indices.begin(), indices.end(), static_cast<size_t>(std::max(0, selectedExecution_))) != indices.end()) ImGui::SetNextItemOpen(true);
                            else ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                            if (ImGui::TreeNodeEx("scope", 0, "%s", scope.c_str())) {
                                ImGuiListClipper clipper; clipper.Begin(static_cast<int>(indices.size()));
                                while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                                    const auto index = indices[row]; const auto& execution = capture_->executions[index]; const auto& event = capture_->events[execution.event];
                                    ImGui::PushID(static_cast<int>(index));
                                    if (ImGui::Selectable((std::to_string(event.block) + "  " + event.name).c_str(), selectedExecution_ == static_cast<int>(index))) SelectExecution(index, true);
                                    if(selectedExecution_==static_cast<int>(index)){
                                        const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
                                        const ImU32 colors[]={IM_COL32(128,136,144,255),IM_COL32(244,192,65,255),IM_COL32(75,196,116,255),IM_COL32(237,90,90,255)};
                                        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(max.x-10,(min.y+max.y)*.5f),3,colors[static_cast<int>(ExecutionState(index))],16);
                                        if(ImGui::IsMouseHoveringRect(ImVec2(max.x-18,min.y),max)){
                                            const auto status=executionStatuses_.find(index);ImGui::SetTooltip("%s",status==executionStatuses_.end()?"No fetch requested":status->second.detail.c_str());
                                        }
                                    }
                                    ImGui::SetItemTooltip("Recorded block %llu, CB %llu, generation %llu\nExecuted in submit %llu, frame %u\n%s",
                                        event.block, event.commandBuffer, execution.generation, execution.submit, execution.frame, event.label.c_str());
                                    ImGui::PopID();
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        revealExecution_ = false;
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%zu calls | %zu executions", capture_->events.size(), capture_->executions.size());
    ImGui::SetItemTooltip("Frame numbers are capture-relative. Detailed records are loaded on demand.");
    ImGui::End();
}
} // namespace gfx
