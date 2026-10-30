#pragma once
// Host decode of compressed (ASTC) image artifacts to PNG via the bundled astcenc tool.
#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <nlohmann/json.hpp>

namespace gfx {
// VK_FORMAT_ASTC_<x>x<y>_{UNORM,SRGB}_BLOCK. SFLOAT (HDR) blocks are not decoded to PNG.
bool IsAstcFormat(const std::string& vkFormat);
bool AstcBlockSize(const std::string& vkFormat, int& blockX, int& blockY);
// A raw ASTC block dump (.bin) whose metadata names an ASTC format; the viewer decodes these to PNG.
bool IsAstcArtifact(const std::filesystem::path& file, const nlohmann::json& metadata);
// Wraps raw ASTC blocks in a .astc header and runs `astcenc -dl|-ds` to write a PNG.
// metadata: "format" (VK format name) and "extent" or "dimensions" ([w,h,(d)] or {width,height,depth}).
// Returns {"status":"ok","png","decoded_by","mode","block","raw"} or {"status":"unavailable","reason"}; never throws
// for a missing decoder, unsupported format or tool failure. The raw file is always kept.
nlohmann::json DecodeCompressedImage(const std::filesystem::path& raw, const nlohmann::json& metadata, const std::filesystem::path& png,
                                     const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel = nullptr);
} // namespace gfx
