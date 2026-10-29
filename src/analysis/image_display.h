#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gfx {
struct ImageDisplayRange {
    float minimum=0,maximum=1;
    uint8_t MapValue(float sample)const{
        if(!std::isfinite(sample))return 0;
        const float span=maximum-minimum;
        if(!std::isfinite(span)||span<=0)return sample>minimum?255:0;
        return static_cast<uint8_t>(std::lround(std::clamp((sample-minimum)/span,0.0f,1.0f)*255));
    }
    uint8_t Map(uint8_t value,bool linearize=false)const{
        if(!linearize&&minimum==0&&maximum==1)return value;
        float sample=value/255.0f;
        if(linearize)sample=sample<=0.04045f?sample/12.92f:std::pow((sample+.055f)/1.055f,2.4f);
        return MapValue(sample);
    }
};
}
