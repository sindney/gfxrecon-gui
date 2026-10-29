#include "analysis/cache.h"
#include "core/artifacts.h"
#include <chrono>
#include <fstream>

namespace gfx {
namespace {
bool Valid(const std::filesystem::path& directory, const std::string& source, const std::string& tools,
           const std::atomic_bool* cancel) {
    const auto marker = directory / "cache.json";
    if (!std::filesystem::is_regular_file(marker)) return false;
    try {
        std::ifstream input(marker); const auto metadata = Json::parse(input);
        if (metadata.at("schema") != kIndexSchema || metadata.at("owner") != "gfxrecon_gui" || !metadata.at("complete").get<bool>() ||
            metadata.at("capture_sha256") != source || metadata.at("tools") != tools) return false;
        return metadata.at("index_sha256") == Sha256File(directory / "index.bin", cancel) &&
            metadata.at("jsonl_sha256") == Sha256File(directory / "capture.jsonl", cancel);
    } catch (...) {
        if (cancel && cancel->load()) throw std::runtime_error("Cache validation cancelled");
        return false;
    }
}
}
CachedCapture OpenCachedCapture(const std::filesystem::path& source, const std::filesystem::path& cacheRoot,
                                 const std::string& tools,
                                 const std::function<std::shared_ptr<Capture>(const std::filesystem::path&)>& build,
                                 const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Import cancelled");
    const auto fingerprint = Sha256File(source, cancel);
    if (tools.find_first_not_of("0123456789abcdef-jsonlnoextract") != std::string::npos || tools.size() > 150)
        throw std::runtime_error("Invalid tool cache identity");
    std::filesystem::create_directories(cacheRoot);
    const auto key = fingerprint + "-" + tools + "-v" + std::to_string(kIndexSchema);
    const auto destination = cacheRoot / key;
    if (Valid(destination, fingerprint, tools, cancel)) {
        try {
            auto capture = ReadCaptureIndex(destination / "capture.jsonl", destination / "index.bin", cancel);
            capture->source = source;
            return {std::move(capture), destination, fingerprint, true};
        } catch (...) { if (cancel && cancel->load()) throw; }
    }
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto staging = cacheRoot / (".building-" + token);
    if (!std::filesystem::create_directory(staging)) throw std::runtime_error("Cannot create import staging directory");
    auto capture = build(staging);
    if (!capture || capture->events.empty() || capture->jsonl != staging / "capture.jsonl") throw std::runtime_error("Import did not produce a complete indexed capture");
    WriteCaptureIndex(*capture, staging / "index.bin", cancel);
    if (Sha256File(source, cancel) != fingerprint) throw std::runtime_error("Capture changed during import; reopen the stable file");
    if (cancel && cancel->load()) throw std::runtime_error("Import cancelled before cache publication");
    AtomicJson(staging / "cache.json", Json{{"owner", "gfxrecon_gui"}, {"schema", kIndexSchema}, {"complete", true},
        {"capture_sha256", fingerprint}, {"tools", tools}, {"index_sha256", Sha256File(staging / "index.bin", cancel)},
        {"jsonl_sha256", Sha256File(staging / "capture.jsonl", cancel)}});
    // No deletion: preserve failed/stale derived data and all authored revisions.
    // Every renamed path is a direct, generated child of the owned cache root.
    const auto parent = std::filesystem::weakly_canonical(cacheRoot);
    if (std::filesystem::weakly_canonical(staging.parent_path()) != parent || std::filesystem::weakly_canonical(destination.parent_path()) != parent)
        throw std::runtime_error("Cache publication escaped its owner directory");
    if (std::filesystem::exists(destination)) std::filesystem::rename(destination, cacheRoot / (".previous-" + token));
    std::filesystem::rename(staging, destination);
    capture->jsonl = destination / "capture.jsonl"; capture->source = source;
    return {std::move(capture), destination, fingerprint, false};
}
} // namespace gfx
