#pragma once

#include <filesystem>
#include <string>

namespace gfx {
// Conservative interface equality for initial module-based editing. A differing
// signature requires manual review and is rejected before launching replay.
void CheckShaderInterface(const std::filesystem::path& original,
                           const std::filesystem::path& candidate,
                           const std::string& entry);
std::string DescribeShaderInterface(const std::filesystem::path& binary,const std::string& entry);
} // namespace gfx
