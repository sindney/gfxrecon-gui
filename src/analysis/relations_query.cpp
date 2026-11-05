#include "analysis/relations_internal.h"
#include "core/artifacts.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace gfx {
namespace {
using namespace relation_detail;
[[noreturn]] void Usage(const std::string& code, const std::string& message) { throw RelationError(RelationErrorKind::Usage, code, message); }
[[noreturn]] void Data(const std::string& message) { throw RelationError(RelationErrorKind::Data, "invalid_index", message); }
std::string Lower(std::string text) { for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; }
template<class T> void Unique(std::vector<T>& values) { std::sort(values.begin(), values.end()); values.erase(std::unique(values.begin(), values.end()), values.end()); }
void OrderedUnique(std::vector<std::string>& values) { std::set<std::string> seen; values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& value) { return !seen.insert(value).second; }), values.end()); }
bool Contains(const std::vector<std::string>& list, const std::string& value) { return std::find(list.begin(), list.end(), value) != list.end(); }
RelationField Field(const char* name, const char* type, const char* description, bool selected = false, bool filter = false, bool section = false, bool nullable = false) {
    return {name, type, description, selected, filter, section, nullable};
}
Json FieldJson(const RelationField& field) { return {{"name", field.name}, {"type", field.type}, {"description", field.description}, {"default", field.defaultField}, {"filterable", field.filterable}, {"section", field.section}, {"nullable", field.nullable}}; }
std::vector<std::string> Groups(std::vector<std::string> groups) {
    for (auto& group : groups) { if (group == "pipeline") group = "pipeline_id"; if (group == "shader") group = "shader_id"; }
    Unique(groups);
    if (Contains(groups, "pass")) { if (!Contains(groups, "frame")) groups.push_back("frame"); return {"frame", "pass"}; }
    return groups;
}
RelationQuery Normalize(RelationQuery query) {
    Unique(query.filters.executions); Unique(query.filters.blocks); Unique(query.filters.pipelines); Unique(query.filters.shaders);
    Unique(query.filters.frames); Unique(query.filters.passes);
    for (auto& stage : query.filters.stages) stage = NormalizeRelationStage(stage);
    Unique(query.filters.stages); OrderedUnique(query.columns); OrderedUnique(query.sections); query.groupBy = Groups(query.groupBy);
    return query;
}
std::vector<RelationField> CountFields(const RelationQuery& query) {
    std::vector<RelationField> fields;
    for (const auto& key : query.groupBy) fields.push_back({key, key == "frame" || key == "pass" ? "uint32" : "uint64", "Group identity", true, false, false, key == "pipeline_id"});
    for (const auto* key : {"execution_count", "draw_count", "dispatch_count", "unknown_count"}) fields.push_back({key, "uint64", "Distinct submitted executions in this group", true});
    return fields;
}
std::vector<std::string> Projection(const RelationQuery& query) {
    auto fields = query.groupBy.empty() ? RelationFields(query.entity) : CountFields(query);
    auto names = query.columns;
    if (names.empty()) for (const auto& field : fields) if (field.defaultField) names.push_back(field.name);
    for (const auto& section : query.sections) if (!Contains(names, section)) names.push_back(section);
    return names;
}
std::string Fingerprint(const RelationQuery& query) {
    return Sha256Text(Json{{"entity", RelationEntityName(query.entity)}, {"kind", int(query.kind)},
        {"executions", query.filters.executions}, {"blocks", query.filters.blocks}, {"frames", query.filters.frames}, {"passes", query.filters.passes},
        {"pipelines", query.filters.pipelines}, {"shaders", query.filters.shaders}, {"stages", query.filters.stages},
        {"occurrence", query.filters.occurrence ? Json(*query.filters.occurrence) : Json(nullptr)},
        {"columns", Projection(query)}, {"sections", query.sections}, {"groups", query.groupBy}, {"order", "ascending_entity_unique_key"}}.dump());
}
const char* Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
std::string Encode(const std::string& bytes) {
    std::string out; unsigned bits = 0; uint32_t value = 0;
    for (unsigned char byte : bytes) { value = (value << 8) | byte; bits += 8; while (bits >= 6) { bits -= 6; out += Alphabet[(value >> bits) & 63]; } }
    if (bits) out += Alphabet[(value << (6 - bits)) & 63]; return out;
}
std::string Decode(const std::string& token) {
    if (token.empty() || token.size() > 16384 || token.size() % 4 == 1) Usage("invalid_cursor", "Malformed relation cursor");
    std::string out; unsigned bits = 0; uint32_t value = 0;
    for (const char c : token) {
        const auto at = std::string(Alphabet).find(c); if (at == std::string::npos) Usage("invalid_cursor", "Malformed relation cursor encoding");
        value = (value << 6) | unsigned(at); bits += 6;
        if (bits >= 8) { bits -= 8; out += char((value >> bits) & 255); }
    }
    if (bits && (value & ((uint32_t(1) << bits) - 1))) Usage("invalid_cursor", "Noncanonical relation cursor encoding");
    return out;
}
Json Cursor(const std::string& text) {
    try {
        auto value = Json::parse(Decode(text));
        if (!value.is_object() || value.value("v", 0) != 1 || !value.at("snapshot").is_string() || !value.at("query").is_string() || !value.at("last").is_array() || !value.at("check").is_string())
            Usage("invalid_cursor", "Malformed relation cursor payload");
        const auto check = value.at("check").get<std::string>(); value.erase("check");
        if (Sha256Text(value.dump()) != check) Usage("invalid_cursor", "Relation cursor checksum does not match");
        return value;
    } catch (const RelationError&) { throw; } catch (...) { Usage("invalid_cursor", "Malformed relation cursor payload"); }
}
std::string NextCursor(const std::string& snapshot, const RelationQuery& query, Json key) {
    Json value{{"v", 1}, {"snapshot", snapshot}, {"query", Fingerprint(query)}, {"last", std::move(key)}};
    value["check"] = Sha256Text(value.dump()); return Encode(value.dump());
}
struct Bound {
    enum Type { Id, Integer, Text } type;
    uint64_t id = 0; int64_t integer = 0; std::string text;
};
struct Sql {
    std::vector<Bound> values;
    std::string Id(uint64_t value) { values.push_back({Bound::Id, value}); return "?" + std::to_string(values.size()); }
    std::string Integer(int64_t value) { values.push_back({Bound::Integer, 0, value}); return "?" + std::to_string(values.size()); }
    std::string Text(const std::string& value) { values.push_back({Bound::Text, 0, 0, value}); return "?" + std::to_string(values.size()); }
    void Bind(Statement& statement) const {
        for (size_t i = 0; i < values.size(); ++i) {
            const auto& value = values[i]; const auto index = int(i + 1);
            if (value.type == Bound::Id) statement.Id(index, value.id); else if (value.type == Bound::Integer) statement.Integer(index, value.integer); else statement.Text(index, value.text);
        }
    }
};
template<class T> std::string In(Sql& sql, const std::string& column, const std::vector<T>& values, bool ids = false) {
    if (values.empty()) return {};
    std::string out = column + " IN (";
    for (size_t i = 0; i < values.size(); ++i) { if (i) out += ','; out += ids ? sql.Id(uint64_t(values[i])) : sql.Integer(int64_t(values[i])); }
    return out + ')';
}
std::string TextIn(Sql& sql, const std::string& column, const std::vector<std::string>& values) {
    if (values.empty()) return {}; std::string out = column + " IN (";
    for (size_t i = 0; i < values.size(); ++i) { if (i) out += ','; out += sql.Text(values[i]); } return out + ')';
}
std::string And(const std::vector<std::string>& terms) {
    std::string out; for (const auto& term : terms) if (!term.empty()) { if (!out.empty()) out += " AND "; out += '(' + term + ')'; } return out.empty() ? "1" : out;
}
std::string EdgeWhere(Sql& sql, const RelationQuery& query, const std::string& alias, bool shaders = true) {
    return And({shaders ? In(sql, alias + ".module_id", query.filters.shaders, true) : std::string{},
        shaders ? TextIn(sql, alias + ".stage", query.filters.stages) : std::string{}});
}
std::string ExecutionWhere(Sql& sql, const RelationQuery& query, bool relationships = true, const std::string& edge = {}) {
    std::vector<std::string> terms{In(sql, "e.execution", query.filters.executions, true), In(sql, "e.block", query.filters.blocks, true),
        In(sql, "e.frame", query.filters.frames), In(sql, "e.pass", query.filters.passes), In(sql, "e.pipeline_id", query.filters.pipelines, true)};
    if (query.kind != RelationKind::All) terms.push_back("e.kind=" + sql.Text(query.kind == RelationKind::Draw ? "draw" : "dispatch"));
    if (query.filters.occurrence) terms.push_back("e.occurrence=" + sql.Integer(*query.filters.occurrence));
    if (relationships && (!query.filters.shaders.empty() || !query.filters.stages.empty())) {
        if (edge.empty()) terms.push_back("EXISTS(SELECT 1 FROM pipeline_stages f WHERE f.pipeline_id=e.pipeline_id AND " + EdgeWhere(sql, query, "f") + ')');
        else terms.push_back(EdgeWhere(sql, query, edge));
    }
    return And(terms);
}
bool ExecutionScope(const RelationQuery& query) {
    return query.kind != RelationKind::All || !query.filters.executions.empty() || !query.filters.blocks.empty() ||
        !query.filters.frames.empty() || !query.filters.passes.empty() || query.filters.occurrence.has_value();
}
std::string EntityWhere(Sql& sql, const RelationQuery& query, bool relationships = true) {
    if (query.entity == RelationEntity::Executions) return ExecutionWhere(sql, query, relationships);
    if (query.entity == RelationEntity::Pipelines) {
        std::vector<std::string> terms{In(sql, "p.id", query.filters.pipelines, true)};
        if (ExecutionScope(query)) terms.push_back("EXISTS(SELECT 1 FROM executions e WHERE e.pipeline_id=p.id AND " + ExecutionWhere(sql, query, relationships) + ')');
        else if (relationships && (!query.filters.shaders.empty() || !query.filters.stages.empty()))
            terms.push_back("EXISTS(SELECT 1 FROM pipeline_stages f WHERE f.pipeline_id=p.id AND " + EdgeWhere(sql, query, "f") + ')');
        return And(terms);
    }
    if (query.entity == RelationEntity::Shaders) {
        std::vector<std::string> terms{relationships ? In(sql, "s.id", query.filters.shaders, true) : std::string{}};
        if (ExecutionScope(query)) terms.push_back("EXISTS(SELECT 1 FROM executions e JOIN pipeline_stages u ON u.pipeline_id=e.pipeline_id AND u.module_id=s.id WHERE " + ExecutionWhere(sql, query, relationships, "u") + ')');
        else if (!query.filters.pipelines.empty() || (relationships && !query.filters.stages.empty()))
            terms.push_back("EXISTS(SELECT 1 FROM pipeline_stages u WHERE u.module_id=s.id AND " + And({In(sql, "u.pipeline_id", query.filters.pipelines, true), relationships ? TextIn(sql, "u.stage", query.filters.stages) : std::string{}}) + ')');
        return And(terms);
    }
    std::vector<std::string> terms{In(sql, "p.frame", query.filters.frames), In(sql, "p.pass", query.filters.passes)};
    if (ExecutionScope(query) || !query.filters.pipelines.empty() || (relationships && (!query.filters.shaders.empty() || !query.filters.stages.empty())))
        terms.push_back("EXISTS(SELECT 1 FROM executions e WHERE e.frame=p.frame AND e.pass=p.pass AND " + ExecutionWhere(sql, query, relationships) + ')');
    return And(terms);
}
std::string CountExpression(Sql& sql, const RelationQuery& query, const char* kind = nullptr) {
    std::string from = "executions e", relation;
    if (query.entity == RelationEntity::Pipelines) relation = "e.pipeline_id=p.id";
    else if (query.entity == RelationEntity::Shaders) { from += " JOIN pipeline_stages u ON u.pipeline_id=e.pipeline_id AND u.module_id=s.id"; relation = "1"; }
    else relation = "e.frame=p.frame AND e.pass=p.pass";
    auto where = ExecutionWhere(sql, query, true, query.entity == RelationEntity::Shaders ? "u" : "");
    if (kind) where += " AND e.kind=" + sql.Text(kind);
    return "(SELECT COUNT(DISTINCT e.execution) FROM " + from + " WHERE " + relation + " AND " + where + ')';
}
Json JsonText(const Statement& statement, int column) { try { return Json::parse(statement.TextColumn(column)); } catch (...) { Data("Stored relation detail is malformed JSON"); } }
Json Rasterization(const Json& creation, const Json& dynamic) {
    const Json empty = Json::object(); const auto& info = creation.is_object() ? creation : empty;
    Json calls = Json::object();
    const auto recorded = dynamic.is_object() ? dynamic.value("calls", Json::object()) : Json::object();
    if (recorded.is_object()) for (const auto& [name, args] : recorded.items()) {
        if (name.find("Raster") != std::string::npos || name.find("Cull") != std::string::npos || name.find("FrontFace") != std::string::npos ||
            name.find("Polygon") != std::string::npos || name.find("Depth") != std::string::npos || name.find("Line") != std::string::npos ||
            name.find("Viewport") != std::string::npos || name.find("Scissor") != std::string::npos || name.find("Conservative") != std::string::npos)
            calls[name] = args;
    }
    return {{"status", info.contains("pRasterizationState") ? "recorded_components" : "unavailable_static_component"},
        {"static", info.value("pRasterizationState", Json{})}, {"dynamic_declared", info.value("pDynamicState", Json{})},
        {"dynamic_recorded", calls}, {"viewports", dynamic.is_object() ? dynamic.value("viewports", Json{}) : Json{}},
        {"scissors", dynamic.is_object() ? dynamic.value("scissors", Json{}) : Json{}}, {"effective_status", "unresolved"},
        {"reason", "Recorded static and dynamic components only; effective Vulkan rasterization state is not inferred"}};
}
struct StageReader {
    Statement read;
    bool detailed;
    std::map<uint64_t, Json> cache;
    StageReader(Database& db, bool full) : read(db, full ? "SELECT module_id,stage,entry,specialization_json FROM pipeline_stages WHERE pipeline_id=?1 ORDER BY ordinal" : "SELECT module_id,stage FROM pipeline_stages WHERE pipeline_id=?1 ORDER BY ordinal"), detailed(full) {}
    Json Get(const Json& id) {
        if (id.is_null()) return Json::array(); const auto pipeline = id.get<uint64_t>();
        const auto found = cache.find(pipeline); if (found != cache.end()) return found->second;
        read.Id(1, pipeline); Json result = Json::array();
        while (read.Step()) { Json edge{{"shader_id", read.IdColumn(0)}, {"stage", read.TextColumn(1)}}; if (detailed) { edge["entry_point"] = read.TextColumn(2); edge["specialization"] = JsonText(read, 3); } result.push_back(std::move(edge)); }
        read.Reset(); cache[pipeline] = result; return result;
    }
};
Json Coverage(Database& db, const RelationQuery& query, const Json& metadata) {
    const auto saved = metadata.value("coverage", Json::object());
    const bool global = query.kind == RelationKind::All && query.filters.executions.empty() && query.filters.blocks.empty() &&
        query.filters.frames.empty() && query.filters.passes.empty() && query.filters.pipelines.empty() && !query.filters.occurrence;
    uint64_t candidates = 0, known = 0, unresolved = 0;
    if (global && saved.contains("known_relationship_candidates") && saved.contains("unresolved_candidate_executions")) {
        candidates = metadata.at("counts").at("executions"); known = saved.at("known_relationship_candidates"); unresolved = saved.at("unresolved_candidate_executions");
    } else {
    Sql parameters; const auto where = ExecutionWhere(parameters, query, false);
    Statement count(db, "SELECT COUNT(*),COALESCE(SUM(CASE WHEN e.pipeline_id IS NOT NULL AND EXISTS(SELECT 1 FROM pipeline_stages k WHERE k.pipeline_id=e.pipeline_id AND k.module_id IS NOT NULL) THEN 1 ELSE 0 END),0),COALESCE(SUM(CASE WHEN e.relation_status!='resolved' OR e.valid=0 THEN 1 ELSE 0 END),0) FROM executions e WHERE " + where);
    parameters.Bind(count); if (!count.Step()) Data("Missing relation coverage result");
    candidates = count.CountColumn(0); known = count.CountColumn(1); unresolved = count.CountColumn(2);
    }
    Json result{{"candidate_executions", candidates}, {"known_relationship_candidates", known}, {"unresolved_candidate_executions", unresolved},
        {"candidate_scope", "kind and non-shader filters, before shader/stage matching"}, {"shader_filter_applied", !query.filters.shaders.empty()},
        {"stage_filter_applied", !query.filters.stages.empty()}, {"negative_answer_complete", unresolved == 0},
        {"shader_use_semantics", "captured_pipeline_stage_reference; does not establish instruction execution or visibility"}};
    if ((query.entity == RelationEntity::Pipelines || query.entity == RelationEntity::Shaders) && (!query.filters.shaders.empty() || !query.filters.stages.empty())) {
        auto candidateQuery = query; candidateQuery.entity = RelationEntity::Pipelines;
        Sql parameters; Statement pipelineCoverage(db, "SELECT COUNT(*),COALESCE(SUM(CASE WHEN p.relation_status!='resolved' THEN 1 ELSE 0 END),0) FROM pipelines p WHERE " + EntityWhere(parameters, candidateQuery, false));
        parameters.Bind(pipelineCoverage); if (!pipelineCoverage.Step()) Data("Missing candidate pipeline coverage");
        result["candidate_pipelines"] = pipelineCoverage.CountColumn(0);
        result["unresolved_candidate_pipelines"] = pipelineCoverage.CountColumn(1);
        result["negative_answer_complete"] = unresolved == 0 && pipelineCoverage.CountColumn(1) == 0;
    }
    return result;
}
Json Source(const RelationIndexRef& index, const Json& metadata) {
    return {{"snapshot", metadata.at("snapshot")}, {"index", index.path.u8string()}, {"source_path", metadata.value("source_path", Json{})},
        {"source_sha256", metadata.value("source_sha256", Json{})}, {"decoded_path", metadata.value("jsonl_path", Json{})},
        {"decoded_sha256", metadata.value("decoded_sha256", Json{})}, {"identity_semantics", "as_indexed_snapshot"},
        {"external_evidence", "references only; availability is not required for snapshot queries"}};
}
std::string Table(RelationEntity entity) { switch (entity) { case RelationEntity::Executions: return "executions e"; case RelationEntity::Pipelines: return "pipelines p"; case RelationEntity::Shaders: return "shaders s"; case RelationEntity::Passes: return "passes p"; } return {}; }
Json ScalarKey(const Statement& statement, RelationEntity entity) {
    if (entity == RelationEntity::Passes) return Json::array({statement.CountColumn(0), statement.CountColumn(1)});
    return Json::array({statement.IdColumn(0)});
}
void ValidateLast(const Json& last, const std::vector<bool>& ids, bool nullable = false) {
    if (!last.is_array() || last.size() != ids.size()) Usage("invalid_cursor", "Cursor key does not match entity ordering");
    for (size_t i = 0; i < ids.size(); ++i) {
        if (nullable && last[i].is_null()) continue;
        if ((!last[i].is_number_unsigned() && !last[i].is_number_integer()) || (last[i].is_number_integer() && !last[i].is_number_unsigned() && last[i].get<int64_t>() < 0)) Usage("invalid_cursor", "Invalid cursor key type");
        if (!ids[i] && last[i].get<uint64_t>() > uint64_t(INT64_MAX)) Usage("invalid_cursor", "Cursor scalar key overflows");
    }
}
std::string After(Sql& sql, const std::vector<std::string>& columns, const Json& last, const std::vector<bool>& ids, bool nullable = false) {
    if (last.is_null()) return {}; ValidateLast(last, ids, nullable);
    if (columns.size() == 1) {
        if (last[0].is_null()) return columns[0] + " IS NOT NULL";
        return columns[0] + '>' + (ids[0] ? sql.Id(last[0]) : sql.Integer(last[0]));
    }
    return '(' + columns[0] + '>' + sql.Integer(last[0]) + " OR (" + columns[0] + '=' + sql.Integer(last[0]) + " AND " + columns[1] + '>' + sql.Integer(last[1]) + "))";
}
}

RelationEntity ParseRelationEntity(const std::string& name) {
    const auto text = Lower(name);
    if (text == "executions" || text == "execution" || text == "draws" || text == "draw" || text == "stats") return RelationEntity::Executions;
    if (text == "pipelines" || text == "pipeline") return RelationEntity::Pipelines;
    if (text == "shaders" || text == "shader") return RelationEntity::Shaders;
    if (text == "passes" || text == "pass") return RelationEntity::Passes;
    Usage("invalid_entity", "Unknown relation entity: " + name);
}
std::string RelationEntityName(RelationEntity entity) {
    switch (entity) { case RelationEntity::Executions: return "executions"; case RelationEntity::Pipelines: return "pipelines"; case RelationEntity::Shaders: return "shaders"; case RelationEntity::Passes: return "passes"; }
    Usage("invalid_entity", "Invalid relation entity");
}
std::string NormalizeRelationStage(const std::string& stage) {
    auto text = Lower(stage);
    if (text.rfind("vk_shader_stage_", 0) == 0) text.erase(0, 16);
    for (const auto* suffix : {"_bit_ext", "_bit_khr", "_bit_nv", "_bit"}) if (text.size() >= std::char_traits<char>::length(suffix) && text.compare(text.size() - std::char_traits<char>::length(suffix), std::char_traits<char>::length(suffix), suffix) == 0) { text.erase(text.size() - std::char_traits<char>::length(suffix)); break; }
    std::replace(text.begin(), text.end(), '_', '-');
    static const std::map<std::string, std::string> names{{"vertex","vertex"},{"vert","vertex"},{"vs","vertex"},{"fragment","fragment"},{"frag","fragment"},{"fs","fragment"},{"pixel","fragment"},{"ps","fragment"},
        {"compute","compute"},{"comp","compute"},{"cs","compute"},{"geometry","geometry"},{"geom","geometry"},{"gs","geometry"},
        {"tessellation-control","tess-control"},{"tess-control","tess-control"},{"tesc","tess-control"},{"hs","tess-control"},
        {"tessellation-evaluation","tess-eval"},{"tess-eval","tess-eval"},{"tese","tess-eval"},{"ds","tess-eval"},
        {"task","task"},{"mesh","mesh"},{"raygen","raygen"},{"ray-generation","raygen"},{"rgen","raygen"},
        {"any-hit","any-hit"},{"anyhit","any-hit"},{"rahit","any-hit"},{"closest-hit","closest-hit"},{"closesthit","closest-hit"},{"rchit","closest-hit"},
        {"miss","miss"},{"rmiss","miss"},{"intersection","intersection"},{"rint","intersection"},{"callable","callable"},{"rcall","callable"}};
    const auto found = names.find(text); if (found != names.end()) return found->second;
    Usage("invalid_stage", "Unknown shader stage: " + stage);
}
std::vector<RelationField> RelationFields(RelationEntity entity) {
    if (entity == RelationEntity::Executions) return {
        Field("execution","uint64","Global capture-scoped submitted execution index",true,true), Field("block","uint64","Recorded API block",true,true), Field("occurrence","uint32","Submitted occurrence of this API block",false,true),
        Field("frame","uint32","Capture-relative one-based frame",true,true), Field("pass","uint32","Zero-based pass within frame",true,true), Field("pipeline_id","uint64","Captured bound pipeline",true,true,false,true),
        Field("command","string","Recorded draw or dispatch API name",true), Field("kind","string","draw, dispatch or unknown",true,true),
        Field("vertex_count","uint64","Recorded direct count; null if not applicable/unavailable",true,false,false,true), Field("index_count","uint64","Recorded direct index count; never inferred for indirect work",true,false,false,true),
        Field("instance_count","uint64","Recorded direct instance count; never inferred for indirect work",true,false,false,true), Field("shaders","array","Captured stage/module references; --section adds entry/specialization",true,false,true),
        Field("unavailable","array","Reasons static counts or relationships are unavailable",true), Field("valid","boolean","Submitted selector validity"), Field("relation_status","string","resolved, partial or unresolved"),
        Field("arguments","object","Recorded API arguments, without replay readback",false,false,true), Field("rasterization","object","Recorded static/dynamic components; effective state unresolved",false,false,true),
        Field("resource_refs","object","Compact recorded resource references; no bytes",false,false,true), Field("provenance","object","Submission, recording, invocation and logical evidence references",false,false,true)};
    if (entity == RelationEntity::Pipelines) return {
        Field("pipeline_id","uint64","Captured pipeline identity",true,true), Field("kind","string","graphics, compute or unknown",true), Field("name","string","Captured debug name",true),
        Field("execution_count","uint64","Distinct matching submitted executions",true), Field("draw_count","uint64","Matching graphics draws",true), Field("dispatch_count","uint64","Matching dispatches",true),
        Field("relation_status","string","Stage relationship completeness",true), Field("shader_count","uint64","Distinct known modules"),
        Field("shaders","array","All recorded stage/module/entry/specialization edges",false,false,true), Field("create_info","object","Recorded pipeline creation metadata",false,false,true,true),
        Field("rasterization","object","Recorded static components and declared dynamic state",false,false,true), Field("provenance","object","Logical pipeline evidence references",false,false,true)};
    if (entity == RelationEntity::Shaders) return {
        Field("shader_id","uint64","Capture-scoped shader module identity",true,true), Field("name","string","Captured shader debug name",true), Field("execution_count","uint64","Distinct matching submitted executions",true),
        Field("draw_count","uint64","Matching graphics draws",true), Field("dispatch_count","uint64","Matching dispatches",true), Field("stages","array","Known stage uses within the selected scope",true),
        Field("pipeline_count","uint64","Distinct known pipelines within the selected scope"), Field("provenance","object","Logical shader evidence references; never shader text",false,false,true)};
    if (entity == RelationEntity::Passes) return {
        Field("frame","uint32","Capture-relative one-based frame",true,true), Field("pass","uint32","Frame-scoped zero-based pass ordinal",true,true), Field("kind","string","Pass grouping kind",true),
        Field("execution_count","uint64","Distinct matching submitted executions",true), Field("draw_count","uint64","Matching graphics draws",true), Field("dispatch_count","uint64","Matching dispatches",true),
        Field("submit","uint64","Capture submission block"), Field("recording_begin","uint64","Recording begin block"), Field("provenance","object","Pass scope and logical evidence references",false,false,true)};
    Usage("invalid_entity", "Invalid relation entity");
}
Json RelationFieldCatalog(RelationEntity entity) {
    Json fields = Json::array(), defaults = Json::array(), sections = Json::array();
    for (const auto& field : RelationFields(entity)) { fields.push_back(FieldJson(field)); if (field.defaultField) defaults.push_back(field.name); if (field.section) sections.push_back(field.name); }
    Json aggregateFields = Json::array();
    if (entity == RelationEntity::Executions) for (const auto& field : CountFields(RelationQuery{})) aggregateFields.push_back(FieldJson(field));
    return {{"schema", kRelationQuerySchema}, {"entity", RelationEntityName(entity)}, {"fields", fields}, {"default_columns", defaults}, {"sections", sections},
        {"filters", {{"execution","uint64 OR list"},{"block","uint64 OR list"},{"frame","one-based uint32 OR list"},{"pass","zero-based OR list; requires one frame"},{"pipeline_id","uint64 OR list"},{"shader_id","known module OR list"},{"stage","same stage edge as module filter"},{"kind","draw|dispatch|all"},{"occurrence","uint32 with one block"}}},
        {"operators", {"equal", "same-field OR", "cross-field AND"}},
        {"group_by", entity == RelationEntity::Executions ? Json{"frame", "frame,pass", "pipeline_id", "shader_id"} : Json::array()},
        {"aggregate_fields", aggregateFields},
        {"page", {{"default_limit",100},{"maximum_limit",1000},{"order","ascending entity unique key"}}},
        {"relationship_semantics","Captured pipeline-stage references, not instruction execution or visibility"}};
}
void ValidateRelationQuery(const RelationQuery& original) {
    RelationEntityName(original.entity);
    if (original.kind != RelationKind::All && original.kind != RelationKind::Draw && original.kind != RelationKind::Dispatch) Usage("invalid_kind", "Unknown relation command kind");
    if (!original.limit || original.limit > 1000) Usage("invalid_limit", "Relation page limit must be 1..1000");
    if (original.filters.frames.end() != std::find(original.filters.frames.begin(), original.filters.frames.end(), uint32_t(0))) Usage("invalid_frame", "Frames are capture-relative and start at 1");
    auto frames = original.filters.frames; Unique(frames);
    if (!original.filters.passes.empty() && frames.size() != 1) Usage("ambiguous_pass", "A pass filter requires one explicit frame");
    auto blocks = original.filters.blocks; Unique(blocks);
    if (original.filters.occurrence && blocks.size() != 1) Usage("ambiguous_occurrence", "Occurrence requires one block filter");
    if (!original.groupBy.empty()) {
        auto raw = original.groupBy; for (auto& group : raw) { if (group == "pipeline") group = "pipeline_id"; if (group == "shader") group = "shader_id"; } Unique(raw);
        const bool frameGroups = std::all_of(raw.begin(), raw.end(), [](const auto& key) { return key == "frame" || key == "pass"; });
        if (original.entity != RelationEntity::Executions || (!frameGroups && !(raw.size() == 1 && (raw.front() == "pipeline_id" || raw.front() == "shader_id")))) Usage("invalid_group", "Group by frame/pass, pipeline_id or shader_id on executions");
        if (!original.sections.empty()) Usage("invalid_section", "Structured sections cannot be attached to grouped counts");
    }
    auto query = Normalize(original);
    auto fields = query.groupBy.empty() ? RelationFields(query.entity) : CountFields(query);
    for (const auto& name : query.columns) {
        const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& field) { return field.name == name; });
        if (found == fields.end() || (found->section && !found->defaultField)) Usage("unknown_field", "Unknown/structured column '" + name + "'; use the field catalog or --section");
    }
    for (const auto& name : query.sections) {
        const auto found = std::find_if(fields.begin(), fields.end(), [&](const auto& field) { return field.name == name && field.section; });
        if (found == fields.end()) Usage("unknown_section", "Unknown section: " + name);
    }
    const auto filterCount = query.filters.executions.size() + query.filters.blocks.size() + query.filters.frames.size() + query.filters.passes.size() + query.filters.pipelines.size() + query.filters.shaders.size() + query.filters.stages.size();
    if (filterCount > 4000) Usage("too_many_filters", "Relation filters exceed the supported bound");
    if (!query.cursor.empty()) {
        const auto value = Cursor(query.cursor);
        const bool pair = query.groupBy == std::vector<std::string>{"frame","pass"} || (query.groupBy.empty() && query.entity == RelationEntity::Passes);
        const bool scalar = !query.groupBy.empty() && query.groupBy == std::vector<std::string>{"frame"};
        ValidateLast(value.at("last"), pair ? std::vector<bool>{false,false} : std::vector<bool>{!scalar}, query.groupBy == std::vector<std::string>{"pipeline_id"});
    }
}

RelationQueryResult QueryRelations(const RelationIndexRef& index, const RelationQuery& requested, const std::atomic_bool* cancel) {
    ValidateRelationQuery(requested); Cancelled(cancel); const auto query = Normalize(requested);
    Database db(index.path, false, cancel); ValidateDatabase(db); const auto metadata = ReadMetadata(db);
    const auto snapshot = metadata.at("snapshot").get<std::string>();
    if (!index.snapshot.empty() && index.snapshot != snapshot) throw RelationError(RelationErrorKind::Data, "snapshot_changed", "Relation index generation differs from the opened snapshot");
    Json last;
    if (!query.cursor.empty()) {
        const auto cursor = Cursor(query.cursor);
        if (cursor.at("snapshot") != snapshot || cursor.at("query") != Fingerprint(query)) Usage("cursor_mismatch", "Cursor belongs to another snapshot or query/projection/order");
        last = cursor.at("last");
    }
    RelationQueryResult result; result.entity = query.groupBy.empty() ? RelationEntityName(query.entity) : "stats";
    result.source = Source(index, metadata); result.coverage = Coverage(db, query, metadata);
    if (Contains(query.sections,"provenance") || Contains(query.sections,"resource_refs")) {
        std::error_code sourceError, decodedError;
        const auto sourcePath = metadata.value("source_path", std::string{}), decodedPath = metadata.value("jsonl_path", std::string{});
        result.source["evidence_availability"] = {{"source_file", !sourcePath.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(sourcePath),sourceError) ? "present" : "missing_or_unavailable"},
            {"decoded_file", !decodedPath.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(decodedPath),decodedError) ? "present" : "missing_or_unavailable"},
            {"logical_dump_paths", "not_generated"}};
    }
    const auto projection = Projection(query); const auto definitions = query.groupBy.empty() ? RelationFields(query.entity) : CountFields(query);
    for (const auto& name : projection) { const auto found = std::find_if(definitions.begin(), definitions.end(), [&](const auto& field) { return field.name == name; }); result.fields.push_back(FieldJson(*found)); }
    Sql parameters; std::string sql, fullCount;
    Json key;
    if (!query.groupBy.empty()) {
        const bool shader = query.groupBy == std::vector<std::string>{"shader_id"};
        const bool pipeline = query.groupBy == std::vector<std::string>{"pipeline_id"};
        std::vector<std::string> keys; std::vector<bool> ids;
        if (shader) { keys = {"g.module_id"}; ids = {true}; }
        else if (pipeline) { keys = {"e.pipeline_id"}; ids = {true}; }
        else { keys = {"e.frame"}; ids = {false}; if (query.groupBy.size() == 2) { keys.push_back("e.pass"); ids.push_back(false); } }
        const auto from = std::string("executions e") + (shader ? " JOIN pipeline_stages g ON g.pipeline_id=e.pipeline_id AND g.module_id IS NOT NULL" : "");
        const auto where = ExecutionWhere(parameters, query, true, shader ? "g" : "");
        const auto select = keys.size() == 2 ? keys[0] + ',' + keys[1] : keys[0];
        const std::string counts = ",COUNT(DISTINCT e.execution),COUNT(DISTINCT CASE WHEN e.kind='draw' THEN e.execution END),COUNT(DISTINCT CASE WHEN e.kind='dispatch' THEN e.execution END),COUNT(DISTINCT CASE WHEN e.kind='unknown' THEN e.execution END)";
        const auto base = " FROM " + from + " WHERE " + where;
        fullCount = "SELECT COUNT(*) FROM (SELECT " + select + base + " GROUP BY " + select + ')';
        if (query.includeCount) { Statement total(db, fullCount); parameters.Bind(total); if (!total.Step()) Data("Missing grouped count"); result.total = total.CountColumn(0); }
        const auto after = After(parameters, keys, last, ids, pipeline);
        sql = "SELECT " + select + counts + base + (after.empty() ? "" : " AND " + after) + " GROUP BY " + select + " ORDER BY " + select + " LIMIT " + parameters.Integer(int64_t(query.limit) + 1);
        Statement rows(db, sql); parameters.Bind(rows);
        while (rows.Step()) {
            Cancelled(cancel); if (result.rows.size() == query.limit) { result.hasMore = true; break; }
            Json row = Json::object(); key = Json::array();
            for (size_t i = 0; i < keys.size(); ++i) { const auto value = ids[i] ? rows.IdColumn(int(i)) : Json(rows.CountColumn(int(i))); row[query.groupBy[i]] = value; key.push_back(value); }
            const char* names[]{"execution_count","draw_count","dispatch_count","unknown_count"};
            for (int i = 0; i < 4; ++i) row[names[i]] = rows.CountColumn(int(keys.size()) + i);
            Json projected = Json::object(); for (const auto& field : projection) projected[field] = row.at(field); result.rows.push_back(std::move(projected));
        }
        result.coverage["shader_groups_overlap"] = shader;
        if (shader) result.coverage["group_count_semantics"] = "Distinct executions per known shader; groups may overlap and are not additive";
    } else {
        const auto where = EntityWhere(parameters, query);
        if (query.includeCount) { Statement total(db, "SELECT COUNT(*) FROM " + Table(query.entity) + " WHERE " + where); parameters.Bind(total); if (!total.Step()) Data("Missing relation count"); result.total = total.CountColumn(0); }
        std::vector<std::string> keys = query.entity == RelationEntity::Executions ? std::vector<std::string>{"e.execution"} : query.entity == RelationEntity::Shaders ? std::vector<std::string>{"s.id"} : query.entity == RelationEntity::Pipelines ? std::vector<std::string>{"p.id"} : std::vector<std::string>{"p.frame","p.pass"};
        const auto after = After(parameters, keys, last, query.entity == RelationEntity::Passes ? std::vector<bool>{false,false} : std::vector<bool>{true});
        std::vector<std::string> select; std::vector<std::pair<std::string,int>> columns;
        auto add = [&](const std::string& name, const std::string& expression) { columns.push_back({name, int(select.size())}); select.push_back(expression); };
        for (size_t i = 0; i < keys.size(); ++i) add(i ? "_key2" : "_key", keys[i]);
        const bool raster = Contains(projection, "rasterization"), fullStages = Contains(query.sections, "shaders") || query.entity == RelationEntity::Pipelines;
        std::map<std::string,std::string> expressions;
        if (query.entity == RelationEntity::Executions) {
            for (const auto* field : {"execution","block","occurrence","frame","pass","pipeline_id","command","kind","vertex_count","index_count","instance_count","valid","relation_status","unavailable"}) expressions[field] = std::string("e.") + field;
            expressions["arguments"] = "e.arguments_json"; expressions["resource_refs"] = "e.resources_json"; expressions["provenance"] = "e.source_json";
            if (raster) { add("_creation", "(SELECT p.create_json FROM pipelines p WHERE p.id=e.pipeline_id)"); add("_dynamic", "e.dynamic_json"); }
            if (Contains(projection,"shaders")) add("_pipeline", "e.pipeline_id");
        } else if (query.entity == RelationEntity::Pipelines) {
            expressions = {{"pipeline_id","p.id"},{"kind","p.kind"},{"name","p.name"},{"relation_status","p.relation_status"},{"create_info","p.create_json"},
                {"shader_count","(SELECT COUNT(DISTINCT module_id) FROM pipeline_stages WHERE pipeline_id=p.id)"}};
            if (raster) add("_creation", "p.create_json");
            if (Contains(projection,"shaders")) add("_pipeline", "p.id");
        } else if (query.entity == RelationEntity::Shaders) expressions = {{"shader_id","s.id"},{"name","s.name"}};
        else {
            expressions = {{"frame","p.frame"},{"pass","p.pass"},{"kind","p.kind"},{"submit","p.submit"},{"recording_begin","p.recording_begin"},{"provenance","p.source_json"}};
            if (Contains(projection,"provenance")) add("_pass_scope","p.scope_json");
        }
        if (query.entity != RelationEntity::Executions) for (const auto* count : {"execution_count","draw_count","dispatch_count"})
            if (Contains(projection,count)) expressions[count] = CountExpression(parameters,query, std::string(count)=="draw_count"?"draw":std::string(count)=="dispatch_count"?"dispatch":nullptr);
        for (const auto& field : projection) { const auto found = expressions.find(field); if (found != expressions.end()) add(field, found->second); }
        std::string selected; for (const auto& expression : select) { if (!selected.empty()) selected += ','; selected += expression; }
        sql = "SELECT " + selected + " FROM " + Table(query.entity) + " WHERE " + where + (after.empty() ? "" : " AND " + after) + " ORDER BY " + (keys.size()==2?keys[0]+','+keys[1]:keys[0]) + " LIMIT " + parameters.Integer(int64_t(query.limit)+1);
        Statement rows(db,sql); parameters.Bind(rows); StageReader stages(db,fullStages);
        while (rows.Step()) {
            Cancelled(cancel); if (result.rows.size()==query.limit) { result.hasMore=true; break; }
            key=ScalarKey(rows,query.entity); Json row=Json::object(), hidden=Json::object();
            for (const auto& [field,column] : columns) {
                if (field=="_key" || field=="_key2") continue;
                if (field=="_creation") { hidden[field]=sqlite3_column_type(rows.handle,column)==SQLITE_NULL?Json{}:JsonText(rows,column); continue; }
                if (field=="_dynamic") { hidden[field]=JsonText(rows,column); continue; }
                if (field=="_pipeline") { hidden[field]=rows.IdColumn(column); continue; }
                if (field=="_pass_scope") { hidden[field]=JsonText(rows,column); continue; }
                if (field=="execution" || field=="block" || field=="pipeline_id" || field=="shader_id" || field=="vertex_count" || field=="index_count" || field=="instance_count" || field=="submit" || field=="recording_begin") row[field]=rows.IdColumn(column);
                else if (field=="frame" || field=="pass" || field=="occurrence" || field.find("_count")!=std::string::npos) row[field]=rows.CountColumn(column);
                else if (field=="valid") row[field]=rows.CountColumn(column)!=0;
                else if (field=="unavailable" || field=="arguments" || field=="resource_refs" || field=="provenance" || field=="create_info") row[field]=JsonText(rows,column);
                else row[field]=rows.TextColumn(column);
            }
            if (Contains(projection,"shaders")) row["shaders"]=stages.Get(hidden.at("_pipeline"));
            if (raster) row["rasterization"]=Rasterization(hidden.value("_creation",Json{}),hidden.value("_dynamic",Json::object()));
            if (query.entity==RelationEntity::Passes && Contains(projection,"provenance")) row["provenance"]["scope"]=hidden.at("_pass_scope");
            if (query.entity==RelationEntity::Shaders && (Contains(projection,"stages") || Contains(projection,"pipeline_count"))) {
                const auto shader=rows.IdColumn(0); Sql scope;
                std::vector<std::string> terms{"u.module_id="+scope.Id(shader),In(scope,"u.pipeline_id",query.filters.pipelines,true),TextIn(scope,"u.stage",query.filters.stages)};
                if (ExecutionScope(query)) terms.push_back("EXISTS(SELECT 1 FROM executions e WHERE e.pipeline_id=u.pipeline_id AND "+ExecutionWhere(scope,query,true,"u")+')');
                Statement edge(db,"SELECT DISTINCT u.pipeline_id,u.stage FROM pipeline_stages u WHERE "+And(terms)+" ORDER BY u.pipeline_id,u.stage"); scope.Bind(edge);
                std::set<std::string> names; std::set<uint64_t> pipelines;
                while(edge.Step()){pipelines.insert(edge.IdColumn(0).get<uint64_t>());names.insert(edge.TextColumn(1));}
                if(Contains(projection,"stages"))row["stages"]=names;
                if(Contains(projection,"pipeline_count"))row["pipeline_count"]=pipelines.size();
            }
            if (Contains(projection,"provenance") && query.entity!=RelationEntity::Executions && query.entity!=RelationEntity::Passes) {
                const auto id=rows.IdColumn(0).get<uint64_t>(); const auto path=(query.entity==RelationEntity::Pipelines?"pipelines/":"shaders/")+std::to_string(id)+(query.entity==RelationEntity::Pipelines?".json":"/module.spv");
                row["provenance"]={{"logical_dump_path",path},{"availability","not_generated"},{"snapshot",snapshot}};
            }
            Json projected=Json::object();for(const auto& field:projection)projected[field]=row.at(field);result.rows.push_back(std::move(projected));
        }
    }
    if(result.hasMore) result.nextCursor=NextCursor(snapshot,query,key);
    return result;
}
Json RelationQueryResult::ToJson() const {
    Json page{{"returned",rows.size()},{"has_more",hasMore},{"next_cursor",nextCursor.empty()?Json(nullptr):Json(nextCursor)}};
    if(total)page["total"]=*total;
    return {{"schema",kRelationQuerySchema},{"entity",entity},{"source",source},{"fields",fields},{"rows",rows},{"page",page},{"coverage",coverage}};
}
}
