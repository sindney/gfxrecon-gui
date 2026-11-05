#pragma once

#include "replay/replay.h"

namespace gfx {
// A capture is imported once by the caller. Both CLI and GUI use this immutable
// session description; edited shader files are snapshotted before replay starts.
struct PerfSessionRequest {
    std::shared_ptr<const Capture> capture;
    ReplayTarget target;
    std::map<std::string, std::string> tools;
    std::filesystem::path output, cacheDirectory;
    // Optional caller nonce used to attribute receipts to this invocation.
    // The session generates one when absent.
    std::string sessionId;
    std::map<uint64_t, std::filesystem::path> overrides;
    uint32_t frame = 1, runs = 1;
    uint64_t counterPeriodNs = 1000000;
    std::string counterProfile = "none";
    std::string swapchainMode = "offscreen";
    std::string memoryTranslation = "default";
    bool screenOff = false;
    std::chrono::seconds timeout{600};
    std::function<void(const std::string&)> progress;
};

void ValidatePerfSessionRequest(const PerfSessionRequest& request);
Json DiscoverPerfCapabilities(const PerfSessionRequest& request,
                             const std::atomic_bool* cancel = nullptr);
Json RunPerfSession(const PerfSessionRequest& request,
                    const std::atomic_bool* cancel = nullptr);

// Pure reporting helpers shared by interactive plots and CLI artifacts.
Json SummarizePerfValues(std::vector<double> values);
Json BuildPerfSessionResult(const PerfSessionRequest& request, const Json& runs);
}
