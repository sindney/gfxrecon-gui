#include "gui/image_viewer.h"
#include "analysis/depth_sample.h"
#include "core/image_io.h"
#include <cstdio>

#include <imgui.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef LoadImage // windows.h macro; gfx::LoadImage is used below
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <cmath>

namespace gfx {
std::array<unsigned char, 4> ImageViewer::Pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) throw std::out_of_range("Pixel outside image");
    const auto offset = (static_cast<size_t>(y) * width_ + x) * 4;
    return {rgba_[offset], rgba_[offset + 1], rgba_[offset + 2], rgba_[offset + 3]};
}
std::array<unsigned char,4> ImageViewer::DisplayPixel(int x,int y)const{
    auto pixel=Pixel(x,y);const auto offset=static_cast<size_t>(y)*width_+x;
    for(int c=0;c<3;++c)pixel[c]=channels_[c]?(depthValues_.empty()?range_.Map(pixel[c],!scalar_&&!srgbDisplay_):range_.MapValue(depthValues_[offset])):0;
    if(!channels_[3])pixel[3]=255;return pixel;
}
namespace {
bool RotateButton(const char* id,bool right){
    const bool clicked=ImGui::Button(id,ImVec2(ImGui::GetFrameHeight(),ImGui::GetFrameHeight()));
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const ImVec2 center((a.x+b.x)*.5f,(a.y+b.y)*.5f);
    const float radius=ImGui::GetFontSize()*.32f,sign=right?1.0f:-1.0f;
    auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    for(int i=0;i<=16;++i){const float angle=-1.5707963f+i*4.712389f/16;draw->PathLineTo(ImVec2(center.x+sign*std::cos(angle)*radius,center.y+std::sin(angle)*radius));}
    draw->PathStroke(color,0,1.5f);const ImVec2 tip(center.x-sign*radius,center.y-3);
    draw->AddTriangleFilled(tip,ImVec2(tip.x-3,tip.y+5),ImVec2(tip.x+3,tip.y+5),color);return clicked;
}
}
void ImageViewer::AbsoluteDifference(const ImageViewer& baseline, const ImageViewer& modified) {
    if (baseline.width_ != modified.width_ || baseline.height_ != modified.height_)
        throw std::runtime_error("Comparison images have different dimensions");
    width_ = baseline.width_; height_ = baseline.height_;
    fitToView_ = true;
    rgba_.resize(baseline.rgba_.size());
    for (size_t i = 0; i < rgba_.size(); i += 4) {
        for (int c = 0; c < 3; ++c)
            rgba_[i + c] = static_cast<unsigned char>(std::abs(static_cast<int>(baseline.rgba_[i + c]) - modified.rgba_[i + c]));
        rgba_[i + 3] = 255;
    }
    name_ = "Absolute RGB difference";
    Upload();
}
void ImageViewer::Release() {
    if (texture_) glDeleteTextures(1, &texture_);
    texture_ = 0;
    width_ = height_ = 0; rgba_.clear(); depthValues_.clear(); name_.clear();
}
void ImageViewer::MakeThumbnail(const ImageViewer& image) {
    if (!image.width_ || !image.height_) { Release(); return; }
    const auto scale = std::min(100.0 / image.width_, 68.0 / image.height_);
    width_ = std::max(1, static_cast<int>(image.width_ * scale)); height_ = std::max(1, static_cast<int>(image.height_ * scale));
    rgba_.resize(static_cast<size_t>(width_) * height_ * 4);
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
        const auto pixel = image.Pixel(x * image.width_ / width_, y * image.height_ / height_);
        std::copy(pixel.begin(), pixel.end(), rgba_.begin() + (static_cast<size_t>(y) * width_ + x) * 4);
    }
    Upload();
}
bool ImageViewer::DrawThumbnail(const char* id, float height) const {
    if (!texture_) return false;
    const bool clicked = ImGui::Button(id, ImVec2(100, height));
    const auto min = ImGui::GetItemRectMin();
    const auto scale = std::min(96.0f / width_, (height - 4) / height_);
    const ImVec2 size(width_ * scale, height_ * scale);
    const ImVec2 origin(min.x + (100 - size.x) * .5f, min.y + (height - size.y) * .5f);
    ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture_), origin, ImVec2(origin.x + size.x, origin.y + size.y));
    return clicked;
}
void ImageViewer::LoadThumbnailBmp(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);std::array<unsigned char,54> header{};
    input.read(reinterpret_cast<char*>(header.data()),header.size());
    auto u16=[&](size_t n){uint16_t v;std::memcpy(&v,header.data()+n,2);return v;};
    auto u32=[&](size_t n){uint32_t v;std::memcpy(&v,header.data()+n,4);return v;};
    if(!input||header[0]!='B'||header[1]!='M'||u32(14)<40||u16(26)!=1||u32(30)!=0)throw std::runtime_error("Unsupported thumbnail BMP");
    const int w=static_cast<int32_t>(u32(18)),h=static_cast<int32_t>(u32(22));const auto depth=u16(28);
    if(w<=0||w>16384||h==0||h < -16384||h>16384||(depth!=24&&depth!=32))throw std::runtime_error("Unsupported thumbnail dimensions");
    const int height=std::abs(h);const size_t stride=(static_cast<size_t>(w)*depth+31)/32*4,offset=u32(10);
    if(offset<54||std::filesystem::file_size(path)<offset+stride*height)throw std::runtime_error("Truncated thumbnail BMP");
    const auto scale=std::min(192.0/w,128.0/height);width_=std::max(1,int(w*scale));height_=std::max(1,int(height*scale));
    rgba_.resize(static_cast<size_t>(width_)*height_*4);std::vector<unsigned char> row(stride);
    for(int y=0;y<height_;++y){
        const int sourceY=y*height/height_;input.seekg(offset+(h>0?height-sourceY-1:sourceY)*stride);input.read(reinterpret_cast<char*>(row.data()),row.size());
        if(!input)throw std::runtime_error("Thumbnail row read failed");
        for(int x=0;x<width_;++x){const size_t s=x*w/width_*(depth/8),d=(static_cast<size_t>(y)*width_+x)*4;rgba_[d]=row[s+2];rgba_[d+1]=row[s+1];rgba_[d+2]=row[s];rgba_[d+3]=255;}
    }Upload();
}
bool ImageViewer::DrawPreview(const char* id,ImVec2 size,bool selected)const{
    if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));const bool clicked=ImGui::Button(id,size);ImGui::PopStyleColor();if(selected)ImGui::PopStyleColor();
    const auto min=ImGui::GetItemRectMin();
    if(texture_){const float scale=std::min((size.x-8)/width_,(size.y-8)/height_);const ImVec2 start(min.x+(size.x-width_*scale)*.5f,min.y+(size.y-height_*scale)*.5f);
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture_),start,ImVec2(start.x+width_*scale,start.y+height_*scale));}
    return clicked;
}
void ImageViewer::LoadBmp(const std::filesystem::path& path,const std::string& aspect,const std::string& format) {
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    auto u16 = [&](size_t at) { uint16_t v; std::memcpy(&v, bytes.data() + at, 2); return v; };
    auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, bytes.data() + at, 4); return v; };
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M' || u32(14) < 40 || u16(26) != 1)
        throw std::runtime_error("Unsupported BMP header");
    const auto offset = u32(10);
    const int32_t width = static_cast<int32_t>(u32(18));
    const int32_t height = static_cast<int32_t>(u32(22));
    const auto depth = u16(28);
    if (width <= 0 || width > 16384 || !height || height < -16384 || height > 16384 ||
        (depth != 24 && depth != 32) || u32(30) != 0)
        throw std::runtime_error("Viewer currently supports uncompressed 24/32-bit replay BMP images");
    const auto stride = (static_cast<size_t>(width) * depth + 31) / 32 * 4;
    const auto rows = static_cast<size_t>(height < 0 ? -height : height);
    if (offset < 54 || offset > bytes.size() || rows > (bytes.size() - offset) / stride)
        throw std::runtime_error("Truncated BMP image");
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * rows * 4);
    for (size_t y = 0; y < rows; ++y) for (int x = 0; x < width; ++x) {
        const auto src = offset + (height > 0 ? rows - y - 1 : y) * stride + static_cast<size_t>(x) * (depth / 8);
        const auto dst = (y * width + x) * 4;
        pixels[dst] = bytes[src + 2]; pixels[dst + 1] = bytes[src + 1]; pixels[dst + 2] = bytes[src];
        pixels[dst + 3] = 255; // BI_RGB alpha is undefined; this is a display screenshot.
    }
    width_ = width; height_ = static_cast<int>(rows); rgba_ = std::move(pixels);
    fitToView_ = true;
    name_ = path.filename().u8string();scalar_=aspect=="DEPTH"||aspect=="STENCIL";range_={};rangeDomainMaximum_=1;depthValues_.clear();
    const auto sampleBytes=DepthSampleSize(format);const auto sidecar=std::filesystem::path(path.u8string()+".depth.bin");
    if(aspect=="DEPTH"&&sampleBytes&&std::filesystem::is_regular_file(sidecar)&&std::filesystem::file_size(sidecar)==static_cast<size_t>(width_)*height_*sampleBytes){
        std::ifstream raw(sidecar,std::ios::binary);std::vector<uint8_t> data(static_cast<size_t>(width_)*height_*sampleBytes);raw.read(reinterpret_cast<char*>(data.data()),data.size());
        if(raw){depthValues_.resize(static_cast<size_t>(width_)*height_);for(size_t i=0;i<depthValues_.size();++i)depthValues_[i]=DecodeDepthSample(data.data()+i*sampleBytes,format);}
    }
    Upload();
}
void ImageViewer::LoadPng(const std::filesystem::path& path) {
    auto image = LoadImage(path);  // decoded host-side (e.g. from raw ASTC blocks); opaque/straight alpha as written by astcenc
    width_ = static_cast<int>(image.width); height_ = static_cast<int>(image.height); rgba_ = std::move(image.rgba);
    fitToView_ = true;
    name_ = path.filename().u8string();scalar_=false;range_={};rangeDomainMaximum_=1;depthValues_.clear();
    Upload();
}
void ImageViewer::Upload() {
    if (!texture_) glGenTextures(1, &texture_);
    auto pixels = rgba_;
    for(size_t i=0;i<pixels.size();i+=4){
        const auto depth=depthValues_.empty()?0:range_.MapValue(depthValues_[i/4]);
        for(int c=0;c<3;++c)pixels[i+c]=channels_[c]?(depthValues_.empty()?range_.Map(pixels[i+c],!scalar_&&!srgbDisplay_):depth):0;
        if(!channels_[3])pixels[i+3]=255;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}
void ImageViewer::Draw(const std::string& details,const std::string& tooltip) {
    if (!texture_) { ImGui::TextWrapped("Render a frame to inspect its replay screenshot."); return; }
    if (ImGui::Button("Fit")) fitToView_ = true;
    ImGui::SameLine(); const float sliderWidth=ImGui::GetFontSize()*14; ImGui::SetNextItemWidth(std::min(sliderWidth,std::max(40.0f,ImGui::GetContentRegionAvail().x)));
    if (ImGui::SliderFloat("##scale", &zoom_, 0.01f, 32.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) fitToView_ = false;
    ImGui::SetItemTooltip("Image scale. Use the mouse wheel over the image to zoom around the pointer.");
    bool changed = false;
    const char* labels[] = {"R", "G", "B"};
    for (int c = 0; c < 3; ++c) { if (c) ImGui::SameLine(); changed |= ImGui::Checkbox(labels[c], &channels_[c]); }
    ImGui::SameLine();
    bool srgb=!scalar_&&srgbDisplay_;ImGui::BeginDisabled(scalar_);
    if(ImGui::Checkbox("sRGB",&srgb)){srgbDisplay_=srgb;changed=true;}
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Display encoded sRGB, or disable to inspect linearized RGB.\nPixel readout retains unchanged screenshot values.");
    if (changed) Upload();
    if(ImGui::GetContentRegionAvail().x>600)ImGui::SameLine();
    if(RotateButton("##rotate-left",false)){orientation_.Rotate(-1);fitToView_=true;}ImGui::SetItemTooltip("Rotate left 90 degrees");
    ImGui::SameLine();if(RotateButton("##rotate-right",true)){orientation_.Rotate();fitToView_=true;}ImGui::SetItemTooltip("Rotate right 90 degrees");
    ImGui::SameLine();if(ImGui::Button("Flip H"))orientation_.horizontal=!orientation_.horizontal;
    ImGui::SameLine();if(ImGui::Button("Flip V"))orientation_.vertical=!orientation_.vertical;
    bool rangeChanged=false;
    ImGui::SetNextItemWidth(100);rangeChanged|=ImGui::DragFloat("##range-min",&range_.minimum,.0001f,0,1,"%.6g",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Black point. Ctrl+click to enter an exact value.");ImGui::SameLine();
    const float buttons=ImGui::CalcTextSize("AutoReset").x+ImGui::GetStyle().FramePadding.x*4+ImGui::GetStyle().ItemSpacing.x*4;
    const auto start=ImGui::GetCursorScreenPos();const ImVec2 barSize(std::min(sliderWidth,std::max(40.0f,ImGui::GetContentRegionAvail().x-100-buttons)),ImGui::GetFrameHeight());
    ImGui::InvisibleButton("##range-bar",barSize);
    if(ImGui::IsItemActivated()){
        const float u=std::clamp((ImGui::GetMousePos().x-start.x)/barSize.x,0.0f,1.0f)*rangeDomainMaximum_;rangeHandle_=std::abs(u-range_.minimum)<std::abs(u-range_.maximum)?0:1;
    }
    if(ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        const float u=std::clamp((ImGui::GetMousePos().x-start.x)/barSize.x,0.0f,1.0f)*rangeDomainMaximum_;
        if(rangeHandle_==0)range_.minimum=std::min(u,std::nextafter(range_.maximum,0.0f));else range_.maximum=std::max(u,std::nextafter(range_.minimum,1.0f));rangeChanged=true;
    }
    auto* draw=ImGui::GetWindowDrawList();const float y=start.y+barSize.y*.5f;
    draw->AddRectFilledMultiColor(ImVec2(start.x,y-4),ImVec2(start.x+barSize.x,y+4),IM_COL32(0,0,0,255),IM_COL32(255,255,255,255),IM_COL32(255,255,255,255),IM_COL32(0,0,0,255));
    const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    const float low=start.x+std::clamp(range_.minimum/rangeDomainMaximum_,0.0f,1.0f)*barSize.x,high=start.x+std::clamp(range_.maximum/rangeDomainMaximum_,0.0f,1.0f)*barSize.x;
    draw->AddLine(ImVec2(low,y-9),ImVec2(low,y+9),color,2);draw->AddTriangleFilled(ImVec2(low,y-3),ImVec2(low-4,y-10),ImVec2(low+4,y-10),color);
    draw->AddLine(ImVec2(high,y-9),ImVec2(high,y+9),color,2);draw->AddTriangleFilled(ImVec2(high,y+3),ImVec2(high-4,y+10),ImVec2(high+4,y+10),color);
    ImGui::SetItemTooltip("Display range: drag the black and white points.");ImGui::SameLine();ImGui::SetNextItemWidth(100);
    rangeChanged|=ImGui::DragFloat("##range-max",&range_.maximum,.0001f,0,1,"%.6g",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("White point. Ctrl+click to enter an exact value.");ImGui::SameLine();
    if(ImGui::Button("Auto")){
        float low=1,high=0;
        if(!depthValues_.empty()){for(float value:depthValues_)if(std::isfinite(value)){low=std::min(low,value);high=std::max(high,value);}}
        else{
            unsigned lo=255,hi=0;for(size_t i=0;i<rgba_.size();i+=4)for(int c=0;c<(scalar_?1:3);++c)if(channels_[c]){lo=std::min(lo,unsigned(rgba_[i+c]));hi=std::max(hi,unsigned(rgba_[i+c]));}
            low=lo/255.0f;high=hi/255.0f;
            if(!scalar_&&!srgbDisplay_){auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};low=linear(low);high=linear(high);}
        }
        if(low>high){low=0;high=1;}
        range_.minimum=low;range_.maximum=high;
        if(high<=low){range_.minimum=std::max(0.0f,low-1.0f/255);range_.maximum=std::min(1.0f,high+1.0f/255);}rangeDomainMaximum_=std::max(range_.maximum,1e-12f);rangeChanged=true;
    }
    ImGui::SetItemTooltip("Fit the range to decoded image values.");ImGui::SameLine();if(ImGui::Button("Reset")){range_={};rangeDomainMaximum_=1;rangeChanged=true;}
    if(rangeChanged){range_.minimum=std::clamp(range_.minimum,0.0f,std::nextafter(1.0f,0.0f));range_.maximum=std::clamp(range_.maximum,std::nextafter(range_.minimum,1.0f),1.0f);Upload();}
    const auto displayedWidth=orientation_.Width(width_,height_),displayedHeight=orientation_.Height(width_,height_);
    if (fitToView_) {
        const auto available = ImGui::GetContentRegionAvail();
        zoom_ = std::clamp(std::min((available.x - 20.0f) / displayedWidth, (available.y - 20.0f - ImGui::GetTextLineHeightWithSpacing()) / displayedHeight), 0.01f, 32.0f);
    }
    ImGui::BeginChild("pixels", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin = ImGui::GetCursorScreenPos();
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0) {
        const auto mouse = ImGui::GetMousePos(); const auto old = zoom_;
        zoom_ = std::clamp(zoom_ * std::pow(1.2f, ImGui::GetIO().MouseWheel), 0.01f, 32.0f); fitToView_ = false;
        ImGui::SetScrollX(ImGui::GetScrollX() + (mouse.x - origin.x) / old * (zoom_ - old));
        ImGui::SetScrollY(ImGui::GetScrollY() + (mouse.y - origin.y) / old * (zoom_ - old));
    }
    const ImVec2 size(displayedWidth*zoom_,displayedHeight*zoom_);
    ImGui::InvisibleButton("##image-content",size);
    const auto a=orientation_.Uv(0,0),b=orientation_.Uv(1,0),c=orientation_.Uv(1,1),d=orientation_.Uv(0,1);
    ImGui::GetWindowDrawList()->AddImageQuad(static_cast<ImTextureID>(texture_),origin,ImVec2(origin.x+size.x,origin.y),ImVec2(origin.x+size.x,origin.y+size.y),ImVec2(origin.x,origin.y+size.y),ImVec2(a[0],a[1]),ImVec2(b[0],b[1]),ImVec2(c[0],c[1]),ImVec2(d[0],d[1]));
    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseDelta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
        }
        const auto mouse = ImGui::GetMousePos();
        int x = static_cast<int>((mouse.x - origin.x) / zoom_), y = static_cast<int>((mouse.y - origin.y) / zoom_);
        if (x >= 0 && y >= 0 && x < displayedWidth && y < displayedHeight) {
            const auto source=orientation_.SourcePixel(x,y,width_,height_);x=source[0];y=source[1];
            const auto p = (static_cast<size_t>(y) * width_ + x) * 4;
            if(!depthValues_.empty())ImGui::SetTooltip("(%d, %d) Depth %.9g",x,y,depthValues_[static_cast<size_t>(y)*width_+x]);
            else ImGui::SetTooltip("(%d, %d) RGB %u, %u, %u\nDecoded display values; original texel storage unavailable.",
                              x, y, rgba_[p], rgba_[p + 1], rgba_[p + 2]);
        }
    }
    ImGui::EndChild();
    char display[128];std::snprintf(display,sizeof(display),"%d x %d  |  %.2fx  |  %s",width_,height_,zoom_,scalar_?"Scalar":srgbDisplay_?"sRGB":"Linearized RGB");
    const auto footer=std::string(display)+(details.empty()?"":"  |  "+details);
    const auto position=ImGui::GetCursorScreenPos();const auto available=ImGui::GetContentRegionAvail().x;
    const ImVec4 clip(position.x,position.y,position.x+available,position.y+ImGui::GetTextLineHeight());
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(),ImGui::GetFontSize(),position,ImGui::GetColorU32(ImGuiCol_TextDisabled),footer.c_str(),nullptr,0,&clip);
    ImGui::Dummy(ImVec2(available,ImGui::GetTextLineHeight()));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",footer.c_str(),tooltip.empty()?(!depthValues_.empty()?"Full-precision depth readback. Display range does not change source values.":name_.c_str()):tooltip.c_str());
}
} // namespace gfx
