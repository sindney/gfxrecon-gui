#include "gui/workspace.h"
#include "gui/refresh_overlay.h"
#include "core/log.h"
#include "core/image_decode.h"
#include "analysis/image_subresources.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>

namespace gfx {
namespace {
// Raw ASTC artifacts whose decode failed (path -> reason), so the per-frame tab refresh does not retry forever.
std::mutex g_decodeFailuresMutex;
std::map<std::filesystem::path, std::string> g_decodeFailures;
// Decoded PNGs live in a "decoded" subdirectory so resource scans of the dump directory do not list them.
std::filesystem::path DecodedPngPath(const std::filesystem::path& raw) { return raw.parent_path() / "decoded" / (raw.filename().u8string() + ".png"); }
}
void Workspace::OpenResource(const ResourceArtifact& artifact, uint64_t offset,bool activate) {
    if (Busy() && !autoReplayRunning_) return;
    resourceOffset_ = offset;
    const auto file = artifact.file; const auto generation = selectionGeneration_;
    const bool astc=IsAstcArtifact(file,artifact.metadata);
    if(file.extension()==".bmp"||astc){
        auto enriched=artifact;if(capture_)EnrichImageMetadata(*capture_,enriched.metadata);
        resourceMetadata_=enriched.metadata;
        auto found=std::find_if(imageTabs_.begin(),imageTabs_.end(),[&](const auto& tab){return ImageBindingKey(tab.metadata)==ImageBindingKey(enriched.metadata);});
        if(found==imageTabs_.end()){
            if(imageTabs_.size()>=32)imageTabs_.erase(imageTabs_.begin());
            imageTabs_.push_back(enriched);imageTab_=static_cast<int>(imageTabs_.size()-1);
        }else{*found=enriched;imageTab_=static_cast<int>(found-imageTabs_.begin());}
        imageTabRequest_=true;imageView_=artifact.metadata.value("category","")=="input"?2:1;selectedDrawImage_=file;
        if(astc){
            const auto png=DecodedPngPath(file);std::error_code ec;
            {std::lock_guard<std::mutex> lock(g_decodeFailuresMutex);const auto failed=g_decodeFailures.find(file);
                if(failed!=g_decodeFailures.end()){resourceImage_.Release();status_=failed->second;ReportProblem("Image decode unavailable",status_,"image-decode");return;}}
            if(!(std::filesystem::is_regular_file(png,ec)&&std::filesystem::last_write_time(png,ec)>=std::filesystem::last_write_time(file,ec))){
                const auto metadata=artifact.metadata;const auto tools=tools_;
                Start("Decoding ASTC image",[file,png,metadata,tools,generation,this]{
                    WorkspaceResult result;result.selectionGeneration=generation;
                    const auto decoded=DecodeCompressedImage(file,metadata,png,tools,&cancel_);
                    if(decoded.value("status",std::string{})=="ok"){result.resourceImage=png;result.message="Decoded ASTC image with "+decoded.value("decoded_by",std::string{});}
                    else{
                        const auto reason="ASTC image not decoded: "+decoded.value("reason",std::string{"unknown"});
                        {std::lock_guard<std::mutex> lock(g_decodeFailuresMutex);g_decodeFailures[file]=reason;}
                        result.message=reason;result.failed=true;
                    }
                    return result;
                },JobKind::Resource);
                return;
            }
            try{resourceImage_.LoadPng(png,artifact.metadata.value("format",std::string{}));resourceMode_=1;if(activate){focusImage_=true;status_="Replay resource: "+png.u8string();}}
            catch(const std::exception& error){resourceImage_.Release();status_=error.what();LOGE("resources")<<status_;ReportProblem("Image could not be opened",status_,"image-open");}
            return;
        }
        try{resourceImage_.LoadBmp(file,artifact.metadata.value("aspect",std::string{}),artifact.metadata.value("format",std::string{}));
            if(artifact.metadata.contains("overlayFile"))try{resourceImage_.SetOverlay(std::filesystem::u8path(artifact.metadata.at("overlayFile").get<std::string>()));}catch(const std::exception& error){ReportProblem("Geometry overlay unavailable",error.what(),"image-overlay");}
            else if(artifact.metadata.contains("overlayUnavailable"))ReportProblem("Geometry overlay unavailable",artifact.metadata.at("overlayUnavailable").get<std::string>(),"image-overlay");
            resourceMode_=1;if(activate){focusImage_=true;status_="Replay resource: "+file.u8string();LOGI("resources")<<status_;}}
        catch(const std::exception& error){resourceImage_.Release();status_=error.what();LOGE("resources")<<status_;ReportProblem("Image could not be opened",status_,"image-open");}
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
void Workspace::SelectImageMip(uint32_t mip){
    if(Busy()||!capture_||imageTab_<0||imageTab_>=int(imageTabs_.size())||selectedExecution_<0)return;
    const auto source=imageTabs_[imageTab_];const auto key=ImageBindingKey(source.metadata);
    if(mip==source.metadata.value("mipLevel",0u))return;
    if(!source.metadata.contains("mipLevels")||mip>=source.metadata["mipLevels"].get<uint32_t>())return;
    for(const auto& asset:resources_)if(ImageBindingKey(asset.metadata)==key&&asset.metadata.value("mipLevel",0u)==mip){OpenResource(asset,0,false);return;}
    if(source.metadata.value("category","")!="input"){status_="Only the bound output mip is available for this attachment.";return;}
    const auto capture=capture_;const auto execution=selectedExecution_,subdraw=selectedSubdraw_;const auto generation=selectionGeneration_;
    const auto mode=replayMode_;const auto serial=androidSerial_;const auto tools=tools_;const auto memory=memoryTranslation_;const auto overrides=activeOverrides_;
    Start("Reading texture mip "+std::to_string(mip),[this,capture,execution,subdraw,generation,source,key,mip,mode,serial,tools,memory,overrides]{
        ReplayRequest request;request.capture=capture->source;request.expectedCaptureHash=capture->fingerprint;request.output=sessionsDir_/std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        request.frame=capture->executions.at(execution).frame;request.dump=InputMipDumpRequest(*capture,execution,source.metadata,mip);
        request.outputsOnly=true;request.descriptorImages=true;request.memoryTranslation=memory;request.overrides=overrides;
        request.cacheDirectory=sessionsDir_.parent_path()/"replay-cache";
        request.indirectSubdraw=subdraw;request.indirectDrawIdSafe=true;request.visualizationPipeline=capture->events.at(capture->executions.at(execution).event).pipeline;
        const auto target=ResolveReplayTarget(mode,capture->android,serial,tools.at("adb"),&cancel_);
        const auto replay=ReplayCapture(request,target,tools,&cancel_);
        for(auto asset:replay.resources)if(ImageBindingKey(asset.metadata)==key&&asset.metadata.value("mipLevel",0u)==mip){
            EnrichImageMetadata(*capture,asset.metadata);
            if(IsAstcArtifact(asset.file,asset.metadata)){
                const auto decoded=DecodeCompressedImage(asset.file,asset.metadata,DecodedPngPath(asset.file),tools,&cancel_);
                if(decoded.value("status",std::string{})!="ok")throw std::runtime_error("Mip decode failed: "+decoded.value("reason",std::string("unknown")));
            }
            WorkspaceResult result;result.selectionGeneration=generation;result.mipResource=std::move(asset);result.message="Texture mip "+std::to_string(mip)+" ready";return result;
        }
        throw std::runtime_error("Replay did not return the selected texture mip; previous preview retained");
    },JobKind::Resource);
}
void Workspace::RestoreImageSelection(){
    if(!pendingImageSelection_)return;
    const auto key=ImageBindingKey(*pendingImageSelection_);const auto mip=pendingImageSelection_->value("mipLevel",0u);pendingImageSelection_.reset();
    for(const auto& asset:resources_)if(ImageBindingKey(asset.metadata)==key&&asset.metadata.value("mipLevel",0u)==mip){OpenResource(asset,0,false);return;}
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
                if (!Busy() && !pendingDraw_ && capture_ && (capture_->events[selectedEvent_].block != dumpBlock_ || selectedExecution_ != dumpExecution_))
                    ReportProblem("Resource data is from a previous selection","Dump the new selection to refresh its resource contents.","stale-resources");
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
            if(validationReport_.is_null())ImGui::TextWrapped("Enable Vulkan validation in Settings > General > Replay to run a fresh validation replay.");
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
            bool formatChanged = ImGui::Combo("Type", &scalarType_, "uint8\0uint16\0uint32\0uint64\0float32\0float64\0");
            ImGui::SetNextItemWidth(100); formatChanged |= ImGui::SliderInt("Values per row", &bufferColumns_, 1, 8);
            ImGui::SetNextItemWidth(100); ImGui::InputInt("Stride (bytes)", &bufferStride_);
            const bool strideCommitted = ImGui::IsItemDeactivatedAfterEdit();
            const int widths[] = {1, 2, 4, 8, 4, 8};
            if (bufferStride_ < bufferColumns_ * widths[scalarType_] || bufferStride_ > 4096) {
                if (formatChanged || strideCommitted) ReportProblem("Buffer preview unavailable","Set a stride large enough for the selected values (maximum 4096 bytes).");
            }
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
