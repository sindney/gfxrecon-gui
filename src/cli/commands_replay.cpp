// replay frame/draw, compare, shader show/export/compile/check and perf.
#include "cli/cli.h"
#include "core/artifacts.h"
#include "core/image_io.h"
#include "shaders/decode.h"
#include "shaders/interface.h"
#include "shaders/metadata.h"
#include "shaders/diagnostics.h"
#include "analysis/dump_dynamic.h"
#include "analysis/geometry_summary.h"
#include "replay/gpu_measurement.h"
#include "replay/perf_session.h"
#include "platform/device_lock.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

namespace gfx::cli {
namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;

fs::path Path(const std::string& text) { return fs::absolute(fs::u8path(text)); }
std::string U8(const fs::path& path) { return path.u8string(); }
uint64_t ParseModule(const std::string& text) {
    try { size_t used = 0; const auto value = std::stoull(text, &used, 0); if (used == text.size() && value) return value; } catch (...) {}
    throw Usage("Shader module id must be a positive integer, got '" + text + "'");
}
void WriteFile(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc); out << text;
    if (!out) throw CliError(kFailed, "write_failed", "Cannot write " + U8(file));
}

// ---- shader helpers -----------------------------------------------------------------------------------------
struct ModuleInfo { ShaderStage stage; fs::path binary; std::vector<uint64_t> pipelines; };
ModuleInfo FindModule(const ImportResult& import, uint64_t module) {
    ModuleInfo info;
    bool found = false;
    for (const auto& [pipeline, stages] : import.capture->pipelines)
        for (const auto& stage : stages) if (stage.module == module) {
            if (!found) { info.stage = stage; found = true; }
            info.pipelines.push_back(pipeline);
        }
    if (!found) throw CliError(kFailed, "module_not_found", "Shader module " + std::to_string(module) + " is not used by any pipeline in this capture", {{"module", module}});
    info.binary = import.directory / "shaders" / ("sh" + std::to_string(module));
    if (!fs::is_regular_file(info.binary)) throw CliError(kFailed, "shader_unavailable", "The shader binary for module " + std::to_string(module) + " was not extracted from the capture (is gfxrecon-extract configured?)", {{"module", module}});
    return info;
}
std::string StageName(const std::string& text) {
    static const std::map<std::string, std::string> names{{"vert", "VK_SHADER_STAGE_VERTEX_BIT"}, {"frag", "VK_SHADER_STAGE_FRAGMENT_BIT"}, {"comp", "VK_SHADER_STAGE_COMPUTE_BIT"}};
    if (const auto found = names.find(text); found != names.end()) return found->second;
    if (text.rfind("VK_SHADER_STAGE_", 0) == 0) return text;
    throw Usage("--stage must be vert, frag, comp or a VK_SHADER_STAGE_* name, got '" + text + "'");
}
void RequireShaderTools(const Context& context, const fs::path& source) {
    const auto extension = source.extension().u8string();
    std::vector<std::string> needed{"validator"};
    needed.push_back(extension == ".spv" ? "validator" : extension == ".spvasm" ? "assembler" : "compiler");
    for (const auto& tool : needed) if (ToolPath(context, tool).empty()) throw MissingTool(tool, "Configure the '" + tool + "' tool (glslang/SPIRV-Tools) to compile and validate shaders.");
}
// Compiles `source` for `stage` into `output` (validated). Compiler diagnostics become compile_failed.
void CompileOrThrow(const Context& context, const fs::path& source, const ShaderStage& stage, const fs::path& output) {
    if (!fs::is_regular_file(source)) throw CliError(kFailed, "file_not_found", "Shader file does not exist: " + U8(source), {{"file", U8(source)}});
    RequireShaderTools(context, source);
    try { CompileShader(source, stage, output, context.tools, context.cancel); }
    catch (const std::exception& error) {
        if (context.cancel && context.cancel->load()) throw;
        throw CliError(kFailed, "compile_failed", "Shader compilation failed: " + std::string(error.what()), {{"file", U8(source)}, {"stage", stage.stage}, {"entry", stage.entry}, {"diagnostics", error.what()}});
    }
}
Json SafeInterface(const fs::path& binary, const std::string& entry) {
    try { return Json::parse(DescribeShaderInterface(binary, entry)); }
    catch (const std::exception& error) { return {{"unavailable", error.what()}}; }
}
void CheckInterfaceOrThrow(uint64_t module, const fs::path& original, const fs::path& candidate, const std::string& entry) {
    try { CheckShaderInterface(original, candidate, entry); }
    catch (const std::exception& error) {
        throw CliError(kFailed, "interface_mismatch", error.what(), {{"module", module}, {"entry", entry}, {"original_interface", SafeInterface(original, entry)},
            {"candidate_interface", SafeInterface(candidate, entry)}, {"hint", "Pass --allow-interface-change to replay a candidate with a different interface (risky)."}});
    }
}

// ---- overrides ----------------------------------------------------------------------------------------------
struct Overrides { std::map<uint64_t, fs::path> files; Json report = Json::array(); };
Overrides PrepareOverrides(const Context& context, const ImportResult& import, const Args& args, const fs::path& staging) {
    Overrides result;
    for (const auto& spec : args.All("override")) {
        const auto equals = spec.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 == spec.size()) throw Usage("--override expects MOD=FILE, got '" + spec + "'");
        const auto module = ParseModule(spec.substr(0, equals));
        if (result.files.count(module)) throw Usage("Module " + std::to_string(module) + " is overridden more than once");
        const auto source = Path(spec.substr(equals + 1));
        const auto info = FindModule(import, module);
        if (const auto limitation = import.capture->ModuleEditLimitation(module); !limitation.empty())
            throw CliError(kFailed, "module_not_editable", "Module " + std::to_string(module) + " cannot be replaced: " + limitation, {{"module", module}, {"limitation", limitation}});
        fs::create_directories(staging);
        const auto compiled = staging / ("sh" + std::to_string(module) + ".spv");
        CompileOrThrow(context, source, info.stage, compiled);
        const bool allow = args.Has("allow-interface-change");
        if (!allow) CheckInterfaceOrThrow(module, info.binary, compiled, info.stage.entry);
        result.files[module] = compiled;
        result.report.push_back({{"module", module}, {"source", U8(source)}, {"compiled", U8(compiled)}, {"sha256", Sha256File(compiled, context.cancel)},
            {"original_sha256", Sha256File(info.binary, context.cancel)}, {"stage", info.stage.stage}, {"entry", info.stage.entry},
            {"interface_checked", !allow}, {"interface_change_allowed", allow}, {"pipelines_using_module", info.pipelines.size()},
            {"scope", "Replacement affects every captured use of this module (all pipelines/draws that reference it), not only the selected draw."}});
    }
    return result;
}

// ---- replay plumbing ----------------------------------------------------------------------------------------
std::chrono::seconds ReplayTimeout(const Context& context, const ReplayTarget& target, bool validate) {
    if (context.timeout.count()) return context.timeout;
    return validate ? 900s : target.android ? 600s : 300s;   // Android cold state load alone is ~80 s
}
Json TargetJson(const ReplayTarget& target) { return {{"label", target.Label()}, {"android", target.android}, {"serial", target.serial}}; }
struct Opened { ImportResult import; fs::path file; };
struct InspectionTiming {
    double replaySeconds = 0;
    uint64_t replayCalls = 0;
    Json Report(std::chrono::steady_clock::time_point start) const {
        const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return {{"total_seconds", total}, {"replay_wall_seconds", replaySeconds},
            {"frontend_seconds", std::max(0.0, total - replaySeconds)}, {"replay_calls", replayCalls},
            {"scope", "Command processing including import, preparation and PNG conversion; excludes process startup and final result serialization. Replay calls include auxiliary passes and cache restores."}};
    }
};
Opened OpenReplayable(const Context& context, const fs::path& file) {
    Opened opened{OpenCapture(context, file), file};
    if (opened.import.capture->source.extension() != ".gfxr")
        throw CliError(kFailed, "not_replayable", "Only .gfxr captures can be replayed: " + U8(file), {{"capture", U8(file)}});
    return opened;
}
std::string ReplaySwapchainMode(const Args& args) {
    const auto mode = args.Get("swapchain", "virtual");
    if (mode != "virtual" && mode != "captured" && mode != "offscreen")
        throw Usage("--swapchain expects virtual, captured or offscreen");
    return mode;
}
std::string ReplayMemoryTranslation(const Args& args) {
    const auto mode = args.Get("memory-translation", "default");
    try { (void)ResolveMemoryTranslation(mode, false); }
    catch (const std::exception&) { throw Usage("--memory-translation expects default, none, remap, realign or rebind"); }
    return mode;
}
ReplayRequest BaseRequest(const Context& context, const Capture& capture, const ReplayTarget& target, const Args& args, bool validate) {
    ReplayRequest request;
    request.capture = capture.source;
    request.expectedCaptureHash = capture.fingerprint;
    request.cacheDirectory = context.ReplayCache();
    request.optimize = !args.Has("no-optimize");
    request.reuseArtifacts = !args.Has("fresh");
    request.screenOff = args.Has("screen-off") && target.android;
    request.validation = validate;
    request.swapchainMode = ReplaySwapchainMode(args);
    request.memoryTranslation = ReplayMemoryTranslation(args);
    request.timeout = ReplayTimeout(context, target, validate);
    request.progress = [&context](const std::string& text) { context.Progress(text); };
    return request;
}
// ReplayCapture with CLI error codes for the failures the contract names.
ReplayResult RunReplay(const Context& context, const ReplayRequest& request, const ReplayTarget& target, InspectionTiming* timing = nullptr) {
    struct Measure {
        InspectionTiming* timing;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~Measure() { if (timing) { ++timing->replayCalls; timing->replaySeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); } }
    } measure{timing};
    context.Progress("Replaying (" + request.prefix + ") on " + target.Label());
    try { return ReplayCapture(request, target, context.tools, context.cancel); }
    catch (const CliError&) { throw; }
    catch (const std::exception& error) {
        const std::string text = error.what();
        if (context.cancel && context.cancel->load()) throw;
        if (text.find("did not confirm replacement") != std::string::npos) {
            Json modules = Json::array(); for (const auto& [module, file] : request.overrides) modules.push_back(module);
            throw CliError(kFailed, "override_not_confirmed", text, {{"modules", modules}, {"replay_log", U8(request.output / "replay.log")}});
        }
        if (request.validation && text.find("was not enabled") != std::string::npos)
            throw CliError(kMissingTool, "validation_unavailable", text, {{"validation", {{"status", "unavailable"}, {"report", U8(request.output / "validation.json")}}}});
        if (request.measurement.gpu)
            throw CliError(kFailed, "gpu_measurement_failed", text, {{"gpu_report", U8(request.output / "gpu-measurement.json")},
                {"replay_log", U8(request.output / "replay.log")}, {"cpu_fallback", false}});
        throw;
    }
}
Json ValidationSummary(const ReplayResult& result, const fs::path& dir) {
    const auto& report = result.validationReport;
    if (report.is_null()) return nullptr;
    std::map<std::string, std::pair<int, std::string>> byVuid;
    for (const auto& message : report.value("messages", Json::array())) {
        auto& entry = byVuid[message.value("vuid", "").empty() ? "(no VUID)" : message.value("vuid", "")];
        ++entry.first; entry.second = message.value("severity", "");
    }
    Json counts = Json::object();
    for (const auto& [vuid, entry] : byVuid) counts[vuid] = {{"count", entry.first}, {"severity", entry.second}};
    return {{"status", report.value("status", "")}, {"enabled", report.value("enabled", false)}, {"errors", report.value("errors", 0)},
        {"warnings", report.value("warnings", 0)}, {"by_vuid", counts}, {"truncated", report.value("truncated", false)}, {"report", U8(dir / "validation.json")},
        {"scope", "Validation covers the whole replay (state load and all replayed frames), not only the selected frame or draw."}};
}
// BMP -> PNG next to the source; failures are recorded, not fatal.
Json WithPng(const fs::path& image, std::string* pngOut = nullptr) {
    Json entry{{"file", U8(image)}};
    const auto extension = image.extension().u8string();
    if (extension == ".png") { entry["png"] = U8(image); if (pngOut) *pngOut = U8(image); return entry; }
    if (extension != ".bmp") return entry;
    auto png = image; png.replace_extension(".png");
    try {
        const auto decoded = ConvertToPng(image, png);
        entry["png"] = U8(png); entry["width"] = decoded.width; entry["height"] = decoded.height;
        if (pngOut) *pngOut = U8(png);
    } catch (const std::exception& error) { entry["png_error"] = error.what(); }
    return entry;
}
std::string Category(const ResourceArtifact& r) { return r.metadata.value("category", ""); }
Json RunJson(const ReplayRequest& request, const ReplayResult& result, const ReplayTarget& target) {
    Json run{{"run", request.prefix}, {"output_dir", U8(request.output)}, {"target", result.target}, {"serial", target.serial},
        {"replay_log", U8(request.output / "replay.log")}, {"manifest", U8(request.output / "manifest.json")}, {"timings", result.timings},
        {"cache_hit", result.timings.value("artifact_cache_hit", false)}, {"optimized_replay", result.timings.value("optimized_replay", false)}};
    run["swapchain"] = result.timings.value("swapchain", Json{{"requested", request.swapchainMode}, {"effective", nullptr}, {"source", "unavailable"}});
    run["memory_translation"] = result.timings.value("memory_translation", ReplayMemoryTranslationPolicy(request.memoryTranslation, target.android));
    if (!result.image.empty()) {
        std::string png; auto entry = WithPng(result.image, &png);
        run["image"] = U8(result.image); run["image_png"] = png; run["image_info"] = entry;
        for (const auto& resource : result.resources) if (resource.file == result.image && Category(resource) == "output") {
            run["selected_output"] = resource.metadata; break;
        }
    }
    if (request.validation) run["validation"] = ValidationSummary(result, request.output);
    if (!request.dump.is_null()) {
        Json outputs = Json::array(), inputs = Json::array(), mesh = Json::array(), other = Json::array();
        for (const auto& resource : result.resources) {
            auto entry = WithPng(resource.file);
            entry["size"] = resource.size; entry["role"] = resource.role; entry["metadata"] = resource.metadata;
            const auto category = Category(resource);
            (category == "output" ? outputs : category == "input" ? inputs : category == "mesh" ? mesh : other).push_back(entry);
        }
        run["outputs"] = outputs; run["inputs"] = inputs; run["mesh"] = mesh; run["other_resources"] = other;
    }
    return run;
}
// Pairing key for outputs of two replays of the same draw.
std::string ResourceKey(const ResourceArtifact& r) {
    return Category(r) + "|" + r.metadata.value("imageId", Json()).dump() + "|" + r.metadata.value("aspect", Json()).dump() + "|" +
        r.metadata.value("mipLevel", Json()).dump() + "|" + r.metadata.value("arrayLayer", Json()).dump() + "|" + r.file.extension().u8string();
}
Json CompareSafe(const fs::path& a, const fs::path& b, const fs::path& diff) {
    try { return CompareImages(a, b, diff); }
    catch (const ImageExtentMismatch& error) {
        return {{"extent_equal", false}, {"error", error.what()}, {"a", U8(a)}, {"b", U8(b)}, {"a_extent", {error.aw, error.ah}}, {"b_extent", {error.bw, error.bh}}};
    } catch (const std::exception& error) { return {{"error", error.what()}, {"a", U8(a)}, {"b", U8(b)}}; }
}
Json CompareRuns(const ReplayResult& baseline, const ReplayResult& modified, const fs::path& root) {
    Json comparison = CompareSafe(baseline.image, modified.image, root / "diff.png");
    Json extra = Json::array();
    std::map<std::string, const ResourceArtifact*> modifiedByKey;
    for (const auto& resource : modified.resources) if (Category(resource) == "output") modifiedByKey[ResourceKey(resource)] = &resource;
    size_t index = 0;
    for (const auto& resource : baseline.resources) {
        if (Category(resource) != "output") continue;
        const auto other = modifiedByKey.find(ResourceKey(resource));
        if (other == modifiedByKey.end() || (resource.file.extension() != ".bmp" && resource.file.extension() != ".png")) continue;
        auto item = CompareSafe(resource.file, other->second->file, root / ("diff-output-" + std::to_string(index++) + ".png"));
        item["metadata"] = resource.metadata; extra.push_back(item);
    }
    if (!extra.empty()) comparison["outputs"] = extra;
    return comparison;
}

// Shared: runs the requested replay(s) and builds the result document.
Json ExecuteReplay(const Context& context, const Args& args, const Opened& opened, const ReplayTarget& target, const Overrides& overrides,
                   const fs::path& root, ReplayRequest request, Json scope, const std::function<void(ReplayRequest&, bool)>& prepare,
                   InspectionTiming* timing = nullptr, const OutputSelection* selection = nullptr) {
    const bool validate = args.Has("validate"), baseline = args.Has("baseline");
    Json result{{"capture", {{"path", U8(opened.import.capture->source)}, {"sha256", opened.import.capture->fingerprint}, {"android", opened.import.capture->android}}},
        {"target", TargetJson(target)}, {"output_dir", U8(root)}};
    result.update(scope);
    Json notes = Json::array();
    auto launch = [&](const std::string& prefix, const std::string& directory, bool withOverrides, bool validation) {
        auto r = request;
        r.prefix = prefix; r.output = root / directory; r.validation = validation;
        if (withOverrides) r.overrides = overrides.files;
        if (fs::exists(r.output) && !fs::is_empty(r.output)) throw CliError(kFailed, "output_exists", "Output directory is not empty: " + U8(r.output), {{"output_dir", U8(r.output)}});
        prepare(r, withOverrides);
        auto replay = RunReplay(context, r, target, timing);
        if (selection) {
            replay.image = SelectOutputImage(replay.resources, *selection);
            if (replay.image.empty() && (!selection->aspect.empty() || selection->image || selection->mip || selection->layer)) {
                Json available = Json::array();
                for (const auto& asset : replay.resources) if (Category(asset) == "output") available.push_back(asset.metadata);
                throw CliError(kFailed, "output_unavailable", "Requested output was not returned by replay", {{"available_outputs", available}, {"output_dir", U8(r.output)}});
            }
        }
        return std::make_pair(r, std::move(replay));
    };
    std::optional<std::pair<ReplayRequest, ReplayResult>> base, modifiedRunResult;
    if (baseline) base = launch("baseline", "baseline", false, false);
    const bool modifiedRun = !overrides.files.empty() || baseline;
    modifiedRunResult = launch(modifiedRun ? "modified" : "baseline", baseline ? "modified" : "replay", true, validate);
    Json runs = Json::object();
    if (base) runs["baseline"] = RunJson(base->first, base->second, target);
    runs[modifiedRunResult->first.prefix] = RunJson(modifiedRunResult->first, modifiedRunResult->second, target);
    result["runs"] = runs;
    const auto& last = runs[modifiedRunResult->first.prefix];
    for (const char* key : {"image", "image_png", "selected_output", "replay_log", "manifest", "timings", "cache_hit", "swapchain", "memory_translation", "outputs", "inputs", "mesh", "validation"}) if (last.contains(key)) result[key] = last[key];
    if (base) result["comparison"] = CompareRuns(base->second, modifiedRunResult->second, root);
    if (!overrides.files.empty()) {
        result["overrides"] = overrides.report;
        for (auto& item : result["overrides"]) item["confirmed"] = true;   // ReplayCapture fails unless the backend logged each replacement
        notes.push_back("Each override was confirmed by the replay backend. A replacement affects every captured use of the module, not only the selected draw.");
    }
    if (validate) notes.push_back("Validation covers the whole replay, not only the selected frame or draw." + std::string(baseline ? " Only the modified run was validated." : ""));
    result["notes"] = notes;
    AtomicJson(root / "result.json", result);
    return result;
}
fs::path ResolveRoot(const Context& context, const Args& args, const char* label) {
    const auto root = args.Has("out") ? Path(args.Get("out")) : context.NewSession(label);
    fs::create_directories(root);
    return root;
}
std::vector<Option> ReplayOptions(std::vector<Option> specific) {
    std::vector<Option> options{
        {"capture", "FILE", "Capture (.gfxr)"},
        {"out", "DIR", "Output directory (default: a new session under <settings>/sessions)"},
        {"override", "MOD=FILE", "Replace shader module MOD with FILE (.spv, GLSL .glsl/.vert/.frag/.comp, or .spvasm); repeatable", true},
        {"allow-interface-change", "", "Allow overrides whose inputs/outputs/bindings differ from the original"},
        {"validate", "", "Run a fresh, unoptimized, uncached replay with the Khronos validation layer and write validation.json"},
        {"baseline", "", "Also replay without overrides (prefix baseline) and compare the images"},
        {"screen-off", "", "Turn the Android screen off during replay (restored afterwards)"},
        {"no-optimize", "", "Replay the original capture instead of the optimized copy"},
        {"memory-translation", "default|none|remap|realign|rebind", "Memory portability policy; default preserves the platform default", false, false, "default"},
        {"fresh", "", "Compatibility option; every CLI invocation performs a fresh replay"}};
    options.insert(options.end(), specific.begin(), specific.end());
    return options;
}
fs::path RequireCapture(const Args& args) {
    (void)ReplayMemoryTranslation(args); // Reject unsupported policy before import/device work.
    if (!args.Has("capture")) throw Usage("--capture FILE is required");
    return Path(args.Get("capture"));
}

// ---- replay frame -------------------------------------------------------------------------------------------
Json ReplayFrame(Context& context, const Args& args) {
    const auto started = std::chrono::steady_clock::now(); InspectionTiming timing;
    (void)ReplaySwapchainMode(args); // Validate before importing or launching replay.
    const auto opened = OpenReplayable(context, RequireCapture(args));
    const auto& capture = *opened.import.capture;
    const auto frame = args.Number("frame", 0);
    if (frame < 1 || frame > capture.frames) throw CliError(kUsage, "frame_out_of_range", "--frame must be 1.." + std::to_string(capture.frames), {{"frames", capture.frames}});
    const auto target = ResolveTarget(context, capture);
    const auto root = ResolveRoot(context, args, "replay");
    const auto overrides = PrepareOverrides(context, opened.import, args, root / "overrides");
    auto request = BaseRequest(context, capture, target, args, false);
    request.frame = static_cast<uint32_t>(frame);
    auto result = ExecuteReplay(context, args, opened, target, overrides, root, request, {{"scope", "frame"}, {"frame", frame}}, [](ReplayRequest&, bool) {}, &timing);
    result["operation_timings"] = timing.Report(started);
    AtomicJson(root / "result.json", result); return result;
}

// ---- replay draw --------------------------------------------------------------------------------------------
uint64_t Decimal(const std::string& value, const std::string& option) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) throw Usage(option + " expects a nonnegative decimal integer");
    try { return std::stoull(value); } catch (...) { throw Usage(option + " is out of range"); }
}
OutputSelection ReadOutputSelection(const Args& args) {
    OutputSelection result;
    if (args.Has("image") && args.Has("attachment")) throw Usage("--image and --attachment are mutually exclusive");
    if (args.Has("image")) result.image = args.Number("image", 0);
    if (args.Has("attachment")) {
        const auto value = args.Get("attachment");
        if (value == "depth") result.aspect = "DEPTH";
        else if (value == "stencil") result.aspect = "STENCIL";
        else if (value.rfind("color:", 0) == 0) { result.aspect = "COLOR"; result.location = Decimal(value.substr(6), "--attachment color:N"); }
        else throw Usage("--attachment must be color:N, depth or stencil");
    }
    if (args.Has("mip")) result.mip = args.Number("mip", 0);
    if (args.Has("layer")) result.layer = args.Number("layer", 0);
    return result;
}
void AddGeometry(Json& run, const Capture& capture, size_t index) {
    const auto& execution = capture.executions[index]; const auto& event = capture.events[execution.event];
    try {
        const auto dir = fs::u8path(run.value("output_dir", ""));
        const auto draw = ReadDrawMetadata(dir, event.block, execution.submit);
        Json parameters;
        if (draw.is_object()) parameters = draw.value("parameters", Json::object());
        else if (event.name.find("Indirect") == std::string::npos) parameters = capture.ReadEvent(execution.event)["function"].value("args", Json::object());
        else parameters = Json::object();
        if (draw.is_object()) run["draw_parameters"] = parameters;
        const auto pipeline = capture.pipelineInfo.find(event.pipeline);
        run["geometry"] = SummarizeGeometry(event.name, pipeline == capture.pipelineInfo.end() ? Json::object() : pipeline->second, parameters, draw, dir);
    } catch (const std::exception& e) { run["geometry"] = {{"status", "unavailable"}, {"reason", e.what()}, {"submitted_triangles", nullptr}, {"unique_vertices", nullptr}}; }
}
struct DrawSelector { std::optional<fs::path> capture; std::optional<uint64_t> block, execution; uint64_t occurrence = 0; std::string sha256; };
DrawSelector ReadDrawDirectory(const fs::path& directory) {
    DrawSelector selector;
    auto read = [](const fs::path& file) {
        std::ifstream in(file); if (!in) throw CliError(kFailed, "dump_unreadable", "Cannot read " + U8(file), {{"file", U8(file)}});
        try { return Json::parse(in); } catch (const std::exception& error) { throw CliError(kFailed, "dump_unreadable", U8(file) + ": " + error.what()); }
    };
    const auto drawFile = directory / "draw.json";
    if (!fs::is_regular_file(drawFile)) throw CliError(kFailed, "draw_not_found", "No draw.json in " + U8(directory), {{"draw", U8(directory)}});
    const auto draw = read(drawFile);
    if (draw.contains("block") && draw["block"].is_number_unsigned()) selector.block = draw["block"].get<uint64_t>();
    if (draw.contains("execution") && draw["execution"].is_number_unsigned()) selector.execution = draw["execution"].get<uint64_t>();
    selector.occurrence = draw.value("occurrence", uint64_t(0));
    if (!selector.block && !selector.execution) throw CliError(kFailed, "dump_unreadable", "draw.json has neither 'block' nor 'execution'", {{"file", U8(drawFile)}});
    for (auto dir = fs::absolute(directory).parent_path();; dir = dir.parent_path()) {
        if (fs::is_regular_file(dir / "manifest.json")) {
            const auto manifest = read(dir / "manifest.json");
            if (manifest.contains("capture") && manifest["capture"].is_object()) {
                auto path = fs::u8path(manifest["capture"].value("path", ""));
                if (!path.empty()) selector.capture = path.is_absolute() ? path : fs::weakly_canonical(dir / path);
                selector.sha256 = manifest["capture"].value("sha256", "");
                break;
            }
        }
        if (dir == dir.parent_path()) break;
    }
    return selector;
}
Json InspectDraw(Context& context, const Args& args, const Opened& opened, size_t index,
                 const ReplayTarget& target, const Overrides& overrides, const fs::path& root, InspectionTiming& timing,
                 std::chrono::steady_clock::time_point inspectionStarted) {
    const auto& capture = *opened.import.capture;
    const auto& execution = capture.executions[index];
    const auto& event = capture.events[execution.event];
    const auto selection = ReadOutputSelection(args);
    const bool full = args.Has("full");
    const auto visualization = args.Get("visualize", "original");
    if (visualization != "original" && visualization != "solid" && visualization != "wireframe" && visualization != "depth" && visualization != "outline" && visualization != "uv")
        throw Usage("--visualize must be original, solid, wireframe, depth, outline or uv");
    const bool subdraw = args.Has("subdraw");
    const bool isDraw = event.name.rfind("vkCmdDraw", 0) == 0;
    if ((visualization != "original" || subdraw) && !isDraw) throw CliError(kFailed, "visualization_unsupported", "Visualization and --subdraw apply to vkCmdDraw* calls, not " + event.name);
    if(subdraw&&event.name.find("Indirect")==std::string::npos)throw Usage("--subdraw requires an indirect draw call.");
    if(subdraw&&event.name.find("IndirectCount")!=std::string::npos)throw CliError(kFailed,"subdraw_unsupported","Individual GPU-counted indirect records are not supported; omit --subdraw to inspect All.");
    if (subdraw && args.Number("subdraw", 0) > 65535) throw Usage("--subdraw must be 0..65535");
    Json dump;
    try { dump = capture.DumpRequest(index); }
    catch (const std::exception& error) { throw CliError(kFailed, "draw_not_selectable", error.what(), {{"block", event.block}, {"execution", index}}); }
    const auto pipeline = event.pipeline;
    const auto stages = capture.pipelines.count(pipeline) ? capture.pipelines.at(pipeline) : std::vector<ShaderStage>{};
    if (visualization != "original") for (const auto& stage : stages)
        if (const auto limitation = capture.ModuleEditLimitation(stage.module); !limitation.empty())
            throw CliError(kFailed, "visualization_unsupported", "Draw visualization unavailable for this pipeline: " + limitation, {{"module", stage.module}});
    auto request = BaseRequest(context, capture, target, args, false);
    request.frame = execution.frame; request.dump = dump; request.outputsOnly = true;
    request.drawInspection = full; request.descriptorImages = full;
    request.allImageSubresources = selection.mip.value_or(0) != 0 || selection.layer.value_or(0) != 0;
    request.visualization = visualization=="outline"?"silhouette":visualization; request.visualizationPipeline = pipeline;
    request.indirectSubdraw = subdraw ? static_cast<int>(args.Number("subdraw", 0)) : -1;
    Json scope{{"scope", "draw"}, {"frame", execution.frame}, {"draw", {{"block", event.block}, {"execution", index}, {"call", event.name}, {"pipeline", pipeline}, {"detail", full ? "full" : "outputs"}}},
        {"visualization", visualization}};
    if (subdraw) scope["subdraw"] = request.indirectSubdraw;
    Json diagnosticSources=Json::object();
    auto result = ExecuteReplay(context, args, opened, target, overrides, root, request, scope, [&](ReplayRequest& r, bool withOverrides) {
        if(r.indirectSubdraw>=0){
            // Check the effective shaders, including replacements, before issuing one-record commands.
            for (const auto& stage : stages) {
                const auto module = withOverrides && overrides.files.count(stage.module) ? overrides.files.at(stage.module) : opened.import.directory / "shaders" / ("sh" + std::to_string(stage.module));
                if (fs::is_regular_file(module) && ShaderUsesDrawIndex(module))
                    throw CliError(kFailed, "subdraw_unsupported", "Individual indirect draws using gl_DrawID are not supported; omit --subdraw to replay all draws.", {{"module", stage.module}});
            }
            r.indirectDrawIdSafe = true;
        }
        if(visualization=="uv"){
            const auto stage=std::find_if(stages.begin(),stages.end(),[](const auto& s){return s.stage.find("FRAGMENT")!=std::string::npos;});
            if(stage==stages.end())throw CliError(kFailed,"visualization_unsupported","UV density requires a fragment stage.");
            if(!stage->specialization.is_null())throw CliError(kFailed,"visualization_unsupported","UV density is unavailable for specialized fragment interfaces.");
            const auto module=r.overrides.count(stage->module)?r.overrides.at(stage->module):opened.import.directory/"shaders"/("sh"+std::to_string(stage->module));
            const auto directory=root/"diagnostic-sources"/r.prefix;fs::create_directories(directory);
            const auto source=directory/"uv.frag";std::string detail,text;
            try{text=UvCheckerboardSource(module,stage->entry,detail);}
            catch(const std::exception& error){throw CliError(kFailed,"visualization_unsupported",error.what(),{{"mode","uv"},{"module",stage->module}});}
            WriteFile(source,text);r.uvShader=directory/"uv.spv";
            CompileShader(source,{0,"VK_SHADER_STAGE_FRAGMENT_BIT","main"},r.uvShader,context.tools,context.cancel);
            diagnosticSources[r.prefix]={{"uv",{{"detail",detail},{"module",stage->module},{"source",U8(source)},{"binary",U8(r.uvShader)}}}};
        }
        if(visualization=="outline"){
            auto contextRequest=r;contextRequest.visualization="original";contextRequest.drawInspection=false;contextRequest.descriptorImages=false;
            contextRequest.output=root/"diagnostic-sources"/r.prefix/"original";
            context.Progress("Preparing original color for the outline");
            const auto original=RunReplay(context,contextRequest,target,&timing);
            auto color=[](const ReplayResult& run){
                for(const auto& asset:run.resources)if(asset.metadata.value("category","")=="output"&&asset.metadata.value("aspect","")=="COLOR"&&asset.metadata.value("location",0u)==0&&asset.file.extension()==".bmp")return asset.file;
                throw CliError(kFailed,"visualization_unsupported","Orange outline requires a readable COLOR 0 attachment.");
            };
            r.outlineBaseImage=color(original);
            Json sources;sources["original"]={{"image",U8(r.outlineBaseImage)},{"output_dir",U8(contextRequest.output)},{"manifest",U8(contextRequest.output/"manifest.json")},{"timings",original.timings}};
            if(contextRequest.validation)sources["original"]["validation"]=ValidationSummary(original,contextRequest.output);
            contextRequest.visualization="solid";contextRequest.output=root/"diagnostic-sources"/r.prefix/"solid";
            context.Progress("Preparing solid coverage for the outline");
            const auto coverage=RunReplay(context,contextRequest,target,&timing);r.outlineCoverageImage=color(coverage);
            sources["solid"]={{"image",U8(r.outlineCoverageImage)},{"output_dir",U8(contextRequest.output)},{"manifest",U8(contextRequest.output/"manifest.json")},{"timings",coverage.timings}};
            if(contextRequest.validation)sources["solid"]["validation"]=ValidationSummary(coverage,contextRequest.output);
            diagnosticSources[r.prefix]=sources;
        }
    }, &timing, &selection);
    if(!diagnosticSources.empty())result["diagnostic_sources"]=diagnosticSources;
    if(visualization=="outline")result["notes"].push_back("Outline automatically replays original color and solid coverage before compositing its boundary. Auxiliary manifests and timings are in diagnostic_sources.");
    for (auto& [name, run] : result["runs"].items()) AddGeometry(run, capture, index);
    const auto last = result["runs"].contains("modified") ? "modified" : "baseline";
    result["geometry"] = result["runs"][last]["geometry"];
    result["notes"].push_back("Replay-to-draw replays the whole frame with resource readback at the selected draw; it does not truncate the command buffer.");
    result["operation_timings"] = timing.Report(inspectionStarted);
    result["inspection_seconds"] = result["operation_timings"]["total_seconds"];
    AtomicJson(root / "result.json", result);
    return result;
}

Json ReplayDraw(Context& context, const Args& args) {
    const auto inspectionStarted=std::chrono::steady_clock::now();
    ReadOutputSelection(args);
    const int selectors = int(args.Has("block")) + int(args.Has("draw")) + int(args.Has("execution"));
    if (selectors != 1) throw Usage("Give exactly one of --block B, --draw <dump draw dir>, --execution I");
    if (args.Has("outputs") && args.Has("full")) throw Usage("--outputs and --full are mutually exclusive");
    DrawSelector selector;
    if (args.Has("draw")) selector = ReadDrawDirectory(Path(args.Get("draw")));
    else if (args.Has("block")) { selector.block = args.Number("block", 0); selector.occurrence = args.Number("occurrence", 0); }
    else selector.execution = args.Number("execution", 0);
    fs::path capturePath;
    if (args.Has("capture")) capturePath = Path(args.Get("capture"));
    else if (selector.capture) capturePath = *selector.capture;
    else throw Usage("--capture FILE is required (the dump manifest does not name one)");
    const auto opened = OpenReplayable(context, capturePath);
    const auto& capture = *opened.import.capture;
    if (!selector.sha256.empty() && selector.sha256 != capture.fingerprint)
        throw CliError(kFailed, "capture_mismatch", "The capture differs from the one the dump was made from", {{"dump_sha256", selector.sha256}, {"capture_sha256", capture.fingerprint}});
    size_t index;
    if (selector.execution && *selector.execution < capture.executions.size() &&
        (!selector.block || capture.events[capture.executions[*selector.execution].event].block == *selector.block)) index = static_cast<size_t>(*selector.execution);
    else if (selector.block) index = FindExecution(capture, *selector.block, static_cast<size_t>(selector.occurrence));
    else throw CliError(kFailed, "draw_not_found", "Execution index " + std::to_string(*selector.execution) + " is out of range (" + std::to_string(capture.executions.size()) + " executions)");
    const auto target = ResolveTarget(context, capture);
    const auto root = ResolveRoot(context, args, "replay");
    const auto overrides = PrepareOverrides(context, opened.import, args, root / "overrides");
    InspectionTiming timing;
    return InspectDraw(context, args, opened, index, target, overrides, root, timing, inspectionStarted);
}

std::vector<size_t> ExecutionList(const std::string& text, size_t count) {
    std::vector<size_t> result; std::set<size_t> seen;
    std::istringstream stream(text); std::string token;
    if (text.empty() || text.back() == ',') throw Usage("--executions expects indexes or inclusive ranges, e.g. 0,2-5");
    while (std::getline(stream, token, ',')) {
        const auto dash = token.find('-');
        const auto first = Decimal(token.substr(0, dash), "--executions");
        const auto last = dash == std::string::npos ? first : Decimal(token.substr(dash + 1), "--executions");
        if (last < first || last >= count) throw Usage("--executions range is outside the capture or reversed");
        if (last - first >= 4096 || result.size() + (last - first + 1) > 4096) throw Usage("At most 4096 executions can be inspected per command");
        for (auto n = first; n <= last; ++n) {
            if (!seen.insert(static_cast<size_t>(n)).second) throw Usage("--executions contains a duplicate index");
            result.push_back(static_cast<size_t>(n));
        }
    }
    return result;
}
Json ReplayDraws(Context& context, const Args& args) {
    const auto started = std::chrono::steady_clock::now();
    const auto selection = ReadOutputSelection(args);
    if (args.Has("outputs") && args.Has("full")) throw Usage("--outputs and --full are mutually exclusive");
    const auto limit = args.Number("batch-draws", 32);
    if (!limit || limit > 256) throw Usage("--batch-draws must be 1..256");
    const auto opened = OpenReplayable(context, RequireCapture(args)); const auto& capture = *opened.import.capture;
    const auto executions = ExecutionList(args.Get("executions"), capture.executions.size());
    // Validate every selector before launching any replay, including repeated secondary invocations.
    Json rejected; BuildBatchDumpRequest(capture, executions, rejected);
    if (!rejected.empty()) throw CliError(kFailed, "draw_not_selectable", "Some batch executions cannot be selected unambiguously", {{"rejected", rejected}});
    const auto mode = args.Get("visualize", "original");
    if (mode != "original" && mode != "solid" && mode != "wireframe" && mode != "depth" && mode != "outline" && mode != "uv")
        throw Usage("--visualize must be original, solid, wireframe, depth, outline or uv");
    const bool isolated = mode != "original" || args.Has("subdraw") || args.Has("baseline");
    if (!isolated) {
        std::set<std::pair<uint64_t, uint64_t>> identities;
        for (const auto index : executions) {
            const auto& e = capture.executions[index]; const auto block = capture.events[e.event].block;
            if (!identities.insert({e.submit, block}).second)
                throw CliError(kFailed, "draw_not_selectable", "Batch artifact metadata cannot distinguish these executions sharing a submit and call block; inspect them separately", {{"execution", index}, {"submit", e.submit}, {"block", block}});
        }
    }
    const auto target = ResolveTarget(context, capture);
    const auto root = args.Has("out") ? Path(args.Get("out")) : context.NewSession("inspect-batch");
    if (fs::exists(root) && !fs::is_empty(root)) throw CliError(kFailed, "output_exists", "Output directory is not empty: " + U8(root));
    fs::create_directories(root);
    const auto overrides = PrepareOverrides(context, opened.import, args, root / "overrides");
    InspectionTiming timing;
    Json result{{"scope", "draws"}, {"strategy", isolated ? "isolated" : "merged"}, {"visualization", mode}, {"output_dir", U8(root)},
        {"capture", {{"path", U8(capture.source)}, {"sha256", capture.fingerprint}}}, {"target", TargetJson(target)},
        {"draws", Json::array()}, {"batches", Json::array()}};
    std::map<size_t, size_t> positions;
    for (const auto index : executions) {
        positions[index] = result["draws"].size();
        const auto& e = capture.executions[index]; const auto& event = capture.events[e.event];
        result["draws"].push_back({{"execution", index}, {"block", event.block}, {"frame", e.frame}, {"status", "pending"}});
    }
    auto save = [&] {
        result["operation_timings"] = timing.Report(started);
        size_t ok = 0, failed = 0;
        for (const auto& row : result["draws"]) { ok += row.value("status", "") == "ok"; failed += row.value("status", "") == "failed"; }
        result["summary"] = {{"requested", executions.size()}, {"succeeded", ok}, {"failed", failed}, {"pending", executions.size() - ok - failed}};
        AtomicJson(root / "result.json", result);
    };
    auto errorOrStop = [&]() -> Json {
        const auto error = ErrorJson(std::current_exception());
        if (error.first != kFailed) {
            save(); auto detail = error.second.value("detail", Json::object()); detail["batch_result"] = U8(root / "result.json");
            throw CliError(error.first, error.second.value("code", "failed"), error.second.value("message", "Batch interrupted"), detail);
        }
        return error.second;
    };
    save();
    if (isolated) {
        result["notes"] = {"Diagnostic modes, subdraws and baseline comparison use isolated draws to preserve per-draw pipeline and outline context. Import and override compilation are shared."};
        for (const auto index : executions) {
            auto& row = result["draws"][positions.at(index)]; InspectionTiming one;
            const auto drawStarted = std::chrono::steady_clock::now();
            try {
                row["result"] = InspectDraw(context, args, opened, index, target, overrides, root / ("execution-" + std::to_string(index)), one, drawStarted);
                row["status"] = "ok";
            } catch (const std::exception&) {
                if (context.cancel && context.cancel->load()) throw;
                row["status"] = "failed"; row["error"] = errorOrStop();
            }
            timing.replaySeconds += one.replaySeconds; timing.replayCalls += one.replayCalls;
            save();
        }
    } else {
        std::map<uint32_t, std::vector<size_t>> byFrame;
        for (const auto index : executions) byFrame[capture.executions[index].frame].push_back(index);
        size_t batchId = 0;
        for (const auto& [frame, list] : byFrame) for (size_t offset = 0; offset < list.size(); offset += static_cast<size_t>(limit)) {
            const std::vector<size_t> batch(list.begin() + offset, list.begin() + std::min(list.size(), offset + static_cast<size_t>(limit)));
            auto request = BaseRequest(context, capture, target, args, args.Has("validate"));
            request.frame = frame; request.output = root / ("batch-" + std::to_string(batchId++)); request.prefix = overrides.files.empty() ? "baseline" : "modified";
            request.dump = BuildBatchDumpRequest(capture, batch, rejected); request.outputsOnly = true;
            request.drawInspection = args.Has("full"); request.descriptorImages = args.Has("full"); request.overrides = overrides.files;
            request.allImageSubresources = selection.mip.value_or(0) != 0 || selection.layer.value_or(0) != 0;
            Json record{{"executions", batch}, {"frame", frame}, {"output_dir", U8(request.output)}};
            try {
                const auto replay = RunReplay(context, request, target, &timing);
                record["status"] = "ok"; record["manifest"] = U8(request.output / "manifest.json"); record["timings"] = replay.timings;
                for (const auto index : batch) {
                    auto& row = result["draws"][positions.at(index)]; const auto& e = capture.executions[index]; const auto& event = capture.events[e.event];
                    try {
                        ReplayResult one = replay; one.resources.clear();
                        for (const auto& asset : replay.resources) {
                            const auto& m = asset.metadata;
                            if (m.is_object() && m.value("queueSubmitIndex", uint64_t(0)) == e.submit &&
                                (m.value("drawIndex", uint64_t(0)) == event.block || m.value("dispatchIndex", uint64_t(0)) == event.block)) one.resources.push_back(asset);
                        }
                        one.image = SelectOutputImage(one.resources, selection);
                        if (one.image.empty() && (!selection.aspect.empty() || selection.image || selection.mip || selection.layer)) {
                            Json available = Json::array(); for (const auto& asset : one.resources) if (Category(asset) == "output") available.push_back(asset.metadata);
                            throw CliError(kFailed, "output_unavailable", "Requested output was not returned for this execution", {{"available_outputs", available}});
                        }
                        auto run = RunJson(request, one, target); AddGeometry(run, capture, index);
                        if (!overrides.files.empty()) { run["overrides"] = overrides.report; for (auto& item : run["overrides"]) item["confirmed"] = true; }
                        run["timing_scope"] = "Shared batch replay; timings are not attributable to this draw alone.";
                        row["result"] = run; row["status"] = "ok";
                    } catch (const std::exception&) { if (context.cancel && context.cancel->load()) throw; row["status"] = "failed"; row["error"] = errorOrStop(); }
                }
            } catch (const std::exception&) {
                if (context.cancel && context.cancel->load()) throw;
                record["status"] = "failed"; record["error"] = errorOrStop();
                for (const auto index : batch) { auto& row = result["draws"][positions.at(index)]; row["status"] = "failed"; row["error"] = record["error"]; }
            }
            result["batches"].push_back(record); save();
        }
    }
    save();
    if (result["summary"]["failed"] != 0) throw CliError(kFailed, "batch_incomplete", "Some executions failed; completed results were preserved", {{"result", U8(root / "result.json")}, {"summary", result["summary"]}, {"operation_timings", result["operation_timings"]}});
    return result;
}

// ---- compare ------------------------------------------------------------------------------------------------
Json Compare(Context&, const Args& args) {
    const auto a = Path(args.positional.at(0)), b = Path(args.positional.at(1));
    for (const auto& file : {a, b}) if (!fs::is_regular_file(file)) throw CliError(kFailed, "image_not_found", "Image does not exist: " + U8(file), {{"file", U8(file)}});
    fs::path diff;
    if (args.Has("out")) { diff = Path(args.Get("out")); if (diff.has_parent_path()) fs::create_directories(diff.parent_path()); }
    try { return CompareImages(a, b, diff); }
    catch (const ImageExtentMismatch& error) {
        throw CliError(kFailed, "extent_mismatch", error.what(), {{"extent_equal", false}, {"a_extent", {error.aw, error.ah}}, {"b_extent", {error.bw, error.bh}}, {"a", U8(a)}, {"b", U8(b)}});
    } catch (const std::exception& error) { throw CliError(kFailed, "image_unreadable", error.what()); }
}

// ---- shader -------------------------------------------------------------------------------------------------
Json ShaderShow(Context& context, const Args& args) {
    const auto module = ParseModule(args.positional.at(0));
    const auto format = args.Get("format", "glsl");
    if (format != "glsl" && format != "spvasm" && format != "reflection" && format != "all") throw Usage("--format must be glsl, spvasm, reflection or all");
    const auto import = OpenCapture(context, RequireCapture(args));
    const auto info = FindModule(import, module);
    const auto decoded = DecodeShader(info.binary, info.stage, context.tools, context.cancel);
    Json result{{"module", module}, {"stage", decoded.stage.stage}, {"entry", decoded.stage.entry}, {"binary", U8(info.binary)}, {"format", format},
        {"sha256", decoded.metadata.value("binary_sha256", "")}, {"pipelines_using_module", info.pipelines.size()}, {"specialization", info.stage.specialization}};
    const auto limitation = import.capture->ModuleEditLimitation(module);
    result["editable"] = limitation.empty(); if (!limitation.empty()) result["edit_limitation"] = limitation;
    if (format == "glsl" || format == "all") { result["glsl"] = decoded.glsl; if (!decoded.glslError.empty()) result["glsl_error"] = decoded.glslError; }
    if (format == "spvasm" || format == "all") { result["spvasm"] = decoded.assembly; if (!decoded.assemblyError.empty()) result["spvasm_error"] = decoded.assemblyError; }
    if (format == "reflection" || format == "all") { result["reflection"] = decoded.reflection; result["resources"] = decoded.resources; result["metadata"] = decoded.metadata; }
    if (!decoded.capturedSource.empty()) result["captured_source_available"] = true;
    if (format == "all" && !decoded.capturedSource.empty()) result["captured_source"] = decoded.capturedSource;
    return result;
}
Json ShaderExport(Context& context, const Args& args) {
    const auto module = ParseModule(args.positional.at(0));
    const auto import = OpenCapture(context, RequireCapture(args));
    const auto info = FindModule(import, module);
    const auto directory = Path(args.Get("out"));
    fs::create_directories(directory);
    const auto decoded = DecodeShader(info.binary, info.stage, context.tools, context.cancel);
    Json files = Json::object(), problems = Json::object();
    auto put = [&](const char* name, const std::string& text) { WriteFile(directory / name, text); files[name] = U8(directory / name); };
    fs::copy_file(info.binary, directory / "module.spv", fs::copy_options::overwrite_existing); files["module.spv"] = U8(directory / "module.spv");
    if (!decoded.glsl.empty()) put("module.glsl", decoded.glsl); else problems["module.glsl"] = decoded.glslError.empty() ? "unavailable" : decoded.glslError;
    if (!decoded.assembly.empty()) put("module.spvasm", decoded.assembly); else problems["module.spvasm"] = decoded.assemblyError.empty() ? "unavailable" : decoded.assemblyError;
    put("reflection.json", Json{{"module", module}, {"text", decoded.reflection}, {"resources", decoded.resources}, {"metadata", decoded.metadata}}.dump(2));
    const auto iface = SafeInterface(info.binary, info.stage.entry);
    put("interface.json", iface.dump(2));
    put("interface.txt", decoded.reflection);
    Json stage{{"module", module}, {"stage", decoded.stage.stage}, {"entry", decoded.stage.entry}, {"specialization", info.stage.specialization}, {"pipelines", info.pipelines},
        {"sha256", decoded.metadata.value("binary_sha256", "")}};
    const auto limitation = import.capture->ModuleEditLimitation(module);
    stage["editable"] = limitation.empty(); if (!limitation.empty()) stage["edit_limitation"] = limitation;
    put("stage.json", stage.dump(2));
    Json result{{"module", module}, {"directory", U8(directory)}, {"stage", decoded.stage.stage}, {"entry", decoded.stage.entry}, {"files", files}, {"editable", limitation.empty()},
        {"next", "Edit module.glsl or module.spvasm, then: replay draw|frame --override " + std::to_string(module) + "=<file>"}};
    if (!problems.empty()) result["unavailable"] = problems;
    if (iface.contains("unavailable")) result["interface_error"] = iface["unavailable"];
    return result;
}
Json ShaderCompile(Context& context, const Args& args) {
    const auto source = Path(args.positional.at(0));
    const auto output = Path(args.Get("output"));
    const auto extension = source.extension().u8string();
    if (extension != ".spv" && extension != ".spvasm" && !args.Has("stage")) throw Usage("--stage vert|frag|comp is required to compile GLSL");
    ShaderStage stage; stage.stage = args.Has("stage") ? StageName(args.Get("stage")) : ""; stage.entry = args.Get("entry", "main");
    if (output.has_parent_path()) fs::create_directories(output.parent_path());
    CompileOrThrow(context, source, stage, output);
    return {{"source", U8(source)}, {"output", U8(output)}, {"sha256", Sha256File(output, context.cancel)}, {"bytes", fs::file_size(output)},
        {"stage", stage.stage}, {"entry", stage.entry}, {"validated", true}};
}
Json ShaderCheck(Context& context, const Args& args) {
    const auto module = ParseModule(args.positional.at(0));
    const auto candidate = Path(args.positional.at(1));
    const auto import = OpenCapture(context, RequireCapture(args));
    const auto info = FindModule(import, module);
    auto compiled = candidate;
    if (candidate.extension() != ".spv") {
        const auto staging = context.NewSession("shader-check");
        compiled = staging / ("sh" + std::to_string(module) + ".spv");
        CompileOrThrow(context, candidate, info.stage, compiled);
    } else if (!fs::is_regular_file(candidate)) throw CliError(kFailed, "file_not_found", "Shader file does not exist: " + U8(candidate));
    Json result{{"module", module}, {"candidate", U8(candidate)}, {"stage", info.stage.stage}, {"entry", info.stage.entry}};
    if (args.Has("allow-interface-change")) { result["interface_check"] = "skipped (--allow-interface-change)"; result["compatible"] = nullptr; return result; }
    CheckInterfaceOrThrow(module, info.binary, compiled, info.stage.entry);
    result["compatible"] = true; result["sha256"] = Sha256File(compiled, context.cancel);
    return result;
}

// ---- perf ---------------------------------------------------------------------------------------------------
Json Stats(std::vector<double> values) {
    if (values.empty()) return nullptr;
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    const double median = n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
    double sum = 0; for (double v : values) sum += v;
    return {{"min", values.front()}, {"median", median}, {"max", values.back()}, {"mean", sum / n}, {"runs", n}};
}
double Median(const Json& stats, const char* key) { return stats.is_null() || !stats.contains(key) ? NAN : stats[key].get<double>(); }
Json ParseRange(const std::string& text, uint32_t frames, std::string& upstream) {
    uint64_t first = 1, last = frames;
    static const std::regex single("[0-9]+"), range("([0-9]+)-([0-9]+)");
    std::smatch match;
    if (text.empty()) {}
    else if (std::regex_match(text, single)) first = last = std::stoull(text);
    else if (std::regex_match(text, match, range)) { first = std::stoull(match[1]); last = std::stoull(match[2]); }
    else throw Usage("--frames expects N or A-B (inclusive), got '" + text + "'");
    if (first < 1 || last < first || last > frames) throw CliError(kUsage, "frame_out_of_range", "--frames must be within 1.." + std::to_string(frames) + " (inclusive range)", {{"frames", frames}});
    upstream = std::to_string(first) + "-" + std::to_string(last + 1);   // upstream's end frame is exclusive
    return {{"first", first}, {"last", last}, {"upstream_range", upstream}};
}
double LoadSeconds(const std::string& log) {
    static const std::regex load("Load time:\\s+([0-9.eE+-]+) seconds");
    std::smatch match;
    return std::regex_search(log, match, load) ? std::stod(match[1]) : NAN;
}
Json PerfRun(const Context& context, const ReplayRequest& base, const ReplayTarget& target, const fs::path& dir, const std::string& variant, size_t index) {
    auto request = base;
    request.output = dir; request.prefix = variant == "baseline" ? "baseline" : "modified";
    const auto result = RunReplay(context, request, target);
    Json run{{"variant", variant}, {"index", index}, {"output_dir", U8(dir)}, {"replay_log", U8(dir / "replay.log")}, {"measurement_file", U8(dir / "measurement.json")},
        {"cache_hit", result.timings.value("artifact_cache_hit", false)}, {"capture_upload_reused", result.timings.value("capture_upload_reused", false)},
        {"pipeline_cache_loaded", result.timings.value("pipeline_cache_loaded", false)}, {"screen_off", result.timings.value("screen_off", false)},
        {"optimized_replay", result.timings.value("optimized_replay", false)}, {"timings", result.timings},
        {"swapchain", result.timings.at("swapchain")}, {"memory_translation", result.timings.at("memory_translation")}};
    const double load = LoadSeconds(result.log);
    run["load_seconds"] = std::isnan(load) ? Json(nullptr) : Json(load);
    const auto range = result.measurement.value("frame_range", Json::object());
    const double duration = range.value("duration", 0.0);
    const uint64_t frames = range.value("frame_count", uint64_t(0));
    run["frames_measured"] = frames; run["measured_seconds"] = duration;
    run["fps"] = range.value("fps", 0.0);
    run["frame_time_ms"] = frames && duration > 0 ? Json(duration / frames * 1000.0) : Json(nullptr);
    if (range.contains("frame_durations") && range["frame_durations"].is_array() && !range["frame_durations"].empty()) {
        std::vector<double> ms; for (const auto& d : range["frame_durations"]) ms.push_back(d.get<double>() / 1e6);   // upstream stores nanoseconds
        const auto s = Stats(ms);
        run["per_frame_ms"] = {{"min", s["min"]}, {"median", s["median"]}, {"max", s["max"]}};
    }
    run["manifest"] = U8(dir / "manifest.json");
    if (base.measurement.gpu) {
        run["gpu_measurement_file"] = U8(dir / "gpu-measurement.json");
        run["gpu"] = result.gpuMeasurement;
    }
    return run;
}

// Compare corresponding frame samples only. The interval may include waits and
// CPU submission gaps between endpoints; it never uses the upstream replay timer.
Json GpuFrameDeltas(const Json& baseline, const Json& modified) {
    const auto& b = baseline.at("gpu"); const auto& m = modified.at("gpu");
    std::string incompatibility;
    if (b.at("backend_sha256") != m.at("backend_sha256") || b.at("protocol") != m.at("protocol")) incompatibility = "Replay backend or GPU protocol differs";
    else if (b.at("capture_sha256") != m.at("capture_sha256")) incompatibility = "Replayed capture bytes differ";
    else if (b.at("device") != m.at("device")) incompatibility = "Replay device or driver differs";
    else if (b.at("swapchain") != m.at("swapchain")) incompatibility = "Requested or effective swapchain modes differ";
    else if (b.value("memory_translation", Json{}) != m.value("memory_translation", Json{})) incompatibility = "Memory translation policy differs";
    else if (b.at("mode") != m.at("mode")) incompatibility = "GPU measurement modes differ";
    else if (b.at("provenance").at("cache_policy") != m.at("provenance").at("cache_policy")) incompatibility = "Explicit cache policies differ";
    else if (baseline.at("pipeline_cache_loaded") != modified.at("pipeline_cache_loaded")) incompatibility = "Pipeline cache load state differs";
    Json deltas = Json::array();
    for (const auto& before : b.at("samples")) {
        const auto frame = before.at("frame");
        const auto after = std::find_if(m.at("samples").begin(), m.at("samples").end(), [&](const Json& sample) { return sample.at("frame") == frame; });
        Json row{{"frame", frame}, {"index", baseline.at("index")}, {"status", "incomparable"}};
        std::string reason = incompatibility;
        if (after == m.at("samples").end()) reason = "Modified frame sample is missing";
        else if (!before.value("valid", false) || !after->value("valid", false)) reason = "A GPU sample is invalid";
        else {
            for (const char* key : {"queue_id", "first_submit", "last_submit", "submit_blocks", "start_stage", "end_stage", "clock_domain", "execution_mode", "timestamp_period_ns", "timestamp_valid_bits", "completion"})
                if (before.at(key) != after->at(key)) { reason = "GPU sample scopes differ"; break; }
        }
        if (reason.empty()) {
            const double previous = before.at("elapsed_ms").get<double>(), current = after->at("elapsed_ms").get<double>();
            row["status"] = "comparable"; row["baseline_ms"] = previous; row["modified_ms"] = current;
            row["delta_ms"] = current - previous;
            row["delta_percent"] = previous > 0 ? Json((current - previous) / previous * 100.0) : Json(nullptr);
        } else row["reason"] = reason;
        deltas.push_back(std::move(row));
    }
    return deltas;
}

Json Perf(Context& context, const Args& args) {
    auto counterProfile = args.Get("counter-profile", args.Has("counters") ? "all" : "none");
    if(args.Has("counters") && counterProfile=="none")throw Usage("--counters conflicts with --counter-profile none");
    if (counterProfile != "none" && counterProfile != "all" && counterProfile != "precision" && counterProfile != "bandwidth")
        throw Usage("--counter-profile expects none or all (precision/bandwidth are compatibility aliases for all)");
    if(counterProfile=="precision" || counterProfile=="bandwidth")counterProfile="all";
    const bool counters = counterProfile != "none";
    if (!counters && args.Has("counter-period-us")) throw Usage("--counter-period-us requires an active --counter-profile");
    if (counters && (args.Get("frames").empty() || !std::regex_match(args.Get("frames"), std::regex("[0-9]+"))))
        throw Usage("Counter sampling requires --frames N for one selected frame");
    const auto periodUs = args.Number("counter-period-us", 1000);
    if (counters && (!periodUs || periodUs > 1000000)) throw Usage("--counter-period-us must be 1..1000000 (and supported by the device)");
    const auto gpuMode = args.Get("gpu-timing", args.Has("counter-profile") || args.Has("counters") ? "isolated-frame" : "");
    const bool gpu = args.Has("gpu-timing") || args.Has("counter-profile") || args.Has("counters");
    if (gpu && gpuMode != "isolated-frame") throw Usage("--gpu-timing expects isolated-frame");
    const auto swapchain = args.Get("swapchain", counters ? "offscreen" : "virtual");
    if (swapchain != "virtual" && swapchain != "captured" && swapchain != "offscreen")
        throw Usage("--swapchain expects virtual, captured or offscreen");
    const auto runsWanted = args.Number("runs", gpu ? 1 : 3);
    if (runsWanted < 1 || runsWanted > 50) throw Usage("--runs must be 1..50");
    const auto opened = OpenReplayable(context, RequireCapture(args));
    const auto& capture = *opened.import.capture;
    std::string upstream;
    const auto range = ParseRange(args.Get("frames"), capture.frames, upstream);
    const auto target = ResolveTarget(context, capture);
    const auto root = ResolveRoot(context, args, "perf");
    if (counters && (fs::is_symlink(fs::symlink_status(root)) || !fs::is_empty(root)))
        throw CliError(kFailed, "perf_output_not_fresh", "Use a new performance output directory; existing evidence will not be overwritten", {{"output_dir", U8(root)}});
    const auto overrides = PrepareOverrides(context, opened.import, args, root / "overrides");
    if (counters) {
        PerfSessionRequest session;
        session.capture = opened.import.capture; session.target = target; session.tools = context.tools;
        session.output = root; session.cacheDirectory = context.ReplayCache(); session.frame = range["first"].get<uint32_t>();
        session.runs = static_cast<uint32_t>(runsWanted); session.overrides = overrides.files;
        session.counterPeriodNs = periodUs * 1000; session.swapchainMode = swapchain;
        session.counterProfile = counterProfile;
        session.memoryTranslation = ReplayMemoryTranslation(args);
        session.screenOff = args.Has("screen-off"); session.timeout = ReplayTimeout(context, target, false);
        session.progress = [&context](const std::string& text) { context.Progress(text); };
        session.sessionId = Sha256Text(U8(root) + ":" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        try {
            auto result = RunPerfSession(session, context.cancel);
            result["overrides"] = overrides.report;
            AtomicJson(root / "result.json", result);
            return result;
        } catch (const DeviceBusy&) { throw; }
        catch (const std::exception& error) {
            Json detail{{"output_dir", U8(root)}, {"counter_requested", true}, {"gpu_timing_required", true}, {"cpu_fallback", false}};
            try {
                std::ifstream receiptFile(root / "failure.json"), ownerFile(root / ".perf-session-owner" / "session.json");
                const auto receipt = Json::parse(receiptFile), owner = Json::parse(ownerFile);
                if (receipt.value("session_id", std::string{}) == session.sessionId && owner.value("session_id", std::string{}) == session.sessionId)
                    detail["failure_report"] = U8(root / "failure.json");
            } catch (...) {}
            const auto classified = ErrorJson(std::current_exception());
            const auto causeDetail = classified.second.value("detail", Json::object());
            if (causeDetail.is_object()) detail.update(causeDetail);
            throw CliError(classified.first, classified.first == kFailed ? "perf_session_failed" :
                classified.second.at("code").get<std::string>(), error.what(), detail);
        }
    }
    auto request = BaseRequest(context, capture, target, args, false);
    request.swapchainMode = swapchain;
    request.frame = range["first"].get<uint32_t>();
    request.measurement = {true, upstream, true};
    if (gpu) {
        request.measurement.gpu = true;
        request.optimize = false;
        try { request.measurement.gpuPlan = BuildGpuMeasurementPlan(capture, range["first"].get<uint32_t>(), range["last"].get<uint32_t>(), context.cancel); }
        catch (const std::exception& error) { throw CliError(kFailed, "gpu_profiling_unsupported", error.what(), {{"cpu_fallback", false}}); }
    }
    request.reuseArtifacts = false;   // result caches are never used for measurements (pipeline caches are)
    const bool compare = !overrides.files.empty();
    std::vector<Json> runs; std::vector<double> loads[2], times[2], fps[2];
    for (size_t i = 0; i < runsWanted; ++i) {
        for (int variant = 0; variant < (compare ? 2 : 1); ++variant) {
            const bool modified = compare && variant == 1;
            auto r = request;
            if (modified) r.overrides = overrides.files;
            const std::string name = compare ? (modified ? "modified" : "baseline") : "baseline";
            auto run = PerfRun(context, r, target, root / "runs" / (name + "-" + std::to_string(i + 1)), name, i + 1);
            const int slot = modified ? 1 : 0;
            if (run["load_seconds"].is_number()) loads[slot].push_back(run["load_seconds"].get<double>());
            if (run["frame_time_ms"].is_number()) times[slot].push_back(run["frame_time_ms"].get<double>());
            fps[slot].push_back(run["fps"].get<double>());
            runs.push_back(run);
        }
    }
    auto summary = [&](int slot) { return Json{{"load_seconds", Stats(loads[slot])}, {"frame_time_ms", Stats(times[slot])}, {"fps", Stats(fps[slot])}}; };
    Json result{{"capture", {{"path", U8(capture.source)}, {"sha256", capture.fingerprint}}}, {"target", TargetJson(target)}, {"output_dir", U8(root)},
        {"frames", range}, {"runs_requested", runsWanted}, {"runs", runs}, {"result_cache_used", false},
        {"summary", compare ? Json{{"baseline", summary(0)}, {"modified", summary(1)}} : summary(0)},
        {"screen_off", request.screenOff}, {"swapchain_requested", request.swapchainMode}, {"memory_translation", ReplayMemoryTranslationPolicy(request.memoryTranslation, target.android)},
        {"notes", Json::array({"Fresh replays: result caches are bypassed. Pipeline caches may be loaded; see pipeline_cache_loaded per run. Driver and hardware cache state is uncontrolled.",
            "frame_time_ms is measured_seconds / frames in the measured range; load_seconds is the upstream 'Load time' (state load). These fields measure replay timing, not GPU execution."})}};
    if (compare) {
        result["overrides"] = overrides.report;
        const double b = Median(summary(0)["frame_time_ms"], "median"), m = Median(summary(1)["frame_time_ms"], "median");
        Json delta = Json::object();
        bool swapchainsMatch = true;
        for (size_t i = 0; i + 1 < runs.size(); i += 2)
            swapchainsMatch = swapchainsMatch && runs[i].at("swapchain") == runs[i + 1].at("swapchain") &&
                runs[i].at("memory_translation") == runs[i + 1].at("memory_translation");
        if (!swapchainsMatch) delta = {{"status", "incomparable"}, {"reason", "Requested/effective swapchain or memory translation policies differ"}};
        else if (!std::isnan(b) && !std::isnan(m)) { delta["baseline_median_ms"] = b; delta["modified_median_ms"] = m; delta["delta_ms"] = m - b; delta["delta_percent"] = b > 0 ? Json((m - b) / b * 100.0) : Json(nullptr); }
        result["frame_time_delta"] = delta;
    }
    if (gpu) {
        result["gpu_timing"] = {{"mode", gpuMode}, {"sampling", "fresh processes; no automatic warm-up or frame looping"},
            {"scope", "Isolated full-frame GPU elapsed interval on one command queue; may include waits and CPU submission gaps between endpoints; waits gating the start marker are excluded"},
            {"natural_frame_latency", false}, {"original_capture_replayed", true}};
        if (compare) {
            Json deltas = Json::array();
            for (size_t i = 0; i + 1 < runs.size(); i += 2)
                for (auto& row : GpuFrameDeltas(runs[i], runs[i + 1])) deltas.push_back(std::move(row));
            result["gpu_frame_deltas"] = std::move(deltas);
        }
        result["notes"].push_back("GPU samples use device timestamps and are reported per frame. Isolation changes adjacent-frame overlap; these are not pure GPU busy time or natural game frame latency. CPU fields retain replay-elapsed meaning.");
        result["notes"].push_back("Use --runs 1 and a new output directory for each manual sample pair. No sample is discarded and no steady-state or significance claim is made.");
    }
    AtomicJson(root / "result.json", result);
    return result;
}
} // namespace

void RegisterReplayCommands() {
    const std::vector<Option> inspectionOptions{
        {"outputs", "", "Return output attachments only (default)"}, {"full", "", "Also read bound inputs and vertex/index buffers for geometry evidence"},
        {"visualize", "original|solid|wireframe|depth|outline|uv", "Inspect mode (graphics draws only); outline prepares original and coverage automatically"},
        {"subdraw", "K", "Inspect only record K of a supported indirect draw (default: all)"},
        {"attachment", "color:N|depth|stencil", "Select primary output attachment (default: prefer COLOR 0); mutually exclusive with --image"},
        {"image", "ID", "Select primary output by captured image ID; excludes descriptor inputs"},
        {"mip", "N", "Select output mip level; nonzero enables all-subresource readback"},
        {"layer", "N", "Select output array layer; nonzero enables all-subresource readback"}};
    auto drawOptions = [&](std::vector<Option> options) {
        options.insert(options.end(), inspectionOptions.begin(), inspectionOptions.end()); return ReplayOptions(options);
    };
    Register({"replay", "frame", "Replay a capture to a frame with swapchain selection, optional shader overrides, baseline comparison and validation", {},
        ReplayOptions({{"frame", "N", "Frame number (1-based)", false, true},
            {"swapchain", "virtual|captured|offscreen", "Replay swapchain implementation; match perf mode for image comparisons", false, false, "virtual"}}), ReplayFrame});
    Register({"replay", "draw", "Replay the frame of one draw/dispatch with resource readback at that draw", {},
        drawOptions({{"block", "B", "Block number of the draw/dispatch call"}, {"occurrence", "K", "K-th submitted execution of that block (default 0)"},
                       {"draw", "DIR", "A dump draw directory (contains draw.json); the capture is taken from the dump manifest"}, {"execution", "I", "Global submitted execution index (0-based); --block selects an API call ID"},
                       }), ReplayDraw});
    Register({"replay", "draws", "Inspect execution indexes/ranges without a static export; original mode batches readbacks per frame", {},
        drawOptions({{"executions", "LIST", "Global zero-based indexes and inclusive ranges, e.g. 0,2-5 (max 4096, no duplicates)", false, true},
            {"batch-draws", "N", "Maximum draws per merged original replay (1..256, default 32); diagnostics/baseline/subdraws use isolated strategy"}}), ReplayDraws});
    Register({"compare", "", "Compare two images: extent, per-channel max/mean difference, differing pixels, PSNR", {"A", "B"},
        {{"out", "FILE.png", "Write a difference image"}}, Compare});
    Register({"shader", "show", "Show a captured shader module as GLSL, SPIR-V assembly and/or reflection", {"MOD"},
        {{"capture", "FILE", "Capture (.gfxr)", false, true}, {"format", "glsl|spvasm|reflection|all", "Representation to return", false, false, "glsl"}}, ShaderShow});
    Register({"shader", "export", "Write a module's SPIR-V, GLSL, assembly, reflection, interface and stage to a directory for editing", {"MOD"},
        {{"capture", "FILE", "Capture (.gfxr)", false, true}, {"out", "DIR", "Output directory", false, true}}, ShaderExport});
    Register({"shader", "compile", "Compile GLSL/SPIR-V assembly to a validated .spv", {"FILE"},
        {{"stage", "vert|frag|comp|VK_SHADER_STAGE_*", "Shader stage (required for GLSL)"}, {"entry", "NAME", "Entry point", false, false, "main"},
         {"output", "OUT.spv", "Output SPIR-V file", false, true}}, ShaderCompile});
    Register({"shader", "check", "Check that a candidate shader keeps module MOD's interface (compiles GLSL/assembly first)", {"MOD", "CANDIDATE"},
        {{"capture", "FILE", "Capture (.gfxr)", false, true}, {"allow-interface-change", "", "Skip the interface check"}}, ShaderCheck});
    Register({"perf", "", "Fresh replay wall timing; use --gpu-timing isolated-frame or --counter-profile for device GPU measurements", {},
        {{"capture", "FILE", "Capture (.gfxr)", false, true}, {"frames", "N|A-B", "Inclusive frame range to measure (default: all frames)"}, {"runs", "N", "Fresh runs per variant (default: 3 for replay timing, 1 for GPU timing)"},
         {"gpu-timing", "isolated-frame", "Measure GPU timestamps for isolated whole frames on one command queue; replays the original capture"},
         {"counters", "", "Collect all provider counters for one frame, subject to hardware limits; enables GPU timing"},
         {"counter-profile", "none|all|precision|bandwidth", "Counter capture (default none); precision/bandwidth are compatibility aliases for all"},
         {"counter-period-us", "N", "Counter interval in microseconds; requires an active counter profile (default 1000)"},
         {"swapchain", "virtual|captured|offscreen", "Replay swapchain implementation (default virtual; offscreen for counter profiles)"},
         {"memory-translation", "default|none|remap|realign|rebind", "Memory portability policy; default is rebind for local macOS, none elsewhere", false, false, "default"},
         {"out", "DIR", "Output directory"}, {"override", "MOD=FILE", "Shader override; also measures the baseline", true},
         {"allow-interface-change", "", "Allow overrides whose interface differs"}, {"screen-off", "", "Turn the Android screen off during replay"},
         {"no-optimize", "", "Replay the original capture instead of the optimized copy"}}, Perf});
}
} // namespace gfx::cli
