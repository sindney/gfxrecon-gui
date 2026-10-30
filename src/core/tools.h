#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <string>

namespace gfx {
using ToolMap = std::map<std::string, std::string>;
// Bundled tools beside the executable, then local-tools.json (development checkouts only),
// then <settings>/gfxrecon_tools.json. The GUI and CLI resolve identically.
ToolMap ResolveTools(const std::filesystem::path& settingsDir, const std::filesystem::path& exeDir);
// Cheap presence check; versions are probed separately.
std::map<std::string, bool> ToolAvailability(const ToolMap& tools);
// Runs each executable tool's version command; files report presence only.
std::map<std::string, std::string> ProbeToolVersions(const ToolMap& tools, std::map<std::string, bool>& ready,
                                                     const std::atomic_bool* cancel = nullptr);
} // namespace gfx
