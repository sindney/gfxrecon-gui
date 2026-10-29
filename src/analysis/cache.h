#pragma once
#include "analysis/index.h"
#include <functional>

namespace gfx {
struct CachedCapture {
    std::shared_ptr<Capture> capture;
    std::filesystem::path directory;
    std::string fingerprint;
    bool hit = false;
};
CachedCapture OpenCachedCapture(const std::filesystem::path& source, const std::filesystem::path& cacheRoot,
                                 const std::string& toolFingerprint,
                                 const std::function<std::shared_ptr<Capture>(const std::filesystem::path&)>& build,
                                 const std::atomic_bool* cancel = nullptr);
} // namespace gfx
