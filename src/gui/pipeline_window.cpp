#include "gui/workspace.h"
#include "gui/refresh_overlay.h"
#include <algorithm>
#include <set>
#include <tuple>
#include <imgui_internal.h>

namespace gfx {
namespace {
std::string Value(const Json& value) { return value.is_string() ? value.get<std::string>() : value.dump(); }
void Flatten(const Json& value, const std::string& prefix, std::vector<std::pair<std::string, std::string>>& rows, unsigned depth = 0) {
    if (rows.size() >= 4000 || depth > 12) return;
    if (value.is_object()) for (auto it = value.begin(); it != value.end(); ++it) {
        if (it.key() == "sType" || it.value().is_null()) continue;
        Flatten(it.value(), prefix.empty() ? it.key() : prefix + " / " + it.key(), rows, depth + 1);
    } else if (value.is_array()) for (size_t i = 0; i < value.size(); ++i) Flatten(value[i], prefix + "[" + std::to_string(i) + "]", rows, depth + 1);
    else rows.emplace_back(prefix, Value(value));
}
void Properties(const char* id, const Json& value) {
    if (value.is_null() || value.empty()) { ImGui::TextDisabled("Not recorded / not bound"); return; }
    std::vector<std::pair<std::string, std::string>> rows; Flatten(value, "", rows);
    if (rows.empty()) { ImGui::TextDisabled("Not recorded / not bound"); return; }
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f); ImGui::TableHeadersRow();
        for (const auto& [field, text] : rows) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextWrapped("%s", field.c_str());
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", text.c_str());
        }
        ImGui::EndTable();
    }
}
void Records(const char* id, const Json& value) {
    if (!value.is_array() || value.empty() || !value.front().is_object()) { Properties(id, value); return; }
    std::set<std::string> names;
    for (const auto& row : value) if (row.is_object()) for (auto it = row.begin(); it != row.end(); ++it)
        if (!it.value().is_structured() && !it.value().is_null() && it.key() != "sType") names.insert(it.key());
    if (names.empty() || names.size() > 12) { Properties(id, value); return; }
    if (ImGui::BeginTable(id, static_cast<int>(names.size()), ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX)) {
        for (const auto& name : names) ImGui::TableSetupColumn(name.c_str(), ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableHeadersRow();
        for (const auto& row : value) { ImGui::TableNextRow(); for (const auto& name : names) {
            ImGui::TableNextColumn(); ImGui::TextUnformatted(row.contains(name) ? Value(row[name]).c_str() : "-");
        } }
        ImGui::EndTable();
    }
}
}
void Workspace::JumpToPipeline(uint64_t pipeline) {
    if (!capture_ || !capture_->pipelineInfo.count(pipeline)) return;
    inspectedPipeline_ = pipeline; focusPipeline_ = true;
}
void Workspace::DrawPipeline() {
    if(focusPipeline_){ImGui::SetNextWindowFocus();focusPipeline_=false;}
    if(!ImGui::Begin("Pipeline")){ImGui::End();return;}
    if(!capture_){ImGui::TextDisabled("Select an event to inspect its pipeline.");ImGui::End();return;}
    {
    RefreshOverlay refresh(Refreshing("Pipeline"));
    const auto& event=capture_->events[selectedEvent_];const auto pipeline=inspectedPipeline_?inspectedPipeline_:event.pipeline;
    const auto execution=selectedExecution_>=0&&pipeline==event.pipeline?&capture_->executions.at(selectedExecution_):nullptr;
    ImGui::SetNextItemWidth(240);
    if(ImGui::BeginCombo("##pipeline",("Pipeline "+std::to_string(pipeline)).c_str())){
        std::vector<uint64_t> ids;for(const auto& item:capture_->pipelines)ids.push_back(item.first);
        ImGuiListClipper clip;clip.Begin(static_cast<int>(ids.size()));
        while(clip.Step())for(int i=clip.DisplayStart;i<clip.DisplayEnd;++i){const auto id=ids[i];const auto object=capture_->objects.find(id);
            const auto name=std::to_string(id)+(object!=capture_->objects.end()&&!object->second.name.empty()?"  "+object->second.name:"");
            if(ImGui::Selectable(name.c_str(),id==pipeline))JumpToPipeline(id);
        }ImGui::EndCombo();
    }
    ImGui::SameLine();ImGui::TextDisabled("Block %llu | submit %llu",event.block,execution?execution->submit:0);
    const auto found=capture_->pipelineInfo.find(pipeline);const Json info=found==capture_->pipelineInfo.end()?Json::object():found->second;
    const Json state=pipeline==event.pipeline&&event.state?*event.state:Json::object();
    const auto stages=capture_->pipelines.find(pipeline);
    const char* labels[]={"VTX","VS","TCS","TES","GS","Rasterizer","FS","FB","CS"};
    const char* keys[]={"","VERTEX","TESSELLATION_CONTROL","TESSELLATION_EVALUATION","GEOMETRY","","FRAGMENT","","COMPUTE"};
    auto stageFor=[&](int index)->const ShaderStage*{if(stages!=capture_->pipelines.end()&&*keys[index])for(const auto& stage:stages->second)if(stage.stage.find(keys[index])!=std::string::npos)return &stage;return nullptr;};
    const bool compute=stageFor(8)!=nullptr;
    if(compute&&pipelineView_!=8)pipelineView_=8;
    else if(!compute&&pipelineView_==8)pipelineView_=6;
    ImGui::BeginChild("stage-flow",ImVec2(0,ImGui::GetFrameHeight()+20),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const float width=std::max(58.0f,(ImGui::GetContentRegionAvail().x-8*(ImGui::GetFontSize()+14))/9);
    bool changedStage=false;
    for(int i=0;i<9;++i){
        if(i){ImGui::SameLine(0,6);ImGui::TextDisabled(i==8?"|":"\xe2\x86\x92");ImGui::SameLine(0,6);}
        const bool active=i==0||i==5||i==7?!compute:stageFor(i)!=nullptr;
        ImGui::BeginDisabled(!active);
        const bool selected=pipelineView_==i;
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if(ImGui::Button(labels[i],ImVec2(i==5?std::max(width,85.0f):width,ImGui::GetFrameHeight()))){pipelineView_=i;changedStage=true;}
        if(selected)ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if(!active)ImGui::SetItemTooltip("Not bound in this pipeline");
    }
    if(ImGui::IsWindowHovered()&&ImGui::GetIO().MouseWheel!=0)ImGui::SetScrollX(ImGui::GetScrollX()-ImGui::GetIO().MouseWheel*80);
    ImGui::EndChild();
    if(changedStage)ImGui::SetNextWindowScroll(ImVec2(0,0));
    ImGui::BeginChild("stage-details");
    const char* headings[]={"Vertex input","Vertex shader","Tessellation control","Tessellation evaluation","Geometry shader","Rasterizer","Fragment shader","Framebuffer / output merger","Compute shader"};
    ImGui::SeparatorText(headings[std::clamp(pipelineView_,0,8)]);
    auto section=[&](const char* name,const Json& values){if(ImGui::CollapsingHeader(name,ImGuiTreeNodeFlags_DefaultOpen))Records(name,values);};
    auto number=[](const Json& value,const char* key)->uint64_t{const auto it=value.find(key);return it!=value.end()&&it->is_number()?it->get<uint64_t>():0;};
    auto bufferRows=[&](const char* heading,const std::vector<const ResourceArtifact*>& rows){
        if(!ImGui::CollapsingHeader(heading,ImGuiTreeNodeFlags_DefaultOpen))return;
        auto ordered=rows;std::stable_sort(ordered.begin(),ordered.end(),[&](const auto* a,const auto* b){return std::make_tuple(number(a->metadata,"set"),number(a->metadata,"binding"),number(a->metadata,"vertexBufferBinding"),number(a->metadata,"arrayIndex"))<std::make_tuple(number(b->metadata,"set"),number(b->metadata,"binding"),number(b->metadata,"vertexBufferBinding"),number(b->metadata,"arrayIndex"));});
        if(rows.empty()){ImGui::TextDisabled("None bound / no readable data");return;}
        if(ImGui::BeginTable(heading,5,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)){
            for(auto* name:{"Set / binding","Buffer","Offset","Bytes","Go"})ImGui::TableSetupColumn(name);ImGui::TableHeadersRow();
            int row=0;for(const auto* asset:ordered){const auto& m=asset->metadata;ImGui::PushID(row++);ImGui::TableNextRow();ImGui::TableNextColumn();
                if(m.value("category","")=="mesh")ImGui::Text("%llu",number(m,"vertexBufferBinding"));else ImGui::Text("%llu / %llu [%llu]",number(m,"set"),number(m,"binding"),number(m,"arrayIndex"));ImGui::TableNextColumn();
                const auto id=number(m,"bufferId");const auto object=capture_->objects.find(id);const auto name=object!=capture_->objects.end()&&!object->second.name.empty()?object->second.name:std::to_string(id);
                if(ImGui::Selectable(name.c_str()))OpenResource(*asset);ImGui::TableNextColumn();ImGui::Text("%llu",number(m,"offset"));ImGui::TableNextColumn();ImGui::Text("%llu",asset->size);ImGui::TableNextColumn();
                if(ImGui::SmallButton("View"))OpenResource(*asset);ImGui::PopID();
            }ImGui::EndTable();
        }
    };
    const bool current=execution&&dumpExecution_==selectedExecution_&&dumpBlock_==event.block;
    if(pipelineView_==0){
        if(ImGui::Button("Mesh"))focusMesh_=true;
        const auto input=info.value("pVertexInputState",Json::object());
        section("Attributes",input.value("pVertexAttributeDescriptions",Json{}));section("Bindings",input.value("pVertexBindingDescriptions",Json{}));
        std::vector<const ResourceArtifact*> vertex,index;
        if(current)for(const auto& asset:resources_)if(asset.file.extension()==".bin"&&asset.metadata.value("category","")=="mesh"){
            (asset.role=="indexBuffer"?index:vertex).push_back(&asset);
        }
        bufferRows("Vertex buffers",vertex);bufferRows("Index buffer",index);
        section("Input assembly",info.value("pInputAssemblyState",Json{}));
    }else if(pipelineView_==5){
        section("Rasterization",info.value("pRasterizationState",Json{}));
        section("Viewports",state.value("viewports",Json{}));section("Scissors",state.value("scissors",Json{}));
        section("Multisample",info.value("pMultisampleState",Json{}));
    }else if(pipelineView_==7){
        ImGui::SeparatorText("Render targets");
        if(current)DrawStageImages("",true);else ImGui::TextDisabled("Select a submitted draw for attachment readback.");
        section("Blend",info.value("pColorBlendState",Json{}));section("Depth / stencil",info.value("pDepthStencilState",Json{}));
    }else if(const auto* stage=stageFor(pipelineView_)){
        ImGui::Text("Module %llu | entry %s",stage->module,stage->entry.c_str());ImGui::SameLine();
        if(ImGui::Button("View shader"))LoadShader(*stage);
        const auto stageName=pipelineView_==1?"vertex":pipelineView_==6?"fragment":pipelineView_==8?"compute":pipelineView_==2?"tessellationControl":pipelineView_==3?"tessellationEvaluation":"geometry";
        ImGui::SeparatorText("Resources");if(current)DrawStageImages(stageName,false);else ImGui::TextDisabled("Select a submitted draw for resource readback.");
        std::vector<const ResourceArtifact*> uniform,storage;
        if(current)for(const auto& asset:resources_)if(asset.file.extension()==".bin"&&asset.metadata.value("category","")=="input"&&asset.metadata.value("stage","")==stageName){
            (asset.metadata.value("type",std::string{}).find("STORAGE")!=std::string::npos?storage:uniform).push_back(&asset);
        }
        bufferRows("Uniform buffers",uniform);bufferRows("Storage buffers",storage);
        if(ImGui::CollapsingHeader("Samplers",ImGuiTreeNodeFlags_DefaultOpen))DrawStageSamplers(*stage,execution);
        section("Push constants",state.value("pushConstants",Json{}));
    }else ImGui::TextDisabled("No shader bound.");
    if(ImGui::CollapsingHeader("Capture details")){
        section("Dynamic commands",state.value("dynamic",Json{}));
        if(execution)for(const auto& note:execution->notes)ImGui::BulletText("%s",note.c_str());
        if(ImGui::TreeNode("API call")){const auto record=Json::parse(arguments_,nullptr,false);if(!record.is_discarded())Properties("api",record);ImGui::TreePop();}
    }
    ImGui::EndChild();
    }
    ImGui::End();
}
} // namespace gfx
