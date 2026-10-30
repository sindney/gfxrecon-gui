#pragma once

#include "analysis/capture.h"
#include "platform/adb.h"
#include "analysis/resources.h"
#include <atomic>
#include <chrono>
#include <functional>

namespace gfx {
enum class ReplayMode { Automatic, Local, Android };
struct AndroidDevice {
    std::string serial, state, details;
};
struct ReplayTarget {
    bool android = false;
    std::string serial;
    std::string Label() const { return android ? "Android " + serial : "Local Windows"; }
};
// Upstream FPS measurement (--measurement-frame-range/--measurement-file). Measurement runs bypass result caches.
struct ReplayMeasurement {
    bool enabled = false;
    std::string frameRange; // "<start>-<end>", end exclusive (upstream semantics)
    bool quitAfter = true;  // --quit-after-measurement-range
};
struct ReplayRequest {
    std::filesystem::path capture, output;
    std::filesystem::path cacheDirectory;
    uint32_t frame = 1;
    std::string prefix = "baseline";
    Json dump;
    std::map<uint64_t, std::filesystem::path> overrides;
    std::string expectedCaptureHash;
    uint64_t selectionGeneration = 0;
    bool outputsOnly = false;
    bool descriptorImages = false;
    bool drawInspection = false;
    std::string visualization="original";
    std::filesystem::path uvShader;
    std::filesystem::path outlineBaseImage,outlineCoverageImage;
    uint64_t visualizationPipeline=0;
    int indirectSubdraw=-1;
    bool indirectDrawIdSafe=false;
    bool validation = false;
    bool screenOff = false;
    bool reuseArtifacts = true;
    bool optimize = true;
    ReplayMeasurement measurement;
    std::string imageFormat = "bmp"; // dump-resources image encoding: bmp | png (backend-side)
    std::chrono::seconds timeout{120};
    std::function<void(const std::string&)> progress;
};
struct ReplayResult {
    std::filesystem::path image;
    std::string log, target;
    std::vector<ResourceArtifact> resources;
    Json timings;
    Json validationReport;
    Json measurement; // parsed measurement file when ReplayRequest::measurement.enabled
};
std::vector<AndroidDevice> ParseAdbDevices(const std::string& output);
std::vector<AndroidDevice> QueryAndroidDevices(const std::string& adb, const std::atomic_bool* cancel = nullptr);
ReplayTarget SelectReplayTarget(ReplayMode mode, bool androidCapture, const std::string& serial,
                                 const std::vector<AndroidDevice>& devices);
ReplayTarget ResolveReplayTarget(ReplayMode mode, bool androidCapture, const std::string& serial,
                                  const std::string& adb, const std::atomic_bool* cancel = nullptr);
ReplayResult ReplayCapture(const ReplayRequest& request, const ReplayTarget& target,
                            const std::map<std::string, std::string>& tools,
                            const std::atomic_bool* cancel = nullptr);
Json ParseValidationLog(const std::string& log);
std::string DescribeAndroidReplayExit(const std::string& info);
} // namespace gfx
