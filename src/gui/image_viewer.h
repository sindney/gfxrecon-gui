#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <imgui.h>
#include "analysis/image_orientation.h"
#include "analysis/image_display.h"

namespace gfx {
class ImageViewer {
public:
    void LoadBmp(const std::filesystem::path& path,const std::string& aspect = {},const std::string& format = {});
    void LoadPng(const std::filesystem::path& path);
    void Draw(const std::string& details = {}, const std::string& tooltip = {});
    void Release();
    void AbsoluteDifference(const ImageViewer& baseline, const ImageViewer& modified);
    void MakeThumbnail(const ImageViewer& image);
    bool DrawThumbnail(const char* id, float height = 68) const;
    void LoadThumbnailBmp(const std::filesystem::path& path);
    bool DrawPreview(const char* id, ImVec2 size, bool selected) const;
    std::array<unsigned char, 4> Pixel(int x, int y) const;
    int Width() const { return width_; }
    int Height() const { return height_; }
    float Zoom() const { return zoom_; }
    bool SrgbDisplay() const { return srgbDisplay_; }
    const ImageOrientation& Orientation()const{return orientation_;}
    const ImageDisplayRange& Range()const{return range_;}
    std::array<unsigned char,4> DisplayPixel(int x,int y)const;
    bool HasDepthValues()const{return !depthValues_.empty();}
private:
    void Upload();
    unsigned texture_ = 0;
    int width_ = 0, height_ = 0;
    float zoom_ = 0.5f;
    bool fitToView_ = true;
    bool srgbDisplay_ = true;
    bool channels_[4] = {true, true, true, true};
    std::vector<unsigned char> rgba_;
    std::string name_;
    ImageOrientation orientation_;
    ImageDisplayRange range_;
    bool scalar_=false;
    int rangeHandle_=0;
    float rangeDomainMaximum_=1;
    std::vector<float> depthValues_;
};
} // namespace gfx
