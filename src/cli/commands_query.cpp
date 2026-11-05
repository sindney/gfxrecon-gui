#include "cli/cli.h"
#include "analysis/export.h"
#include "analysis/relations.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>

namespace gfx::cli {
namespace {
const std::vector<Option> kCaptureOption{{"capture", "FILE", "Capture (.gfxr or converted .jsonl)", false, true}};
Json Opened(const Context& context, const Args& args, ImportResult& imported) {
    imported = OpenCapture(context, std::filesystem::u8path(args.Get("capture")));
    return {{"capture", imported.capture->source.u8string()}, {"cache_hit", imported.cacheHit}};
}
uint64_t Id(const Args& args, const char* what) {
    if (args.positional.empty()) throw Usage(std::string("Missing ") + what + " id");
    if (args.positional[0].empty() || args.positional[0][0] == '-' || args.positional[0][0] == '+') throw Usage(std::string(what) + " id must be a non-negative integer");
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
    if (args.Has("execution") == args.Has("block")) throw Usage("Specify exactly one of --execution I or --block B");
    if (args.Has("execution") && args.Has("occurrence")) throw Usage("--occurrence requires --block and conflicts with --execution");
    auto number = [&](const char* name) {
        if (args.Get(name).empty() || args.Get(name).front() == '-' || args.Get(name).front() == '+') throw Usage(std::string("--") + name + " expects a non-negative integer");
        return args.Number(name, 0);
    };
    const auto selector = number(args.Has("execution") ? "execution" : "block");
    const auto occurrence = args.Has("occurrence") ? number("occurrence") : 0;
    if (occurrence > std::numeric_limits<uint32_t>::max()) throw Usage("--occurrence is out of range");
    if (args.Has("columns") || args.Has("section")) {
        RelationQuery query;
        if (args.Has("execution")) query.filters.executions = {selector};
        else { query.filters.blocks = {selector}; query.filters.occurrence = static_cast<uint32_t>(occurrence); }
        auto names = [&](const char* name, std::vector<std::string>& out) {
            for (const auto& text : args.All(name)) {
                std::istringstream input(text); std::string value;
                while (std::getline(input, value, ',')) {
                    auto first = value.find_first_not_of(" \t\r\n"), last = value.find_last_not_of(" \t\r\n");
                    if (first == std::string::npos) throw Usage(std::string("--") + name + " contains an empty value");
                    out.push_back(value.substr(first, last - first + 1));
                }
                if (text.empty() || text.back() == ',') throw Usage(std::string("--") + name + " contains an empty value");
            }
        };
        names("columns", query.columns); names("section", query.sections); ValidateRelationQuery(query);
        RelationBuildOptions options; options.progress = [&](const std::string& text) { context.Progress(text); };
        const auto index = EnsureRelationIndex(std::filesystem::u8path(args.Get("capture")), context.CaptureCache(), context.tools, options, context.cancel);
        return QueryRelations(index, query, context.cancel).ToJson();
    }
    ImportResult imported; Opened(context, args, imported);
    const auto& c = *imported.capture;
    if (args.Has("execution") && selector >= c.executions.size()) throw CliError(kFailed, "draw_not_found", "Submitted execution index is out of range", {{"execution", selector}});
    const auto execution = args.Has("execution") ? static_cast<size_t>(selector) : FindExecution(c, selector, static_cast<size_t>(occurrence));
    // The result IS the draw.json document (links are dump-root relative), so queries equal dumped files.
    return ExportDraw(c, DumpIndex::Build(c), execution);
}
Json PipelineQuery(Context& context, const Args& args) {
    const auto id = Id(args, "pipeline");
    if (args.Has("columns") || args.Has("section")) {
        RelationQuery query; query.entity = RelationEntity::Pipelines; query.filters.pipelines = {id};
        for (const auto* key : {"columns", "section"}) for (const auto& text : args.All(key)) {
            std::istringstream input(text); std::string value;
            while (std::getline(input, value, ',')) {
                const auto first = value.find_first_not_of(" \t\r\n"), last = value.find_last_not_of(" \t\r\n");
                if (first == std::string::npos) throw Usage(std::string("--") + key + " contains an empty value");
                (std::string(key) == "columns" ? query.columns : query.sections).push_back(value.substr(first, last - first + 1));
            }
            if (text.empty() || text.back() == ',') throw Usage(std::string("--") + key + " contains an empty value");
        }
        ValidateRelationQuery(query);
        RelationBuildOptions options; options.progress = [&](const std::string& text) { context.Progress(text); };
        const auto index = EnsureRelationIndex(std::filesystem::u8path(args.Get("capture")), context.CaptureCache(), context.tools, options, context.cancel);
        return QueryRelations(index, query, context.cancel).ToJson();
    }
    ImportResult imported; Opened(context, args, imported);
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
    drawOptions.push_back({"block", "B", "Block number of the draw/dispatch call (alternative to --execution)"});
    drawOptions.push_back({"execution", "I", "Submitted execution index (0-based; alternative to --block)"});
    drawOptions.push_back({"occurrence", "K", "K-th submitted execution of --block (0-based; default 0)"});
    drawOptions.push_back({"columns", "NAMES", "Opt into a projected gfxrecon-query/1 response", true});
    drawOptions.push_back({"section", "NAMES", "Opt into named relation detail sections", true});
    Register({"draw", "", "Static data of one submitted draw/dispatch; equals the dump's draw.json", {}, drawOptions, DrawQuery});
    auto pipelineOptions = kCaptureOption;
    pipelineOptions.push_back({"columns", "NAMES", "Opt into a projected gfxrecon-query/1 response", true});
    pipelineOptions.push_back({"section", "NAMES", "Opt into named relation detail sections", true});
    Register({"pipeline", "", "Pipeline create info and shader stages; equals the dump's pipelines/<id>.json", {"ID"}, pipelineOptions, PipelineQuery});
    Register({"object", "", "Object type, name, lifetime and create info", {"ID"}, kCaptureOption, ObjectQuery});
}
} // namespace gfx::cli
