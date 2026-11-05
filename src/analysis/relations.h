#pragma once
#include "analysis/capture.h"
#include <functional>
#include <optional>
#include <stdexcept>

namespace gfx {
inline constexpr const char* kRelationQuerySchema = "gfxrecon-query/1";
inline constexpr uint32_t kRelationStorageVersion = 1;
inline constexpr uint32_t kRelationBuilderVersion = 1;
enum class RelationEntity { Executions, Pipelines, Shaders, Passes };
enum class RelationKind { All, Draw, Dispatch };
enum class RelationErrorKind { Usage, Data, Cancelled, MissingTool };
struct RelationError : std::runtime_error {
    RelationErrorKind kind;
    std::string code;
    Json detail;
    RelationError(RelationErrorKind category, std::string name, const std::string& message, Json extra = {})
        : std::runtime_error(message), kind(category), code(std::move(name)), detail(std::move(extra)) {}
};
struct RelationFilters {
    std::vector<uint64_t> executions, blocks, pipelines, shaders;
    std::vector<uint32_t> frames, passes;
    std::vector<std::string> stages;
    std::optional<uint32_t> occurrence;
};
struct RelationQuery {
    RelationEntity entity = RelationEntity::Executions;
    RelationKind kind = RelationKind::All;
    RelationFilters filters;
    std::vector<std::string> columns, sections, groupBy;
    uint32_t limit = 100;
    std::string cursor;
    bool includeCount = false;
};
struct RelationField {
    std::string name, type, description;
    bool defaultField = false, filterable = false, section = false, nullable = false;
};
struct RelationIndexRef {
    std::filesystem::path path;
    std::string snapshot;
    Json metadata;
    bool reused = false;
};
struct RelationBuildOptions {
    bool rebuild = false;
    std::function<void(const std::string&)> progress;
};
struct RelationQueryResult {
    std::string entity;
    Json fields = Json::array(), rows = Json::array(), source, coverage;
    bool hasMore = false;
    std::string nextCursor;
    std::optional<uint64_t> total;
    Json ToJson() const;
};
RelationEntity ParseRelationEntity(const std::string& name);
std::string RelationEntityName(RelationEntity entity);
std::string NormalizeRelationStage(const std::string& stage);
std::vector<RelationField> RelationFields(RelationEntity entity);
Json RelationFieldCatalog(RelationEntity entity);
// Validation is source-independent. Call before import/conversion in adapters.
void ValidateRelationQuery(const RelationQuery& query);
RelationQueryResult QueryRelations(const RelationIndexRef& index, const RelationQuery& query,
                                   const std::atomic_bool* cancel = nullptr);
// Builds one new immutable file; refuses an existing target, even with rebuild.
// Normal callers use EnsureRelationIndex, which safely publishes generations.
RelationIndexRef BuildRelationSnapshot(const Capture& capture, const std::filesystem::path& database,
                                      const std::atomic_bool* cancel = nullptr,
                                      const std::function<void(const std::string&)>& progress = {});
RelationIndexRef OpenRelationIndex(const std::filesystem::path& database,
                                  const std::atomic_bool* cancel = nullptr);
RelationIndexRef EnsureRelationIndex(const std::filesystem::path& capture,
                                    const std::filesystem::path& cacheRoot,
                                    const std::map<std::string, std::string>& tools,
                                    const RelationBuildOptions& options = {},
                                    const std::atomic_bool* cancel = nullptr);
// Export preserves projected pages and returns a compact JSON receipt.
Json ExportRelations(const RelationIndexRef& index, RelationQuery query, const std::filesystem::path& output,
                     const std::string& format, bool all = false, const std::atomic_bool* cancel = nullptr);
} // namespace gfx
