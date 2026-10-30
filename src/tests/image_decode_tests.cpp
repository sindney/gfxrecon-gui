#include "core/image_decode.h"
#include "core/settings.h"
#include "core/tools.h"
#include "platform/process.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
namespace gfx::tests {
using Json = nlohmann::json;
namespace {
// 32-bit uncompressed top-left TGA with a gradient plus checker so every block has content.
void WriteTga(const std::filesystem::path& file, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8), 32, 0x28};
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const bool checker = ((x / 4) + (y / 4)) & 1;
            out.insert(out.end(), {uint8_t(y * 255 / h), uint8_t(checker ? 200 : 40), uint8_t(x * 255 / w), 255});  // B G R A
        }
    std::ofstream(file, std::ios::binary) .write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
}
std::vector<uint8_t> ReadAll(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
uint32_t Be32(const std::vector<uint8_t>& v, size_t at) { return uint32_t(v[at]) << 24 | uint32_t(v[at + 1]) << 16 | uint32_t(v[at + 2]) << 8 | v[at + 3]; }
} // namespace

void RunImageDecodeTests(const std::filesystem::path& root) {
    CHECK(IsAstcFormat("VK_FORMAT_ASTC_4x4_UNORM_BLOCK"));
    CHECK(IsAstcFormat("VK_FORMAT_ASTC_12x12_SRGB_BLOCK"));
    CHECK(!IsAstcFormat("VK_FORMAT_R8G8B8A8_UNORM"));
    CHECK(!IsAstcFormat("VK_FORMAT_ASTC_7x7_UNORM_BLOCK"));
    int bx = 0, by = 0;
    CHECK(AstcBlockSize("VK_FORMAT_ASTC_10x8_SRGB_BLOCK", bx, by) && bx == 10 && by == 8);

    const auto work = std::filesystem::temp_directory_path() / "gfxrecon_image_decode_tests";
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
    const std::map<std::string, std::string> none;
    const Json fakeMeta = {{"format", "VK_FORMAT_R8G8B8A8_UNORM"}, {"extent", {4, 4, 1}}};
    std::ofstream(work / "fake.bin", std::ios::binary) << "0123456789abcdef";
    auto status = DecodeCompressedImage(work / "fake.bin", fakeMeta, work / "fake.png", none);
    CHECK(status["status"] == "unavailable" && !std::filesystem::exists(work / "fake.png"));
    const Json astcMeta = {{"format", "VK_FORMAT_ASTC_4x4_UNORM_BLOCK"}, {"extent", {4, 4, 1}}};
    status = DecodeCompressedImage(work / "fake.bin", astcMeta, work / "fake.png", none);
    if (!(status["status"] == "unavailable" && status["reason"].get<std::string>().find("astcenc") != std::string::npos)) throw std::runtime_error("unexpected: " + status.dump());
    status = DecodeCompressedImage(work / "fake.bin", astcMeta, work / "fake.png", {{"astcenc", (work / "no-such-astcenc.exe").u8string()}});
    CHECK(status["status"] == "unavailable");

    const auto tools = ResolveTools(root, std::filesystem::u8path(tpl::ExeDirectory()));
    const auto tool = tools.at("astcenc");
    if (tool.empty() || !std::filesystem::is_regular_file(std::filesystem::u8path(tool))) {
        std::cout << "  image decode: astcenc not found, skipping round-trip\n";
        return;
    }
    const uint32_t W = 37, H = 23;
    WriteTga(work / "src.tga", W, H);
    struct Case { int bx, by; const char* name; };
    for (const Case& c : {Case{4, 4, "4x4"}, Case{10, 8, "10x8"}, Case{12, 12, "12x12"}})
        for (bool srgb : {false, true}) {
            const std::string tag = std::string(c.name) + (srgb ? "_srgb" : "_unorm");
            const auto astc = work / (tag + ".astc");
            auto enc = RunProcess({tool, {srgb ? "-cs" : "-cl", (work / "src.tga").u8string(), astc.u8string(), c.name, "-fast", "-silent"}});
            CHECK(enc.Success());
            auto data = ReadAll(astc);
            CHECK(data.size() > 16 && data[0] == 0x13 && data[1] == 0xAB && data[2] == 0xA1 && data[3] == 0x5C);
            CHECK(data[4] == c.bx && data[5] == c.by && data[6] == 1);
            CHECK(data[7] == W && data[10] == H);
            const auto raw = work / (tag + ".bin");
            std::ofstream(raw, std::ios::binary).write(reinterpret_cast<const char*>(data.data()) + 16, std::streamsize(data.size() - 16));
            const std::string format = std::string("VK_FORMAT_ASTC_") + c.name + (srgb ? "_SRGB_BLOCK" : "_UNORM_BLOCK");
            const auto png = work / (tag + ".png");
            auto result = DecodeCompressedImage(raw, {{"format", format}, {"extent", {W, H, 1}}}, png, tools);
            CHECK(result["status"] == "ok" && result["mode"] == (srgb ? "-ds" : "-dl"));
            CHECK(result["decoded_by"] == "astcenc 5.7.0" && result["block"] == Json::array({c.bx, c.by}));
            CHECK(std::filesystem::exists(raw) && std::filesystem::exists(png));
            const auto bytes = ReadAll(png);
            CHECK(bytes.size() > 33 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G');
            CHECK(Be32(bytes, 16) == W && Be32(bytes, 20) == H);
            // Dimensions as object, and a truncated raw file is reported, not thrown.
            result = DecodeCompressedImage(raw, {{"format", format}, {"dimensions", {{"width", W}, {"height", H}}}}, work / (tag + "_b.png"), tools);
            CHECK(result["status"] == "ok");
            result = DecodeCompressedImage(raw, {{"format", format}, {"extent", {W + 40, H, 1}}}, work / (tag + "_c.png"), tools);
            CHECK(result["status"] == "unavailable" && !std::filesystem::exists(work / (tag + "_c.png")));
        }
    std::filesystem::remove_all(work);
    std::cout << "  image decode: ASTC round trip 4x4/10x8/12x12 UNORM+SRGB ok\n";
}
} // namespace gfx::tests
