#include "core/image_io.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gfx::tests {
void ImageDisplayOutlineTests(const std::filesystem::path&) {
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(std::string("Image display outline: ")+message);};
    Image original;original.width=original.height=7;original.rgba.resize(7*7*4);
    for(size_t i=0;i<original.rgba.size();i+=4){original.rgba[i]=uint8_t(20+i/4);original.rgba[i+1]=40;original.rgba[i+2]=60;original.rgba[i+3]=17;}
    auto coverageA=original,coverageB=original;
    for(unsigned y=2;y<=4;++y)for(unsigned x=2;x<=4;++x)coverageB.rgba[(y*7+x)*4]^=1;
    const auto originalBytes=original.rgba,coverageABytes=coverageA.rgba,coverageBBytes=coverageB.rgba;
    const auto legacy=ComposeDrawOutline(original,coverageA,coverageB,1);
    const auto encoded=ComposeDrawOutline(original,coverageA,coverageB,1,ImageSourceEncoding::Srgb);
    const auto linear=ComposeDrawOutline(original,coverageA,coverageB,1,ImageSourceEncoding::Linear);
    require(legacy.rgba==encoded.rgba,"default callers must preserve the existing display-encoded palette");
    const auto expectedLinearGreen=static_cast<uint8_t>(std::lround(SrgbToLinear(140.0f/255.0f)*255.0f));
    require(expectedLinearGreen==67,"linear palette must derive from encoded140 rather than linear0.55");
    unsigned outlined=0;
    for(unsigned y=0;y<7;++y)for(unsigned x=0;x<7;++x){
        const auto p=(y*7+x)*4;const bool boundary=x>=2&&x<=4&&y>=2&&y<=4&&(x==2||x==4||y==2||y==4);
        if(boundary){
            ++outlined;
            require(encoded.rgba[p]==255&&encoded.rgba[p+1]==140&&encoded.rgba[p+2]==0&&encoded.rgba[p+3]==255,"encoded outline RGB/opaque alpha");
            require(linear.rgba[p]==255&&linear.rgba[p+1]==67&&linear.rgba[p+2]==0&&linear.rgba[p+3]==255,"linear outline RGB/opaque alpha");
            const auto displayed=static_cast<int>(std::lround(LinearToSrgb(linear.rgba[p+1]/255.0f)*255.0f));
            require(std::abs(displayed-140)<=1,"linear outline must re-encode to the same displayed orange");
        }else{
            require(std::equal(original.rgba.begin()+p,original.rgba.begin()+p+4,linear.rgba.begin()+p),"linear background and covered interior bytes must remain unchanged");
            require(std::equal(original.rgba.begin()+p,original.rgba.begin()+p+4,encoded.rgba.begin()+p),"encoded background and covered interior bytes must remain unchanged");
        }
    }
    require(outlined==8,"outline coverage geometry changed");
    require(ComposeDrawOutline(original,coverageA,coverageA,1,ImageSourceEncoding::Linear).rgba==originalBytes,"empty coverage must preserve all original bytes");
    require(original.rgba==originalBytes&&coverageA.rgba==coverageABytes&&coverageB.rgba==coverageBBytes,"compositing must not mutate original/coverage inputs used by raw comparisons");
    const auto overlay=MakeDrawOverlay(coverageA,coverageB,original,ImageSourceEncoding::Srgb);
    require(overlay.rgba[0+3]==0&&overlay.rgba[(3*7+3)*4+3]==192,"overlay must be transparent outside independently measured coverage");
    const auto boundary=MakeDrawOverlay(coverageA,coverageB,original,ImageSourceEncoding::Srgb,true);
    require(boundary.rgba[3]==0&&boundary.rgba[(3*7+3)*4+3]==255,"two-pixel outline must be a separate RGBA annotation");
    auto smaller=coverageA;smaller.width=6;bool mismatch=false;
    try{MakeDrawOverlay(smaller,coverageB,original,ImageSourceEncoding::Linear);}catch(const ImageExtentMismatch&){mismatch=true;}
    require(mismatch,"unmatched attachment masks must not be resized silently");
    require(original.rgba==originalBytes,"scalar overlay preparation changed source pixels");
}
}
