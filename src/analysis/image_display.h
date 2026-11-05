#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace gfx {
enum class ImageSourceEncoding { Linear, Srgb };

// Untyped presentation files retain their display-encoded interpretation.
// Resource readbacks carry their Vulkan format, including decoded ASTC data.
inline ImageSourceEncoding SourceEncodingForImage(const std::string& format) {
    return format.empty() || format.find("_SRGB") != std::string::npos ? ImageSourceEncoding::Srgb : ImageSourceEncoding::Linear;
}
inline float SrgbToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}
inline float LinearToSrgb(float value) {
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}
inline float DecodeImageColor(uint8_t value, ImageSourceEncoding encoding) {
    const auto normalized = value / 255.0f;
    return encoding == ImageSourceEncoding::Srgb ? SrgbToLinear(normalized) : normalized;
}
// The input is linear; encoding selects the destination byte representation.
inline uint8_t EncodeImageColor(float value, ImageSourceEncoding encoding) {
    if (std::isnan(value)) return 0;
    value = std::clamp(value, 0.0f, 1.0f);
    if (encoding == ImageSourceEncoding::Srgb) value = LinearToSrgb(value);
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}
struct ImageDisplayRange {
    float minimum=0,maximum=1;
    float Normalize(float sample)const{
        if(!std::isfinite(sample))return 0;
        const float span=maximum-minimum;
        if(!std::isfinite(span)||span<=0)return sample>minimum?1.0f:0.0f;
        return std::clamp((sample-minimum)/span,0.0f,1.0f);
    }
    uint8_t MapValue(float sample)const{
        return EncodeImageColor(Normalize(sample),ImageSourceEncoding::Linear);
    }
    uint8_t MapColor(uint8_t value,ImageSourceEncoding sourceEncoding,bool srgbDisplay)const{
        // Retain float precision through both transfer functions and the range.
        // In particular, dark sRGB values must not quantize to linear U8 first.
        return EncodeImageColor(Normalize(DecodeImageColor(value,sourceEncoding)),
            srgbDisplay?ImageSourceEncoding::Srgb:ImageSourceEncoding::Linear);
    }
    uint8_t Map(uint8_t value,bool linearize=false)const{
        if(!linearize&&minimum==0&&maximum==1)return value;
        float sample=value/255.0f;
        if(linearize)sample=SrgbToLinear(sample);
        return MapValue(sample);
    }
};
}
