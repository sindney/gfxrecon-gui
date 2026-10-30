#include "gui/workspace.h"
#include "analysis/image_details.h"
#include <misc/cpp/imgui_stdlib.h>
#include <tuple>
#include "gui/refresh_overlay.h"
#include "core/image_decode.h"
#include <algorithm>

namespace gfx {
namespace {
bool IsViewableImage(const ResourceArtifact& a){return a.file.extension()==".bmp"||IsAstcArtifact(a.file,a.metadata);}
uint64_t Number(const Json& m,const char* key){const auto it=m.find(key);return it!=m.end()&&it->is_number()?it->get<uint64_t>():0;}
std::string ImageBinding(const ResourceArtifact& asset){const auto& m=asset.metadata;
    if(m.value("category","")=="input")return m.value("stage",std::string("shader"))+" s"+std::to_string(Number(m,"set"))+" b"+std::to_string(Number(m,"binding"))+"["+std::to_string(Number(m,"arrayIndex"))+"]";
    return m.value("aspect",std::string("Output"))+" "+std::to_string(Number(m,"location"));
}
std::string ImageName(const ResourceArtifact& asset){return "Image "+std::to_string(Number(asset.metadata,"imageId"))+" | "+asset.metadata.value("format",std::string("decoded RGB"));}
}

void Workspace::DrawBoundImages(const char* id,bool compact){
    if(!capture_||selectedExecution_<0){ImGui::TextWrapped("Select a submitted draw.");return;}
    if(dumpExecution_!=selectedExecution_||dumpBlock_!=capture_->events[selectedEvent_].block){
        ImGui::TextWrapped("%s",ExecutionState(selectedExecution_)==PanelState::Running?"Fetching bound textures and render targets...":"No image readback for this selection.");return;
    }
    ImGui::PushID(id);
    for(const auto& category:{std::string("output"),std::string("input")}){
        ImGui::SeparatorText(category=="output"?"Render targets":"Textures");
        std::vector<const ResourceArtifact*> images;
        for(const auto& image:resources_)if(IsViewableImage(image)&&image.metadata.value("category","")==category)images.push_back(&image);
        auto number=[](const Json& object,const char* key){const auto value=object.find(key);return value!=object.end()&&value->is_number()?value->get<uint64_t>():0;};
        std::stable_sort(images.begin(),images.end(),[&](const auto* a,const auto* b){return std::make_tuple(a->metadata.value("stage",std::string{}),number(a->metadata,"set"),number(a->metadata,"binding"),number(a->metadata,"arrayIndex"),number(a->metadata,"location"),a->metadata.value("aspect",std::string{}))<std::make_tuple(b->metadata.value("stage",std::string{}),number(b->metadata,"set"),number(b->metadata,"binding"),number(b->metadata,"arrayIndex"),number(b->metadata,"location"),b->metadata.value("aspect",std::string{}));});
        if(images.empty()){ImGui::TextDisabled("None readable");continue;}
        if(ImGui::BeginTable(category.c_str(),compact?2:4,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)){
            ImGui::TableSetupColumn("Binding");ImGui::TableSetupColumn("Image");if(!compact){ImGui::TableSetupColumn("Format");ImGui::TableSetupColumn("Size");}ImGui::TableHeadersRow();
            size_t row=0;for(const auto* image:images){const auto& m=image->metadata;
                std::string binding=category=="output"?m.value("aspect",std::string("COLOR"))+" "+std::to_string(number(m,"location")):m.value("stage",std::string("shader"))+" s"+std::to_string(number(m,"set"))+" b"+std::to_string(number(m,"binding"))+"["+std::to_string(number(m,"arrayIndex"))+"]";
                ImGui::PushID(static_cast<int>(row++));ImGui::TableNextRow();ImGui::TableNextColumn();
                ImGui::BeginDisabled(Busy()&&!autoReplayRunning_);
                if(ImGui::Selectable(binding.c_str(),selectedDrawImage_==image->file&&resourceMetadata_==m))OpenResource(*image);
                ImGui::EndDisabled();ImGui::SetItemTooltip("Open image %llu in Image\n%s",number(m,"imageId"),image->file.filename().u8string().c_str());
                ImGui::TableNextColumn();const auto object=capture_->objects.find(number(m,"imageId"));
                const auto name=object!=capture_->objects.end()&&!object->second.name.empty()?object->second.name:std::to_string(number(m,"imageId"));if(ImGui::Selectable(name.c_str(),selectedDrawImage_==image->file&&resourceMetadata_==m))OpenResource(*image);
                if(!compact){ImGui::TableNextColumn();ImGui::TextUnformatted(m.value("format",std::string{}).c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(m.value("dimensions",m.value("extent",Json())).dump().c_str());}
                ImGui::PopID();
            }ImGui::EndTable();
        }
    }ImGui::PopID();
}
void Workspace::InvalidateDrawResources(){
    resources_.clear();dumpExecution_=-1;dumpBlock_=0;drawHasInputs_=false;
    executionStatuses_.clear();
    ResetImageDocuments();resourceMode_=0;imageShownGeneration_=0;
}
void Workspace::DrawStageImages(const std::string& stage,bool outputs){
    std::vector<const ResourceArtifact*> images;
    for(const auto& asset:resources_)if(IsViewableImage(asset)&&asset.metadata.value("category","")== (outputs?"output":"input")&&(outputs||asset.metadata.value("stage","")==stage))images.push_back(&asset);
    std::stable_sort(images.begin(),images.end(),[](const auto* a,const auto* b){return std::make_tuple(Number(a->metadata,"set"),Number(a->metadata,"binding"),Number(a->metadata,"arrayIndex"),Number(a->metadata,"location"))<std::make_tuple(Number(b->metadata,"set"),Number(b->metadata,"binding"),Number(b->metadata,"arrayIndex"),Number(b->metadata,"location"));});
    if(images.empty()){ImGui::TextDisabled("None bound / no readable images");return;}
    if(ImGui::BeginTable("stage-images",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)){
        for(const auto* name:{"Binding","Type","Resource","Contents","Format"})ImGui::TableSetupColumn(name);ImGui::TableHeadersRow();
        int row=0;for(const auto* asset:images){const auto& m=asset->metadata;ImGui::PushID(row++);ImGui::TableNextRow();ImGui::TableNextColumn();
            if(ImGui::Selectable(ImageBinding(*asset).c_str()))OpenResource(*asset);
            ImGui::TableNextColumn();ImGui::TextUnformatted(outputs?"Attachment":"Texture");ImGui::TableNextColumn();
            const auto id=Number(m,"imageId");const auto found=capture_->objects.find(id);const auto name=found!=capture_->objects.end()&&!found->second.name.empty()?found->second.name:"Image "+std::to_string(id);
            if(ImGui::Selectable(name.c_str()))OpenResource(*asset);
            ImGui::SetItemTooltip("%s",ImageDetails(m).c_str());ImGui::TableNextColumn();
            const auto dimensions=m.value("dimensions",m.value("extent",Json::array()));
            if(dimensions.is_array()&&dimensions.size()>=2)ImGui::Text("%s x %s",dimensions[0].dump().c_str(),dimensions[1].dump().c_str());
            ImGui::TableNextColumn();const auto format=m.value("format",std::string{});ImGui::TextUnformatted(format.c_str());ImGui::SetItemTooltip("%s",format.c_str());ImGui::PopID();
        }ImGui::EndTable();
    }
}
void Workspace::DrawStageSamplers(const ShaderStage& stage,const Execution* execution){
    if(!execution){ImGui::TextDisabled("No submitted bindings");return;}
    const auto samplers=CollectStageSamplers(*capture_,*execution,stage);
    if(samplers.empty()){ImGui::TextDisabled("No recorded sampler bindings for this stage.");return;}
    if(ImGui::BeginTable("stage-samplers",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)){
        for(const auto* title:{"Set / binding","Sampler","Filter","Address mode"})ImGui::TableSetupColumn(title);ImGui::TableHeadersRow();
        for(const auto& slot:samplers){
            const auto object=capture_->objects.find(slot.sampler);const auto info=object!=capture_->objects.end()?object->second.createInfo:Json::object();
            ImGui::PushID((std::to_string(slot.set)+":"+std::to_string(slot.binding)+":"+std::to_string(slot.arrayIndex)).c_str());
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%llu / %llu [%llu]",slot.set,slot.binding,slot.arrayIndex);
            ImGui::TableNextColumn();
            if(ImGui::Selectable(("Sampler "+std::to_string(slot.sampler)).c_str())){objectDetails_=info;objectLabel_="Sampler "+std::to_string(slot.sampler);resourceTab_=5;resourceTabRequest_=true;focusResources_=true;}
            ImGui::SetItemTooltip("%s%s",slot.immutable?"Immutable sampler\n":"",info.dump(2).c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(info.value("magFilter",Json()).dump().c_str());
            ImGui::TableNextColumn();ImGui::TextUnformatted(info.value("addressModeU",Json()).dump().c_str());ImGui::PopID();
        }ImGui::EndTable();
    }
}
void Workspace::ResetImageDocuments(){
    for(auto& [path,thumbnail]:assetThumbnails_)thumbnail.Release();
    assetThumbnails_.clear();failedThumbnails_.clear();imageTabs_.clear();imageTab_=-1;imageTabRequest_=false;
    resourceImage_.Release();selectedDrawImage_.clear();
}
void Workspace::DrawImages(){
    if(focusImage_){ImGui::SetNextWindowFocus();focusImage_=false;}
    if(!ImGui::Begin("Image")){ImGui::End();return;}
    {
    RefreshOverlay refresh(Refreshing("Image"));
    if(capture_&&selectedExecution_>=0&&capture_->events[selectedEvent_].name.rfind("vkCmdDraw",0)==0){
        int mode=drawVisualization_;ImGui::SetNextItemWidth(210);
        if(ImGui::Combo("##draw-visualization",&mode,"Original\0Solid coverage\0Wireframe\0Depth pass & fail\0"))SetDrawVisualization(mode,selectedSubdraw_);
        ImGui::SetItemTooltip("Solid/wireframe: geometry without depth occlusion.\nDepth: green passes, red fails against depth before the selected draw/batch.\nFragment discard, stencil and shader depth writes are not emulated.");
        const auto args=capture_->ReadEvent(selectedEvent_).value("function",Json::object()).value("args",Json::object());
        const auto count=args.value("drawCount",0u);
        if(capture_->events[selectedEvent_].name.find("Indirect")!=std::string::npos&&count){
            ImGui::SameLine();ImGui::SetNextItemWidth(230);const auto label=selectedSubdraw_<0?"All "+std::to_string(count)+" draws":"Draw "+std::to_string(selectedSubdraw_)+" / "+std::to_string(count);
            if(ImGui::BeginCombo("##indirect-subdraw",label.c_str())){
                if(ImGui::Selectable(("All "+std::to_string(count)+" draws").c_str(),selectedSubdraw_<0))SetDrawVisualization(drawVisualization_,-1);
                ImGuiListClipper clipper;clipper.Begin(static_cast<int>(std::min(count,65536u)));
                while(clipper.Step())for(int index=clipper.DisplayStart;index<clipper.DisplayEnd;++index){const auto i=static_cast<uint32_t>(index);std::string name="Draw "+std::to_string(i);
                    if(indirectParameters_.contains("indirectParams")&&i<indirectParameters_["indirectParams"].size()){const auto& p=indirectParameters_["indirectParams"][i];name+=" | "+p.value("indexCount",p.value("vertexCount",Json())).dump()+" vertices/indices, "+p.value("instanceCount",Json()).dump()+" instances";}
                    name+="###subdraw-"+std::to_string(i);
                    if(ImGui::Selectable(name.c_str(),selectedSubdraw_==int(i)))SetDrawVisualization(drawVisualization_,int(i));
                }ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Earlier subdraws replay normally; the chosen subdraw is inspected or highlighted. Later subdraws are omitted.");
        }
    }
    const bool matches=capture_&&selectedExecution_>=0&&dumpExecution_==selectedExecution_&&dumpBlock_==capture_->events[selectedEvent_].block;
    if(resourceMode_==2){
        if(ImGui::Button("Back to images"))resourceMode_=0;
        DrawBuffer();
    }else{
        // Every selection initially opens color, independently of the previously viewed input slot.
        if(matches&&imageView_!=0&&imageTabs_.empty()&&!Refreshing("Image")){
            const ResourceArtifact* initial=nullptr;
            for(const auto& asset:resources_)if(IsViewableImage(asset)&&asset.metadata.value("category","")=="output"){
                if(!initial)initial=&asset;
                if(asset.metadata.value("aspect","")=="COLOR"){initial=&asset;break;}
            }
            if(initial)OpenResource(*initial,0,false);
        }
        const float available=ImGui::GetContentRegionAvail().x;
        const float sidebar=std::clamp(available*.23f,170.0f,280.0f);
        ImGui::BeginChild("image-document",ImVec2(std::max(120.0f,available-sidebar-8),0));
        if(ImGui::BeginTabBar("image-documents",ImGuiTabBarFlags_FittingPolicyScroll|ImGuiTabBarFlags_AutoSelectNewTabs)){
            if(ImGui::BeginTabItem("Frame presentation",nullptr,imageTabRequest_&&imageTab_<0?ImGuiTabItemFlags_SetSelected:0)){
                if(!imageTabRequest_||imageTab_<0){imageTab_=-1;imageView_=0;resourceMode_=0;}
                ImGui::EndTabItem();
            }
            for(size_t i=0;i<imageTabs_.size();){
                const auto label=ImageBinding(imageTabs_[i])+"###asset-tab-"+std::to_string(i);bool open=true;
                if(ImGui::BeginTabItem(label.c_str(),&open,imageTabRequest_&&imageTab_==int(i)?ImGuiTabItemFlags_SetSelected:0)){
                    if((!imageTabRequest_||imageTab_==int(i))&&(imageTab_!=int(i)||!resourceImage_.Width()))OpenResource(imageTabs_[i]);
                    ImGui::EndTabItem();
                }
                if(!open){imageTabs_.erase(imageTabs_.begin()+i);if(imageTab_>=int(i)){imageTab_=-1;imageView_=0;resourceMode_=0;imageTabRequest_=true;}continue;}++i;
            }
            ImGui::EndTabBar();imageTabRequest_=false;
        }
        if(imageTab_>=0&&imageTab_<int(imageTabs_.size())){
            const auto& asset=imageTabs_[imageTab_];
            ImGui::TextDisabled("%s | mip %llu | slice %llu",ImageName(asset).c_str(),Number(asset.metadata,"mipLevel"),Number(asset.metadata,"arrayLayer"));ImGui::SetItemTooltip("%s",ImageDetails(asset.metadata).c_str());
            resourceImage_.Draw();
        }else if(hasComparison_){
            ImGui::Combo("Compare",&comparisonMode_,"Side by side\0Absolute difference\0");
            if(comparisonMode_==1)differenceImage_.Draw();
            else{ImGui::Columns(2,"comparison",true);ImGui::PushID("baseline");image_.Draw();ImGui::PopID();ImGui::NextColumn();ImGui::PushID("modified");modifiedImage_.Draw();ImGui::PopID();ImGui::Columns(1);}
        }else image_.Draw();
        ImGui::EndChild();ImGui::SameLine(0,8);
        ImGui::BeginChild("image-bindings",ImVec2(0,0));
        if(ImGui::BeginTabBar("image-binding-tabs")){
            if(ImGui::BeginTabItem("Inputs")){imageSidebar_=0;ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Outputs",nullptr,imageTabs_.empty()?ImGuiTabItemFlags_SetSelected:0)){imageSidebar_=1;ImGui::EndTabItem();}
            ImGui::EndTabBar();
        }
        ImGui::BeginChild("asset-list");
        if(!matches)ImGui::TextWrapped("Select a draw to inspect its inputs and outputs.");
        else{
            int row=0;bool decoded=false,any=false;
            std::vector<const ResourceArtifact*> assets;
            for(const auto& asset:resources_)if(IsViewableImage(asset)&&asset.metadata.value("category","")== (imageSidebar_?"output":"input"))assets.push_back(&asset);
            std::stable_sort(assets.begin(),assets.end(),[](const auto* a,const auto* b){return std::make_tuple(a->metadata.value("stage",std::string{}),Number(a->metadata,"set"),Number(a->metadata,"binding"),Number(a->metadata,"arrayIndex"),Number(a->metadata,"location"),a->metadata.value("aspect",std::string{}))<std::make_tuple(b->metadata.value("stage",std::string{}),Number(b->metadata,"set"),Number(b->metadata,"binding"),Number(b->metadata,"arrayIndex"),Number(b->metadata,"location"),b->metadata.value("aspect",std::string{}));});
            for(const auto* item:assets){const auto& asset=*item;
                any=true;ImGui::PushID(row++);auto& thumbnail=assetThumbnails_[asset.file];
                // Bound thumbnail storage and decode one visible preview per UI frame.
                if(ImGui::IsRectVisible(ImVec2(100,112))&&!decoded&&!thumbnail.Width()&&!failedThumbnails_.count(asset.file)&&assetThumbnails_.size()<=128){
                    try{thumbnail.LoadThumbnailBmp(asset.file);}catch(...){failedThumbnails_.insert(asset.file);}decoded=true;
                }
                const auto label=ImageBinding(asset);
                const bool selected=selectedDrawImage_==asset.file&&resourceMetadata_==asset.metadata&&imageTab_>=0;
                if(thumbnail.DrawPreview((label+"##asset").c_str(),ImVec2(std::max(100.0f,ImGui::GetContentRegionAvail().x),112),selected))OpenResource(asset);
                ImGui::SetItemTooltip("%s",ImageDetails(asset.metadata).c_str());
                ImGui::TextWrapped("%s",label.c_str());ImGui::TextDisabled("%s",ImageName(asset).c_str());ImGui::Spacing();ImGui::PopID();
            }
            if(!any)ImGui::TextWrapped(imageSidebar_?"No readable render targets.":"No readable input textures.");
        }
        ImGui::EndChild();ImGui::EndChild();
    }
    }
    ImGui::End();
}
}
