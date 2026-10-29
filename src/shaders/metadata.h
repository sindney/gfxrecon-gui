#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace gfx {
struct ShaderMetadata {
    nlohmann::json debug;
    std::string capturedSource;
};
ShaderMetadata ReadShaderMetadata(const std::vector<uint32_t>& words);
bool ShaderUsesDrawIndex(const std::filesystem::path& binary);
std::vector<uint32_t> ReadSpirvBinary(const std::filesystem::path& path);
} // namespace gfx
