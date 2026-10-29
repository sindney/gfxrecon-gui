#pragma once

#include "platform/adb.h"
#include "analysis/capture.h"
#include <functional>

namespace gfx {
struct AndroidInfo {
    std::string serial, model, androidVersion, abi;
    std::vector<std::string> packages;
};
struct CaptureRequest {
    std::filesystem::path output;
    std::string package, arguments, frames = "1-3";
    std::chrono::seconds timeout{90};
    std::function<void(const std::string&)> progress;
};
struct CaptureResult {
    std::vector<std::filesystem::path> files;
    std::filesystem::path journal;
};
void ValidateCaptureFrames(const std::string& frames);
AndroidInfo InspectAndroidDevice(const AdbConnection& adb);
CaptureResult CaptureAndroid(const AdbConnection& adb, const CaptureRequest& request);
void RestoreAndroidCapture(const AdbConnection& adb, const std::filesystem::path& journal);
// Pure restore planning permits deterministic failure/conflict tests without a phone.
enum class RestoreDecision { AlreadyRestored, Restore, Conflict };
RestoreDecision PlanRestore(const std::string& previous, const std::string& requested, const std::string& current);
CaptureResult CaptureWindows(const ProcessRequest& process, const std::string& layerDirectory,
                              const std::string& frames, const std::filesystem::path& output,
                              const std::atomic_bool* cancel = nullptr);
} // namespace gfx
