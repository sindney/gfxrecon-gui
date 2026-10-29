#pragma once
#include <string>
#include <cstdint>
#include <cstring>
namespace gfx {
inline unsigned DepthSampleSize(const std::string& format){return format.find("D16")!=std::string::npos?2:format.find("D24")!=std::string::npos||format.find("D32")!=std::string::npos?4:0;}
inline float DecodeDepthSample(const uint8_t* bytes,const std::string& format){
    if(format.find("D16")!=std::string::npos){uint16_t v;std::memcpy(&v,bytes,2);return v/65535.0f;}
    if(format.find("D24")!=std::string::npos){uint32_t v;std::memcpy(&v,bytes,4);return (v&0xffffffu)/16777215.0f;}
    float value;std::memcpy(&value,bytes,4);return value;
}
}
