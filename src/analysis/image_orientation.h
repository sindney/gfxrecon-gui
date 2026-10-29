#pragma once
#include <array>
#include <utility>

namespace gfx {
struct ImageOrientation {
    unsigned turns=0;bool horizontal=false,vertical=false;
    void Rotate(int steps=1){const auto amount=static_cast<unsigned>((steps%4+4)%4);turns=(turns+amount)%4;if(amount%2)std::swap(horizontal,vertical);}
    int Width(int w,int h)const{return turns%2?h:w;}
    int Height(int w,int h)const{return turns%2?w:h;}
    std::array<float,2> Uv(float u,float v)const{
        if(horizontal)u=1-u;if(vertical)v=1-v;
        switch(turns){case 1:return {v,1-u};case 2:return {1-u,1-v};case 3:return {1-v,u};default:return {u,v};}
    }
    std::array<int,2> SourcePixel(int x,int y,int w,int h)const{
        if(horizontal)x=Width(w,h)-1-x;if(vertical)y=Height(w,h)-1-y;
        switch(turns){case 1:return {y,h-1-x};case 2:return {w-1-x,h-1-y};case 3:return {w-1-y,x};default:return {x,y};}
    }
};
}
