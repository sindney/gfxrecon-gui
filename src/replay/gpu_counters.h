#pragma once

#include "analysis/capture.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gfx {
// Small bounded decoders for the supported OEM Perfetto GPU protocol. Unknown
// fields are skipped; absolute MONOTONIC/RAW clocks require BOOTTIME snapshot
// bridges. Malformed/interned/incremental clocks fail explicitly.
Json DecodeGpuCounterServiceState(const std::vector<uint8_t>& bytes);
Json DecodeGpuCounterTrace(const std::vector<uint8_t>& bytes);
Json SelectGpuCounters(const Json& serviceState, uint64_t periodNs, const std::string& profile);
Json SelectGpuPrecisionCounters(const Json& serviceState, uint64_t periodNs);
Json SummarizeGpuCounterWindow(const Json& decodedTrace, const Json& selection,
                              uint64_t beginBootNs, uint64_t endBootNs);

// Caller owns the target-level lease through this object's entire lifetime.
// No clock/driver/layer settings are changed. Cleanup touches only this session.
class AndroidGpuCounters {
public:
    AndroidGpuCounters(std::string adb, std::string serial,
                       std::filesystem::path output, std::filesystem::path helper,
                       const std::atomic_bool* cancel = nullptr);
    ~AndroidGpuCounters();
    AndroidGpuCounters(const AndroidGpuCounters&) = delete;
    AndroidGpuCounters& operator=(const AndroidGpuCounters&) = delete;

    Json Discover(const std::string& profile, uint64_t periodNs = 1000000);
    Json DiscoverPrecision(uint64_t periodNs = 1000000);
    // Called after prefix drain and before the selected frame's GPU interval.
    // Uses a binary Perfetto config and waits for producer start acknowledgement.
    void Start(uint64_t durationMs);
    // (0,0) explicitly stops and retains raw evidence when clock correlation
    // is unavailable. No mean or frame attribution is invented in that case.
    Json StopAndDecode(uint64_t beginBootNs, uint64_t endBootNs);
    void Cleanup() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
