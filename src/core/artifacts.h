#pragma once
#include <atomic>
#include <filesystem>
#include <string_view>
#include <nlohmann/json.hpp>

namespace gfx {
std::string Sha256File(const std::filesystem::path& file, const std::atomic_bool* cancel = nullptr);
// Reuse a verified hash while file identity, size, modification and change times
// are unchanged. Entries are process-local and bounded; changed files are read again.
std::string CachedSha256File(const std::filesystem::path& file, const std::atomic_bool* cancel = nullptr);
std::string Sha256Text(std::string_view text);
void AtomicJson(const std::filesystem::path& file, const nlohmann::json& value);
} // namespace gfx
