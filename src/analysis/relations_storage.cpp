#include "analysis/relations_internal.h"
#include "analysis/export.h"
#include "analysis/index.h"
#include "core/artifacts.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <exception>
#include <limits>
#include <random>
#include <set>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace gfx::relation_detail {
namespace {
[[noreturn]] void Invalid(const std::string& message) {
    throw RelationError(RelationErrorKind::Data, "invalid_index", message);
}
int Progress(void* context) {
    return static_cast<const std::atomic_bool*>(context)->load() ? 1 : 0;
}
}

void SqlCheck(int code, sqlite3* db, const char* operation) {
    if (code == SQLITE_OK || code == SQLITE_ROW || code == SQLITE_DONE) return;
    if ((code & 255) == SQLITE_INTERRUPT)
        throw RelationError(RelationErrorKind::Cancelled, "cancelled", "Relation operation cancelled");
    throw RelationError(RelationErrorKind::Data, "relation_storage", std::string(operation) + ": " +
        (db ? sqlite3_errmsg(db) : sqlite3_errstr(code)), {{"sqlite_code", code}});
}
Database::Database(const std::filesystem::path& path, bool writable, const std::atomic_bool* cancel) {
    Cancelled(cancel);
    const auto filename = path.u8string();
    const int flags = (writable ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY) |
        SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_EXRESCODE;
    const int result = sqlite3_open_v2(filename.c_str(), &handle, flags, nullptr);
    if (result != SQLITE_OK) {
        const std::string reason = handle ? sqlite3_errmsg(handle) : sqlite3_errstr(result);
        if (handle) sqlite3_close(handle);
        handle = nullptr;
        throw RelationError(RelationErrorKind::Data, "index_open", "Cannot open relation index: " + reason);
    }
    try {
        sqlite3_extended_result_codes(handle, 1);
        SqlCheck(sqlite3_db_config(handle, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, nullptr), handle, "Disable extensions");
        SqlCheck(sqlite3_db_config(handle, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr), handle, "Enable defensive storage");
        sqlite3_limit(handle, SQLITE_LIMIT_ATTACHED, 0);
        sqlite3_limit(handle, SQLITE_LIMIT_EXPR_DEPTH, 100);
        sqlite3_limit(handle, SQLITE_LIMIT_LENGTH, 128 * 1024 * 1024);
        sqlite3_busy_timeout(handle, 1000);
        if (cancel) sqlite3_progress_handler(handle, 1000, Progress, const_cast<std::atomic_bool*>(cancel));
        Exec("PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON;");
        if (!writable) Exec("PRAGMA query_only=ON;");
    } catch (...) { sqlite3_close(handle); handle = nullptr; throw; }
}
Database::~Database() { if (handle) sqlite3_close_v2(handle); }
void Database::Exec(const std::string& sql) {
    SqlCheck(sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, nullptr), handle, "Execute relation statement");
}
Statement::Statement(Database& database, const std::string& sql) : db(database.handle) {
    if (sql.size() > static_cast<size_t>(std::numeric_limits<int>::max())) Invalid("SQL statement is too large");
    const int result = sqlite3_prepare_v3(db, sql.c_str(), static_cast<int>(sql.size()), SQLITE_PREPARE_PERSISTENT, &handle, nullptr);
    if (result != SQLITE_OK) { if (handle) sqlite3_finalize(handle); handle = nullptr; SqlCheck(result, db, "Prepare relation statement"); }
}
Statement::~Statement() { if (handle) sqlite3_finalize(handle); }
void Statement::Text(int position, const std::string& value) {
    SqlCheck(sqlite3_bind_text64(handle, position, value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8), db, "Bind relation text");
}
void Statement::Id(int position, uint64_t value) {
    const auto bytes = EncodeId(value);
    SqlCheck(sqlite3_bind_blob(handle, position, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT), db, "Bind captured ID");
}
void Statement::Integer(int position, int64_t value) {
    SqlCheck(sqlite3_bind_int64(handle, position, value), db, "Bind relation integer");
}
void Statement::Null(int position) { SqlCheck(sqlite3_bind_null(handle, position), db, "Bind relation null"); }
bool Statement::Step() {
    const int result = sqlite3_step(handle); SqlCheck(result, db, "Read/write relation row"); return result == SQLITE_ROW;
}
void Statement::Reset() {
    SqlCheck(sqlite3_reset(handle), db, "Reset relation statement");
    SqlCheck(sqlite3_clear_bindings(handle), db, "Clear relation bindings");
}
std::string Statement::TextColumn(int column) const {
    if (sqlite3_column_type(handle, column) == SQLITE_NULL) return {};
    if (sqlite3_column_type(handle, column) != SQLITE_TEXT) Invalid("Relation text column has an invalid type");
    const auto* bytes = sqlite3_column_text(handle, column);
    return std::string(reinterpret_cast<const char*>(bytes), sqlite3_column_bytes(handle, column));
}
Json Statement::IdColumn(int column) const {
    if (sqlite3_column_type(handle, column) == SQLITE_NULL) return nullptr;
    if (sqlite3_column_type(handle, column) != SQLITE_BLOB) Invalid("Captured ID is not a BLOB");
    return DecodeId(sqlite3_column_blob(handle, column), sqlite3_column_bytes(handle, column));
}
uint64_t Statement::CountColumn(int column) const {
    if (sqlite3_column_type(handle, column) != SQLITE_INTEGER || sqlite3_column_int64(handle, column) < 0)
        Invalid("Relation count is negative, overflowing, or not an integer");
    return static_cast<uint64_t>(sqlite3_column_int64(handle, column));
}
std::array<unsigned char, 8> EncodeId(uint64_t id) {
    std::array<unsigned char, 8> bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) bytes[bytes.size() - i - 1] = static_cast<unsigned char>(id >> (i * 8));
    return bytes;
}
uint64_t DecodeId(const void* bytes, int length) {
    if (!bytes || length != 8) Invalid("Captured ID must contain exactly eight bytes");
    uint64_t id = 0;
    for (int i = 0; i < length; ++i) id = (id << 8) | static_cast<const unsigned char*>(bytes)[i];
    return id;
}
Json ReadMetadata(Database& db) {
    Statement read(db, "SELECT key,value FROM meta ORDER BY key"); Json metadata = Json::object();
    while (read.Step()) {
        const auto key = read.TextColumn(0), value = read.TextColumn(1);
        if (key.size() > 256 || value.size() > 1024 * 1024 || metadata.size() >= 128) Invalid("Relation metadata exceeds its bounds");
        try { metadata[key] = Json::parse(value); }
        catch (const Json::exception&) { Invalid("Relation metadata contains invalid JSON: " + key); }
    }
    return metadata;
}

namespace {
const std::map<std::string, std::vector<std::string>>& Columns() {
    static const std::map<std::string, std::vector<std::string>> columns{
        {"meta", {"key", "value"}},
        {"executions", {"execution", "block", "occurrence", "frame", "pass", "pipeline_id", "command", "kind", "vertex_count", "index_count", "instance_count", "valid", "relation_status", "unavailable", "arguments_json", "dynamic_json", "resources_json", "source_json"}},
        {"pipelines", {"id", "kind", "name", "create_json", "relation_status"}},
        {"shaders", {"id", "name"}},
        {"pipeline_stages", {"pipeline_id", "ordinal", "module_id", "stage", "entry", "specialization_json"}},
        {"passes", {"frame", "pass", "kind", "submit", "recording_begin", "scope_json", "source_json"}}
    };
    return columns;
}
void ValidateSchema(Database& db) {
    Statement app(db, "PRAGMA application_id");
    if (!app.Step() || app.CountColumn(0) != kApplicationId)
        throw RelationError(RelationErrorKind::Data, "relation_index_unowned", "Database is not an owned relation index");
    Statement version(db, "PRAGMA user_version");
    if (!version.Step()) Invalid("Relation storage version is missing");
    const auto storageVersion = version.CountColumn(0);
    if (storageVersion > kRelationStorageVersion)
        throw RelationError(RelationErrorKind::Data, "relation_index_version", "Relation index was created by a newer storage schema");
    if (storageVersion != kRelationStorageVersion)
        throw RelationError(RelationErrorKind::Data, "unsupported_index", "Unsupported relation storage schema");
    for (const auto& [table, expected] : Columns()) {
        Statement object(db, "SELECT type FROM sqlite_schema WHERE name=?1"); object.Text(1, table);
        if (!object.Step() || object.TextColumn(0) != "table") Invalid("Relation table is missing: " + table);
        Statement info(db, "PRAGMA table_info(" + table + ")"); std::vector<std::string> actual;
        while (info.Step()) actual.push_back(info.TextColumn(1));
        if (actual != expected) Invalid("Relation table columns are incompatible: " + table);
    }
}
void ValidateMetadata(const Json& metadata, bool requireComplete) {
    try {
        if (metadata.at("owner") != "gfxrecon_gui_relations")
            throw RelationError(RelationErrorKind::Data, "relation_index_unowned", "Relation index owner is incompatible");
        for (const auto& version : std::map<std::string, uint64_t>{{"schema", kRelationStorageVersion}, {"builder", kRelationBuilderVersion}, {"parser_schema", kIndexSchema}, {"grouping_version", 1}}) {
            const auto& actual = metadata.at(version.first);
            if ((actual.is_number_unsigned() || actual.is_number_integer()) && actual > version.second)
                throw RelationError(RelationErrorKind::Data, "relation_index_version", "Relation index requires a newer " + version.first);
        }
        if (metadata.at("schema") != kRelationStorageVersion || metadata.at("builder") != kRelationBuilderVersion ||
            metadata.at("parser_schema") != kIndexSchema || metadata.at("grouping_version") != 1)
            throw RelationError(RelationErrorKind::Data, "unsupported_index", "Unsupported relation builder/parser/grouping schema");
        if (!metadata.at("complete").is_boolean() || (requireComplete && metadata.at("complete") != true))
            Invalid("Relation index is incomplete");
        for (const char* key : {"source_path", "jsonl_path", "snapshot", "source_sha256", "decoded_sha256"})
            if (!metadata.at(key).is_string()) Invalid(std::string("Invalid relation metadata: ") + key);
        for (const char* key : {"snapshot", "source_sha256", "decoded_sha256"}) {
            const auto value = metadata.at(key).get<std::string>();
            if (value.size() != 64 || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
                Invalid(std::string("Invalid relation digest: ") + key);
        }
        if (!metadata.at("counts").is_object() || !metadata.at("coverage").is_object()) Invalid("Relation summary metadata is missing");
        for (const char* key : {"executions", "pipelines", "shaders", "passes", "pipeline_stages"}) {
            const auto& count = metadata.at("counts").at(key);
            if ((!count.is_number_unsigned() && !count.is_number_integer()) ||
                (count.is_number_integer() && !count.is_number_unsigned() && count.get<int64_t>() < 0)) Invalid("Invalid relation metadata count");
        }
        if (requireComplete && metadata.at("integrity") != "validated") Invalid("Relation index was not validated before completion");
    } catch (const Json::exception&) { Invalid("Required relation metadata is missing or malformed"); }
}
}
void ValidateDatabase(Database& db) {
    ValidateSchema(db);
    ValidateMetadata(ReadMetadata(db), true);
}
namespace {
void ValidateBuild(Database& db) {
    ValidateSchema(db);
    const auto metadata = ReadMetadata(db);
    ValidateMetadata(metadata, false);
    Statement integrity(db, "PRAGMA integrity_check");
    if (!integrity.Step() || integrity.TextColumn(0) != "ok" || integrity.Step()) Invalid("Relation database integrity check failed");
    Statement foreign(db, "PRAGMA foreign_key_check");
    if (foreign.Step()) Invalid("Relation database has broken foreign keys");
    for (const char* table : {"executions", "pipelines", "shaders", "passes", "pipeline_stages"}) {
        Statement count(db, std::string("SELECT COUNT(*) FROM ") + table);
        if (!count.Step() || metadata.at("counts").at(table) != count.CountColumn(0))
            Invalid(std::string("Relation metadata count differs from stored rows: ") + table);
    }
}
}
} // namespace gfx::relation_detail

namespace gfx {
namespace {
using namespace relation_detail;
namespace fs = std::filesystem;
constexpr size_t kRecordLimit = 64 * 1024 * 1024;
int64_t Scalar(size_t value) {
    if (value > static_cast<size_t>(std::numeric_limits<int64_t>::max()))
        throw RelationError(RelationErrorKind::Data, "count_overflow", "Relation row count exceeds the storage range");
    return static_cast<int64_t>(value);
}
std::optional<uint64_t> Unsigned(const Json& object, const char* key) {
    if (!object.is_object()) return {};
    const auto it = object.find(key);
    if (it == object.end()) return {};
    if (it->is_number_unsigned()) return it->get<uint64_t>();
    if (it->is_number_integer() && it->get<int64_t>() >= 0) return static_cast<uint64_t>(it->get<int64_t>());
    return {};
}
Json Value(const Json& object, const char* key, Json fallback = nullptr) {
    if (!object.is_object()) return fallback;
    const auto it = object.find(key); return it == object.end() ? fallback : *it;
}
std::string Kind(const Event& event) {
    if (event.name.rfind("vkCmdDispatch", 0) == 0) return "dispatch";
    if (event.name.rfind("vkCmdDraw", 0) == 0) return "draw";
    return "unknown";
}
Json ReadArguments(std::ifstream& input, const Event& event, const std::atomic_bool* cancel) {
    if (event.offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw RelationError(RelationErrorKind::Data, "invalid_source", "Decoded event offset exceeds the file range");
    input.clear(); input.seekg(static_cast<std::streamoff>(event.offset));
    std::string line;
    for (;;) {
        Cancelled(cancel); char chunk[16384]; input.getline(chunk, sizeof(chunk));
        const auto read = input.gcount();
        const bool delimiter = !input.fail() && !input.eof();
        const auto bytes = static_cast<size_t>(read - (delimiter ? 1 : 0));
        if (line.size() + bytes > kRecordLimit)
            throw RelationError(RelationErrorKind::Data, "invalid_source", "Decoded relation record exceeds 64 MiB");
        line.append(chunk, bytes);
        if (delimiter || input.eof()) break;
        if (input.bad() || !read)
            throw RelationError(RelationErrorKind::Data, "invalid_source", "Decoded relation record is not readable");
        input.clear(input.rdstate() & ~std::ios::failbit);
    }
    if (line.empty()) throw RelationError(RelationErrorKind::Data, "invalid_source", "Decoded relation record is empty");
    try {
        const auto record = Json::parse(line);
        const auto function = record.find("function");
        if (function == record.end() || !function->is_object())
            throw RelationError(RelationErrorKind::Data, "invalid_source", "Execution does not reference a decoded API record");
        if (function->value("name", std::string{}) != event.name)
            throw RelationError(RelationErrorKind::Data, "invalid_source", "Decoded event identity changed during relation build");
        return Value(*function, "args", Json::object());
    } catch (const Json::exception&) {
        throw RelationError(RelationErrorKind::Data, "invalid_source", "Decoded relation record contains invalid JSON");
    }
}
std::string StageName(const std::string& raw, bool& resolved) {
    try { return NormalizeRelationStage(raw); }
    catch (const RelationError& error) {
        if (error.kind != RelationErrorKind::Usage) throw;
        resolved = false; std::string stage = raw;
        std::transform(stage.begin(), stage.end(), stage.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return stage.empty() ? "unknown" : stage;
    }
}
bool LibraryFlag(const Json& flags) {
    // Vulkan-Headers vulkan_core.h: both LIBRARY bits are 0x00000800.
    constexpr uint64_t library = 0x800;
    if (flags.is_number_unsigned()) return (flags.get<uint64_t>() & library) != 0;
    if (flags.is_number_integer()) return flags.get<int64_t>() >= 0 && (flags.get<uint64_t>() & library) != 0;
    if (!flags.is_string()) return false;
    const auto text = flags.get<std::string>();
    if (text.find("VK_PIPELINE_CREATE_LIBRARY_BIT_KHR") != std::string::npos ||
        text.find("VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR") != std::string::npos) return true;
    try {
        size_t consumed = 0; const auto bits = std::stoull(text, &consumed, 0);
        return consumed == text.size() && (bits & library) != 0;
    } catch (const std::exception&) { return false; }
}
bool HasUnresolvedComposition(const Json& value, unsigned depth = 0) {
    if (depth > 64) return true;
    if (value.is_object()) {
        const auto type = Value(value, "sType");
        const bool flags2 = (type.is_string() && type.get_ref<const std::string&>().find("PIPELINE_CREATE_FLAGS_2_CREATE_INFO") != std::string::npos) ||
            (type.is_number_integer() && type == 1000470005);
        if (flags2 && LibraryFlag(Value(value, "flags"))) return true;
    }
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        for (const char* marker : {"PIPELINE_LIBRARY_CREATE_INFO", "GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO", "PIPELINE_BINARY_INFO", "SHADER_MODULE_CREATE_INFO"})
            if (text.find(marker) != std::string::npos) return true;
    } else if (value.is_structured()) for (const auto& item : value)
        if (HasUnresolvedComposition(item, depth + 1)) return true;
    return false;
}
struct PipelineRecord { std::string kind, status; bool normalized = true, knownModule = false; };
PipelineRecord DescribePipeline(const Capture& capture, uint64_t id) {
    const auto info = capture.pipelineInfo.find(id); const auto edges = capture.pipelines.find(id);
    PipelineRecord result{"unknown", "unresolved"};
    if (info != capture.pipelineInfo.end() && info->second.is_object())
        result.kind = info->second.contains("stage") ? "compute" : "graphics";
    if (edges == capture.pipelines.end() || edges->second.empty()) return result;
    bool modules = true, compute = false;
    for (const auto& edge : edges->second) {
        const auto stage = StageName(edge.stage, result.normalized); compute |= stage == "compute";
        modules &= edge.module != 0 && !edge.entry.empty();
        result.knownModule |= edge.module != 0;
    }
    if (result.kind == "unknown") result.kind = compute ? "compute" : "graphics";
    result.status = info != capture.pipelineInfo.end() && info->second.is_object() && modules && result.normalized &&
        !LibraryFlag(Value(info->second, "flags")) && !HasUnresolvedComposition(info->second) ? "resolved" : "partial";
    return result;
}
Json Source(const Event& event, const Execution& execution, const DrawLocation& location) {
    Json invocations = Json::array();
    for (const auto& v : execution.invocations)
        invocations.push_back({{"command_buffer", v.commandBuffer}, {"recording_begin", v.begin}, {"generation", v.generation}, {"execute_block", v.execute}, {"slot", v.ordinal}});
    return {{"block", event.block}, {"jsonl_offset", event.offset}, {"submit", execution.submit},
        {"recording_begin", execution.begin}, {"recording_generation", execution.generation}, {"command_buffer", event.commandBuffer},
        {"primary", execution.primary}, {"invocation_path", execution.invocationPath}, {"invocations", invocations},
        {"logical_dump_path", location.path + "/draw.json"}, {"dump_availability", "not_generated"}, {"notes", execution.notes}};
}
void ResourceHandles(const Json& value, std::set<uint64_t>& ids, size_t& visited, bool& truncated, unsigned depth = 0) {
    if (++visited > 100000 || depth > 32 || ids.size() >= 4096) { truncated = true; return; }
    if (value.is_object()) for (auto it = value.begin(); it != value.end(); ++it) {
        const auto& key = it.key();
        if (key == "buffer" || key == "image" || key == "imageView" || key == "sampler" || key == "bufferView" || key == "set" || key == "descriptorSet" || key == "framebuffer" || key == "renderPass") {
            const auto id = Unsigned(value, key.c_str()); if (id && *id) ids.insert(*id);
        } else if (it.value().is_structured()) ResourceHandles(it.value(), ids, visited, truncated, depth + 1);
        if (visited > 100000 || ids.size() >= 4096) { truncated = true; break; }
    } else if (value.is_array()) for (const auto& child : value) {
        ResourceHandles(child, ids, visited, truncated, depth + 1);
        if (visited > 100000 || ids.size() >= 4096) { truncated = true; break; }
    }
}
Json ResourceRefs(const Capture& capture, const Event& event, const Execution& execution) {
    std::set<uint64_t> ids; bool truncated = false; size_t visited = 0;
    for (auto id : event.objects) { if (id) ids.insert(id); if (ids.size() >= 4096) { truncated = true; break; } }
    if (event.state) for (const char* key : {"vertexBuffers", "indexBuffer", "descriptorSets"})
        ResourceHandles(Value(*event.state, key), ids, visited, truncated);
    ResourceHandles(execution.descriptorValues, ids, visited, truncated);
    ResourceHandles(execution.renderTargets, ids, visited, truncated);
    Json refs = Json::array();
    for (auto id : ids) {
        Json ref{{"id", id}}; const auto found = capture.objects.find(id);
        if (found != capture.objects.end()) { ref["type"] = found->second.type; ref["name"] = found->second.name; ref["availability"] = "recorded_object"; }
        else ref["availability"] = "recorded_handle_only";
        refs.push_back(std::move(ref));
    }
    return {{"status", "recorded_refs"}, {"objects", refs}, {"availability", "metadata_only"},
        {"descriptor_values", "not_materialized"}, {"truncated", truncated}};
}
void CreateSchema(Database& db) {
    db.Exec("PRAGMA application_id=1196574289; PRAGMA user_version=1; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;");
    db.Exec(R"SQL(
CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL) WITHOUT ROWID;
CREATE TABLE pipelines(id BLOB PRIMARY KEY CHECK(typeof(id)='blob' AND length(id)=8),kind TEXT NOT NULL CHECK(kind IN ('graphics','compute','unknown')),name TEXT NOT NULL,create_json TEXT NOT NULL,relation_status TEXT NOT NULL CHECK(relation_status IN ('resolved','partial','unresolved'))) WITHOUT ROWID;
CREATE TABLE shaders(id BLOB PRIMARY KEY CHECK(typeof(id)='blob' AND length(id)=8),name TEXT NOT NULL) WITHOUT ROWID;
CREATE TABLE pipeline_stages(pipeline_id BLOB NOT NULL REFERENCES pipelines(id),ordinal INTEGER NOT NULL CHECK(ordinal>=0),module_id BLOB REFERENCES shaders(id),stage TEXT NOT NULL,entry TEXT NOT NULL,specialization_json TEXT NOT NULL,PRIMARY KEY(pipeline_id,ordinal),CHECK(module_id IS NULL OR (typeof(module_id)='blob' AND length(module_id)=8))) WITHOUT ROWID;
CREATE TABLE passes(frame INTEGER NOT NULL CHECK(frame>=1),pass INTEGER NOT NULL CHECK(pass>=0),kind TEXT NOT NULL,submit BLOB NOT NULL CHECK(typeof(submit)='blob' AND length(submit)=8),recording_begin BLOB NOT NULL CHECK(typeof(recording_begin)='blob' AND length(recording_begin)=8),scope_json TEXT NOT NULL,source_json TEXT NOT NULL,PRIMARY KEY(frame,pass)) WITHOUT ROWID;
CREATE TABLE executions(execution BLOB PRIMARY KEY CHECK(typeof(execution)='blob' AND length(execution)=8),block BLOB NOT NULL CHECK(typeof(block)='blob' AND length(block)=8),occurrence INTEGER NOT NULL CHECK(occurrence>=0),frame INTEGER NOT NULL,pass INTEGER NOT NULL,pipeline_id BLOB REFERENCES pipelines(id),command TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('draw','dispatch','unknown')),vertex_count BLOB,index_count BLOB,instance_count BLOB,valid INTEGER NOT NULL CHECK(valid IN (0,1)),relation_status TEXT NOT NULL CHECK(relation_status IN ('resolved','partial','unresolved')),unavailable TEXT NOT NULL,arguments_json TEXT NOT NULL,dynamic_json TEXT NOT NULL,resources_json TEXT NOT NULL,source_json TEXT NOT NULL,FOREIGN KEY(frame,pass) REFERENCES passes(frame,pass),CHECK(pipeline_id IS NULL OR (typeof(pipeline_id)='blob' AND length(pipeline_id)=8)),CHECK(vertex_count IS NULL OR (typeof(vertex_count)='blob' AND length(vertex_count)=8)),CHECK(index_count IS NULL OR (typeof(index_count)='blob' AND length(index_count)=8)),CHECK(instance_count IS NULL OR (typeof(instance_count)='blob' AND length(instance_count)=8))) WITHOUT ROWID;
CREATE INDEX executions_frame_pass ON executions(frame,pass,execution);
CREATE INDEX executions_pipeline ON executions(pipeline_id,execution);
CREATE INDEX executions_block_occurrence ON executions(block,occurrence,execution);
CREATE INDEX stages_module_stage_pipeline ON pipeline_stages(module_id,stage,pipeline_id);
CREATE INDEX stages_stage_pipeline ON pipeline_stages(stage,pipeline_id,module_id);
)SQL");
}
void WriteMetadata(Database& db, const Json& metadata) {
    Statement insert(db, "INSERT OR REPLACE INTO meta(key,value) VALUES(?1,?2)");
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        insert.Text(1, it.key()); insert.Text(2, it.value().dump()); insert.Step(); insert.Reset();
    }
}
// Claim only a previously absent target. Failed builds may remove only this claim.
void Claim(const fs::path& path) {
#ifdef _WIN32
    const int fd = _wopen(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
#endif
    if (fd < 0) throw RelationError(RelationErrorKind::Data, "index_target_exists", "Cannot claim new relation target (existing targets are never replaced): " + path.u8string());
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
}
std::string Nonce() {
    std::random_device random;
    std::string nonce = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    for (unsigned i = 0; i < 8; ++i) nonce += ":" + std::to_string(random());
    return nonce;
}
void BindCount(Statement& statement, int column, const std::optional<uint64_t>& value) {
    if (value) statement.Id(column, *value); else statement.Null(column);
}
}

RelationIndexRef BuildRelationSnapshot(const Capture& capture, const fs::path& database,
                                      const std::atomic_bool* cancel,
                                      const std::function<void(const std::string&)>& progress) {
    using namespace relation_detail;
    Cancelled(cancel);
    if (progress) progress("Hashing relation source");
    if (database.empty()) throw RelationError(RelationErrorKind::Data, "invalid_index_path", "Relation target path is empty");
    const auto source = capture.source.empty() ? capture.jsonl : capture.source;
    bool owned = false;
    try {
        const auto sourceHash = Sha256File(source, cancel), decodedHash = Sha256File(capture.jsonl, cancel);
        Cancelled(cancel);
        if (!capture.fingerprint.empty() && capture.fingerprint != sourceHash)
            throw RelationError(RelationErrorKind::Data, "source_changed", "Capture differs from the parsed source identity");
        for (const auto& execution : capture.executions)
            if (execution.event >= capture.events.size() || execution.frame == 0 || execution.frame > capture.frames)
                throw RelationError(RelationErrorKind::Data, "invalid_capture_model", "Execution refers to an invalid event or frame");
        if (progress) progress("Grouping submitted executions using DumpIndex");
        const auto dump = DumpIndex::Build(capture); Cancelled(cancel);
        if (!database.parent_path().empty()) fs::create_directories(database.parent_path());
        Claim(database); owned = true;
        Json metadata{{"owner", "gfxrecon_gui_relations"}, {"schema", kRelationStorageVersion}, {"builder", kRelationBuilderVersion},
            {"parser_schema", kIndexSchema}, {"grouping_version", 1}, {"complete", false}, {"integrity", "pending"},
            {"snapshot", Sha256Text(sourceHash + ":" + decodedHash + ":" + Nonce())},
            {"source_path", source.u8string()}, {"source_sha256", sourceHash}, {"jsonl_path", capture.jsonl.u8string()}, {"decoded_sha256", decodedHash},
            {"counts", Json::object()}, {"coverage", Json::object()}};
        {
            Database db(database, true, cancel); CreateSchema(db);
            db.Exec("BEGIN IMMEDIATE;");
            WriteMetadata(db, metadata);
            size_t sinceCommit = 0;
            auto batch = [&] {
                Cancelled(cancel);
                if (++sinceCommit >= 1000) { db.Exec("COMMIT; BEGIN IMMEDIATE;"); sinceCommit = 0; }
            };
            std::set<uint64_t> pipelines, shaders;
            for (const auto& [id, info] : capture.pipelineInfo) if (id) pipelines.insert(id);
            for (const auto& [id, stages] : capture.pipelines) {
                if (id) pipelines.insert(id);
                for (const auto& stage : stages) if (stage.module) shaders.insert(stage.module);
            }
            for (const auto& [id, object] : capture.objects) if (id && object.type == "ShaderModule") shaders.insert(id);
            for (const auto& execution : capture.executions) {
                const auto id = capture.events.at(execution.event).pipeline; if (id) pipelines.insert(id);
            }
            Statement shader(db, "INSERT INTO shaders VALUES(?1,?2)");
            for (auto id : shaders) {
                shader.Id(1, id); const auto object = capture.objects.find(id);
                shader.Text(2, object == capture.objects.end() ? "" : object->second.name); shader.Step(); shader.Reset(); batch();
            }
            std::map<uint64_t, PipelineRecord> pipelineRecords;
            Statement pipeline(db, "INSERT INTO pipelines VALUES(?1,?2,?3,?4,?5)");
            Statement stage(db, "INSERT INTO pipeline_stages VALUES(?1,?2,?3,?4,?5,?6)");
            size_t stageCount = 0;
            for (auto id : pipelines) {
                const auto record = DescribePipeline(capture, id); pipelineRecords.emplace(id, record);
                pipeline.Id(1, id); pipeline.Text(2, record.kind);
                const auto object = capture.objects.find(id), end = capture.objects.end();
                pipeline.Text(3, object == end ? "" : object->second.name);
                const auto info = capture.pipelineInfo.find(id);
                pipeline.Text(4, info == capture.pipelineInfo.end() ? "null" : info->second.dump());
                pipeline.Text(5, record.status); pipeline.Step(); pipeline.Reset(); batch();
                const auto edges = capture.pipelines.find(id);
                if (edges == capture.pipelines.end()) continue;
                for (size_t ordinal = 0; ordinal < edges->second.size(); ++ordinal) {
                    const auto& edge = edges->second[ordinal]; bool known = true;
                    stage.Id(1, id); stage.Integer(2, Scalar(ordinal));
                    if (edge.module) stage.Id(3, edge.module); else stage.Null(3);
                    stage.Text(4, StageName(edge.stage, known)); stage.Text(5, edge.entry); stage.Text(6, edge.specialization.dump());
                    stage.Step(); stage.Reset(); ++stageCount; batch();
                }
            }
            Statement pass(db, "INSERT INTO passes VALUES(?1,?2,?3,?4,?5,?6,?7)"); size_t passCount = 0;
            for (const auto& frame : dump.frames) for (const auto& p : frame.passes) {
                pass.Integer(1, frame.frame); pass.Integer(2, p.ordinal); pass.Text(3, p.kind); pass.Id(4, p.submit); pass.Id(5, p.begin);
                pass.Text(6, Json(p.scope).dump());
                pass.Text(7, Json{{"logical_dump_path", FrameDirectory(frame.frame) + "/passes/" + p.Directory() + "/pass.json"}, {"dump_availability", "not_generated"}}.dump());
                pass.Step(); pass.Reset(); ++passCount; batch();
            }
            if (progress) progress("Materializing execution arguments and compact evidence references");
            std::ifstream input(capture.jsonl, std::ios::binary);
            if (!input) throw RelationError(RelationErrorKind::Data, "invalid_source", "Cannot open decoded relation source");
            Statement execution(db, "INSERT INTO executions VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18)");
            size_t resolved = 0, partial = 0, unresolved = 0, draws = 0, dispatches = 0, invalid = 0, indirect = 0;
            size_t knownCandidates = 0, unresolvedCandidates = 0;
            size_t lastEvent = std::numeric_limits<size_t>::max(); Json arguments;
            for (size_t i = 0; i < capture.executions.size(); ++i) {
                Cancelled(cancel); const auto& x = capture.executions[i]; const auto& event = capture.events[x.event]; const auto& location = dump.draws.at(i);
                if (location.frame != x.frame) throw RelationError(RelationErrorKind::Data, "invalid_capture_model", "DumpIndex frame mapping is inconsistent");
                if (lastEvent != x.event) { arguments = ReadArguments(input, event, cancel); lastEvent = x.event; }
                const auto kind = Kind(event); draws += kind == "draw"; dispatches += kind == "dispatch"; invalid += !x.valid;
                std::string status = event.pipeline ? pipelineRecords.at(event.pipeline).status : "unresolved";
                if (!x.valid && status == "resolved") status = "partial";
                knownCandidates += event.pipeline && pipelineRecords.at(event.pipeline).knownModule;
                unresolvedCandidates += status != "resolved" || !x.valid;
                if (status == "resolved") ++resolved; else if (status == "partial") ++partial; else ++unresolved;
                Json unavailable = Json::array(); if (!x.valid) unavailable.push_back(x.unavailable);
                if (status != "resolved") unavailable.push_back("Pipeline/module relationships are " + status + "; known stage edges are positive evidence only");
                const bool isIndirect = event.name.find("Indirect") != std::string::npos; indirect += isIndirect;
                std::optional<uint64_t> vertices, indices, instances;
                if (isIndirect) unavailable.push_back("Indirect argument contents require replay readback; counts are unavailable");
                else if (event.name == "vkCmdDraw") { vertices = Unsigned(arguments, "vertexCount"); instances = Unsigned(arguments, "instanceCount"); }
                else if (event.name == "vkCmdDrawIndexed") { indices = Unsigned(arguments, "indexCount"); instances = Unsigned(arguments, "instanceCount"); }
                else if (kind == "draw") unavailable.push_back("This command has no supported static vertex/index/instance count interpretation");
                if (!isIndirect && ((event.name == "vkCmdDraw" && (!vertices || !instances)) ||
                    (event.name == "vkCmdDrawIndexed" && (!indices || !instances))))
                    unavailable.push_back("One or more direct argument counts are absent or not unsigned integers in recorded evidence");
                const Json emptyState = Json::object();
                const Json& state = event.state ? *event.state : emptyState;
                const Json dynamic{{"calls", Value(state, "dynamic", Json::object())}, {"viewports", Value(state, "viewports", Json::object())}, {"scissors", Value(state, "scissors", Json::object())}};
                execution.Id(1, static_cast<uint64_t>(i)); execution.Id(2, event.block); execution.Integer(3, location.occurrence);
                execution.Integer(4, location.frame); execution.Integer(5, Scalar(location.pass));
                if (event.pipeline) execution.Id(6, event.pipeline); else execution.Null(6);
                execution.Text(7, event.name); execution.Text(8, kind); BindCount(execution, 9, vertices); BindCount(execution, 10, indices); BindCount(execution, 11, instances);
                execution.Integer(12, x.valid ? 1 : 0); execution.Text(13, status); execution.Text(14, unavailable.dump()); execution.Text(15, arguments.dump());
                execution.Text(16, dynamic.dump()); execution.Text(17, ResourceRefs(capture, event, x).dump()); execution.Text(18, Source(event, x, location).dump());
                execution.Step(); execution.Reset(); batch();
                if (progress && i && i % 10000 == 0) progress("Indexed " + std::to_string(i) + " submitted executions");
            }
            metadata["counts"] = {{"executions", capture.executions.size()}, {"pipelines", pipelines.size()}, {"shaders", shaders.size()}, {"pipeline_stages", stageCount}, {"passes", passCount}};
            metadata["coverage"] = {{"executions", capture.executions.size()}, {"resolved", resolved}, {"partial", partial}, {"unresolved", unresolved},
                {"invalid", invalid}, {"draws", draws}, {"dispatches", dispatches}, {"indirect_counts_unavailable", indirect},
                {"known_relationship_candidates", knownCandidates}, {"unresolved_candidate_executions", unresolvedCandidates},
                {"shader_use_semantics", "captured_pipeline_stage_reference"}, {"limitations", capture.limitations}};
            WriteMetadata(db, metadata); db.Exec("COMMIT;");
            if (progress) progress("Validating relation database and source stability");
            ValidateBuild(db); Cancelled(cancel);
            if (Sha256File(source, cancel) != sourceHash || Sha256File(capture.jsonl, cancel) != decodedHash)
                throw RelationError(RelationErrorKind::Data, "source_changed", "Capture or decoded source changed during relation build");
            Cancelled(cancel); metadata["complete"] = true; metadata["integrity"] = "validated";
            db.Exec("BEGIN IMMEDIATE;"); WriteMetadata(db, metadata); db.Exec("COMMIT;");
        }
        Cancelled(cancel);
        return {database, metadata.at("snapshot").get<std::string>(), std::move(metadata), false};
    } catch (...) {
        const auto failure = std::current_exception();
        if (owned) {
            std::error_code ignored; fs::remove(database, ignored);
            fs::remove(fs::path(database.u8string() + "-journal"), ignored);
        }
        Cancelled(cancel);
        try { std::rethrow_exception(failure); }
        catch (const RelationError&) { throw; }
        catch (const std::exception& error) {
            throw RelationError(RelationErrorKind::Data, "relation_build", error.what());
        }
    }
}

RelationIndexRef OpenRelationIndex(const fs::path& database, const std::atomic_bool* cancel) {
    using namespace relation_detail;
    Cancelled(cancel); Database db(database, false, cancel);
    ValidateDatabase(db); auto metadata = ReadMetadata(db); Cancelled(cancel);
    return {database, metadata.at("snapshot").get<std::string>(), std::move(metadata), true};
}
} // namespace gfx
