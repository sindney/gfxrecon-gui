#include "analysis/relations.h"
#include "analysis/index.h"
#include "core/artifacts.h"
#include "platform/process.h"
#include <chrono>
#include <fstream>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace gfx {
namespace {
namespace fs = std::filesystem;
void Cancel(const std::atomic_bool* flag) {
    if (flag && flag->load()) throw RelationError(RelationErrorKind::Cancelled, "cancelled", "Relation indexing cancelled");
}
Json ReadJson(const fs::path& path) {
    if (fs::file_size(path) > 4 * 1024 * 1024) throw RelationError(RelationErrorKind::Data, "relation_manifest_invalid", "Relation manifest is too large");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw RelationError(RelationErrorKind::Data, "relation_manifest_invalid", "Cannot read relation manifest");
    return Json::parse(input);
}
void PublishJson(const fs::path& path, const Json& document, const std::atomic_bool* cancel) {
    auto pending = path; pending += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".partial";
    { std::ofstream output(pending, std::ios::binary); output << document.dump(2); if (!output) throw RelationError(RelationErrorKind::Data, "relation_publication_failed", "Cannot write relation manifest"); }
    // Windows readers can briefly hold a manifest open without delete sharing.
    // Retrying atomic replacement never removes the last published generation.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    for (;;) {
        Cancel(cancel); std::error_code error; fs::rename(pending, path, error);
        if (!error) return;
        if (std::chrono::steady_clock::now() >= deadline)
            throw RelationError(RelationErrorKind::Data, "relation_publication_failed", "Cannot publish relation manifest: " + error.message());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
bool Under(const fs::path& path, const fs::path& parent) {
    const auto relative = fs::weakly_canonical(path).lexically_relative(fs::weakly_canonical(parent));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
struct BuildLease {
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int file = -1;
#endif
    BuildLease(const fs::path& path, const std::atomic_bool* cancel) {
#ifdef _WIN32
        file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw RelationError(RelationErrorKind::Data, "relation_index_busy", "Cannot open relation build lock");
#else
        file = open(path.c_str(), O_RDWR | O_CREAT, 0644);
        if (file < 0) throw RelationError(RelationErrorKind::Data, "relation_index_busy", "Cannot open relation build lock");
#endif
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        try {
            for (;;) {
                Cancel(cancel);
#ifdef _WIN32
                OVERLAPPED overlap{};
                if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap)) break;
#else
                if (flock(file, LOCK_EX | LOCK_NB) == 0) break;
#endif
                if (std::chrono::steady_clock::now() >= deadline) throw RelationError(RelationErrorKind::Data, "relation_index_busy", "Another process is building this relation index");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } catch (...) {
#ifdef _WIN32
            CloseHandle(file);
#else
            close(file);
#endif
            throw;
        }
    }
    ~BuildLease() {
#ifdef _WIN32
        OVERLAPPED overlap{}; UnlockFileEx(file, 0, 1, 0, &overlap); CloseHandle(file);
#else
        flock(file, LOCK_UN); close(file);
#endif
    }
};
std::string Converter(const std::map<std::string, std::string>& tools) {
    const auto found = tools.find("convert");
    return found == tools.end() ? std::string{} : FindExecutablePath(found->second);
}
std::optional<RelationIndexRef> Cached(const fs::path& root, const std::string& sourceHash,
                                      const std::string& converter, const std::atomic_bool* cancel) {
    if (!fs::is_regular_file(root / "current.json")) return {};
    try {
        Cancel(cancel); const auto manifest = ReadJson(root / "current.json");
        if (manifest.value("owner", "") != "gfxrecon_gui_relations")
            throw RelationError(RelationErrorKind::Data, "relation_index_unowned", "Relation cache manifest belongs to another owner");
        if (manifest.value("schema", 0u) > kRelationStorageVersion || manifest.value("builder", 0u) > kRelationBuilderVersion ||
            manifest.value("parser_schema", 0u) > kIndexSchema || manifest.value("grouping_version", 0u) > 1)
            throw RelationError(RelationErrorKind::Data, "relation_index_version", "Relation cache uses a newer storage schema");
        if (manifest.value("schema", 0u) != kRelationStorageVersion || manifest.value("builder", 0u) != kRelationBuilderVersion ||
            manifest.value("parser_schema", 0u) != kIndexSchema || manifest.value("grouping_version", 0u) != 1 ||
            manifest.value("source_sha256", "") != sourceHash || !manifest.value("complete", false)) return {};
        if (!converter.empty() && manifest.value("converter_fingerprint", "") != converter) return {};
        const auto relative = fs::u8path(manifest.at("database").get<std::string>());
        if (relative.is_absolute() || !Under(root / relative, root))
            throw RelationError(RelationErrorKind::Data, "relation_index_unowned", "Relation cache database escaped its owner directory");
        const auto database = root / relative;
        if (!fs::is_regular_file(database)) return {};
        auto result = OpenRelationIndex(database, cancel);
        if (Sha256File(database, cancel) != manifest.at("database_sha256")) return {};
        if (result.snapshot != manifest.at("snapshot") || result.metadata.at("source_sha256") != sourceHash ||
            result.metadata.at("builder") != kRelationBuilderVersion || result.metadata.at("parser_schema") != kIndexSchema) return {};
        const auto decoded = fs::u8path(result.metadata.at("jsonl_path").get<std::string>());
        if (fs::is_regular_file(decoded) && Sha256File(decoded, cancel) != result.metadata.at("decoded_sha256")) return {};
        result.reused = true; return result;
    } catch (const RelationError& error) {
        if (error.kind == RelationErrorKind::Cancelled || error.code == "relation_index_unowned" || error.code == "relation_index_version") throw;
    } catch (...) { Cancel(cancel); }
    return {};
}
std::shared_ptr<Capture> ExistingDecoded(const fs::path& source, const fs::path& cache,
                                       const std::string& sourceHash, const std::string& converterHash,
                                       const std::atomic_bool* cancel) {
    if (!fs::is_directory(cache)) return {};
    for (const auto& entry : fs::directory_iterator(cache)) {
        Cancel(cancel); if (!entry.is_directory() || entry.path().filename().u8string().rfind(sourceHash + "-", 0) != 0) continue;
        try {
            const auto marker = ReadJson(entry.path() / "cache.json");
            if (marker.value("owner", "") != "gfxrecon_gui" || marker.value("schema", 0u) != kIndexSchema ||
                !marker.value("complete", false) || marker.value("capture_sha256", "") != sourceHash) continue;
            const auto tools = marker.value("tools", std::string{});
            if (!converterHash.empty() && converterHash != "jsonl" && tools.rfind(converterHash.substr(0, 16) + "-", 0) != 0) continue;
            const auto jsonl = entry.path() / "capture.jsonl", index = entry.path() / "index.bin";
            if (!fs::is_regular_file(jsonl) || Sha256File(jsonl, cancel) != marker.at("jsonl_sha256")) continue;
            std::shared_ptr<Capture> capture;
            if (fs::is_regular_file(index) && Sha256File(index, cancel) == marker.at("index_sha256")) capture = ReadCaptureIndex(jsonl, index, cancel);
            else capture = ReadCapture(jsonl, cancel);
            capture->source = source; capture->fingerprint = sourceHash; return capture;
        } catch (...) { Cancel(cancel); }
    }
    return {};
}
} // namespace

RelationIndexRef EnsureRelationIndex(const std::filesystem::path& captureFile, const std::filesystem::path& cacheRoot,
                                    const std::map<std::string, std::string>& tools, const RelationBuildOptions& options,
                                    const std::atomic_bool* cancel) {
    Cancel(cancel);
    if (!fs::is_regular_file(captureFile)) throw RelationError(RelationErrorKind::Data, "relation_source_missing", "Capture file does not exist: " + captureFile.u8string());
    const auto source = fs::absolute(captureFile).lexically_normal(); const auto extension = source.extension().u8string();
    if (extension != ".gfxr" && extension != ".jsonl") throw RelationError(RelationErrorKind::Usage, "relation_source_unsupported", "Relation indexing accepts .gfxr or decoded .jsonl");
    const auto sourceHash = Sha256File(source, cancel); const auto converter = extension == ".gfxr" ? Converter(tools) : std::string{};
    const auto converterHash = extension == ".jsonl" ? "jsonl" : converter.empty() ? std::string{} : Sha256File(fs::u8path(converter), cancel);
    const auto root = fs::absolute(cacheRoot) / ("relations-v" + std::to_string(kRelationStorageVersion)) / sourceHash;
    auto previous = Cached(root, sourceHash, converterHash, cancel);
    if (!options.rebuild && previous) return *previous;
    fs::create_directories(root); BuildLease lease(root / "build.lock", cancel);
    const auto ownerPath = root / "owner.json";
    if (fs::is_regular_file(ownerPath)) {
        const auto owner = ReadJson(ownerPath);
        if (owner.value("owner", "") != "gfxrecon_gui_relations" || owner.value("source_sha256", "") != sourceHash)
            throw RelationError(RelationErrorKind::Data, "relation_index_unowned", "Refusing to replace an unrelated relation cache");
    } else {
        for (const auto& entry : fs::directory_iterator(root)) if (entry.path().filename() != "build.lock")
            throw RelationError(RelationErrorKind::Data, "relation_index_unowned", "Refusing to adopt an existing unowned cache directory");
        PublishJson(ownerPath, {{"owner","gfxrecon_gui_relations"},{"source_sha256",sourceHash}}, cancel);
    }
    previous = Cached(root, sourceHash, converterHash, cancel);
    if (!options.rebuild && previous) return *previous;
    if (options.progress) options.progress("Preparing static capture relations");
    const auto generation = root / ("generation-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(generation)) throw RelationError(RelationErrorKind::Data, "relation_publication_failed", "Cannot create unique relation generation");
    auto capture = ExistingDecoded(source, cacheRoot, sourceHash, converterHash, cancel);
    std::string usedConverter = converterHash;
    if (!capture && extension == ".jsonl") capture = ReadCapture(source, cancel);
    if (!capture && extension == ".gfxr") {
        // An older complete generation can supply decoded metadata during rebuild.
        try {
            const auto& old = previous;
            if (old) {
                const auto decoded = fs::u8path(old->metadata.at("jsonl_path").get<std::string>());
                if (fs::is_regular_file(decoded)) capture = ReadCapture(decoded, cancel);
                if (usedConverter.empty()) usedConverter = ReadJson(root / "current.json").value("converter_fingerprint", std::string{});
            }
        } catch (...) { Cancel(cancel); }
        if (!capture) {
            if (converter.empty()) throw RelationError(RelationErrorKind::MissingTool, "missing_tool", "Configure gfxrecon-convert to index this capture", {{"tool","convert"}});
            if (options.progress) options.progress("Decoding capture metadata (no shader extraction or replay)");
            ProcessRequest request{converter, {"--format","jsonl","--output",(generation / "capture.jsonl").u8string(),source.u8string()}};
            request.timeout = std::chrono::minutes(10); const auto converted = RunProcess(request, cancel);
            { std::ofstream log(generation / "convert.log", std::ios::binary); log << converted.output; }
            Cancel(cancel);
            if (!converted.Success()) throw RelationError(RelationErrorKind::Data, "relation_decode_failed", "Capture metadata conversion failed: " + converted.output.substr(0, 4096));
            capture = ReadCapture(generation / "capture.jsonl", cancel);
        }
    }
    capture->source = source; capture->fingerprint = sourceHash;
    auto result = BuildRelationSnapshot(*capture, generation / "relations.sqlite", cancel, options.progress);
    Cancel(cancel);
    if (Sha256File(source, cancel) != sourceHash) throw RelationError(RelationErrorKind::Data, "relation_source_changed", "Capture changed while building relations; new generation was not published");
    Json manifest{{"owner","gfxrecon_gui_relations"},{"schema",kRelationStorageVersion},{"builder",kRelationBuilderVersion},
        {"parser_schema",kIndexSchema},{"grouping_version",1},{"complete",true},{"source_sha256",sourceHash},
        {"converter_fingerprint",usedConverter},{"snapshot",result.snapshot},{"database",result.path.lexically_relative(root).generic_u8string()},
        {"database_sha256",Sha256File(result.path, cancel)}};
    PublishJson(root / "current.json", manifest, cancel); result.reused = false;
    if (options.progress) options.progress("Published static relation index: " + result.path.u8string());
    return result;
}
} // namespace gfx
