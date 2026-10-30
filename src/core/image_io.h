#pragma once
// Host image helpers: BMP/PNG load, PNG write (stb_image_write), comparison metrics.
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace gfx {
struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // 8-bit RGBA, top-down rows
};
// Different extents are an error for comparison, never a "difference".
struct ImageExtentMismatch : std::runtime_error {
    uint32_t aw, ah, bw, bh;
    ImageExtentMismatch(uint32_t aw, uint32_t ah, uint32_t bw, uint32_t bh)
        : std::runtime_error("Image extents differ: " + std::to_string(aw) + "x" + std::to_string(ah) + " vs " + std::to_string(bw) + "x" + std::to_string(bh)),
          aw(aw), ah(ah), bw(bw), bh(bh) {}
};
// Loads a BMP (24/32-bit, as written by gfxrecon-replay) or PNG by content. Throws on unsupported/corrupt data.
Image LoadImage(const std::filesystem::path& file);
void WritePng(const std::filesystem::path& file, const Image& image);
void WriteBmp(const std::filesystem::path& file, const Image& image);
// Coverage is the RGB difference of two differently colored diagnostic renders.
// Return the original image with only the inner coverage boundary highlighted.
Image ComposeDrawOutline(const Image& original,const Image& coverageA,const Image& coverageB,uint32_t thickness=2);
// BMP (or PNG) -> PNG. Returns the decoded extent.
Image ConvertToPng(const std::filesystem::path& source, const std::filesystem::path& png);
// Per-channel max/mean absolute difference, differing pixel count and PSNR (RGB, null when identical).
// Writes a difference image (|a-b| per channel scaled so the largest difference is 255, alpha opaque) when diffPng is non-empty.
// Throws ImageExtentMismatch when the extents differ.
nlohmann::json CompareImages(const std::filesystem::path& a, const std::filesystem::path& b, const std::filesystem::path& diffPng = {});
nlohmann::json CompareImages(const Image& a, const Image& b, const std::filesystem::path& diffPng = {});
} // namespace gfx
