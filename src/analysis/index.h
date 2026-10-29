#pragma once
#include "analysis/capture.h"

namespace gfx {
inline constexpr uint32_t kIndexSchema = 6;
void WriteCaptureIndex(const Capture& capture, const std::filesystem::path& file, const std::atomic_bool* cancel = nullptr);
std::shared_ptr<Capture> ReadCaptureIndex(const std::filesystem::path& jsonl, const std::filesystem::path& index,
                                         const std::atomic_bool* cancel = nullptr);
} // namespace gfx
