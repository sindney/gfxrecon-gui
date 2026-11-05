#include "cli/cli.h"
#include "analysis/relations.h"

#include <limits>
#include <sstream>

namespace gfx::cli {
namespace {
std::vector<std::string> Values(const Args& args, const std::string& key) {
    std::vector<std::string> result;
    for (const auto& text : args.All(key)) {
        std::istringstream input(text); std::string value;
        while (std::getline(input, value, ',')) {
            const auto first = value.find_first_not_of(" \t\r\n"), last = value.find_last_not_of(" \t\r\n");
            if (first == std::string::npos) throw Usage("--" + key + " contains an empty value");
            result.push_back(value.substr(first, last - first + 1));
        }
        if (text.empty() || text.back() == ',') throw Usage("--" + key + " contains an empty value");
    }
    return result;
}
template<class T> void Numbers(const Args& args, const std::string& key, std::vector<T>& out) {
    for (const auto& text : Values(args, key)) {
        uint64_t number = 0; size_t used = 0;
        try { if (text.front() == '-' || text.front() == '+') throw std::invalid_argument("sign"); number = std::stoull(text, &used, 0); }
        catch (...) { throw Usage("--" + key + " expects non-negative integers"); }
        if (used != text.size() || number > std::numeric_limits<T>::max()) throw Usage("--" + key + " integer is out of range");
        out.push_back(static_cast<T>(number));
    }
}
void Source(const Args& args) {
    if (args.Has("capture") == args.Has("index")) throw Usage("Specify exactly one of --capture FILE and --index FILE");
    if (args.Get(args.Has("capture") ? "capture" : "index").empty()) throw Usage("Source path must not be empty");
}
RelationIndexRef Index(Context& context, const Args& args, bool rebuild = false) {
    Source(args);
    if (args.Has("index")) return OpenRelationIndex(std::filesystem::u8path(args.Get("index")), context.cancel);
    RelationBuildOptions options; options.rebuild = rebuild;
    options.progress = [&context](const std::string& text) { context.Progress(text); };
    return EnsureRelationIndex(std::filesystem::u8path(args.Get("capture")), context.CaptureCache(), context.tools, options, context.cancel);
}
Json IndexInfo(const RelationIndexRef& index) {
    return {{"schema", kRelationQuerySchema}, {"index", index.path.u8string()}, {"snapshot", index.snapshot},
            {"reused", index.reused}, {"metadata", index.metadata}};
}
std::vector<Option> Sources() {
    return {{"capture", "FILE", "Resolve current capture metadata and ensure its relation index"},
            {"index", "FILE", "Read a complete immutable snapshot, without the original capture/tools"}};
}
std::vector<Option> QueryOptions() {
    auto options = Sources();
    for (const auto& item : std::vector<Option>{
        {"frame", "N", "Capture-relative frame (1-based); repeated/comma values are OR", true},
        {"pass", "N", "Pass ordinal (0-based); requires one explicit --frame", true},
        {"pipeline", "ID", "Captured pipeline identity", true},
        {"shader", "ID", "Captured shader module identity", true},
        {"stage", "STAGE", "Shader stage; matches the same edge as --shader", true},
        {"kind", "all|draw|dispatch", "Submitted execution kind", false, false, "all"},
        {"columns", "NAMES", "Comma-separated projected column names; see query fields", true},
        {"section", "NAMES", "Named structured detail sections", true},
        {"limit", "N", "Page size (1..1000)", false, false, "100"},
        {"cursor", "TOKEN", "Resume this exact query/snapshot"},
        {"count", "", "Include total matching row count"},
        {"group-by", "NAMES", "Count executions by frame,pass / pipeline_id / shader_id", true},
        {"export", "FILE", "Write projected rows; stdout contains a compact receipt"},
        {"export-format", "csv|jsonl", "Text export format (default jsonl; requires --export)"},
        {"all", "", "Export all pages with bounded memory; requires --export"}}) options.push_back(item);
    return options;
}
Json Query(Context& context, const Args& args, RelationEntity entity, bool draws, bool stats) {
    Source(args);
    RelationQuery query; query.entity = entity;
    const auto kind = args.Get("kind", "all");
    if (kind != "all" && kind != "draw" && kind != "dispatch") throw Usage("--kind must be all, draw or dispatch");
    if (draws && kind == "dispatch") throw Usage("query draws conflicts with --kind dispatch");
    query.kind = draws || kind == "draw" ? RelationKind::Draw : kind == "dispatch" ? RelationKind::Dispatch : RelationKind::All;
    Numbers(args, "frame", query.filters.frames); Numbers(args, "pass", query.filters.passes);
    Numbers(args, "pipeline", query.filters.pipelines); Numbers(args, "shader", query.filters.shaders);
    query.filters.stages = Values(args, "stage"); query.columns = Values(args, "columns");
    query.sections = Values(args, "section"); query.groupBy = Values(args, "group-by");
    if (stats && query.groupBy.empty()) query.groupBy = {"frame", "pass"};
    const auto limit = args.Number("limit", 100);
    if (limit > 1000 || limit == 0) throw Usage("--limit must be between 1 and 1000");
    query.limit = static_cast<uint32_t>(limit); query.cursor = args.Get("cursor"); query.includeCount = args.Has("count");
    if (args.Has("all") && !args.Has("export")) throw Usage("--all requires --export FILE");
    if (!args.Has("export") && args.Has("export-format")) throw Usage("--export-format requires --export FILE");
    const auto format = args.Get("export-format", "jsonl");
    if (args.Has("export") && (args.Get("export").empty() || (format != "csv" && format != "jsonl"))) throw Usage("--export requires a path and --export-format csv or jsonl");
    ValidateRelationQuery(query); // No source import or model load before query validation.
    const auto index = Index(context, args);
    if (args.Has("export")) return ExportRelations(index, query, std::filesystem::u8path(args.Get("export")), format, args.Has("all"), context.cancel);
    return QueryRelations(index, query, context.cancel).ToJson();
}
} // namespace
void RegisterRelationCommands() {
    auto build = std::vector<Option>{{"capture", "FILE", "Capture or decoded JSONL", false, true}, {"rebuild", "", "Publish a new owned immutable generation"}};
    Register({"index", "build", "Build/reuse static relations without replay, dumps or shader extraction", {}, build,
        [](Context& c, const Args& a) { return IndexInfo(Index(c, a, a.Has("rebuild"))); }});
    Register({"index", "info", "Inspect snapshot identity, completeness and coverage", {}, Sources(),
        [](Context& c, const Args& a) { return IndexInfo(Index(c, a)); }});
    for (const auto& name : {"executions", "draws", "pipelines", "shaders", "passes", "stats"}) {
        const std::string verb(name);
        Register({"query", verb, "Query compact static relations; use query fields to discover columns/sections", {}, QueryOptions(),
            [verb](Context& c, const Args& a) { return Query(c, a, verb == "draws" || verb == "stats" ? RelationEntity::Executions : ParseRelationEntity(verb), verb == "draws", verb == "stats"); }});
    }
    Register({"query", "fields", "Discover typed fields, sections, defaults and filters without a source", {},
        {{"entity", "executions|pipelines|shaders|passes", "Entity field catalog", false, true}},
        [](Context&, const Args& a) { return RelationFieldCatalog(ParseRelationEntity(a.Get("entity"))); }});
}
} // namespace gfx::cli
