#pragma once
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace gfx {
struct PerformanceDisplayScale { double divisor=1;std::string unit; };
// Display only: provider descriptors and saved sample values retain raw units.
inline PerformanceDisplayScale PerformanceScale(const std::string& rawUnit,double magnitude){
    // Mali descriptors explicitly encode dimensionless factors as NONE.
    // Simplify the label only; raw descriptor strings/arrays remain evidence.
    const auto product=[](const std::string& text){
        std::string result;std::istringstream input(text);std::string factor;
        while(std::getline(input,factor,'*'))if(factor!="NONE"){
            if(!result.empty())result+='*';result+=factor;
        }
        return result;
    };
    const auto slash=rawUnit.find('/');
    auto unit=product(rawUnit.substr(0,slash));
    if(unit.empty())unit="1";
    if(slash!=std::string::npos){const auto denominator=product(rawUnit.substr(slash+1));if(!denominator.empty())unit+='/'+denominator;}
    if(unit=="1"&&rawUnit!="1")return {1,"1"};
    std::string suffix=unit;
    bool scalable=true;
    if(unit=="BYTE/SECOND")suffix="B/s";
    else if(unit=="BYTE")suffix="B";
    else if(unit=="INSTRUCTION/SECOND")suffix="instr/s";
    else if(unit=="FRAGMENT/SECOND")suffix="frag/s";
    else if(unit=="PRIMITIVE/SECOND")suffix="prim/s";
    else if(unit=="VERTEX/SECOND")suffix="vert/s";
    else if(unit=="BYTE/FRAGMENT")suffix="B/frag";
    else if(unit=="BYTE/VERTEX")suffix="B/vert";
    else if(unit=="VERTEX/PRIMITIVE")suffix="vert/prim";
    else if(unit=="1/FRAGMENT")suffix="1/frag";
    else if(unit=="1/PIXEL")suffix="1/px";
    else if(unit=="1/VERTEX")suffix="1/vert";
    else if(unit=="1")suffix="count";
    else if(unit=="1/SECOND")suffix="1/s";
    else if(unit=="HERTZ")suffix="Hz";
    else if(unit=="MICROSECOND"){
        if(std::abs(magnitude)>=1e6)return {1e6,"s"};
        if(std::abs(magnitude)>=1e3)return {1e3,"ms"};
        return {1,"us"};
    }
    else {scalable=false;if(unit=="PERCENT")suffix="%";}
    static const char* prefixes[]{"","k","M","G","T","P"};
    unsigned index=0;double divisor=1;
    if(scalable&&std::isfinite(magnitude))while(std::abs(magnitude)/divisor>=1000&&index<5){divisor*=1000;++index;}
    std::string prefix=prefixes[index];if(index==1&&(suffix=="B/s"||suffix=="B"))prefix="K";
    if(index&&suffix=="1/s")suffix="/s";
    return {divisor,prefix+suffix};
}
inline std::string FormatPerformanceValue(double value,const PerformanceDisplayScale& scale){
    if(!std::isfinite(value))return "—";
    value/=scale.divisor;const auto magnitude=std::abs(value);
    const int precision=magnitude==0?0:std::clamp(3-static_cast<int>(std::floor(std::log10(magnitude))),0,9);
    std::ostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;
    auto text=out.str();if(text.find('.')!=std::string::npos){while(text.back()=='0')text.pop_back();if(text.back()=='.')text.pop_back();}
    return text=="-0"?"0":text;
}
}
