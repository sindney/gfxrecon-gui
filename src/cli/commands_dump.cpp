#include "cli/cli.h"
#include "analysis/export.h"
#include "analysis/dump_dynamic.h"
#include "core/artifacts.h"
#include "shaders/decode.h"
#include "shaders/interface.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>

namespace gfx::cli {
namespace {
constexpr int kExporterVersion = 1;
using Clock = std::chrono::steady_clock;

std::string Text(const Json& value, int indent = 2) { return value.dump(indent, ' ', false, Json::error_handler_t::replace) + (indent >= 0 ? "\n" : ""); }
std::string Pad(const char* format, unsigned n) { char b[32]; std::snprintf(b, sizeof(b), format, n); return b; }

// Counting file sink; safe to use from the shader worker threads.
struct Sink {
    std::filesystem::path root;
    std::atomic<uint64_t> bytes{0}, files{0};
    void Write(const std::filesystem::path& relative, const std::string& data) {
        const auto path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) throw std::runtime_error("Cannot write " + path.u8string());
        bytes += data.size(); ++files;
    }
    void Copy(const std::filesystem::path& from, const std::filesystem::path& relative) {
        const auto path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::copy_file(from, path, std::filesystem::copy_options::overwrite_existing);
        bytes += std::filesystem::file_size(path); ++files;
    }
};

std::set<uint32_t> ParseFrames(const std::string& text, uint32_t count) {
    std::set<uint32_t> frames;
    if (text.empty() || text == "all") { for (uint32_t f = 1; f <= count; ++f) frames.insert(f); return frames; }
    size_t pos = 0;
    auto number = [&](const std::string& part) {
        try { size_t used = 0; const auto v = std::stoul(part, &used, 10); if (used == part.size() && v >= 1) return static_cast<uint32_t>(v); } catch (...) {}
        throw Usage("--frames expects a list such as 1,3-4 or 'all'; bad item '" + part + "'");
    };
    while (pos <= text.size()) {
        const auto comma = text.find(',', pos); const auto part = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const auto dash = part.find('-');
        const auto lo = number(dash == std::string::npos ? part : part.substr(0, dash)), hi = dash == std::string::npos ? lo : number(part.substr(dash + 1));
        if (hi < lo || hi > count) throw Usage("--frames item '" + part + "' is outside 1-" + std::to_string(count));
        for (auto f = lo; f <= hi; ++f) frames.insert(f);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return frames;
}
// --draws filters: block=B[,B] | pass=<frame>/<PPP>[,..] | pipeline=P[,P]. Kinds combine with AND, values with OR.
struct DrawFilter {
    std::set<uint64_t> blocks, pipelines; std::set<std::pair<uint32_t, uint32_t>> passes; bool any = false;
    Json ToJson() const {
        Json j = Json::object();
        if (!blocks.empty()) j["block"] = blocks;
        if (!pipelines.empty()) j["pipeline"] = pipelines;
        if (!passes.empty()) { Json list = Json::array(); for (const auto& p : passes) list.push_back(std::to_string(p.first) + "/" + Pad("%03u", p.second)); j["pass"] = list; }
        return j;
    }
    bool Matches(uint64_t block, uint64_t pipeline, uint32_t frame, uint32_t pass) const {
        if (!any) return true;
        return (blocks.empty() || blocks.count(block)) && (pipelines.empty() || pipelines.count(pipeline)) && (passes.empty() || passes.count({frame, pass}));
    }
};
DrawFilter ParseFilter(const std::vector<std::string>& values) {
    DrawFilter filter;
    auto number = [](const std::string& s) {
        try { size_t used = 0; const auto v = std::stoull(s, &used, 0); if (used == s.size()) return static_cast<uint64_t>(v); } catch (...) {}
        throw Usage("--draws expects numeric values, got '" + s + "'");
    };
    for (const auto& value : values) {
        const auto equals = value.find('=');
        if (equals == std::string::npos) throw Usage("--draws expects block=B[,B], pass=<frame>/<PPP> or pipeline=P; got '" + value + "'");
        const auto kind = value.substr(0, equals); const auto list = value.substr(equals + 1);
        size_t pos = 0;
        while (pos <= list.size()) {
            const auto comma = list.find(',', pos); const auto item = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (kind == "block") filter.blocks.insert(number(item));
            else if (kind == "pipeline") filter.pipelines.insert(number(item));
            else if (kind == "pass") {
                const auto slash = item.find('/');
                if (slash == std::string::npos) throw Usage("--draws pass= expects <frame>/<PPP>, got '" + item + "'");
                filter.passes.insert({static_cast<uint32_t>(number(item.substr(0, slash))), static_cast<uint32_t>(number(item.substr(slash + 1)))});
            } else throw Usage("Unknown --draws filter '" + kind + "'; use block, pass or pipeline");
            filter.any = true;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    return filter;
}

struct ModuleUser { uint64_t pipeline; ShaderStage stage; };
void WriteShader(Sink& sink, const std::filesystem::path& cacheDir, uint64_t module, const std::vector<ModuleUser>& users,
                 const std::vector<std::string>& drawPaths, const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    const auto relative = std::filesystem::path(ShaderPath(module));
    const auto binary = cacheDir / "shaders" / ("sh" + std::to_string(module));
    Json files = Json::object();
    auto unavailable = [&](const char* name, const std::string& reason) { files[name] = {{"status", "unavailable"}, {"reason", reason}}; };
    Json reflection{{"schema", kDumpSchema}, {"module", module}};
    if (!std::filesystem::is_regular_file(binary)) {
        const std::string reason = "shader binary was not extracted from the capture (missing shaders/sh" + std::to_string(module) + " in the import cache)";
        for (const char* name : {"module.spv", "module.glsl", "module.spvasm", "interface.json"}) unavailable(name, reason);
        reflection["resources"] = Json::array();
    } else {
        sink.Copy(binary, relative / "module.spv"); files["module.spv"] = {{"status", "ok"}};
        const auto& first = users.front().stage;
        try {
            auto decoded = DecodeShader(binary, first, tools, cancel);
            reflection["stage"] = decoded.stage.stage; reflection["entry"] = decoded.stage.entry; reflection["resources"] = decoded.resources;
            reflection["reflection_text"] = decoded.reflection; reflection["metadata"] = decoded.metadata;
            if (!decoded.capturedSource.empty()) reflection["captured_source"] = decoded.capturedSource;
            if (!decoded.glsl.empty()) { sink.Write(relative / "module.glsl", decoded.glsl); files["module.glsl"] = {{"status", "ok"}, {"generator", "SPIRV-Cross"}}; }
            else unavailable("module.glsl", decoded.glslError.empty() ? "GLSL reconstruction produced no output" : decoded.glslError);
            if (!decoded.assembly.empty()) { sink.Write(relative / "module.spvasm", decoded.assembly); files["module.spvasm"] = {{"status", "ok"}, {"generator", "spirv-dis"}}; }
            else unavailable("module.spvasm", decoded.assemblyError.empty() ? "disassembly produced no output" : decoded.assemblyError);
        } catch (const std::exception& e) {
            if (cancel && cancel->load()) throw;
            unavailable("module.glsl", e.what()); unavailable("module.spvasm", e.what());
            reflection["resources"] = Json::array();
        }
        try { sink.Write(relative / "interface.json", DescribeShaderInterface(binary, first.entry) + "\n"); files["interface.json"] = {{"status", "ok"}}; }
        catch (const std::exception& e) { unavailable("interface.json", e.what()); }
    }
    reflection["files"] = files;
    sink.Write(relative / "reflection.json", Text(reflection));
    Json list = Json::array();
    for (const auto& u : users) list.push_back({{"pipeline", u.pipeline}, {"pipeline_path", PipelinePath(u.pipeline)}, {"stage", u.stage.stage}, {"entry", u.stage.entry}, {"specialization", u.stage.specialization}});
    sink.Write(relative / "users.json", Text({{"schema", kDumpSchema}, {"module", module}, {"pipelines", list}, {"draws", drawPaths}}));
}

Json Dump(Context& context, const Args& args) {
    const auto started = Clock::now();
    const auto tier = args.Get("dynamic", "none");
    if (tier != "none" && tier != "frame" && tier != "pass" && tier != "draws" && tier != "full") throw Usage("--dynamic must be none, frame, pass, draws or full");
    const auto filter = ParseFilter(args.All("draws"));
    if (tier == "full" && !filter.any) throw Usage("--dynamic full requires a --draws filter (block=, pass= or pipeline=); use 'replay draw --full' to probe a single draw");
    const auto captureFile = std::filesystem::absolute(std::filesystem::u8path(args.Get("capture")));
    const auto out = std::filesystem::absolute(std::filesystem::u8path(args.Get("out")));
    const bool overwrite = args.Has("overwrite");
    auto imported = OpenCapture(context, captureFile);
    const auto& capture = *imported.capture;
    const auto sha = capture.fingerprint.empty() ? Sha256File(captureFile, context.cancel) : capture.fingerprint;
    const auto frames = ParseFrames(args.Get("frames", "all"), capture.frames);

    // Provenance: everything that changes static content.
    const auto cacheName = imported.directory.filename().u8string();
    Json tools{{"exporter_version", kExporterVersion}, {"cache_identity", cacheName.size() > 65 ? cacheName.substr(65) : cacheName}};
    const auto disassembler = ToolPath(context, "disassembler");
    if (!disassembler.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(disassembler))) tools["disassembler_sha256"] = Sha256File(std::filesystem::u8path(disassembler), context.cancel);
    else tools["disassembler_sha256"] = nullptr;
    const Json provenance{{"schema", kDumpSchema}, {"capture_sha256", sha}, {"tools", tools},
        {"options", {{"frames", frames}, {"draws", filter.ToJson()}, {"dynamic", tier}}}};

    const auto manifestPath = out / "manifest.json";
    if (std::filesystem::exists(out)) {
        if (!std::filesystem::is_directory(out)) throw CliError(kFailed, "out_not_directory", "--out exists and is not a directory: " + out.u8string());
        if (std::filesystem::is_regular_file(manifestPath)) {
            Json old;
            try { std::ifstream in(manifestPath, std::ios::binary); old = Json::parse(in); } catch (...) {}
            if (old.value("schema", std::string{}) != kDumpSchema) throw CliError(kFailed, "out_not_dump", "--out contains a manifest.json that is not a " + std::string(kDumpSchema) + " dump; refusing to touch it");
            const bool match = old.value("provenance", Json()) == provenance;
            if (match && old.value("complete", false) && !overwrite) {
                Json result{{"out", out.u8string()}, {"reused", true}, {"complete", true}, {"manifest", old}, {"cache_hit", imported.cacheHit}};
                context.Progress("Dump already complete with matching provenance; nothing to do");
                return result;
            }
            if (!match && !overwrite) throw CliError(kFailed, "provenance_mismatch", "--out holds a dump with different provenance (capture, tools or options); use --overwrite to replace it",
                {{"existing", old.value("provenance", Json())}, {"requested", provenance}});
            for (const char* dir : {"frames", "shaders", "pipelines", "objects", "dynamic"}) std::filesystem::remove_all(out / dir);
            for (const char* file : {"manifest.json", "README.md", "capture.json", "index.json"}) std::filesystem::remove(out / file);
        } else if (!std::filesystem::is_empty(out)) {
            throw CliError(kFailed, "out_not_empty", "--out exists, is not empty and is not a dump: " + out.u8string());
        }
    }
    std::filesystem::create_directories(out);
    Sink sink; sink.root = out;
    Json manifest{{"schema", kDumpSchema}, {"complete", false}, {"provenance", provenance}, {"capture", {{"path", captureFile.u8string()}, {"sha256", sha}}},
        {"started_utc", [] { const auto t = std::time(nullptr); char b[32]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t)); return std::string(b); }()}};
    sink.Write("manifest.json", Text(manifest));

    context.Progress("Grouping passes");
    const auto index = DumpIndex::Build(capture);
    std::vector<size_t> selectedExecutions;
    for (const auto& frame : index.frames) if (frames.count(frame.frame)) for (const auto& pass : frame.passes) for (const auto e : pass.executions) selectedExecutions.push_back(e);

    // Shared data. With a frame subset only pipelines/modules used by the selected draws are written.
    std::set<uint64_t> pipelines;
    if (frames.size() == capture.frames) for (const auto& [id, info] : capture.pipelineInfo) pipelines.insert(id);
    for (const auto e : selectedExecutions) { const auto id = capture.events[capture.executions[e].event].pipeline; if (capture.pipelineInfo.count(id)) pipelines.insert(id); }
    std::map<uint64_t, std::vector<ModuleUser>> users;
    for (const auto id : pipelines) {
        const auto stages = capture.pipelines.find(id);
        if (stages != capture.pipelines.end()) for (const auto& s : stages->second) if (s.module) users[s.module].push_back({id, s});
    }
    std::map<uint64_t, std::set<std::string>> moduleDraws; // draw directory paths using each module
    for (const auto e : selectedExecutions) {
        const auto pipeline = capture.events[capture.executions[e].event].pipeline;
        const auto stages = capture.pipelines.find(pipeline);
        if (stages != capture.pipelines.end()) for (const auto& s : stages->second) if (s.module) moduleDraws[s.module].insert(index.draws[e].path);
    }

    context.Progress("Writing capture, object and pipeline data");
    sink.Write("capture.json", Text(ExportCapture(capture, captureFile, sha)));
    {
        std::string lines;
        for (const auto& [id, object] : capture.objects) lines += Text(ExportObject(capture, id), -1) + "\n";
        sink.Write("objects/objects.jsonl", lines);
    }
    for (const auto id : pipelines) sink.Write(PipelinePath(id), Text(ExportPipeline(capture, id)));

    // Shaders in parallel; per-module output is independent so the tree is deterministic.
    context.Progress("Writing " + std::to_string(users.size()) + " shader modules");
    {
        std::vector<uint64_t> modules; for (const auto& [id, list] : users) modules.push_back(id);
        std::atomic<size_t> next{0}; std::mutex errorMutex; std::exception_ptr error;
        const auto cacheDir = imported.directory;
        auto worker = [&] {
            for (;;) {
                const auto n = next.fetch_add(1);
                if (n >= modules.size() || (context.cancel && context.cancel->load())) return;
                try {
                    const auto drawSet = moduleDraws.find(modules[n]);
                    const std::vector<std::string> drawPaths = drawSet == moduleDraws.end() ? std::vector<std::string>{} : std::vector<std::string>(drawSet->second.begin(), drawSet->second.end());
                    WriteShader(sink, cacheDir, modules[n], users.at(modules[n]), drawPaths, context.tools, context.cancel);
                } catch (...) { std::lock_guard<std::mutex> lock(errorMutex); if (!error) error = std::current_exception(); return; }
            }
        };
        std::vector<std::thread> threads;
        const auto count = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), std::max<size_t>(modules.size(), 1));
        for (size_t t = 0; t < count; ++t) threads.emplace_back(worker);
        for (auto& t : threads) t.join();
        if (error) std::rethrow_exception(error);
    }

    // Frames, passes, draws.
    Json entries = Json::array(); uint64_t passCount = 0, drawCount = 0, selectedDraws = 0;
    Json exampleDraw;
    for (const auto& frame : index.frames) {
        if (!frames.count(frame.frame)) continue;
        if (context.cancel && context.cancel->load()) throw CliError(kCancelled, "cancelled", "Operation cancelled");
        context.Progress("Frame " + std::to_string(frame.frame) + ": " + std::to_string(frame.passes.size()) + " passes");
        const auto base = std::filesystem::path(FrameDirectory(frame.frame));
        sink.Write(base / "frame.json", Text(ExportFrame(capture, index, frame)));
        {
            std::ifstream stream(capture.jsonl, std::ios::binary); std::string lines;
            for (size_t i = 0; i < capture.events.size(); ++i) if (capture.events[i].frame == frame.frame) lines += Text(ExportEvent(capture, i, &stream), -1) + "\n";
            sink.Write(base / "events.jsonl", lines);
        }
        for (size_t p = 0; p < frame.passes.size(); ++p) {
            const auto& pass = frame.passes[p];
            const auto passDir = base / "passes" / pass.Directory();
            const auto passJson = ExportPass(capture, index, frame, p);
            sink.Write(passDir / "pass.json", Text(passJson));
            ++passCount;
            entries.push_back({{"type", "pass"}, {"frame", frame.frame}, {"ordinal", pass.ordinal}, {"kind", pass.kind}, {"block", pass.firstBlock}, {"path", passDir.generic_u8string() + "/pass.json"},
                {"draws", pass.executions.size()}, {"classification", passJson["classification"]["label"]}, {"status", "ok"}});
            for (const auto e : pass.executions) {
                const auto& event = capture.events[capture.executions[e].event];
                const auto& location = index.draws[e];
                const auto draw = ExportDraw(capture, index, e);
                sink.Write(std::filesystem::path(location.path) / "draw.json", Text(draw));
                ++drawCount;
                const bool selected = filter.Matches(event.block, event.pipeline, frame.frame, pass.ordinal);
                selectedDraws += selected;
                entries.push_back({{"type", "draw"}, {"frame", frame.frame}, {"pass", pass.ordinal}, {"ordinal", location.ordinal}, {"block", event.block}, {"occurrence", location.occurrence},
                    {"execution", e}, {"call", event.name}, {"pipeline", event.pipeline}, {"path", location.path + "/draw.json"}, {"status", draw.contains("unavailable") ? "unavailable" : "ok"},
                    {"dynamic_status", "skipped"}, {"dynamic_selected", selected}});
                if (exampleDraw.is_null() && draw["selector"].is_object() && !draw["selector"].contains("unavailable") && draw["pipeline"]["path"].is_string())
                    exampleDraw = {{"path", location.path}, {"block", event.block}, {"occurrence", location.occurrence}, {"call", event.name}};
            }
        }
    }
    if (exampleDraw.is_null() && !entries.empty()) for (const auto& e : entries) if (e["type"] == "draw") { exampleDraw = {{"path", e["path"].get<std::string>().substr(0, e["path"].get<std::string>().size() - 10)}, {"block", e["block"]}, {"occurrence", e["occurrence"]}, {"call", e["call"]}}; break; }
    sink.Write("index.json", Text({{"schema", kDumpSchema}, {"frames", frames}, {"entries", entries}}));

    // README
    std::string readme = "# gfxrecon-cli structured dump\n\nSchema: `" + std::string(kDumpSchema) + "`. Generated by `gfxrecon-cli dump`; every path inside the JSON files is relative to this directory (the dump root).\n"
        "`manifest.json` records the source capture (`capture.path`, `capture.sha256`), tool provenance, options, status counts and whether the dump is `complete`.\n\n"
        "## Layout\n\n```\nmanifest.json          provenance, options, counts, complete flag, elapsed time (only file with timestamps)\n"
        "capture.json           capture header, device, extensions, limitations, frame list\n"
        "index.json             flat list of every pass and draw: frame, block, kind, pipeline, path, status\n"
        "objects/objects.jsonl  one Vulkan object per line: id, type, name, created/destroyed block, create info\n"
        "pipelines/<id>.json    pipeline create info, stages (module, entry, specialization), layout, render pass\n"
        "shaders/<module>/      module.spv, module.glsl, module.spvasm, reflection.json, interface.json, users.json\n"
        "frames/<NNNN>/frame.json, events.jsonl   per-frame summary and every recorded API call (grep friendly)\n"
        "frames/<NNNN>/passes/<PPP>-<kind>-b<block>/pass.json   attachments, load/store ops, extent, ordered draw list, heuristic classification\n"
        "frames/<NNNN>/passes/.../draws/<DDDD>-b<block>-<vkCmd>/draw.json   call args, pipeline, stages, descriptors, vertex/index, dynamic state, targets, push constants, selector\n```\n\n"
        "Directory names sort in GPU submission order. `<kind>` is `renderpass`, `rendering` (dynamic rendering) or `compute` (dispatches outside a render scope; the `transfer` kind is reserved: transfer commands are not draw/dispatch executions).\n"
        "Pass `classification` (scene, shadow, postprocess, ui, compute, transfer, unknown) is a heuristic with recorded evidence; it never removes data.\n\n"
        "## Status values\n\nEvery artifact slot carries `status`: `ok` (present), `unavailable` (cannot be produced; `reason` says why, e.g. a shader binary was not extracted), "
        "`failed` (attempted and failed; `reason` has the diagnostic) or `skipped` (not requested, e.g. `static dump (--dynamic none)`). Nothing is omitted silently.\n\n"
        "## Dynamic tiers\n\nThis dump was produced with `--dynamic " + tier + "`. Replay-derived artifacts (`outputs/`, `inputs/`, `mesh/` per draw, `frame.png`, per-pass `outputs/`) are `skipped` in a static dump. "
        "The `--dynamic` tiers are: `none` (default), `frame`, `pass` (default when `--dynamic` is given bare: frame image + every pass's outputs + full post-process pass detail), "
        "`draws` (all draws, outputs only; may be slow and produce large outputs) and `full` (requires a `--draws` filter). "
        "Per-draw inputs (descriptors, textures, vertex/index data) come from `gfxrecon-cli replay draw --full` for the draw you care about.\n\n"
        "## Replay selector\n\nEach `draw.json` has `selector`: the upstream `--dump-resources` JSON (`BeginCommandBuffer`, `QueueSubmit`, `Draw`/`Dispatch`, `RenderPass`, `ExecuteCommands`) that identifies exactly that execution, "
        "or `{\"unavailable\": reason}` when the backend cannot select it uniquely. `execution` indexes the capture's submitted executions; `occurrence` is the n-th execution of that block (0-based).\n\n"
        "## Examples\n\nRun from this directory (the dump root):\n\n";
    if (!exampleDraw.is_null()) {
        readme += "```\ngfxrecon-cli replay draw --draw " + exampleDraw["path"].get<std::string>() + " --outputs\n"
            "gfxrecon-cli draw --capture <capture.path from manifest.json> --block " + std::to_string(exampleDraw["block"].get<uint64_t>()) + " --occurrence " + std::to_string(exampleDraw["occurrence"].get<uint64_t>()) + "\n```\n\n"
            "The example draw is `" + exampleDraw["call"].get<std::string>() + "` at block " + std::to_string(exampleDraw["block"].get<uint64_t>()) + ". `gfxrecon-cli draw ...` prints the same JSON as that draw's `draw.json`.\n"
            "To modify a shader: open the draw's `stages[].path` (`shaders/<module>/`), edit `module.glsl` or `module.spvasm`, then `gfxrecon-cli replay draw --draw <dir> --override <module>=<file> --baseline`.\n";
    } else readme += "(This dump contains no draw executions.)\n";
    sink.Write("README.md", readme);

    // Manifest
    Json counts{{"frames", frames.size()}, {"passes", passCount}, {"draws", drawCount}, {"selected_draws", selectedDraws}, {"pipelines", pipelines.size()}, {"shader_modules", users.size()}, {"objects", capture.objects.size()},
        {"dynamic_slots", {{"skipped", drawCount * 3}}}};
    uint64_t glslOk = 0, asmOk = 0, spvOk = 0;
    for (const auto& [id, list] : users) {
        const auto dir = out / ShaderPath(id);
        glslOk += std::filesystem::exists(dir / "module.glsl"); asmOk += std::filesystem::exists(dir / "module.spvasm"); spvOk += std::filesystem::exists(dir / "module.spv");
    }
    counts["shaders"] = {{"spv_ok", spvOk}, {"glsl_ok", glslOk}, {"spvasm_ok", asmOk}, {"unavailable_spv", users.size() - spvOk}};
    manifest["status_counts"] = counts; manifest["complete"] = true;
    manifest["threads"] = std::max(1u, std::thread::hardware_concurrency());
    manifest["finished_utc"] = [] { const auto t = std::time(nullptr); char b[32]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t)); return std::string(b); }();
    manifest["elapsed_seconds"] = std::chrono::duration<double>(Clock::now() - started).count();
    manifest["replay_count"] = 0;
    manifest["files_written"] = sink.files.load() + 1; manifest["bytes_written"] = sink.bytes.load();

    // Dynamic tiers replay the frame to add frame/pass/draw artifacts (6.x). Static data above is
    // always complete before any replay runs; a failed batch is recorded, not hidden.
    Json dynamicResult = Json::object();
    if (tier != "none") {
        const auto target = ResolveTarget(context, capture);
        DynamicDumpOptions doptions;
        doptions.tier = tier; doptions.batchDraws = static_cast<size_t>(args.Number("batch-draws", 64));
        doptions.maxBytes = args.Number("max-bytes", 0);
        // Executions selected by --draws (needed by tiers that restrict to a filter).
        std::vector<size_t> filtered;
        if (filter.any) for (const auto e : selectedExecutions) {
            const auto& event = capture.events[capture.executions[e].event];
            const auto& loc = index.draws[e];
            if (filter.Matches(event.block, event.pipeline, loc.frame, static_cast<uint32_t>(loc.pass))) filtered.push_back(e);
        }
        doptions.filtered = filtered;
        doptions.full = tier == "full";
        doptions.frames = frames;
        // Pass classification drives the post-process full-detail passes.
        for (const auto& frame : index.frames) for (size_t p = 0; p < frame.passes.size(); ++p) {
            const auto cls = ClassifyPass(capture, frame, p);
            doptions.postprocessPass[{frame.frame, static_cast<uint32_t>(p)}] = cls.value("label", "") == "postprocess";
        }
        if (tier == "draws" && !filter.any)
            context.Progress("Note: the 'draws' tier replays outputs for every draw; this can be slow and produce large outputs. Narrow --draws before replaying.");
        try {
            const auto d = RunDynamicDump(capture, index, out, doptions, target, context.tools, context.ReplayCache(), sha,
                context.timeout.count() ? context.timeout : std::chrono::seconds(3600), context.cancel,
                [&context](const std::string& text) { context.Progress(text); });
            manifest["replay_count"] = d.replays;
            manifest["dynamic"] = {{"tier", tier}, {"replays", d.replays}, {"bytes_written", d.bytesWritten}, {"artifacts", d.artifacts},
                {"draws_ok", d.drawsOk}, {"draws_unavailable", d.drawsUnavailable}, {"batches", d.batches.size()}};
            dynamicResult = {{"tier", tier}, {"replays", d.replays}, {"draws_ok", d.drawsOk}, {"draws_unavailable", d.drawsUnavailable}, {"batches", d.batches}};
            if (!d.error.empty()) { manifest["dynamic"]["error"] = d.error; }
        } catch (const CliError&) { throw; }
        catch (const std::exception& e) {
            if (context.cancel && context.cancel->load()) throw;
            manifest["dynamic"] = {{"tier", tier}, {"status", "failed"}, {"error", e.what()}};
            dynamicResult = {{"tier", tier}, {"status", "failed"}, {"error", e.what()}};
        }
    }
    // Dynamic work happens after the static manifest's timing fields were set; recompute so the
    // manifest reports total elapsed time and total bytes (static + dynamic) for the whole dump.
    manifest["finished_utc"] = [] { const auto t = std::time(nullptr); char b[32]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t)); return std::string(b); }();
    manifest["elapsed_seconds"] = std::chrono::duration<double>(Clock::now() - started).count();
    uint64_t totalBytes = sink.bytes.load();
    if (manifest["dynamic"].is_object() && manifest["dynamic"].contains("bytes_written"))
        totalBytes = manifest["dynamic"]["bytes_written"].get<uint64_t>() + totalBytes;
    manifest["bytes_written"] = totalBytes;
    sink.Write("manifest.json", Text(manifest));
    Json result{{"out", out.u8string()}, {"reused", false}, {"complete", true}, {"manifest", manifest}, {"cache_hit", imported.cacheHit}, {"example_draw", exampleDraw}};
    if (!dynamicResult.empty()) result["dynamic"] = dynamicResult;
    return result;
}
} // namespace
void RegisterDumpCommands() {
    Register({"dump", "", "Decode a capture into frames, passes, draws, pipelines, shaders and objects", {},
        {{"capture", "FILE", "Capture (.gfxr or converted .jsonl)", false, true},
         {"out", "DIR", "Output directory (created; an existing complete dump with matching provenance is reused)", false, true},
         {"frames", "LIST", "Frames to dump: 1,3-4 or all", false, false, "all"},
         {"draws", "FILTER", "Draw selection for dynamic tiers: block=B[,B] | pass=<frame>/<PPP> | pipeline=P (repeatable). Static data is written for all draws of the selected frames; the filter is recorded in index.json (dynamic_selected)", true},
         {"batch-draws", "N", "Draws per batched replay (default 64; bounded by device memory)"},
         {"max-bytes", "N", "Stop scheduling dynamic batches after roughly N bytes of dynamic data (remaining draws are skipped)"},
         {"dynamic", "none|frame|pass|draws|full",
          "Omitted: static only. Bare --dynamic: pass outputs. draws: per-draw outputs (potentially slow/large). full: inputs and geometry; requires --draws. Use replay draw --full for one draw.",
          false, false, "", "pass"},
         {"overwrite", "", "Replace an existing dump with different provenance instead of refusing (provenance_mismatch)"}},
        Dump});
}
} // namespace gfx::cli
