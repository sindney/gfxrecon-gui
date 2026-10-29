#include "gui/workspace.h"
#include "gui/refresh_overlay.h"
#include "core/log.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace gfx {
void Workspace::OpenResource(const ResourceArtifact& artifact, uint64_t offset,bool activate) {
    if (Busy() && !autoReplayRunning_) return;
    resourceOffset_ = offset;
    const auto file = artifact.file; const auto generation = selectionGeneration_;
    if(file.extension()==".bmp"){
        resourceMetadata_=artifact.metadata;
        auto found=std::find_if(imageTabs_.begin(),imageTabs_.end(),[&](const auto& tab){return tab.file==file&&tab.metadata==artifact.metadata;});
        if(found==imageTabs_.end()){
            if(imageTabs_.size()>=32)imageTabs_.erase(imageTabs_.begin());
            imageTabs_.push_back(artifact);imageTab_=static_cast<int>(imageTabs_.size()-1);
        }else imageTab_=static_cast<int>(found-imageTabs_.begin());
        imageTabRequest_=true;imageView_=artifact.metadata.value("category","")=="input"?2:1;selectedDrawImage_=file;
        try{resourceImage_.LoadBmp(file,artifact.metadata.value("aspect",std::string{}),artifact.metadata.value("format",std::string{}));resourceMode_=1;if(activate){focusImage_=true;status_="Replay resource: "+file.u8string();LOGI("resources")<<status_;}}
        catch(const std::exception& error){resourceImage_.Release();status_=error.what();LOGE("resources")<<status_;}
        return;
    }
    bufferMetadata_=artifact.metadata;
    Start("Opening replay resource", [file, generation, offset] {
        WorkspaceResult result; result.selectionGeneration = generation;
        if (file.extension() == ".bmp") result.resourceImage = file;
        else result.resourcePage = ReadResourcePage(file, offset);
        result.message = "Replay resource: " + file.u8string(); return result;
    }, JobKind::Resource);
}
void Workspace::DrawResources() {
    if (focusResources_) { ImGui::SetNextWindowFocus(); focusResources_ = false; }
    if (!ImGui::Begin("Resources")) { ImGui::End(); return; }
    { RefreshOverlay refresh(Refreshing("Resources"));
    if (ImGui::BeginTabBar("resource-tabs")) {
        if (ImGui::BeginTabItem("Shader interface", nullptr, resourceTabRequest_ && resourceTab_ == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (!reflection_.empty()) ImGui::TextUnformatted(reflection_.c_str());
            else ImGui::TextWrapped("Select a pipeline stage to inspect its shader interface.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Object",nullptr,resourceTabRequest_&&resourceTab_==5?ImGuiTabItemFlags_SetSelected:0)){
            if(objectDetails_.is_null())ImGui::TextDisabled("Select a pipeline sampler to inspect its properties.");
            else{
                ImGui::SeparatorText(objectLabel_.c_str());
                if(ImGui::BeginTable("object-properties",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)){
                    ImGui::TableSetupColumn("Property");ImGui::TableSetupColumn("Value");ImGui::TableHeadersRow();
                    for(auto it=objectDetails_.begin();it!=objectDetails_.end();++it)if(it.key()!="sType"&&!it.value().is_null()){
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(it.key().c_str());ImGui::TableNextColumn();
                        const auto text=it.value().is_string()?it.value().get<std::string>():it.value().dump();ImGui::TextWrapped("%s",text.c_str());
                    }ImGui::EndTable();
                }
            }ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Buffer",nullptr,resourceTabRequest_&&resourceTab_==4?ImGuiTabItemFlags_SetSelected:0)){
            if(resourcePage_.file.empty())ImGui::TextDisabled("Select a pipeline buffer to inspect its contents.");
            else{ImGui::TextDisabled("Buffer %s | captured offset %s",bufferMetadata_.value("bufferId",Json()).dump().c_str(),bufferMetadata_.value("offset",Json()).dump().c_str());DrawBuffer();}
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Replay data", nullptr, resourceTabRequest_ && resourceTab_ == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (resources_.empty()) ImGui::TextWrapped("Dump selected resources to retrieve images and buffers for that execution.");
            else {
                ImGui::Text("Last dump: block %llu | %zu files", dumpBlock_, resources_.size());
                ImGui::TextDisabled("%s", lastReplayTarget_.c_str());
                if (capture_ && (capture_->events[selectedEvent_].block != dumpBlock_ || selectedExecution_ != dumpExecution_))
                    ImGui::TextWrapped("This is a previous event's dump. Dump the new selection to refresh its resource contents.");
                if (ImGui::BeginTable("resource-files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn("File"); ImGui::TableSetupColumn("Role"); ImGui::TableSetupColumn("Bytes"); ImGui::TableHeadersRow();
                    for (const auto& artifact : resources_) {
                        ImGui::TableNextRow(); ImGui::TableNextColumn();
                        ImGui::BeginDisabled(Busy() && !autoReplayRunning_);
                        if (ImGui::Selectable(artifact.file.filename().u8string().c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) OpenResource(artifact);
                        ImGui::EndDisabled();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(artifact.role.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%llu", artifact.size);
                    }
                    ImGui::EndTable();
                }
                if (!resourceMetadata_.is_null()) {
                    ImGui::SeparatorText("Captured resource metadata");
                    ImGui::TextUnformatted(resourceMetadata_.dump(2).c_str());
                }
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Images",nullptr,resourceTabRequest_&&resourceTab_==3?ImGuiTabItemFlags_SetSelected:0)){
            DrawBoundImages("resource-images");ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Validation",nullptr,resourceTabRequest_&&resourceTab_==2?ImGuiTabItemFlags_SetSelected:0)){
            if(validationReport_.is_null())ImGui::TextWrapped("Enable Vulkan validation in the frame context menu to run a fresh validation replay.");
            else {
                ImGui::TextWrapped("Replay-wide validation: %s",validationReport_.value("status",std::string("unknown")).c_str());
                if(validationReport_.contains("enabled"))ImGui::TextDisabled("Khronos layer: %s",validationReport_.value("enabled",false)?"enabled":"not enabled");
                if(validationReport_.contains("frame"))ImGui::TextDisabled("Frame %u | %s",validationReport_.value("frame",0u),validationReport_.value("target",std::string{}).c_str());
                ImGui::Text("%d errors | %d warnings",validationReport_.value("errors",0),validationReport_.value("warnings",0));
                if(validationReport_.contains("message"))ImGui::TextWrapped("%s",validationReport_["message"].get<std::string>().c_str());
                if(validationReport_.value("truncated",false))ImGui::TextWrapped("Displayed diagnostics are bounded; the replay session retains the full log.");
                if(ImGui::BeginTable("validation-messages",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY)){
                    ImGui::TableSetupColumn("Severity");ImGui::TableSetupColumn("VUID");ImGui::TableSetupColumn("Message");ImGui::TableHeadersRow();
                    const auto messages=validationReport_.value("messages",Json::array());
                    ImGuiListClipper clip;clip.Begin(static_cast<int>(messages.size()));
                    while(clip.Step())for(int i=clip.DisplayStart;i<clip.DisplayEnd;++i){const auto& message=messages[i];
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(message.value("severity",std::string{}).c_str());
                        ImGui::TableNextColumn();ImGui::TextUnformatted(message.value("vuid",std::string{}).c_str());
                        ImGui::TableNextColumn();ImGui::TextUnformatted(message.value("message",std::string{}).c_str());ImGui::SetItemTooltip("%s",message.value("message",std::string{}).c_str());
                    }ImGui::EndTable();
                }
            }ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    resourceTabRequest_ = false;
    }
    ImGui::End();
}
void Workspace::DrawBuffer() {
    ImGui::TextWrapped("%s | %llu bytes", resourcePage_.file.filename().u8string().c_str(), resourcePage_.totalSize);
    ImGui::SetNextItemWidth(160);
    ImGui::InputScalar("Byte offset", ImGuiDataType_U64, &resourceOffset_);
    ImGui::SameLine(); ImGui::BeginDisabled(Busy() && !autoReplayRunning_);
    if (ImGui::Button("Read page")) OpenResource({resourcePage_.file, resourcePage_.totalSize, "Buffer", bufferMetadata_}, resourceOffset_);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Showing %zu bytes at offset %llu", resourcePage_.bytes.size(), resourcePage_.offset);
    if (ImGui::BeginTabBar("buffer-view")) {
        if (ImGui::BeginTabItem("Hex")) {
            if (editorFont_) ImGui::PushFont(editorFont_);
            if (ImGui::BeginTable("hex-bytes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 130);
                ImGui::TableSetupColumn("Hex bytes", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00 ").x * 16);
                ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("0").x * 16);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper; clipper.Begin(static_cast<int>((resourcePage_.bytes.size() + 15) / 16));
                while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto start = static_cast<size_t>(row) * 16;
                    std::ostringstream hex; std::string ascii;
                    for (size_t n = start; n < std::min(start + 16, resourcePage_.bytes.size()); ++n) {
                        const auto byte = resourcePage_.bytes[n]; hex << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte) << ' ';
                        ascii += byte >= 32 && byte < 127 ? static_cast<char>(byte) : '.';
                    }
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%08llx", resourcePage_.offset + start);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(hex.str().c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(ascii.c_str());
                }
                ImGui::EndTable();
            }
            if (editorFont_) ImGui::PopFont();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Values")) {
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("Type", &scalarType_, "uint8\0uint16\0uint32\0uint64\0float32\0float64\0");
            ImGui::SetNextItemWidth(100); ImGui::SliderInt("Values per row", &bufferColumns_, 1, 8);
            ImGui::SetNextItemWidth(100); ImGui::InputInt("Stride (bytes)", &bufferStride_);
            const int widths[] = {1, 2, 4, 8, 4, 8};
            if (bufferStride_ < bufferColumns_ * widths[scalarType_] || bufferStride_ > 4096)
                ImGui::TextWrapped("Set a stride large enough for the selected values (maximum 4096 bytes).");
            else if (ImGui::BeginTable("typed-values", bufferColumns_ + 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX)) {
                ImGui::TableSetupColumn("Byte offset", ImGuiTableColumnFlags_WidthFixed, 120);
                for (int c = 0; c < bufferColumns_; ++c) ImGui::TableSetupColumn(std::to_string(c).c_str(), ImGuiTableColumnFlags_WidthFixed, 130);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper; clipper.Begin(static_cast<int>((resourcePage_.bytes.size() + bufferStride_ - 1) / bufferStride_));
                while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto start = static_cast<size_t>(row) * bufferStride_;
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%llu", resourcePage_.offset + start);
                    for (int c = 0; c < bufferColumns_; ++c) {
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(DecodeResourceValue(resourcePage_.bytes, start + c * widths[scalarType_], scalarType_).c_str());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
} // namespace gfx
