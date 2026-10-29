#pragma once
#include "analysis/capture.h"

namespace gfx {
inline std::string ImageDetails(const Json& m){
    auto number=[&](const char* key){const auto it=m.find(key);return it!=m.end()&&it->is_number()?std::to_string(it->get<uint64_t>()):std::string("0");};
    std::string format=m.value("format",std::string("Decoded RGB"));if(format.rfind("VK_FORMAT_",0)==0)format.erase(0,10);
    std::string text="Image "+number("imageId");const auto aspect=m.value("aspect",std::string{});if(!aspect.empty())text+=" | "+aspect;
    text+="\n"+format;
    const auto dimensions=m.value("dimensions",m.value("extent",Json::array()));
    if(dimensions.is_array()&&dimensions.size()>=2&&dimensions[0].is_number()&&dimensions[1].is_number()){
        text+="\n"+std::to_string(dimensions[0].get<uint64_t>())+" x "+std::to_string(dimensions[1].get<uint64_t>());
        if(dimensions.size()>2&&dimensions[2].is_number()&&dimensions[2].get<uint64_t>()>1)text+=" x "+std::to_string(dimensions[2].get<uint64_t>());
    }
    text+="\nMip "+number("mipLevel")+" | Layer "+number("arrayLayer");
    if(m.value("category","")=="input")text+="\n"+m.value("stage",std::string("Shader"))+" | Set "+number("set")+" | Binding "+number("binding")+"["+number("arrayIndex")+"]";
    return text;
}
}
