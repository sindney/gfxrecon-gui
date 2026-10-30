#pragma once
#include "analysis/capture.h"

namespace gfx {
struct ImportResult {
    std::shared_ptr<Capture> capture;
    std::filesystem::path directory; // cache entry holding capture.jsonl, index.bin and shaders/
    bool cacheHit = false;
    std::string message;             // human summary, including shader extraction warnings
};
// Converts (.gfxr), extracts shaders and indexes into the content-addressed cache under cacheRoot.
ImportResult ImportCapture(const std::filesystem::path& file, const std::filesystem::path& cacheRoot,
                           const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel = nullptr);
} // namespace gfx
