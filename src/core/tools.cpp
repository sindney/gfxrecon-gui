#include "core/tools.h"
#include "core/log.h"
#include "platform/process.h"

#include <fstream>
#include <nlohmann/json.hpp>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#include <intrin.h>
#endif

namespace gfx {
namespace {
// CPU and OS both support AVX2 (leaf 7 EBX bit 5, OSXSAVE, XMM+YMM state enabled).
bool CpuSupportsAvx2() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int regs[4] = {};
    __cpuid(regs, 0);
    if (regs[0] < 7) return false;
    __cpuid(regs, 1);
    if (!(regs[2] & (1 << 27))) return false;  // OSXSAVE
    if ((_xgetbv(0) & 6) != 6) return false;
    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}
std::string ResolveAstcenc(const std::filesystem::path& bundled) {
    const auto dir = bundled / "astcenc";
    const auto avx2 = dir / "astcenc-avx2.exe", sse2 = dir / "astcenc-sse2.exe";
    if (CpuSupportsAvx2() && std::filesystem::is_regular_file(avx2)) return avx2.u8string();
    if (std::filesystem::is_regular_file(sse2)) return sse2.u8string();
    return "";
}
} // namespace
ToolMap ResolveTools(const std::filesystem::path& dir, const std::filesystem::path& exeDir) {
    ToolMap tools;
#ifdef _WIN32
    const std::string suffix = ".exe";
#else
    const std::string suffix;
#endif
    const std::pair<const char*, const char*> defaults[] = {
        {"convert", "gfxrecon-convert"}, {"info", "gfxrecon-info"}, {"extract", "gfxrecon-extract"}, {"optimize", "gfxrecon-optimize"},
        {"replay", "gfxrecon-replay"}, {"adb", "adb"}, {"compiler", "glslangValidator"},
        {"disassembler", "spirv-dis"}, {"assembler", "spirv-as"}, {"validator", "spirv-val"}};
    auto bundled = exeDir / "tools";
#ifdef __APPLE__
    if (exeDir.filename() == "MacOS" && exeDir.parent_path().filename() == "Contents")
        bundled = exeDir.parent_path() / "Resources/tools";
#endif
    const bool hasBundle = std::filesystem::is_regular_file(bundled / ("gfxrecon-convert" + suffix));
    for (auto [key, executable] : defaults) {
        const auto file = bundled / (std::string(executable) + suffix);
        tools[key] = std::filesystem::is_regular_file(file) ? file.u8string() : FindExecutablePath(executable);
    }
    tools["astcenc"] = ResolveAstcenc(bundled);
    tools["layer"] = hasBundle ? (bundled / "layer").u8string() : "";
    tools["validation_layer"] = hasBundle ? (bundled / "validation").u8string() : "";
    tools["replay_apk"] = hasBundle ? (bundled / "android/replay-debug.apk").u8string() : "";
    tools["test_apk"] = hasBundle ? (bundled / "android/test_launcher-debug.apk").u8string() : "";
    auto localConfig = std::filesystem::current_path() / "local-tools.json";
    if (!std::filesystem::is_regular_file(localConfig)) {
        auto parent = dir;
        for (int i = 0; i < 5; ++i) {
            if (std::filesystem::is_regular_file(parent / "local-tools.json")) { localConfig = parent / "local-tools.json"; break; }
            parent = parent.parent_path();
        }
    }
    for (auto file : {localConfig, dir / "gfxrecon_tools.json"}) {
        if (hasBundle && file == localConfig) continue;
        if (!std::filesystem::is_regular_file(file)) continue;
        try {
            std::ifstream input(file, std::ios::binary);
            const auto config = nlohmann::json::parse(input);
            for (auto it = config.begin(); it != config.end(); ++it)
                if (tools.count(it.key()) && it.value().is_string()) tools[it.key()] = it.value().get<std::string>();
        } catch (const std::exception& e) { LOGW("tools") << e.what(); }
    }
    return tools;
}
std::map<std::string, bool> ToolAvailability(const ToolMap& tools) {
    std::map<std::string, bool> ready;
    for (const auto& [name, path] : tools) {
        const auto file = std::filesystem::u8path(path);
        if (name == "validation_layer") { ready[name] = std::filesystem::is_regular_file(file / "VkLayer_khronos_validation.json"); continue; }
        ready[name] = !path.empty() && (name == "layer" ? std::filesystem::is_regular_file(file / "VkLayer_gfxreconstruct.json") :
            name == "replay_apk" || name == "test_apk" ? std::filesystem::is_regular_file(file) : !FindExecutablePath(path).empty());
    }
    return ready;
}
std::map<std::string, std::string> ProbeToolVersions(const ToolMap& tools, std::map<std::string, bool>& ready,
                                                     const std::atomic_bool* cancel) {
    std::map<std::string, std::string> versions;
    for (const auto& [name, path] : tools) {
        if (cancel && cancel->load()) break;
        if (name == "layer" || name == "validation_layer" || name == "replay_apk" || name == "test_apk") {
            ready[name] = std::filesystem::is_regular_file(std::filesystem::u8path(path) / (name == "layer" ? "VkLayer_gfxreconstruct.json" : ""));
            if (name == "validation_layer") ready[name] = std::filesystem::is_regular_file(std::filesystem::u8path(path) / "VkLayer_khronos_validation.json");
            else if (name != "layer") ready[name] = std::filesystem::is_regular_file(std::filesystem::u8path(path));
            versions[name] = ready[name] ? "File present; compatibility is verified during setup" : "File missing"; continue;
        }
        ready[name] = false;
        if (path.empty()) { versions[name] = "Not configured"; continue; }
        ProcessRequest request{path, {name == "adb" ? "version" : name == "astcenc" ? "-version" : "--version"}};
        if (name == "astcenc") request.timeout = std::chrono::seconds(15);
        const auto result = RunProcess(request, cancel);
        if (result.Success()) { versions[name] = result.output; ready[name] = true; }
        else versions[name] = result.cancelled ? "Operation cancelled" : result.timedOut ? "Tool timed out" :
            "Tool exited " + std::to_string(result.exitCode) + ":\n" + result.output;
    }
    return versions;
}
} // namespace gfx
