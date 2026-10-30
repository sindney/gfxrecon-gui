#include "core/image_io.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
// Pristine stb_image (the SDL copy is patched to need SDL typedefs); static so it cannot clash with other copies.
#include "../../thirdparty/SFML/extlibs/headers/stb_image/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gfx {
namespace {
using Json = nlohmann::json;
} // namespace
Image LoadImage(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read image " + file.u8string());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
    int w = 0, h = 0, n = 0;
    auto* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!pixels) throw std::runtime_error("Unsupported or corrupt BMP/PNG image " + file.u8string() + ": " + stbi_failure_reason());
    Image image; image.width = static_cast<uint32_t>(w); image.height = static_cast<uint32_t>(h);
    image.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return image;
}
void WritePng(const std::filesystem::path& file, const Image& image) {
    if (!image.width || !image.height || image.rgba.size() != static_cast<size_t>(image.width) * image.height * 4) throw std::runtime_error("Invalid image for PNG output");
    int length = 0;
    auto* data = stbi_write_png_to_mem(image.rgba.data(), static_cast<int>(image.width) * 4, static_cast<int>(image.width), static_cast<int>(image.height), 4, &length);
    if (!data) throw std::runtime_error("PNG encoding failed");
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data), length);
    STBIW_FREE(data);
    if (!output) throw std::runtime_error("Cannot write PNG " + file.u8string());
}
Image ConvertToPng(const std::filesystem::path& source, const std::filesystem::path& png) {
    auto image = LoadImage(source);
    WritePng(png, image);
    return image;
}
Json CompareImages(const Image& a, const Image& b, const std::filesystem::path& diffPng) {
    if (a.width != b.width || a.height != b.height) throw ImageExtentMismatch(a.width, a.height, b.width, b.height);
    const size_t pixels = static_cast<size_t>(a.width) * a.height;
    uint64_t sum[4]{}, sumSquaredRgb = 0, differing = 0; uint8_t peak[4]{};
    for (size_t p = 0; p < pixels; ++p) {
        bool pixelDiffers = false;
        for (int c = 0; c < 4; ++c) {
            const int d = std::abs(int(a.rgba[p * 4 + c]) - int(b.rgba[p * 4 + c]));
            sum[c] += d; peak[c] = std::max<uint8_t>(peak[c], static_cast<uint8_t>(d));
            if (c < 3) sumSquaredRgb += static_cast<uint64_t>(d) * d;
            pixelDiffers |= d != 0;
        }
        differing += pixelDiffers;
    }
    const char* names[4]{"r", "g", "b", "a"};
    Json maxDiff = Json::object(), meanDiff = Json::object();
    int overall = 0;
    for (int c = 0; c < 4; ++c) { maxDiff[names[c]] = peak[c]; meanDiff[names[c]] = pixels ? double(sum[c]) / pixels : 0.0; overall = std::max<int>(overall, peak[c]); }
    const double mse = pixels ? double(sumSquaredRgb) / (double(pixels) * 3.0) : 0.0;
    Json result{{"extent_equal", true}, {"width", a.width}, {"height", a.height}, {"pixels", pixels},
        {"max_abs_diff", maxDiff}, {"mean_abs_diff", meanDiff}, {"max_abs_diff_overall", overall},
        {"differing_pixels", differing}, {"differing_fraction", pixels ? double(differing) / pixels : 0.0},
        {"identical", differing == 0}, {"psnr_db", mse > 0.0 ? Json(10.0 * std::log10(255.0 * 255.0 / mse)) : Json(nullptr)},
        {"psnr_note", "PSNR over RGB; null means identical (infinite)"}};
    if (!diffPng.empty()) {
        Image diff; diff.width = a.width; diff.height = a.height; diff.rgba.resize(a.rgba.size());
        const double scale = overall > 0 ? 255.0 / overall : 1.0;
        for (size_t p = 0; p < pixels; ++p) {
            for (int c = 0; c < 3; ++c)
                diff.rgba[p * 4 + c] = static_cast<uint8_t>(std::min(255.0, std::abs(int(a.rgba[p * 4 + c]) - int(b.rgba[p * 4 + c])) * scale + 0.5));
            diff.rgba[p * 4 + 3] = 255;
        }
        WritePng(diffPng, diff);
        result["diff_image"] = diffPng.u8string(); result["diff_scale"] = scale;
    }
    return result;
}
Json CompareImages(const std::filesystem::path& a, const std::filesystem::path& b, const std::filesystem::path& diffPng) {
    auto result = CompareImages(LoadImage(a), LoadImage(b), diffPng);
    result["a"] = a.u8string(); result["b"] = b.u8string();
    return result;
}
} // namespace gfx
