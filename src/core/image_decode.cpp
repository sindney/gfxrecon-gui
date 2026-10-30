#include "core/image_decode.h"
#include "platform/process.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace gfx {
using Json = nlohmann::json;
namespace {
constexpr const char* kDecoder = "astcenc 5.7.0";
Json Unavailable(const std::filesystem::path& raw, const std::string& reason) {
    return {{"status", "unavailable"}, {"reason", reason}, {"raw", raw.u8string()}};
}
bool ParseFormat(const std::string& format, int& bx, int& by, bool& srgb) {
    static const char prefix[] = "VK_FORMAT_ASTC_";
    if (format.compare(0, sizeof(prefix) - 1, prefix) != 0) return false;
    int x = 0, y = 0;
    char tail[16] = {};
    if (sscanf(format.c_str() + sizeof(prefix) - 1, "%dx%d_%15s", &x, &y, tail) != 3) return false;
    const std::string suffix = tail;
    if (suffix != "UNORM_BLOCK" && suffix != "SRGB_BLOCK") return false;
    static const int valid[][2] = {{4,4},{5,4},{5,5},{6,5},{6,6},{8,5},{8,6},{8,8},{10,5},{10,6},{10,8},{10,10},{12,10},{12,12}};
    for (auto& v : valid) if (v[0] == x && v[1] == y) { bx = x; by = y; srgb = suffix == "SRGB_BLOCK"; return true; }
    return false;
}
bool Natural(const Json& value, uint64_t& out) {
    if (!value.is_number_integer() || value.get<int64_t>() < 0) return false;
    out = value.get<uint64_t>();
    return true;
}
bool ReadExtent(const Json& metadata, uint32_t& w, uint32_t& h, uint32_t& d) {
    for (const char* key : {"extent", "dimensions"}) {
        const auto it = metadata.find(key);
        if (it == metadata.end()) continue;
        uint64_t v[3] = {0, 0, 1};
        if (it->is_array() && it->size() >= 2) {
            for (size_t i = 0; i < 3 && i < it->size(); ++i) { if (!Natural((*it)[i], v[i])) return false; }
        } else if (it->is_object() && it->contains("width") && it->contains("height")) {
            if (!Natural((*it)["width"], v[0]) || !Natural((*it)["height"], v[1])) return false;
            if (it->contains("depth") && !Natural((*it)["depth"], v[2])) return false;
        } else continue;
        if (!v[0] || !v[1] || !v[2] || v[0] > 0xFFFFFF || v[1] > 0xFFFFFF || v[2] > 0xFFFFFF) return false;
        w = uint32_t(v[0]); h = uint32_t(v[1]); d = uint32_t(v[2]);
        return true;
    }
    return false;
}
void Put24(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 3; ++i) out.push_back(uint8_t(v >> (8 * i))); }
} // namespace

bool IsAstcFormat(const std::string& vkFormat) { int x, y; bool s; return ParseFormat(vkFormat, x, y, s); }
bool AstcBlockSize(const std::string& vkFormat, int& blockX, int& blockY) { bool s; return ParseFormat(vkFormat, blockX, blockY, s); }

bool IsAstcArtifact(const std::filesystem::path& file, const Json& metadata) {
    return file.extension() == ".bin" && metadata.is_object() && metadata.contains("format") && metadata["format"].is_string() && IsAstcFormat(metadata["format"].get<std::string>());
}

Json DecodeCompressedImage(const std::filesystem::path& raw, const Json& metadata, const std::filesystem::path& png,
                           const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    std::filesystem::path header, tmpPng;
    try {
        const std::string format = metadata.is_object() && metadata.contains("format") && metadata["format"].is_string() ? metadata["format"].get<std::string>() : "";
        int bx = 0, by = 0; bool srgb = false;
        if (!ParseFormat(format, bx, by, srgb)) return Unavailable(raw, "No host decoder for format " + (format.empty() ? "(unknown)" : format));
        uint32_t w = 0, h = 0, d = 1;
        if (!ReadExtent(metadata, w, h, d)) return Unavailable(raw, "Image extent missing or invalid");
        if (d != 1) return Unavailable(raw, "3D compressed images are not decoded");
        const auto toolIt = tools.find("astcenc");
        if (toolIt == tools.end() || toolIt->second.empty()) return Unavailable(raw, "astcenc is not available");
        std::error_code ec;
        if (!std::filesystem::is_regular_file(raw, ec)) return Unavailable(raw, "Raw image file is missing");
        const uint64_t expected = uint64_t((w + bx - 1) / bx) * ((h + by - 1) / by) * 16;
        const uint64_t actual = std::filesystem::file_size(raw, ec);
        if (ec || actual != expected) return Unavailable(raw, "Raw size " + std::to_string(actual) + " does not match " + std::to_string(expected) + " bytes expected for " + std::to_string(w) + "x" + std::to_string(h) + " " + format);
        std::vector<uint8_t> head = {0x13, 0xAB, 0xA1, 0x5C, uint8_t(bx), uint8_t(by), 1};
        Put24(head, w); Put24(head, h); Put24(head, 1);
        header = png; header += ".tmp.astc";
        tmpPng = png; tmpPng += ".tmp.png";
        if (!png.parent_path().empty()) std::filesystem::create_directories(png.parent_path(), ec);
        {
            std::ifstream in(raw, std::ios::binary);
            std::ofstream out(header, std::ios::binary | std::ios::trunc);
            if (!in || !out) return Unavailable(raw, "Cannot write temporary .astc file");
            out.write(reinterpret_cast<const char*>(head.data()), std::streamsize(head.size()));
            out << in.rdbuf();
            out.flush();
            if (!out) { out.close(); std::filesystem::remove(header, ec); return Unavailable(raw, "Cannot write temporary .astc file"); }
        }
        const std::string mode = srgb ? "-ds" : "-dl";
        ProcessRequest request{toolIt->second, {mode, header.u8string(), tmpPng.u8string()}};
        request.timeout = std::chrono::seconds(60);
        const auto result = RunProcess(request, cancel);
        std::filesystem::remove(header, ec);
        if (!result.Success() || !std::filesystem::is_regular_file(tmpPng, ec)) {
            std::filesystem::remove(tmpPng, ec);
            return Unavailable(raw, result.cancelled ? "Decode cancelled" : result.timedOut ? "astcenc timed out" : "astcenc failed (exit " + std::to_string(result.exitCode) + "): " + result.output.substr(0, 400));
        }
        std::filesystem::remove(png, ec);
        std::filesystem::rename(tmpPng, png, ec);
        if (ec) { std::filesystem::remove(tmpPng, ec); return Unavailable(raw, "Cannot write " + png.u8string()); }
        return {{"status", "ok"}, {"png", png.u8string()}, {"decoded_by", kDecoder}, {"mode", mode}, {"block", Json::array({bx, by})}, {"raw", raw.u8string()}};
    } catch (const std::exception& e) {
        std::error_code ec;
        if (!header.empty()) std::filesystem::remove(header, ec);
        if (!tmpPng.empty()) std::filesystem::remove(tmpPng, ec);
        return Unavailable(raw, std::string("Decode failed: ") + e.what());
    }
}
} // namespace gfx
