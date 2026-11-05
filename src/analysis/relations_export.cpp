#include "analysis/relations.h"

#include <chrono>
#include <fstream>
#include <limits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gfx {
namespace {
void CheckExportCancel(const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) throw RelationError(RelationErrorKind::Cancelled, "cancelled", "Relation export cancelled");
}
std::string Csv(const Json& value) {
    if (value.is_null()) return "";
    // Quote every non-null cell. Empty unquoted cells mean null; quoted empty means an empty string.
    const auto text = value.is_string() ? value.get<std::string>() : value.dump();
    std::string result = "\"";
    for (const auto ch : text) { if (ch == '"') result += '"'; result += ch; }
    return result + '"';
}
}
Json ExportRelations(const RelationIndexRef& index, RelationQuery query, const std::filesystem::path& output,
                     const std::string& format, bool all, const std::atomic_bool* cancel) {
    ValidateRelationQuery(query);
    if (format != "csv" && format != "jsonl") throw RelationError(RelationErrorKind::Usage, "invalid_export_format", "Export format must be csv or jsonl");
    if (output.empty()) throw RelationError(RelationErrorKind::Usage, "invalid_export_path", "Export path must not be empty");
    CheckExportCancel(cancel);
    const auto target = std::filesystem::absolute(output);
    // Never replace the snapshot/source/evidence through an export destination.
    auto protect = [&](const std::filesystem::path& evidence) {
        if (evidence.empty()) return;
        std::error_code ec;
        if (target.lexically_normal() == std::filesystem::absolute(evidence).lexically_normal() || std::filesystem::equivalent(target, evidence, ec))
            throw RelationError(RelationErrorKind::Usage, "invalid_export_path", "Export destination is an index/source evidence file");
    };
    protect(index.path);
    for (const auto* key : {"source_path", "jsonl_path"})
        if (index.metadata.contains(key) && index.metadata.at(key).is_string()) protect(std::filesystem::u8path(index.metadata.at(key).get<std::string>()));
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temporary;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        auto directory = target.parent_path() / (".relation-export-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(directory)) { temporary = directory / "rows.partial"; break; }
    }
    if (temporary.empty()) throw RelationError(RelationErrorKind::Data, "export_failed", "Could not create private export staging directory");
    struct Cleanup { std::filesystem::path file; ~Cleanup() { std::error_code ec; std::filesystem::remove(file, ec); std::filesystem::remove(file.parent_path(), ec); } } cleanup{temporary};
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) throw RelationError(RelationErrorKind::Data, "export_failed", "Cannot open export staging file");
    uint64_t rows = 0, pages = 0; Json fields, source, coverage; std::vector<std::string> names;
    RelationQueryResult page;
    do {
        CheckExportCancel(cancel); page = QueryRelations(index, query, cancel);
        if (pages == 0) {
            fields = page.fields; source = page.source; coverage = page.coverage;
            for (const auto& field : fields) names.push_back(field.at("name").get<std::string>());
            if (format == "csv") { for (size_t i = 0; i < names.size(); ++i) { if (i) stream << ','; stream << Csv(names[i]); } stream << "\r\n"; }
        }
        for (const auto& row : page.rows) {
            CheckExportCancel(cancel);
            if (rows == std::numeric_limits<uint64_t>::max()) throw RelationError(RelationErrorKind::Data, "count_overflow", "Export row count overflow");
            if (format == "jsonl") stream << row.dump() << '\n';
            else {
                for (size_t i = 0; i < names.size(); ++i) { if (i) stream << ','; stream << Csv(row.contains(names[i]) ? row.at(names[i]) : Json(nullptr)); }
                stream << "\r\n";
            }
            ++rows;
            if (!stream) throw RelationError(RelationErrorKind::Data, "export_failed", "Writing relation export failed");
        }
        ++pages;
        if (all && page.hasMore && (page.nextCursor.empty() || page.nextCursor == query.cursor))
            throw RelationError(RelationErrorKind::Data, "invalid_export_page", "Query failed to advance its export cursor");
        query.cursor = page.nextCursor;
    } while (all && page.hasMore);
    stream.flush(); if (!stream) throw RelationError(RelationErrorKind::Data, "export_failed", "Flushing relation export failed");
    stream.close(); if (!stream) throw RelationError(RelationErrorKind::Data, "export_failed", "Closing relation export failed");
    CheckExportCancel(cancel);
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw RelationError(RelationErrorKind::Data, "export_failed", "Publishing relation export failed", {{"system_error", GetLastError()}});
#else
    std::error_code ec; std::filesystem::rename(temporary, target, ec);
    if (ec) throw RelationError(RelationErrorKind::Data, "export_failed", "Publishing relation export failed: " + ec.message());
#endif
    return {{"schema", kRelationQuerySchema}, {"entity", page.entity}, {"snapshot", index.snapshot}, {"source", source}, {"coverage", coverage},
        {"export", {{"path", target.u8string()}, {"format", format}, {"all", all}, {"rows", rows}, {"pages", pages}, {"fields", fields},
                    {"has_more", page.hasMore}, {"next_cursor", page.nextCursor}}}};
}
} // namespace gfx
