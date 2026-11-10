#pragma once
#include "analysis/resources.h"

namespace gfx {
inline std::string ImageBindingKey(const Json& metadata) {
    Json key=Json::array();
    for(const auto* name:{"category","stage","set","binding","arrayIndex","location","imageId","aspect","arrayLayer","depth"})
        key.push_back(metadata.value(name,Json{}));
    return key.dump();
}
inline void EnrichImageMetadata(const Capture& capture,Json& metadata) {
    const auto id=metadata.find("imageId");if(id==metadata.end()||!id->is_number_integer()||id->get<int64_t>()<=0)return;
    const auto object=capture.objects.find(id->get<uint64_t>());if(object==capture.objects.end()||!object->second.createInfo.is_object())return;
    for(const auto* key:{"mipLevels","arrayLayers"}){
        const auto found=object->second.createInfo.find(key);
        if(found!=object->second.createInfo.end()&&found->is_number_integer()&&found->get<int64_t>()>0)metadata[key]=*found;
    }
}
inline Json InputMipDumpRequest(const Capture& capture,size_t execution,const Json& metadata,uint32_t mip) {
    if(metadata.value("category","")!="input")throw std::runtime_error("Only bound input textures support additional mip readback");
    auto request=capture.DumpRequest(execution);
    if(!request.contains("Draw")||request["Draw"].empty()||request["Draw"].back().empty())throw std::runtime_error("Input mip inspection requires a graphics draw");
    const auto index=capture.events.at(capture.executions.at(execution).event).block;
    request["Draw"].back()[0]={{"Index",index},{"Descriptors",Json::array({{
        {"Set",metadata.at("set")},{"Binding",metadata.at("binding")},{"ArrayIndex",metadata.value("arrayIndex",0u)},
        {"SubresourceRange",{{"AspectMask","VK_IMAGE_ASPECT_NONE"},{"BaseMipLevel",mip},{"LevelCount",1},{"BaseArrayLayer",metadata.value("arrayLayer",0u)},
            {"LayerCount",1},{"BaseZIndex",metadata.value("depth",0u)},{"ZCount",1}}}}})}};
    return request;
}
// One sidebar/table entry per binding. Prefer its base/lowest fetched mip.
inline std::vector<const ResourceArtifact*> ImageBindingRepresentatives(const std::vector<ResourceArtifact>& resources) {
    std::map<std::string,const ResourceArtifact*> grouped;
    for(const auto& asset:resources){
        if(!asset.metadata.contains("imageId"))continue;
        auto& entry=grouped[ImageBindingKey(asset.metadata)];
        if(!entry||asset.metadata.value("mipLevel",0u)<entry->metadata.value("mipLevel",0u))entry=&asset;
    }
    std::vector<const ResourceArtifact*> result;for(const auto& item:grouped)result.push_back(item.second);return result;
}
}
