#include "analysis/image_display.h"

#include <filesystem>
#include <limits>
#include <stdexcept>

namespace gfx::tests {
namespace {
void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(std::string("Image display: ") + reason);
}
bool Near(float a, float b, float tolerance = 1e-6f) { return std::abs(a - b) <= tolerance; }
}

void ImageDisplayTests(const std::filesystem::path&) {
    using Encoding = ImageSourceEncoding;
    Require(SourceEncodingForImage("") == Encoding::Srgb, "Untyped presentation data changed its default encoding");
    for (const auto* format : {"VK_FORMAT_R8G8B8A8_SRGB", "VK_FORMAT_B8G8R8A8_SRGB", "VK_FORMAT_ASTC_6x6_SRGB_BLOCK"})
        Require(SourceEncodingForImage(format) == Encoding::Srgb, "Typed sRGB resource lost its source encoding");
    for (const auto* format : {"VK_FORMAT_R8G8B8A8_UNORM", "VK_FORMAT_R16G16B16A16_SFLOAT", "VK_FORMAT_B10G11R11_UFLOAT_PACK32",
                              "VK_FORMAT_ASTC_6x6_UNORM_BLOCK", "VK_FORMAT_R32_SFLOAT"})
        Require(SourceEncodingForImage(format) == Encoding::Linear, "Typed linear/HDR/UNORM resource was treated as encoded presentation data");

    Require(Near(SrgbToLinear(0), 0) && Near(SrgbToLinear(1), 1) && Near(LinearToSrgb(0), 0) && Near(LinearToSrgb(1), 1),
        "Transfer functions changed black/white endpoints");
    Require(Near(SrgbToLinear(0.04045f), 0.04045f / 12.92f) && Near(LinearToSrgb(0.0031308f), 0.0031308f * 12.92f),
        "Transfer-function linear toe thresholds changed");
    Require(EncodeImageColor(0.18f, Encoding::Srgb) == 118, "Linear 18-percent gray did not encode to sRGB 118");
    Require(EncodeImageColor(0.18f, Encoding::Linear) == 46, "Linear byte encoding unexpectedly applied gamma");

    ImageDisplayRange range;
    Require(range.MapColor(128, Encoding::Linear, true) == 188, "Linear midpoint was passed through without display encoding");
    Require(range.MapColor(128, Encoding::Linear, false) == 128, "Uncorrected linear display decoded source bytes as sRGB");
    Require(range.MapColor(188, Encoding::Srgb, true) == 188 && range.MapColor(188, Encoding::Srgb, false) == 128,
        "Encoded source display was double-encoded or failed to decode when requested");
    Require(range.MapColor(5, Encoding::Srgb, true) == 5 && DecodeImageColor(5, Encoding::Srgb) < 1.0f / 255.0f,
        "Dark sRGB color quantized prematurely through linear U8");
    for (unsigned value = 0; value < 256; ++value) {
        const auto byte = static_cast<uint8_t>(value);
        Require(range.MapColor(byte, Encoding::Srgb, true) == byte, "Default-range sRGB display did not preserve every source byte");
        Require(range.MapColor(byte, Encoding::Linear, false) == byte, "Default linear display did not preserve every source byte");
        Require(EncodeImageColor(DecodeImageColor(byte, Encoding::Srgb), Encoding::Srgb) == byte,
            "sRGB codec round trip lost byte precision");
    }
    range = {0, .5f};
    Require(range.MapColor(64, Encoding::Linear, true) == 188, "Range normalization occurred after gamma instead of before it");
    Require(range.MapColor(64, Encoding::Linear, false) == 128, "Linear range scaling changed");
    Require(range.MapColor(137, Encoding::Srgb, true) == 188, "Encoded source was not decoded before applying the linear range");
    Require(range.Normalize(-1) == 0 && range.Normalize(2) == 1 && Near(range.Normalize(.25f), .5f), "Range normalization failed to clamp finite values");

    // Existing scalar/range callers keep their byte mapping semantics.
    range = {};
    Require(range.Map(123) == 123 && range.Map(128, true) == 55 && range.MapValue(.5f) == 128, "Legacy mapping API semantics changed");
    range.maximum = 2.0f / 255;
    Require(range.Map(1) == 128 && range.Map(3) == 255, "Legacy byte range scaling changed");
    range.minimum = 1.0f / 255;
    Require(range.Map(1) == 0 && range.Map(2) == 255, "Legacy black/white range endpoints changed");
    range = {.00001f, .00003f};
    Require(range.MapValue(.00002f) == 128, "Full-precision scalar range was reduced to byte precision");

    const auto nan = std::numeric_limits<float>::quiet_NaN(), infinity = std::numeric_limits<float>::infinity();
    Require(EncodeImageColor(-1, Encoding::Linear) == 0 && EncodeImageColor(2, Encoding::Srgb) == 255 &&
        EncodeImageColor(nan, Encoding::Srgb) == 0 && EncodeImageColor(infinity, Encoding::Srgb) == 255 &&
        EncodeImageColor(-infinity, Encoding::Linear) == 0, "Byte encoding failed to clamp invalid/out-of-range input safely");
    range = {};
    Require(range.Normalize(nan) == 0 && range.Normalize(infinity) == 0 && range.MapValue(nan) == 0 && range.MapValue(infinity) == 0,
        "Nonfinite range samples changed legacy mapping or escaped finite normalization");
    range = {.5f, .5f};
    Require(range.Normalize(.5f) == 0 && range.Normalize(.75f) == 1 && range.MapValue(.75f) == 255,
        "Degenerate range lost its legacy threshold behavior");
    range = {1, 0};
    Require(range.Normalize(.5f) == 0 && range.Normalize(2) == 1, "Reversed range lost its legacy threshold behavior");
    range = {nan, 1};
    Require(std::isfinite(range.Normalize(.5f)) && range.MapColor(128, Encoding::Srgb, true) == 0,
        "Invalid range bounds propagated NaN into encoded display bytes");
}
} // namespace gfx::tests
