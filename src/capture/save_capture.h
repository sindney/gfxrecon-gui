#pragma once
#include <atomic>
#include <filesystem>
#include <string>

namespace gfx {
struct CaptureSaveResult { std::filesystem::path source, destination; std::string sha256; };
CaptureSaveResult SaveCaptureFile(const std::filesystem::path& source, std::filesystem::path destination,
    const std::string& expectedHash = {}, bool replaceExisting = false, const std::atomic_bool* cancel = nullptr);
}
