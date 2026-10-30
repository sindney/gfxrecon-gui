// replay frame/draw, compare, shader show/export/compile/check and perf.
#include "cli/cli.h"
#include "core/artifacts.h"
#include "core/image_io.h"
#include "shaders/decode.h"
#include "shaders/interface.h"
#include "shaders/metadata.h"
#include "shaders/diagnostics.h"

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
Opened OpenReplayable(const Context& context, const fs::path& file) {
    Opened opened{OpenCapture(context, file), file};
    if (opened.import.capture->source.extension() != ".gfxr")
        throw CliError(kFailed, "not_replayable", "Only .gfxr captures can be replayed: " + U8(file), {{"capture", U8(file)}});
    return opened;
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
    request.timeout = ReplayTimeout(context, target, validate);
    request.progress = [&context](const std::string& text) { context.Progress(text); };
    return request;
}
// ReplayCapture with CLI error codes for the failures the contract names.
ReplayResult RunReplay(const Context& context, const ReplayRequest& request, const ReplayTarget& target) {
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
    if (!result.image.empty()) {
        std::string png; auto entry = WithPng(result.image, &png);
        run["image"] = U8(result.image); run["image_png"] = png; run["image_info"] = entry;
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
                   const fs::path& root, ReplayRequest request, Json scope, const std::function<void(ReplayRequest&, bool)>& prepare) {
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
        return std::make_pair(r, RunReplay(context, r, target));
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
    for (const char* key : {"image", "image_png", "replay_log", "manifest", "timings", "cache_hit", "outputs", "inputs", "mesh", "validation"}) if (last.contains(key)) result[key] = last[key];
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
        {"fresh", "", "Bypass the shared result cache"}};
    options.insert(options.end(), specific.begin(), specific.end());
    return options;
}
fs::path RequireCapture(const Args& args) {
    if (!args.Has("capture")) throw Usage("--capture FILE is required");
    return Path(args.Get("capture"));
}

// ---- replay frame -------------------------------------------------------------------------------------------
Json ReplayFrame(Context& context, const Args& args) {
    const auto opened = OpenReplayable(context, RequireCapture(args));
    const auto& capture = *opened.import.capture;
    const auto frame = args.Number("frame", 0);
    if (frame < 1 || frame > capture.frames) throw CliError(kUsage, "frame_out_of_range", "--frame must be 1.." + std::to_string(capture.frames), {{"frames", capture.frames}});
    const auto target = ResolveTarget(context, capture);
    const auto root = ResolveRoot(context, args, "replay");
    const auto overrides = PrepareOverrides(context, opened.import, args, root / "overrides");
    auto request = BaseRequest(context, capture, target, args, false);
    request.frame = static_cast<uint32_t>(frame);
    return ExecuteReplay(context, args, opened, target, overrides, root, request, {{"scope", "frame"}, {"frame", frame}}, [](ReplayRequest&, bool) {});
}

// ---- replay draw --------------------------------------------------------------------------------------------
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
Json ReplayDraw(Context& context, const Args& args) {
    const auto inspectionStarted=std::chrono::steady_clock::now();
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
    const auto& execution = capture.executions[index];
    const auto& event = capture.events[execution.event];
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
    const auto target = ResolveTarget(context, capture);
    const auto root = ResolveRoot(context, args, "replay");
    const auto overrides = PrepareOverrides(context, opened.import, args, root / "overrides");
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
            const auto original=RunReplay(context,contextRequest,target);
            auto color=[](const ReplayResult& run){
                for(const auto& asset:run.resources)if(asset.metadata.value("category","")=="output"&&asset.metadata.value("aspect","")=="COLOR"&&asset.metadata.value("location",0u)==0&&asset.file.extension()==".bmp")return asset.file;
                throw CliError(kFailed,"visualization_unsupported","Orange outline requires a readable COLOR 0 attachment.");
            };
            r.outlineBaseImage=color(original);
            Json sources;sources["original"]={{"image",U8(r.outlineBaseImage)},{"output_dir",U8(contextRequest.output)},{"manifest",U8(contextRequest.output/"manifest.json")},{"timings",original.timings}};
            if(contextRequest.validation)sources["original"]["validation"]=ValidationSummary(original,contextRequest.output);
            contextRequest.visualization="solid";contextRequest.output=root/"diagnostic-sources"/r.prefix/"solid";
            context.Progress("Preparing solid coverage for the outline");
            const auto coverage=RunReplay(context,contextRequest,target);r.outlineCoverageImage=color(coverage);
            sources["solid"]={{"image",U8(r.outlineCoverageImage)},{"output_dir",U8(contextRequest.output)},{"manifest",U8(contextRequest.output/"manifest.json")},{"timings",coverage.timings}};
            if(contextRequest.validation)sources["solid"]["validation"]=ValidationSummary(coverage,contextRequest.output);
            diagnosticSources[r.prefix]=sources;
        }
    });
    if(!diagnosticSources.empty())result["diagnostic_sources"]=diagnosticSources;
    if(visualization=="outline")result["notes"].push_back("Outline automatically replays original color and solid coverage before compositing its boundary. Auxiliary manifests and timings are in diagnostic_sources.");
    for (auto& [name, run] : result["runs"].items()) {
        try { const auto parameters = ReadDrawParameters(fs::u8path(run.value("output_dir", "")), event.block); if (!parameters.is_null()) run["draw_parameters"] = parameters; } catch (...) {}
    }
    result["notes"].push_back("Replay-to-draw replays the whole frame with resource readback at the selected draw; it does not truncate the command buffer.");
    result["inspection_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-inspectionStarted).count();
    AtomicJson(root / "result.json", result);
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
        {"optimized_replay", result.timings.value("optimized_replay", false)}, {"timings", result.timings}};
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
    return run;
}
Json Perf(Context& context, const Args& args) {
    const auto opened = OpenReplayable(context, RequireCapture(args));
    const auto& capture = *opened.import.capture;
    const auto runsWanted = args.Number("runs", 3);
    if (runsWanted < 1 || runsWanted > 50) throw Usage("--runs must be 1..50");
    std::string upstream;
    const auto range = ParseRange(args.Get("frames"), capture.frames, upstream);
    const auto target = ResolveTarget(context, capture);
    const auto root = ResolveRoot(context, args, "perf");
    const auto overrides = PrepareOverrides(context, opened.import, args, root / "overrides");
    auto request = BaseRequest(context, capture, target, args, false);
    request.frame = range["first"].get<uint32_t>();
    request.measurement = {true, upstream, true};
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
        {"screen_off", request.screenOff},
        {"notes", Json::array({"Fresh replays: result caches are bypassed. Pipeline caches may be loaded; see pipeline_cache_loaded per run (the first run on a new cache is cold).",
            "frame_time_ms is measured_seconds / frames in the measured range; load_seconds is the upstream 'Load time' (state load). Replay timing only, not a GPU profile."})}};
    if (compare) {
        result["overrides"] = overrides.report;
        const double b = Median(summary(0)["frame_time_ms"], "median"), m = Median(summary(1)["frame_time_ms"], "median");
        Json delta = Json::object();
        if (!std::isnan(b) && !std::isnan(m)) { delta["baseline_median_ms"] = b; delta["modified_median_ms"] = m; delta["delta_ms"] = m - b; delta["delta_percent"] = b > 0 ? Json((m - b) / b * 100.0) : Json(nullptr); }
        result["frame_time_delta"] = delta;
    }
    AtomicJson(root / "result.json", result);
    return result;
}
} // namespace

void RegisterReplayCommands() {
    Register({"replay", "frame", "Replay a capture to a frame (optionally with shader overrides, baseline comparison and validation)", {},
        ReplayOptions({{"frame", "N", "Frame number (1-based)", false, true}}), ReplayFrame});
    Register({"replay", "draw", "Replay the frame of one draw/dispatch with resource readback at that draw", {},
        ReplayOptions({{"block", "B", "Block number of the draw/dispatch call"}, {"occurrence", "K", "K-th submitted execution of that block (default 0)"},
                       {"draw", "DIR", "A dump draw directory (contains draw.json); the capture is taken from the dump manifest"}, {"execution", "I", "Global submitted execution index (0-based); --block selects an API call ID"},
                       {"outputs", "", "Return output attachments only (default)"}, {"full", "", "Also return bound inputs, descriptor images and vertex/index data"},
                       {"visualize", "original|solid|wireframe|depth|outline|uv", "Inspect mode (vkCmdDraw* only); outline prepares its source passes automatically"}, {"subdraw", "K", "Replay only record K of an indirect draw"}}), ReplayDraw});
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
    Register({"perf", "", "Fresh replays of a frame range with upstream measurement: load time, frame time, FPS, min/median/max; baseline-vs-override deltas", {},
        {{"capture", "FILE", "Capture (.gfxr)", false, true}, {"frames", "N|A-B", "Inclusive frame range to measure (default: all frames)"}, {"runs", "N", "Fresh runs per variant", false, false, "3"},
         {"out", "DIR", "Output directory"}, {"override", "MOD=FILE", "Shader override; also measures the baseline", true},
         {"allow-interface-change", "", "Allow overrides whose interface differs"}, {"screen-off", "", "Turn the Android screen off during replay"},
         {"no-optimize", "", "Replay the original capture instead of the optimized copy"}}, Perf});
}
} // namespace gfx::cli
