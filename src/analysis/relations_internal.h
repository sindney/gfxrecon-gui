#pragma once
// Private storage adapter. Never include from public relation/CLI headers.
#include "analysis/relations.h"
#include <sqlite3.h>
#include <array>

namespace gfx::relation_detail {
inline constexpr int kApplicationId = 0x47524651;
inline void Cancelled(const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) throw RelationError(RelationErrorKind::Cancelled, "cancelled", "Relation operation cancelled");
}
void SqlCheck(int code, sqlite3* db, const char* operation);
struct Database {
    sqlite3* handle = nullptr;
    explicit Database(const std::filesystem::path& path, bool writable = false, const std::atomic_bool* cancel = nullptr);
    ~Database();
    Database(const Database&) = delete;
    void Exec(const std::string& sql);
};
struct Statement {
    sqlite3_stmt* handle = nullptr;
    sqlite3* db = nullptr;
    Statement(Database& database, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    void Text(int position, const std::string& value);
    void Id(int position, uint64_t value);
    void Integer(int position, int64_t value);
    void Null(int position);
    bool Step();
    void Reset();
    std::string TextColumn(int column) const;
    Json IdColumn(int column) const;
    uint64_t CountColumn(int column) const;
};
std::array<unsigned char, 8> EncodeId(uint64_t id);
uint64_t DecodeId(const void* bytes, int length);
Json ReadMetadata(Database& db);
void ValidateDatabase(Database& db);
} // namespace gfx::relation_detail
