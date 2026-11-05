#include "replay/perf_session.h"
#include "replay/gpu_measurement.h"
#include "replay/gpu_counters.h"
#include "platform/device_lock.h"
#include "core/artifacts.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>

namespace gfx {
namespace {
namespace fs = std::filesystem;
void Cancelled(const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Performance session cancelled");
}
std::string Tool(const PerfSessionRequest& request, const char* key) {
    const auto found = request.tools.find(key);
    return found == request.tools.end() ? std::string{} : found->second;
}
Json Read(const fs::path& file) { std::ifstream input(file); return Json::parse(input); }
void RefuseActiveReplay(const PerfSessionRequest& request, const std::atomic_bool* cancel) {
    if (!request.target.android) return;
    AdbConnection adb{Tool(request, "adb"), request.target.serial, cancel};
    const auto active = adb.TryShell({"pidof", "com.lunarg.gfxreconstruct.replay"});
    Cancelled(cancel);
    if (active.Success() && active.output.find_first_not_of(" \r\n\t") != std::string::npos)
        throw DeviceBusy(request.target.serial, "Device replay process already active; profiling will not probe or interrupt that session");
}
Json BackendCapability(const PerfSessionRequest& request, const std::atomic_bool* cancel) {
    const auto name = Tool(request, request.target.android ? "replay_apk" : "replay");
    if (name.empty() || !fs::is_regular_file(fs::u8path(name))) throw std::runtime_error("Configure the bundled replay backend before profiling");
    const auto hash = Sha256File(fs::u8path(name), cancel);
    Json capability;
    try { capability = Read(fs::u8path(name + ".capabilities.json")); }
    catch (...) { throw std::runtime_error("Replay backend has no GPU capability file; rebuild/update the bundled backend"); }
    if (capability.value("binary_sha256", "") != hash || capability.value("gpu_profiling_protocol", 0) != 1)
        throw std::runtime_error("Replay GPU capability does not match the configured backend bytes");
    return capability;
}
std::vector<double> MeasuredGpu(const Json& gpu) {
    std::vector<double> values;
    for (const auto& sample : gpu.at("samples"))
        if (sample.value("valid", false))
            values.push_back(sample.at("elapsed_ms").get<double>());
    return values;
}
Json Policy(const PerfSessionRequest& request) {
    return {{"frame", request.frame}, {"execution_model", "fresh_replay_per_run"}, {"runs_per_process", 1},
        {"counter_profile", request.counterProfile}, {"counter_period_ns", request.counterProfile == "none" ? 0 : request.counterPeriodNs},
        {"swapchain_mode", request.swapchainMode}, {"memory_translation", ReplayMemoryTranslationPolicy(request.memoryTranslation, request.target.android)},
        {"gpu_scope", "isolated frame on one command queue"}, {"counter_scope", "system-wide samples within the correlated frame GPU interval"},
        {"result_cache", "bypassed"}, {"precision_conversion_verified", false}};
}
std::string CounterIdentity(const Json& run) {
    const auto counters = run.value("counters", Json::object());
    const auto selection = run.value("counter_selection", Json::object());
    return counters.value("selected_definition_sha256", selection.value("selected_definition_sha256",
        counters.value("descriptor_sha256", selection.value("descriptor_sha256", std::string{}))));
}
Json CompareRuns(const Json& baseline, const Json& modified) {
    const auto& a = baseline.at("gpu"); const auto& b = modified.at("gpu");
    std::string reason;
    for (const char* key : {"backend_sha256", "capture_sha256", "protocol", "mode", "device", "swapchain"})
        if (a.at(key) != b.at(key)) { reason = std::string("GPU ") + key + " differs"; break; }
    if (reason.empty() && a.value("memory_translation", Json{}) != b.value("memory_translation", Json{})) reason = "Memory translation policy differs";
    if (reason.empty() && a.at("provenance").at("cache_policy") != b.at("provenance").at("cache_policy")) reason = "Explicit cache policy differs";
    const auto cache = [](const Json& gpu) { return gpu.at("provenance").at("cache_observations").value("pipeline_cache_loaded", false); };
    if (reason.empty() && cache(a) != cache(b)) reason = "Pipeline cache load state differs";
    if (reason.empty() && baseline.at("policy") != modified.at("policy")) reason = "Frame profiling policy differs";
    if (reason.empty() && (baseline.at("index") != modified.at("index") || baseline.at("variant") != "baseline" || modified.at("variant") != "modified"))
        reason = "Baseline/modified runs are not an alternating pair";
    std::string counterReason = reason;
    const auto hasCounterData = [](const Json& run) {
        const auto collection = run.value("counters", Json::object());
        const auto begin = collection.value("window_begin_boot_ns", uint64_t(0)), end = collection.value("window_end_boot_ns", uint64_t(0));
        if (collection.value("status", "unavailable") != "complete" || !begin || end <= begin) return false;
        for (const auto& counter : collection.value("counters", Json::array())) for (const auto& point : counter.value("samples", Json::array())) {
            const auto timestamp = point.at("timestamp_boot_ns").get<uint64_t>();
            if (timestamp >= begin && timestamp <= end && point.at("value").is_number()) return true;
        }
        return false;
    };
    if (counterReason.empty() && (!hasCounterData(baseline) || !hasCounterData(modified))) counterReason = "Counter window or sampled data unavailable";
    if (counterReason.empty() && (CounterIdentity(baseline).empty() || CounterIdentity(modified).empty())) counterReason = "Selected counter definitions unavailable";
    if (counterReason.empty() && CounterIdentity(baseline) != CounterIdentity(modified)) counterReason = "Selected counter definitions differ";
    Json result{{"index", baseline.at("index")}, {"status", reason.empty() ? "comparable" : "incomparable"}, {"counter_comparable", counterReason.empty()}};
    if (!counterReason.empty()) result["counter_reason"] = counterReason;
    if (!reason.empty()) { result["reason"] = reason; return result; }
    const double before = baseline.at("gpu_summary").at("mean"), after = modified.at("gpu_summary").at("mean");
    result["gpu_delta_ms"] = after - before;
    result["gpu_delta_percent"] = before > 0 ? Json((after - before) / before * 100.0) : Json(nullptr);
    return result;
}
std::string QuoteCsv(std::string text) {
    size_t at = 0; while ((at = text.find('"', at)) != std::string::npos) { text.insert(at, 1, '"'); at += 2; }
    return '"' + text + '"';
}

void ExportCsv(const fs::path& root, const Json& runs) {
    std::ofstream gpu(root / "gpu-samples.csv"), counters(root / "counter-samples.csv");
    gpu << "variant,run,frame,start_boot_ns,end_boot_ns,elapsed_ms\n";
    counters << "variant,run,counter_id,name,unit,timestamp_boot_ns,value,in_frame_window\n";
    gpu.precision(17); counters.precision(17);
    for (const auto& run : runs) {
        for (const auto& sample : run.at("gpu").at("samples"))
            gpu << run.at("variant").get<std::string>() << ',' << run.at("index") << ',' << sample.at("frame") << ','
                << sample.value("start_boot_ns", Json{}) << ',' << sample.value("end_boot_ns", Json{}) << ',' << sample.at("elapsed_ms") << '\n';
        const auto collection = run.value("counters", Json::object());
        const auto begin = collection.value("window_begin_boot_ns", uint64_t(0)), end = collection.value("window_end_boot_ns", uint64_t(0));
        for (const auto& counter : collection.value("counters", Json::array()))
            for (const auto& sample : counter.value("samples", Json::array())) {
                const auto time = sample.at("timestamp_boot_ns").get<uint64_t>();
                counters << run.at("variant").get<std::string>() << ',' << run.at("index") << ',' << counter.at("id") << ','
                    << QuoteCsv(counter.at("name")) << ',' << QuoteCsv(counter.at("unit")) << ',' << time << ',' << sample.at("value") << ','
                    << (begin && end > begin && time >= begin && time <= end ? 1 : 0) << '\n';
            }
    }
    if (!gpu || !counters) throw std::runtime_error("Cannot export performance samples");
}
}

void ValidatePerfSessionRequest(const PerfSessionRequest& request) {
    if (!request.capture || request.capture->source.empty() || !fs::is_regular_file(request.capture->source)) throw std::runtime_error("Performance session needs an imported capture");
    if (!request.frame || request.frame > request.capture->frames) throw std::runtime_error("Performance frame is outside the capture");
    if (!request.runs || request.runs > 50) throw std::runtime_error("Performance runs must be 1..50");
    if (request.swapchainMode != "virtual" && request.swapchainMode != "captured" && request.swapchainMode != "offscreen") throw std::runtime_error("Unsupported performance swapchain mode");
    (void)ResolveMemoryTranslation(request.memoryTranslation, request.target.android);
    if (request.timeout.count() <= 0) throw std::runtime_error("Performance timeout must be positive");
    if (request.counterProfile != "none" && request.counterProfile != "all" && request.counterProfile != "precision" && request.counterProfile != "bandwidth")
        throw std::runtime_error("Counter profile must be none or all (precision/bandwidth are legacy profiles)");
    if (request.counterProfile != "none") {
        if (!request.counterPeriodNs || request.counterPeriodNs > 1000000000ull) throw std::runtime_error("Counter sampling period must be in (0, 1 second]");
    }
}

Json SummarizePerfValues(std::vector<double> values) {
    if (values.empty()) return nullptr;
    for (const auto value : values) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite performance sample");
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / n;
    double square = 0; for (const auto value : values) square += (value - mean) * (value - mean);
    const double deviation = std::sqrt(square / n);
    return {{"count", n}, {"mean", mean}, {"min", values.front()}, {"max", values.back()},
        {"median", n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2},
        {"p95", values[std::max<size_t>(1, size_t(std::ceil(n * 0.95))) - 1]}, {"stddev", deviation},
        {"cv_percent", mean != 0 ? Json(deviation / std::abs(mean) * 100.0) : Json(nullptr)}, {"percentile_policy", "nearest_rank"}};
}

Json DiscoverPerfCapabilities(const PerfSessionRequest& request, const std::atomic_bool* cancel) {
    const bool countersRequested = request.counterProfile != "none";
    Json result{{"status", "unavailable"}, {"available", false}, {"mode", "single-frame"},
        {"counter_profile", request.counterProfile}, {"counter_requested", countersRequested}, {"counter_required", false}};
    try {
        ValidatePerfSessionRequest(request); Cancelled(cancel);
        result["frame_plan"] = BuildGpuMeasurementPlan(*request.capture, request.frame, request.frame, cancel);
        std::optional<DeviceLock> lease;
        if (request.target.android) lease.emplace(request.target.serial, cancel);
        RefuseActiveReplay(request, cancel);
        result["backend"] = BackendCapability(request, cancel);
        result["timestamps"] = {{"status", "advertised"}, {"runtime_validation", "required during replay"}};
        if (countersRequested) {
            try {
                if (!request.target.android) throw std::runtime_error("GPU counters require an Android OEM GPU counter provider");
                if (result.at("backend").value("frame_counter_window_protocol", 0) != 1)
                    throw std::runtime_error("Replay backend does not advertise frame counter-window protocol 1");
                AndroidGpuCounters counters(Tool(request, "adb"), request.target.serial, request.output / "capabilities", fs::u8path(Tool(request, "gpu_counter_producer")), cancel);
                result["counters"] = counters.Discover(request.counterProfile, request.counterPeriodNs);
            } catch (const DeviceBusy&) { throw; }
            catch (const std::exception& error) {
                Cancelled(cancel); result["counters"] = {{"status", "unavailable"}, {"profile", request.counterProfile}, {"reason", error.what()}};
            }
        } else result["counters"] = {{"status", "not_requested"}};
        result["status"] = "supported"; result["available"] = true;
        result["detail"] = countersRequested ? "Selected counters discovered; short frame windows may contain zero or few samples. GPU timestamps are measured independently." :
            "GPU timestamp backend advertised; runtime queue support is validated during each fresh frame replay.";
        if (countersRequested && result.at("counters").value("status", "") == "unavailable")
            result["detail"] = "GPU timing supported; requested counters unavailable: " + result.at("counters").value("reason", std::string{});
    } catch (const DeviceBusy&) { throw; }
    catch (const std::exception& error) { Cancelled(cancel); result["reason"] = error.what(); result["detail"] = error.what(); }
    return result;
}

Json BuildPerfSessionResult(const PerfSessionRequest& request, const Json& runs) {
    if (!runs.is_array() || runs.empty()) throw std::runtime_error("No completed performance runs");
    Json result{{"schema", "gfxrecon-perf/1"}, {"status", "complete"}, {"mode", "single-frame"},
        {"output_dir", request.output.u8string()}, {"policy", Policy(request)}, {"runs_requested", request.runs}, {"runs", runs},
        {"capture", {{"path", request.capture ? request.capture->source.u8string() : std::string{}}, {"sha256", request.capture ? request.capture->fingerprint : std::string{}}}},
        {"target", {{"label", request.target.Label()}, {"serial", request.target.serial}}},
        {"metrics", Json::array()}, {"comparisons", Json::array()}, {"result_cache_used", false},
        {"notes", {"Each sample comes from a fresh replay process. GPU timestamps cover the selected frame on one command queue.",
            "Counters are system-wide samples filtered to the correlated GPU interval; hardware averaging-window extents and per-shader attribution remain unverified.",
            "A short frame can contain zero or one counter sample. Missing counter data does not invalidate a valid GPU duration."}}};
    if (request.counterProfile == "bandwidth") result["notes"].push_back("Bandwidth metrics are provider-labelled byte/second rates, not exact bytes per frame.");
    if (request.counterProfile == "precision") result["notes"].push_back("Full/half metrics are fragment instruction rates, not FP16 utilization or proof of a particular shader conversion.");
    std::map<std::string, std::map<std::string, std::vector<double>>> values;
    std::map<std::string, Json> definitions;
    std::set<std::string> conflictingDefinitions;
    definitions["gpu_time"] = {{"key", "gpu_time"}, {"name", "GPU frame elapsed"}, {"unit", "ms"}, {"scope", "isolated frame render interval"}};
    for (const auto& run : runs) {
        const auto variant = run.at("variant").get<std::string>();
        auto gpu = MeasuredGpu(run.at("gpu"));
        values["gpu_time"][variant].insert(values["gpu_time"][variant].end(), gpu.begin(), gpu.end());
        const auto collection = run.value("counters", Json::object());
        const auto begin = collection.value("window_begin_boot_ns", uint64_t(0)), end = collection.value("window_end_boot_ns", uint64_t(0));
        auto counters = collection.value("counters", Json::array());
        // Retain a zero-sample row for every selected descriptor, including an
        // unavailable clock window. Raw traces/decoded files remain artifacts.
        for (const auto& spec : run.value("counter_selection", Json::object()).value("specs", Json::array())) {
            const auto found = std::find_if(counters.begin(), counters.end(), [&](const Json& counter) { return counter.at("id") == spec.at("id"); });
            if (found == counters.end()) counters.push_back({{"id", spec.at("id")}, {"name", spec.at("name")}, {"unit", spec.at("unit")}, {"samples", Json::array()}});
        }
        for (const auto& counter : counters) {
            const auto key = "counter_" + std::to_string(counter.at("id").get<uint64_t>());
            if (definitions.count(key) && (definitions[key].at("name") != counter.at("name") || definitions[key].at("unit") != counter.at("unit"))) {
                conflictingDefinitions.insert(key); continue;
            }
            definitions[key] = {{"key", key}, {"counter_id", counter.at("id")}, {"name", counter.at("name")}, {"unit", counter.at("unit")},
                {"description",counter.value("description",std::string{})},
                {"scope", "system-wide samples in frame GPU window"}, {"aggregation", "sampled_mean; hardware sample-window extents unverified"}};
            auto& measured = values[key][variant];
            if (!begin || end <= begin) continue;
            for (const auto& point : counter.value("samples", Json::array())) {
                const auto time = point.at("timestamp_boot_ns").get<uint64_t>();
                if (time >= begin && time <= end) measured.push_back(point.at("value").get<double>());
            }
        }
    }
    bool comparable = true, counterComparable = true;
    if (!request.overrides.empty()) {
        if (runs.size() % 2) comparable = counterComparable = false;
        for (size_t i = 0; i + 1 < runs.size(); i += 2) {
            auto comparison = CompareRuns(runs[i], runs[i + 1]); comparable = comparable && comparison.at("status") == "comparable";
            counterComparable = counterComparable && comparison.at("counter_comparable").get<bool>();
            result["comparisons"].push_back(std::move(comparison));
        }
    }
    for (const auto& [key, definition] : definitions) {
        auto metric = definition;
        if (conflictingDefinitions.count(key)) values[key].clear();
        metric["baseline"] = SummarizePerfValues(values[key]["baseline"]); metric["modified"] = SummarizePerfValues(values[key]["modified"]);
        metric["sample_counts"] = {{"baseline", values[key]["baseline"].size()}, {"modified", values[key]["modified"].size()}};
        metric["status"] = metric["baseline"].is_null() && metric["modified"].is_null() ? "unavailable" : "available";
        metric["quality"] = Json::array();
        if (conflictingDefinitions.count(key)) metric["quality"].push_back("Counter name or units changed across runs; only raw per-run evidence is retained");
        if (key != "gpu_time") {
            for(const auto& run:runs)for(const auto& counter:run.value("counters",Json::object()).value("counters",Json::array()))
                if(counter.at("id")==metric.at("counter_id")&&counter.contains("reason"))
                    metric["quality"].push_back(run.value("variant","baseline")+": "+counter.at("reason").get<std::string>());
            for (const auto* variant : {"baseline", "modified"}) {
                if (std::string(variant) == "modified" && request.overrides.empty() && values[key][variant].empty()) continue;
                const auto count = values[key][variant].size();
                if (!count) metric["quality"].push_back(std::string(variant) + ": no counter samples in frame window");
                else if (count == 1) { metric["status"] = "sparse"; metric["quality"].push_back(std::string(variant) + ": single counter sample; sparse estimate"); }
            }
        }
        metric["comparable"] = (key == "gpu_time" ? comparable : counterComparable) && !metric["baseline"].is_null() && !metric["modified"].is_null();
        if (metric["comparable"].get<bool>()) {
            const double b = metric["baseline"]["mean"], m = metric["modified"]["mean"];
            metric["delta"] = m - b; metric["delta_percent"] = b != 0 ? Json((m - b) / std::abs(b) * 100.0) : Json(nullptr);
        }
        result["metrics"].push_back(std::move(metric));
    }
    return result;
}

Json RunPerfSession(const PerfSessionRequest& request, const std::atomic_bool* cancel) {
    // Invalid requests and unrelated output directories never become owned.
    // The CLI may already have created this directory and staged its exact
    // compiled override inputs; no previous performance artifacts are reusable.
    ValidatePerfSessionRequest(request);
    if (request.output.empty()) throw std::runtime_error("Performance output directory is required");
    const auto output = fs::absolute(request.output).lexically_normal();
    if (fs::is_symlink(fs::symlink_status(output))) throw std::runtime_error("Use a new performance output directory, not a symbolic link");
    std::set<fs::path> stagedOverrides;
    for (const auto& [module, file] : request.overrides) {
        const auto staged = output / "overrides" / ("sh" + std::to_string(module) + ".spv");
        if (fs::absolute(file).lexically_normal() == staged) stagedOverrides.insert(staged);
    }
    auto checkContents = [&] {
        for (const auto& entry : fs::recursive_directory_iterator(output)) {
            const auto path = entry.path().lexically_normal();
            if (entry.is_symlink() || (!entry.is_directory() && !stagedOverrides.count(path)) ||
                (entry.is_directory() && path != output / "overrides"))
                throw std::runtime_error("Use a new performance output directory; previous artifacts or unrelated files are present");
        }
    };
    if (fs::exists(output)) {
        if (!fs::is_directory(output)) throw std::runtime_error("Performance output must be a new directory");
        checkContents();
    } else {
        if (!output.parent_path().empty()) fs::create_directories(output.parent_path());
        if (!fs::create_directory(output)) throw std::runtime_error("Performance output was claimed by another operation");
    }
    // An atomic directory claim prevents two sessions from sharing one output.
    // Retain the marker with the artifacts so retries cannot overwrite evidence.
    if (!fs::create_directory(output / ".perf-session-owner")) throw std::runtime_error("Performance output is already owned by another session");
    std::string sessionId = request.sessionId;
    std::string stage = "initialization";
    Json runs = Json::array();
    try {
        if (sessionId.empty()) sessionId = Sha256Text(output.u8string() + ":" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto phase = [&](const char* name) {
            stage = name;
            if (request.progress) request.progress("Performance phase: " + stage);
            Cancelled(cancel);
        };
        AtomicJson(output / ".perf-session-owner" / "session.json", {{"schema", "gfxrecon-perf-session/1"}, {"session_id", sessionId},
            {"capture", request.capture->source.u8string()}, {"frame", request.frame}, {"policy", Policy(request)}});
        Cancelled(cancel);
        phase("preflight");
        const auto plan = BuildGpuMeasurementPlan(*request.capture, request.frame, request.frame, cancel);
        phase("shader_snapshot");
        std::map<uint64_t, fs::path> snapshots;
        for (const auto& [module, file] : request.overrides) {
            const auto hash = Sha256File(file, cancel);
            const auto copy = output / "shader-snapshots" / ("sh" + std::to_string(module) + ".spv");
            fs::create_directories(copy.parent_path()); fs::copy_file(file, copy);
            if (Sha256File(copy, cancel) != hash) throw std::runtime_error("Shader override changed while snapshotting performance inputs");
            snapshots[module] = copy;
        }
        phase("device_lease");
        std::optional<DeviceLock> lease;
        if (request.target.android) lease.emplace(request.target.serial, cancel);
        phase("target_idle_check");
        RefuseActiveReplay(request, cancel);
        phase("backend_capability");
        const auto backend = BackendCapability(request, cancel);
        for (uint32_t index = 1; index <= request.runs; ++index) for (unsigned variant = 0; variant < (snapshots.empty() ? 1u : 2u); ++variant) {
            Cancelled(cancel);
            const std::string name = variant ? "modified" : "baseline";
            const auto directory = request.output / "runs" / (name + "-" + std::to_string(index));
            if (request.progress) request.progress("Preparing " + name + " performance session " + std::to_string(index));
            std::unique_ptr<AndroidGpuCounters> counters;
            Json selection = Json::object();
            Json collected{{"status", request.counterProfile == "none" ? "not_requested" : "unavailable"},
                {"profile", request.counterProfile}, {"counters", Json::array()}};
            if (request.counterProfile != "none") collected["reason"] = "Counter collection did not start";
            bool counterStarted = false;
            if (request.counterProfile != "none") {
                phase("counter_discovery");
                try {
                    if (!request.target.android) throw std::runtime_error("GPU counters require an Android OEM GPU counter provider");
                    if (backend.value("frame_counter_window_protocol", 0) != 1)
                        throw std::runtime_error("Replay backend does not advertise frame counter-window protocol 1");
                    counters = std::make_unique<AndroidGpuCounters>(Tool(request, "adb"), request.target.serial,
                        request.output / "counter-runs" / (name + "-" + std::to_string(index)), fs::u8path(Tool(request, "gpu_counter_producer")), cancel);
                    selection = counters->Discover(request.counterProfile, request.counterPeriodNs);
                } catch (const DeviceBusy&) { throw; }
                catch (const std::exception& error) { Cancelled(cancel); collected["reason"] = error.what(); counters.reset(); }
            }
            ReplayRequest replay;
            replay.capture = request.capture->source; replay.expectedCaptureHash = request.capture->fingerprint;
            replay.output = directory; replay.cacheDirectory = request.cacheDirectory;
            replay.frame = request.frame; replay.prefix = name; replay.swapchainMode = request.swapchainMode;
            replay.memoryTranslation = request.memoryTranslation;
            replay.timeout = request.timeout; replay.progress = request.progress; replay.screenOff = request.screenOff;
            replay.optimize = false; replay.reuseArtifacts = false;
            replay.measurement.enabled = true; replay.measurement.gpu = true;
            replay.measurement.frameRange = std::to_string(request.frame) + "-" + std::to_string(uint64_t(request.frame) + 1);
            replay.measurement.gpuPlan = plan;
            if (variant) replay.overrides = snapshots;
            if (counters) {
                replay.measurement.counterWindow = {{"protocol", 1}, {"period_ns", request.counterPeriodNs}};
                replay.measurementReady = [&](const Json&) {
                    phase("counter_start");
                    try {
                        counters->Start(uint64_t(std::min<int64_t>(60, request.timeout.count())) * 1000);
                        counterStarted = true;
                    } catch (const DeviceBusy&) { throw; }
                    catch (const std::exception& error) { Cancelled(cancel); collected["reason"] = error.what(); counters->Cleanup(); }
                    phase("replay");
                };
            }
            ReplayResult completed;
            phase("replay");
            try { completed = ReplayCapture(replay, request.target, request.tools, cancel); }
            catch (...) {
                if (counterStarted && !(cancel && cancel->load())) try { counters->StopAndDecode(0, 0); } catch (...) {}
                throw;
            }
            if (counterStarted) {
                phase("counter_decode");
                const auto window = completed.gpuMeasurement.value("counter_window", Json::object());
                const bool available = window.value("status", "unavailable") == "available";
                try {
                    collected = counters->StopAndDecode(available ? window.at("start_boot_ns").get<uint64_t>() : 0,
                        available ? window.at("end_boot_ns").get<uint64_t>() : 0);
                } catch (const DeviceBusy&) { throw; }
                catch (const std::exception& error) { Cancelled(cancel); collected["status"] = "unavailable"; collected["reason"] = error.what(); }
                collected["gpu_window"] = window;
                if (!available) {
                    collected["status"] = "unavailable"; collected["counter_window_reason"] = window.value("reason", "GPU-to-counter clock window unavailable");
                    if (!collected.contains("reason")) collected["reason"] = collected.at("counter_window_reason");
                }
            }
            const auto summary = SummarizePerfValues(MeasuredGpu(completed.gpuMeasurement));
            if (summary.is_null()) throw std::runtime_error("No valid selected-frame GPU timestamps were produced");
            Json run{{"variant", name}, {"index", index}, {"status", "complete"}, {"output_dir", directory.u8string()},
                {"manifest", (directory / "manifest.json").u8string()}, {"gpu_report", (directory / "gpu-measurement.json").u8string()},
                {"policy", Policy(request)}, {"gpu", completed.gpuMeasurement}, {"gpu_summary", summary}, {"counters", collected}};
            if (request.counterProfile != "none") run["counter_selection"] = selection;
            phase("run_report");
            AtomicJson(directory / "perf-run.json", run); runs.push_back(std::move(run));
            AtomicJson(request.output / "partial-result.json", {{"status", "running"}, {"runs", runs}});
        }
        phase("report");
        auto result = BuildPerfSessionResult(request, runs);
        result["session_id"] = sessionId;
        result["artifacts"] = {{"gpu_samples_csv", (request.output / "gpu-samples.csv").u8string()},
            {"counter_samples_csv", (request.output / "counter-samples.csv").u8string()}};
        ExportCsv(request.output, runs); AtomicJson(request.output / "result.json", result);
        if (request.progress) request.progress("Performance session completed");
        return result;
    } catch (const std::exception& error) {
        // Receipt/metadata failures must never replace the original exception,
        // including the DeviceBusy type used by CLI exit-code handling.
        try {
            const bool cancelled = cancel && cancel->load();
            Json failure{{"schema", "gfxrecon-perf-failure/1"}, {"session_id", sessionId}, {"status", cancelled ? "cancelled" : "failed"},
                {"stage", stage}, {"cancelled", cancelled}, {"message", error.what()}, {"completed_runs", runs}};
            try { failure["policy"] = Policy(request); } catch (...) {}
            AtomicJson(output / "failure.json", failure);
        } catch (...) {}
        throw;
    }
}
}
