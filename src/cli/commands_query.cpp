#include "cli/cli.h"
#include "analysis/export.h"

#include <algorithm>
#include <fstream>

namespace gfx::cli {
namespace {
const std::vector<Option> kCaptureOption{{"capture", "FILE", "Capture (.gfxr or converted .jsonl)", false, true}};
Json Opened(const Context& context, const Args& args, ImportResult& imported) {
    imported = OpenCapture(context, std::filesystem::u8path(args.Get("capture")));
    return {{"capture", imported.capture->source.u8string()}, {"cache_hit", imported.cacheHit}};
}
uint64_t Id(const Args& args, const char* what) {
    if (args.positional.empty()) throw Usage(std::string("Missing ") + what + " id");
    try { size_t used = 0; const auto value = std::stoull(args.positional[0], &used, 0); if (used == args.positional[0].size()) return value; } catch (...) {}
    throw Usage(std::string(what) + " id must be a non-negative integer");
}
Json Info(Context& context, const Args& args) {
    ImportResult imported; auto result = Opened(context, args, imported);
    const auto& c = *imported.capture;
    size_t draws = 0, dispatches = 0;
    for (const auto& x : c.executions) (c.events[x.event].name.rfind("vkCmdDispatch", 0) == 0 ? dispatches : draws)++;
    result.update({{"frame_count", c.frames}, {"event_count", c.events.size()}, {"execution_count", c.executions.size()}, {"draw_executions", draws}, {"dispatch_executions", dispatches},
        {"object_count", c.objects.size()}, {"pipeline_count", c.pipelines.size()}, {"android", c.android}, {"limitations", c.limitations},
        {"fingerprint", c.fingerprint}, {"cache_directory", imported.directory.u8string()}, {"header", c.header}});
    return result;
}
Json Frames(Context& context, const Args& args) {
    ImportResult imported; auto result = Opened(context, args, imported);
    const auto capture = ExportCapture(*imported.capture, imported.capture->source, imported.capture->fingerprint);
    const auto index = DumpIndex::Build(*imported.capture);
    result["frames"] = capture["frames"];
    for (size_t i = 0; i < result["frames"].size(); ++i) if (i < index.frames.size()) result["frames"][i]["passes"] = index.frames[i].passes.size();
    return result;
}
Json Events(Context& context, const Args& args) {
    ImportResult imported; auto result = Opened(context, args, imported);
    const auto& c = *imported.capture;
    const bool frameGiven = args.Has("frame"); const auto frame = static_cast<uint32_t>(args.Number("frame", 0));
    const auto limit = args.Number("limit", 100);
    const auto filter = args.Get("filter"); const bool drawsOnly = args.Has("draws-only");
    std::string needle = filter; std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::ifstream stream(c.jsonl, std::ios::binary);
    Json list = Json::array(); uint64_t matched = 0;
    for (size_t i = 0; i < c.events.size(); ++i) {
        const auto& e = c.events[i];
        if (frameGiven && e.frame != frame) continue;
        if (drawsOnly && !e.draw) continue;
        if (!needle.empty()) {
            auto text = c.SearchText(i); std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (text.find(needle) == std::string::npos) continue;
        }
        ++matched;
        if (list.size() < limit) list.push_back(ExportEvent(c, i, &stream));
    }
    result.update({{"events", list}, {"returned", list.size()}, {"matched", matched}, {"truncated", matched > list.size()}});
    return result;
}
Json DrawQuery(Context& context, const Args& args) {
    ImportResult imported; Opened(context, args, imported);
    const auto& c = *imported.capture;
    const auto execution = FindExecution(c, args.Number("block", 0), static_cast<size_t>(args.Number("occurrence", 0)));
    // The result IS the draw.json document (links are dump-root relative), so queries equal dumped files.
    return ExportDraw(c, DumpIndex::Build(c), execution);
}
Json PipelineQuery(Context& context, const Args& args) {
    ImportResult imported; Opened(context, args, imported);
    const auto id = Id(args, "pipeline");
    try { return ExportPipeline(*imported.capture, id); }
    catch (const std::out_of_range& e) { throw CliError(kFailed, "pipeline_not_found", e.what(), {{"id", id}}); }
}
Json ObjectQuery(Context& context, const Args& args) {
    ImportResult imported; Opened(context, args, imported);
    const auto id = Id(args, "object");
    try { return ExportObject(*imported.capture, id); }
    catch (const std::out_of_range& e) { throw CliError(kFailed, "object_not_found", e.what(), {{"id", id}}); }
}
} // namespace
void RegisterQueryCommands() {
    Register({"info", "", "Capture summary: frames, events, executions, platform, limitations, cache hit", {}, kCaptureOption, Info});
    Register({"frames", "", "List frames with block ranges, event/execution/pass counts", {}, kCaptureOption, Frames});
    auto eventOptions = kCaptureOption;
    eventOptions.push_back({"frame", "N", "Only events of this capture-relative frame"});
    eventOptions.push_back({"filter", "TEXT", "Case-insensitive substring over block, name, label and object names/ids"});
    eventOptions.push_back({"draws-only", "", "Only draw/dispatch calls"});
    eventOptions.push_back({"limit", "N", "Maximum events returned", false, false, "100"});
    Register({"events", "", "List recorded API calls in block order with arguments", {}, eventOptions, Events});
    auto drawOptions = kCaptureOption;
    drawOptions.push_back({"block", "B", "Block number of the draw/dispatch call", false, true});
    drawOptions.push_back({"occurrence", "K", "K-th submitted execution of that block (0-based)", false, false, "0"});
    Register({"draw", "", "Static data of one submitted draw/dispatch; equals the dump's draw.json", {}, drawOptions, DrawQuery});
    Register({"pipeline", "", "Pipeline create info and shader stages; equals the dump's pipelines/<id>.json", {"ID"}, kCaptureOption, PipelineQuery});
    Register({"object", "", "Object type, name, lifetime and create info", {"ID"}, kCaptureOption, ObjectQuery});
}
} // namespace gfx::cli
