#include "analysis/import.h"
#include "analysis/cache.h"
#include "core/artifacts.h"
#include "platform/process.h"

#include <stdexcept>

namespace gfx {
namespace {
void Run(const std::string& exe, const std::vector<std::string>& args, const std::atomic_bool* cancel) {
    auto result = RunProcess({exe, args}, cancel);
    if (!result.Success()) throw std::runtime_error(result.cancelled ? "Operation cancelled" :
        result.timedOut ? "Tool timed out" : "Tool exited " + std::to_string(result.exitCode) + ":\n" + result.output);
}
std::string Tool(const std::map<std::string, std::string>& tools, const char* name) {
    const auto found = tools.find(name); return found == tools.end() ? "" : found->second;
}
} // namespace
ImportResult ImportCapture(const std::filesystem::path& file, const std::filesystem::path& cacheRoot,
                           const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    ImportResult result;
    if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("Capture file does not exist: " + file.u8string());
    std::string identity = "jsonl";
    const auto convert = Tool(tools, "convert"), extract = Tool(tools, "extract");
    if (file.extension() == ".gfxr") {
        if (convert.empty()) throw std::runtime_error("Configure gfxrecon-convert in View > Tools and devices to open .gfxr files.");
        identity = Sha256File(std::filesystem::u8path(FindExecutablePath(convert)), cancel).substr(0, 16);
        identity += "-" + (std::filesystem::is_regular_file(std::filesystem::u8path(extract)) ?
            Sha256File(std::filesystem::u8path(extract), cancel).substr(0, 16) : std::string("noextract"));
    }
    const auto cached = OpenCachedCapture(file, cacheRoot, identity, [&](const std::filesystem::path& staging) {
        const auto jsonl = staging / "capture.jsonl";
        if (file.extension() == ".gfxr") {
            Run(convert, {"--format", "jsonl", "--output", jsonl.u8string(), file.u8string()}, cancel);
            if (!extract.empty()) {
                try { Run(extract, {"--dir", (staging / "shaders").u8string(), file.u8string()}, cancel); }
                catch (const std::exception& e) { result.message = std::string("Shader extraction failed: ") + e.what() + "\n"; }
            }
        } else std::filesystem::copy_file(file, jsonl);
        return ReadCapture(jsonl, cancel);
    }, cancel);
    result.directory = cached.directory; result.capture = cached.capture; result.cacheHit = cached.hit;
    result.capture->fingerprint = cached.fingerprint;
    result.message += cached.hit ? "Loaded cached index: " : "Indexed ";
    result.message += std::to_string(result.capture->events.size()) + " recorded calls across " + std::to_string(result.capture->frames) + " frames.";
    return result;
}
} // namespace gfx
