#include "replay/perf_session.h"
#include "replay/gpu_measurement.h"
#include "platform/device_lock.h"
#include "core/artifacts.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace gfx::tests {
namespace {
void Require(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
template<class F> void Rejects(F action, const char* expected) {
    try { action(); }
    catch (const std::exception& error) {
        Require(std::string(error.what()).find(expected) != std::string::npos, error.what()); return;
    }
    throw std::runtime_error(std::string("Performance request unexpectedly accepted: ") + expected);
}
Json Run(const std::string& variant, uint32_t index, double duration) {
    return {{"variant", variant}, {"index", index}, {"policy", {{"frame", 1}, {"counter_profile", "precision"}, {"execution_model", "fresh_replay_per_run"}}},
        {"gpu", {{"samples", Json::array({{{"valid", true}, {"frame", 1}, {"elapsed_ms", duration}}})},
            {"device", {{"device_name", "fixture"}}}, {"protocol", 1}, {"mode", "isolated-frame"},
            {"backend_sha256", "same-backend"}, {"capture_sha256", "same-capture"},
            {"swapchain", {{"requested", "offscreen"}, {"effective", "offscreen"}}},
            {"provenance", {{"cache_policy", {{"result_cache", "bypassed"}}}, {"cache_observations", {{"pipeline_cache_loaded", false}}}}}}},
        {"gpu_summary", SummarizePerfValues({duration})},
        {"counter_selection", {{"selected_definition_sha256", "same-counters"}, {"specs", Json::array({{{"id", 26}, {"name", "Full instructions"}, {"unit", "instruction/second"}}})}}},
        {"counters", {{"status", "complete"}, {"descriptor_sha256", "descriptor-wrapper"}, {"selected_definition_sha256", "same-counters"},
            {"window_begin_boot_ns", 100}, {"window_end_boot_ns", 200},
            {"counters", Json::array({{{"id", 26}, {"name", "Full instructions"}, {"unit", "instruction/second"},
                {"samples", Json::array({{{"timestamp_boot_ns", 90}, {"value", 999}}, {{"timestamp_boot_ns", 110}, {"value", 4}},
                    {{"timestamp_boot_ns", 170}, {"value", 6}}, {{"timestamp_boot_ns", 210}, {"value", 999}}})}}})}}}};
}
const Json& Metric(const Json& result, const char* key) {
    for (const auto& metric : result.at("metrics")) if (metric.at("key") == key) return metric;
    throw std::runtime_error(std::string("Missing performance metric ") + key);
}
}

void PerfSessionTests(const std::filesystem::path& root) {
    const auto testLocks = root / "perf-device-locks";
    ConfigureDeviceLocks(testLocks);
    for (const auto* policy : {"none", "remap", "realign", "rebind"})
        Require(ResolveMemoryTranslation(policy, true) == policy && ResolveMemoryTranslation(policy, false) == policy, "Explicit memory policy was changed");
    Require(ResolveMemoryTranslation("default", true) == "none" && ResolveMemoryTranslation("none", false) == "none", "Android default or explicit none changed");
#ifdef __APPLE__
    Require(ResolveMemoryTranslation("default", false) == "rebind", "macOS compatibility default changed");
#else
    Require(ResolveMemoryTranslation("default", false) == "none", "Local compatibility default changed");
#endif
    Rejects([] { ResolveMemoryTranslation("automatic", false); }, "Unsupported memory translation");
    const auto stats = SummarizePerfValues({100, 1, 2});
    Require(stats["median"] == 2 && stats["p95"] == 100 && stats["count"] == 3, "Wrong measured percentile policy");
    Require(SummarizePerfValues({}).is_null(), "Empty performance data became a zero result");
    Require(SummarizePerfValues({0})["mean"] == 0 && SummarizePerfValues({0})["count"] == 1, "Valid zero sample became unavailable");
    Rejects([] { SummarizePerfValues({std::numeric_limits<double>::quiet_NaN()}); }, "Nonfinite");
    PerfSessionRequest request;
    Require(request.counterProfile == "none", "Fresh frame profiling should default to timestamps without counters");
    Rejects([&] { ValidatePerfSessionRequest(request); }, "imported capture");
    const auto file = root / "perf-session-fixture.gfxr";
    { std::ofstream output(file); output << "fixture"; }
    auto capture = std::make_shared<Capture>(); capture->source = file; capture->frames = 2;
    request.capture = capture; request.output = root / "performance-output";
    ValidatePerfSessionRequest(request); // Counter-free local GPU timing is supported.
    request.counterPeriodNs = 0; ValidatePerfSessionRequest(request); request.counterPeriodNs = 1000000;
    request.counterProfile = "precision"; ValidatePerfSessionRequest(request); // Unsupported counters are reported independently of valid local GPU timing.
    request.target = {true, "fixture"}; ValidatePerfSessionRequest(request);
    request.counterProfile = "bandwidth"; ValidatePerfSessionRequest(request);
    request.counterProfile = "all"; ValidatePerfSessionRequest(request);
    request.counterProfile = "busy"; Rejects([&] { ValidatePerfSessionRequest(request); }, "Counter profile"); request.counterProfile = "none";
    request.memoryTranslation = "automatic"; Rejects([&] { ValidatePerfSessionRequest(request); }, "memory translation"); request.memoryTranslation = "default";
    request.counterProfile = "precision"; request.counterPeriodNs = 0;
    Rejects([&] { ValidatePerfSessionRequest(request); }, "sampling period"); request.counterPeriodNs = 1000000;
    request.frame = 3; Rejects([&] { ValidatePerfSessionRequest(request); }, "outside"); request.frame = 1;
    request.runs = 0; Rejects([&] { ValidatePerfSessionRequest(request); }, "runs"); request.runs = 1;
    request.timeout = std::chrono::seconds(0); Rejects([&] { ValidatePerfSessionRequest(request); }, "timeout"); request.timeout = std::chrono::seconds(600);
    std::atomic_bool cancelled{true}; Rejects([&] { DiscoverPerfCapabilities(request, &cancelled); }, "cancelled");

    request.runs = 2; request.overrides[12] = file;
    auto baseline = Run("baseline", 1, 1), modified = Run("modified", 1, 2);
    auto runs = Json::array({baseline, modified, Run("baseline", 2, 3), Run("modified", 2, 4)});
    auto result = BuildPerfSessionResult(request, runs);
    Require(result["mode"] == "single-frame" && result["runs"].size() == 4 && result["comparisons"].size() == 2,
        "Fresh outer runs lost alternating baseline/modified processes");
    Require(result["policy"]["execution_model"] == "fresh_replay_per_run" && result["policy"]["runs_per_process"] == 1 &&
        !result["policy"].contains("fps") && !result["policy"].contains("warmup_ms") && !result["policy"].contains("duration_ms"),
        "Removed inner-loop behavior remained in frame profiling policy");
    Require(Metric(result, "gpu_time")["baseline"]["mean"] == 2 && Metric(result, "gpu_time")["baseline"]["count"] == 2 &&
        Metric(result, "gpu_time")["delta"] == 1, "GPU statistics did not aggregate independent outer runs");
    Require(Metric(result, "counter_26")["baseline"]["mean"] == 5 && Metric(result, "counter_26")["baseline"]["count"] == 4,
        "Counter mean included points outside the measured GPU windows");
    auto incomparable = [&](Json after) {
        const auto mismatch = BuildPerfSessionResult(request, Json::array({baseline, after}));
        Require(mismatch["comparisons"][0]["status"] == "incomparable", "Mismatched frame profiling inputs were compared");
        for (const auto& metric : mismatch.at("metrics")) Require(!metric.contains("delta"), "Incomparable frame runs produced numerical deltas");
    };
    auto changed = modified; changed["policy"]["counter_profile"] = "bandwidth"; incomparable(changed);
    changed = modified; changed["gpu"]["memory_translation"] = ReplayMemoryTranslationPolicy("remap", true); incomparable(changed);
    changed = modified; changed["gpu"]["device"]["device_name"] = "another GPU"; incomparable(changed);
    changed = modified; changed["gpu"]["provenance"]["cache_observations"]["pipeline_cache_loaded"] = true; incomparable(changed);
    changed = modified; changed["counters"]["selected_definition_sha256"] = "different-definitions";
    result = BuildPerfSessionResult(request, Json::array({baseline, changed}));
    Require(result["comparisons"][0]["status"] == "comparable" && result["comparisons"][0]["counter_comparable"] == false &&
        Metric(result, "gpu_time").contains("delta") && !Metric(result, "counter_26").contains("delta"),
        "Counter definition mismatches invalidated GPU timing or produced counter deltas");
    changed["counters"]["counters"][0]["unit"] = "different-unit";
    result = BuildPerfSessionResult(request, Json::array({baseline, changed}));
    Require(Metric(result, "gpu_time").contains("delta") && Metric(result, "counter_26")["status"] == "unavailable",
        "Incompatible counter units invalidated valid GPU timings or produced mixed-unit averages");
    changed = modified; changed["index"] = 2; incomparable(changed);
    changed = modified; changed["counters"]["descriptor_sha256"] = "different-wrapper-same-selected-definitions";
    Require(BuildPerfSessionResult(request, Json::array({baseline, changed}))["comparisons"][0]["status"] == "comparable",
        "Unselected descriptor-wrapper differences invalidated matching selected definitions");

    auto emptyBaseline = baseline, emptyModified = modified;
    for (auto* run : {&emptyBaseline, &emptyModified}) {
        (*run)["gpu"]["counter_window"] = {{"status", "unavailable"}, {"reason", "clock bridge unavailable"}};
        (*run)["counters"]["status"] = "unavailable";
        (*run)["counters"]["window_begin_boot_ns"] = 0; (*run)["counters"]["window_end_boot_ns"] = 0;
        (*run)["counters"]["counters"] = Json::array();
    }
    result = BuildPerfSessionResult(request, Json::array({emptyBaseline, emptyModified}));
    Require(result["status"] == "complete" && Metric(result, "gpu_time")["baseline"]["mean"] == 1 && Metric(result, "gpu_time").contains("delta"),
        "Unavailable counter clock window invalidated valid frame GPU timing");
    Require(Metric(result, "counter_26")["baseline"].is_null() && Metric(result, "counter_26")["status"] == "unavailable" &&
        !Metric(result, "counter_26").contains("delta"), "Zero counter samples fabricated a mean or suppressed the requested metric row");
    auto sparseBaseline = baseline, sparseModified = modified;
    for (auto* run : {&sparseBaseline, &sparseModified})
        (*run)["counters"]["counters"][0]["samples"] = Json::array({{{"timestamp_boot_ns", 150}, {"value", 0}}});
    result = BuildPerfSessionResult(request, Json::array({sparseBaseline, sparseModified}));
    Require(Metric(result, "counter_26")["baseline"]["mean"] == 0 && Metric(result, "counter_26")["baseline"]["count"] == 1 &&
        !Metric(result, "counter_26")["quality"].empty() && Metric(result, "gpu_time")["baseline"]["count"] == 1,
        "One actual counter sample was rejected, fabricated, or stripped of sparse quality metadata");
    auto bandwidth = request; bandwidth.counterProfile = "bandwidth";
    auto bandwidthBase = baseline, bandwidthModified = modified;
    for (auto* run : {&bandwidthBase, &bandwidthModified}) {
        (*run)["policy"]["counter_profile"] = "bandwidth";
        auto& counter = (*run)["counters"]["counters"][0];
        counter["name"] = "Read Total (Bytes/sec)"; counter["unit"] = "BYTE/SECOND";
        counter["samples"][1]["value"] = uint64_t(9007199254740993ULL); counter["samples"][2]["value"] = 2.5;
        (*run)["counter_selection"]["specs"][0]["name"] = counter["name"]; (*run)["counter_selection"]["specs"][0]["unit"] = counter["unit"];
    }
    result = BuildPerfSessionResult(bandwidth, Json::array({bandwidthBase, bandwidthModified}));
    Require(result["policy"]["counter_profile"] == "bandwidth" && result["notes"].dump().find("byte/second") != std::string::npos,
        "Bandwidth policy/interpretation was lost");
    const auto& raw = result["runs"][0]["counters"]["counters"][0]["samples"];
    Require(raw[1]["value"].is_number_unsigned() && raw[1]["value"] == uint64_t(9007199254740993ULL) && raw[2]["value"].is_number_float(),
        "Performance aggregation changed original integer/floating counter evidence");
    auto timingOnly = request; timingOnly.counterProfile = "none";
    auto timingRun = baseline; timingRun.erase("counter_selection"); timingRun["counters"] = {{"status", "not_requested"}, {"counters", Json::array()}};
    result = BuildPerfSessionResult(timingOnly, Json::array({timingRun}));
    Require(result["metrics"].size() == 1 && result["policy"]["counter_profile"] == "none" && result["policy"]["counter_period_ns"] == 0,
        "Default GPU-only frame profiling retained counter metrics or an active sampling policy");

    const Json submit{{"index", 10}, {"function", {{"name", "vkQueueSubmit"}, {"args", {{"queue", 7}, {"submitCount", 1},
        {"pSubmits", Json::array({{{"pNext", nullptr}, {"pCommandBuffers", Json::array({9})}}})}}}}}};
    const Json present{{"index", 11}, {"function", {{"name", "vkQueuePresentKHR"}, {"args", {{"queue", 7}}}}}};
    // Setup failures must retain diagnostics before any counter/helper/replay
    // process can start. All target paths below are deliberately fake.
    const auto failureRoot = root / "perf-session-failures";
    std::filesystem::create_directories(failureRoot);
    auto failureCapture = std::make_shared<Capture>(); failureCapture->source = file;
    failureCapture->jsonl = failureRoot / "frame.jsonl"; failureCapture->frames = 1;
    auto writeFailureFrame = [&](const char* unsupported = nullptr) {
        std::ofstream stream(failureCapture->jsonl);
        if (unsupported) stream << Json{{"index", 1}, {"function", {{"name", unsupported}}}}.dump() << '\n';
        stream << submit.dump() << '\n' << present.dump() << '\n';
    };
    auto failureRequest = [&](const char* name) {
        PerfSessionRequest input; input.capture = failureCapture; input.target = {true, "host-fixture-only"};
        input.output = failureRoot / name;
        input.sessionId = std::string("failure-fixture-") + name;
        input.tools["adb"] = (failureRoot / "no-real-adb.exe").u8string();
        return input;
    };
    auto savedFailure = [&](const PerfSessionRequest& input, const char* stage) {
        std::ifstream stream(input.output / "failure.json"); const auto receipt = Json::parse(stream);
        Require(receipt.at("stage") == stage && receipt.at("completed_runs").empty(), "Failure receipt lost its setup phase or completed-run population");
        Require(!std::filesystem::exists(input.output / "result.json") && !std::filesystem::exists(input.output / "counter-runs"),
            "Setup failure launched counters or published successful results");
        Require(std::filesystem::is_regular_file(input.output / ".perf-session-owner" / "session.json"), "Failure receipt lacks owned-output evidence");
        std::ifstream ownerStream(input.output / ".perf-session-owner" / "session.json"); const auto owner = Json::parse(ownerStream);
        Require(receipt.at("session_id") == input.sessionId && owner.at("session_id") == input.sessionId,
            "Failure receipt was not attributed to its current session owner");
        return receipt;
    };
    writeFailureFrame("vkQueueBindSparse");
    auto failed = failureRequest("unsupported-preflight");
    Rejects([&] { RunPerfSession(failed); }, "vkQueueBindSparse");
    Require(savedFailure(failed, "preflight")["message"].get<std::string>().find("vkQueueBindSparse") != std::string::npos,
        "Preflight failure receipt did not preserve the original unsupported operation");
    const auto firstFailureHash = Sha256File(failed.output / "failure.json");
    Rejects([&] { RunPerfSession(failed); }, "new performance output");
    Require(Sha256File(failed.output / "failure.json") == firstFailureHash, "A retry overwrote the previous failed session receipt");

    writeFailureFrame();
    const auto fakeBackend = failureRoot / "gpu-backend.bin";
    { std::ofstream backend(fakeBackend); backend << "fixture backend bytes"; }
    AtomicJson(std::filesystem::u8path(fakeBackend.u8string() + ".capabilities.json"),
        {{"binary_sha256", Sha256File(fakeBackend)}, {"gpu_profiling_protocol", 1}});
    auto probe = failureRequest("local-counter-probe"); probe.target.android = false; probe.counterProfile = "precision";
    probe.tools["replay"] = fakeBackend.u8string();
    const auto capabilities = DiscoverPerfCapabilities(probe);
    Require(capabilities["available"] == true && capabilities["counter_required"] == false && capabilities["counters"]["status"] == "unavailable" &&
        capabilities["counters"].contains("reason"), "Optional unsupported counter discovery disabled valid local GPU capabilities");
    probe.counterProfile = "none";
    Require(DiscoverPerfCapabilities(probe)["counters"]["status"] == "not_requested", "GPU-only discovery attempted counter collection");
    failed = failureRequest("missing-shader"); failed.overrides[12] = failureRoot / "missing.spv";
    Rejects([&] { RunPerfSession(failed); }, "fingerprint");
    savedFailure(failed, "shader_snapshot");

    failed = failureRequest("backend-capability"); failed.target.android = false;
    Rejects([&] { RunPerfSession(failed); }, "bundled replay backend");
    savedFailure(failed, "backend_capability");
    failed = failureRequest("cli-staged-input"); failed.target.android = false;
    const auto stagedShader = failed.output / "overrides" / "sh12.spv";
    std::filesystem::create_directories(stagedShader.parent_path());
    { std::ofstream shader(stagedShader); shader << "immutable override input"; }
    failed.overrides[12] = stagedShader;
    Rejects([&] { RunPerfSession(failed); }, "bundled replay backend");
    savedFailure(failed, "backend_capability");
    Require(Sha256File(stagedShader) == Sha256File(failed.output / "shader-snapshots" / "sh12.spv"),
        "Fresh CLI-staged override input was rejected or not snapshotted before backend failure");

    failed = failureRequest("initial-cancellation");
    Rejects([&] { RunPerfSession(failed, &cancelled); }, "cancelled");
    Require(savedFailure(failed, "initialization")["status"] == "cancelled", "Initial cancellation did not persist a cancelled failure receipt");
    failed = failureRequest("lease-cancellation"); std::atomic_bool leaseCancel{false};
    failed.progress = [&](const std::string& message) { if (message == "Performance phase: device_lease") leaseCancel = true; };
    Rejects([&] { RunPerfSession(failed, &leaseCancel); }, "cancelled");
    Require(savedFailure(failed, "device_lease")["cancelled"] == true, "Lease cancellation lacked its failure receipt");

    failed = failureRequest("busy-preserves-type");
    failed.progress = [](const std::string& message) { if (message == "Performance phase: target_idle_check") throw DeviceBusy("host-fixture-only", "Synthetic busy target"); };
    bool busyPreserved = false;
    try { RunPerfSession(failed); } catch (const DeviceBusy&) { busyPreserved = true; }
    Require(busyPreserved && savedFailure(failed, "target_idle_check")["message"] == "Synthetic busy target", "Failure reporting replaced DeviceBusy's stable exception type");

    failed = failureRequest("unwritable-receipt");
    failed.progress = [&](const std::string& message) {
        if (message == "Performance phase: preflight") {
            std::filesystem::create_directory(failed.output / "failure.json");
            throw std::runtime_error("Original failure survives receipt write failure");
        }
    };
    Rejects([&] { RunPerfSession(failed); }, "Original failure survives receipt write failure");
    Require(!std::filesystem::is_regular_file(failed.output / "failure.json"), "Receipt write-failure fixture unexpectedly produced a regular receipt");

    failed = failureRequest("invalid-request"); failed.runs = 0;
    Rejects([&] { RunPerfSession(failed); }, "runs");
    Require(!std::filesystem::exists(failed.output), "Invalid request wrote to an unowned output");
    failed = failureRequest("completed-output"); std::filesystem::create_directories(failed.output);
    AtomicJson(failed.output / "result.json", {{"status", "complete"}, {"authored_evidence", true}});
    AtomicJson(failed.output / "failure.json", {{"message", "existing evidence"}});
    const auto resultHash = Sha256File(failed.output / "result.json"), failureHash = Sha256File(failed.output / "failure.json");
    Rejects([&] { RunPerfSession(failed); }, "new performance output");
    Require(Sha256File(failed.output / "result.json") == resultHash && Sha256File(failed.output / "failure.json") == failureHash &&
        !std::filesystem::exists(failed.output / ".perf-session-owner"), "An existing completed session was overwritten or claimed");
    failed = failureRequest("unrelated-output"); std::filesystem::create_directories(failed.output);
    { std::ofstream evidence(failed.output / "authored.txt"); evidence << "retain"; }
    Rejects([&] { RunPerfSession(failed); }, "unrelated files");
    Require(!std::filesystem::exists(failed.output / "failure.json"), "Unowned evidence directory gained a failure receipt");
    std::filesystem::remove_all(failureRoot);

    Rejects([&] { DeviceLock lock("perf-cancelled", &cancelled); }, "cancelled");
    ConfigureDeviceLocks({});
    Require(std::filesystem::weakly_canonical(testLocks).parent_path() == std::filesystem::weakly_canonical(root), "Test lock cleanup escaped its fixture root");
    std::filesystem::remove_all(testLocks);
    std::filesystem::remove(file);
}
}
