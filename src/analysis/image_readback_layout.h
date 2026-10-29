#pragma once
#include <algorithm>
#include <cstdint>
namespace gfx {
struct ImageReadbackLayout {
    uint32_t levels=0,layers=0,baseLayer=0;
    uint32_t Index(uint32_t mip,uint32_t layer)const{return mip*layers+layer-baseLayer;}
};
inline ImageReadbackLayout PlanImageReadback(bool volume,uint32_t levels,uint32_t layers,uint32_t baseMip,uint32_t mipCount,uint32_t baseLayer,uint32_t layerCount){
    if(!levels||!layers||baseMip>=levels||!mipCount||(!volume&&(baseLayer>=layers||!layerCount)))return {};
    return {static_cast<uint32_t>(std::min<uint64_t>(levels,uint64_t(baseMip)+mipCount)),
        volume?layers:std::min(layers-baseLayer,layerCount),volume?0:baseLayer};
}
}
