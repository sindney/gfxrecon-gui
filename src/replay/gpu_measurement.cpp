#include "replay/gpu_measurement.h"
#include "core/artifacts.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>

namespace gfx {
namespace {
[[noreturn]] void Unsupported(const std::string& message) {
    throw std::runtime_error("GPU timing unsupported: " + message + ". Stop and discuss a supported measurement scope.");
}
[[noreturn]] void Invalid(const std::string& message) {
    throw std::runtime_error("Invalid GPU measurement report: " + message);
}
uint64_t Unsigned(const Json& object, const char* key) {
    if (!object.contains(key) || (!object[key].is_number_unsigned() && !object[key].is_number_integer()) ||
        (object[key].is_number_integer() && !object[key].is_number_unsigned() && object[key].get<int64_t>() < 0))
        Invalid(std::string("missing or invalid integer ") + key);
    return object[key].get<uint64_t>();
}
bool HasNext(const Json& value) {
    return !value.is_null() && !(value.is_array() && value.empty()) && !(value.is_number() && value == 0);
}
bool Successful(const Json& call) {
    const auto result = call.value("return", Json{});
    return !(result.is_string() && result.get<std::string>().rfind("VK_ERROR", 0) == 0) &&
        !(result.is_number_integer() && result.get<int64_t>() < 0);
}
bool SuspendedRendering(const Json& args) {
    const auto info = args.value("pRenderingInfo", Json::object());
    if (!info.is_object()) return true;
    const auto flags = info.value("flags", Json(0));
    if (flags.is_string()) {
        const auto text = flags.get<std::string>();
        return text.find("SUSPENDING") != std::string::npos || text.find("RESUMING") != std::string::npos;
    }
    return !flags.is_number_unsigned() && !flags.is_number_integer() ? true : (flags.get<uint64_t>() & 6) != 0;
}
}

Json BuildGpuMeasurementPlan(const Capture& capture, uint32_t first, uint32_t last,
                             const std::atomic_bool* cancel) {
    if (!first || last < first || last > capture.frames) Unsupported("selected frames are outside the capture");
    std::ifstream input(capture.jsonl, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read imported capture for GPU measurement planning");
    std::map<uint32_t, Json> frames;
    std::map<uint32_t, std::set<uint64_t>> queues;
    std::set<uint32_t> completed;
    uint32_t frame = 1, markerFrame = 1;
    bool inState = false;
    std::string line;
    while (std::getline(input, line)) {
        if (cancel && cancel->load()) throw std::runtime_error("GPU measurement planning cancelled");
        if (line.empty() || line == "\r") continue;
        const auto record = Json::parse(line);
        if (record.contains("state")) {
            const auto marker = record.at("state").value("marker_type", std::string{});
            if (marker == "BeginMarker") inState = true;
            else if (marker == "EndMarker") inState = false;
            else Unsupported("unrecognized trimmed-state boundary");
            continue;
        }
        if (record.contains("frame")) {
            const auto marker = record.at("frame").value("marker_type", std::string{});
            if (marker == "EndMarker") { completed.insert(markerFrame); frame = ++markerFrame; }
            continue;
        }
        const char* kind = record.contains("function") ? "function" : record.contains("meta") ? "meta" : nullptr;
        if (!kind || frame > last) continue;
        const auto& call = record.at(kind);
        const auto name = call.value("name", std::string{});
        const auto args = call.value("args", Json::object());
        if (!Successful(call)) {
            if (!inState && name == "vkQueuePresentKHR")
                Unsupported("failed frame presentation prevents proving complete frame boundaries");
            continue;
        }
        // These features can introduce unbounded external dependencies or work
        // outside the submit stream. Reject conservatively instead of omitting it.
        if (name == "vkQueueBindSparse" || name.find("ImportSemaphore") != std::string::npos ||
            name.find("ImportFence") != std::string::npos)
            Unsupported(name + " requires additional synchronization validation");
        if ((name == "vkCmdBeginRendering" || name == "vkCmdBeginRenderingKHR") && SuspendedRendering(args))
            Unsupported("suspended or resumed dynamic rendering cannot be bracketed safely");
        if (inState || frame > last) continue;
        if (name == "vkQueueSubmit" || name == "vkQueueSubmit2" || name == "vkQueueSubmit2KHR") {
            if (frame < first) continue;
            if (!args.is_object() || !args.contains("queue") || !args.at("queue").is_number()) Unsupported("submission queue identity is unavailable");
            const auto queue = args.at("queue").get<uint64_t>();
            if (!queue) Unsupported("submission queue identity is zero");
            const auto submits = args.value("pSubmits", Json::array());
            if (!submits.is_array() || submits.empty()) Unsupported("empty GPU submission batch");
            for (const auto& submit : submits) {
                if (!submit.is_object()) Unsupported("malformed submission batch");
                if (HasNext(submit.value("pNext", Json{}))) Unsupported("submission extension chains need explicit support");
                const auto flags = submit.value("flags", Json(0));
                if ((flags.is_number() && flags.get<uint64_t>() != 0) ||
                    (flags.is_string() && flags.get<std::string>() != "0" && !flags.get<std::string>().empty()))
                    Unsupported("protected or otherwise flagged submissions");
                for (const auto& cb : submit.value("pCommandBufferInfos", Json::array()))
                    if (HasNext(cb.value("pNext", Json{})) || cb.value("deviceMask", uint64_t(1)) > 1)
                        Unsupported("device-group command buffer submissions");
            }
            queues[frame].insert(queue);
            if (queues[frame].size() != 1) Unsupported("frame " + std::to_string(frame) + " uses multiple command queues");
            auto& plan = frames[frame];
            if (plan.is_null()) plan = {{"frame", frame}, {"queue_id", queue}, {"submit_blocks", Json::array()}};
            const auto block = record.at("index").get<uint64_t>();
            plan["submit_blocks"].push_back(block);
        }
        if (name == "vkQueuePresentKHR") { completed.insert(frame); ++frame; }
    }
    if (input.bad()) throw std::runtime_error("Cannot finish reading GPU measurement plan");
    Json result{{"mode", "isolated-frame"}, {"frames", Json::array()},
        {"capture_sha256", capture.fingerprint.empty() ? Sha256File(capture.source, cancel) : capture.fingerprint},
        {"index_sha256", Sha256File(capture.jsonl, cancel)}, {"first_frame", first}, {"last_frame", last}};
    for (uint32_t selected = first; selected <= last; ++selected) {
        const auto found = frames.find(selected);
        if (found == frames.end()) Unsupported("frame " + std::to_string(selected) + " contains no known GPU submissions");
        if (!completed.count(selected)) Unsupported("frame " + std::to_string(selected) + " has no complete captured boundary");
        auto plan = found->second;
        plan["first_submit"] = plan.at("submit_blocks").front();
        plan["last_submit"] = plan.at("submit_blocks").back();
        result["frames"].push_back(std::move(plan));
        if (selected == UINT32_MAX) break;
    }
    return result;
}

void VerifyGpuMeasurementReport(const Json& request, const Json& report) {
    try {
        if (report.value("protocol", 0) != 1 || report.value("mode", "") != "isolated-frame") Invalid("unsupported protocol or mode");
        for (const char* field : {"request_id", "capture_sha256", "backend_sha256"})
            if (!request.contains(field) || !report.contains(field) || request.at(field) != report.at(field)) Invalid(std::string("mismatched ") + field);
        if (report.value("status", "") != "complete") {
            const auto error = report.value("error", Json{});
            Invalid(error.is_object() ? error.value("message", "backend did not complete") : "backend did not complete");
        }
        if (report.contains("error") && !report.at("error").is_null()) Invalid("completed report also contains an error");
        if (request.contains("swapchain_mode")) {
            const auto requested = request.at("swapchain_mode").get<std::string>();
            if (requested != "virtual" && requested != "captured" && requested != "offscreen") Invalid("unsupported requested swapchain mode");
            if (!report.contains("swapchain") || !report.at("swapchain").is_object())
                Invalid("backend lacks effective swapchain evidence; rebuild/update the bundled backend");
            const auto& swapchain = report.at("swapchain");
            if (swapchain.value("requested", "") != requested || swapchain.value("effective", "") != requested ||
                swapchain.value("source", "") != "backend_swapchain_class")
                Invalid("requested and effective swapchain modes do not match");
        }
        const auto& device = report.at("device");
        if (!device.is_object() || device.value("device_name", "").empty()) Invalid("actual replay GPU identity is missing");
        for (const char* field : {"vendor_id", "device_id", "api_version", "driver_version"}) Unsigned(device, field);
        if (request.contains("sustained")) Invalid("inner-loop sustained replay is unsupported; use fresh outer replays");
        const auto& expected = request.at("frames");
        const auto& samples = report.at("samples");
        if (!expected.is_array() || expected.empty() || !samples.is_array() || samples.size() != expected.size()) Invalid("frame sample count does not match request");
        std::set<uint64_t> seen;
        for (const auto& frame : expected) {
            const auto number = Unsigned(frame, "frame");
            const auto sample = std::find_if(samples.begin(), samples.end(), [&](const Json& candidate) { return candidate.value("frame", uint64_t(0)) == number; });
            if (sample == samples.end() || !seen.insert(number).second) Invalid("missing or duplicate requested frame");
            if (!sample->value("valid", false)) Invalid("frame " + std::to_string(number) + " is not valid");
            if (sample->contains("error") && !sample->at("error").is_null()) Invalid("valid sample also contains an error");
            for (const char* field : {"queue_id", "first_submit", "last_submit", "submit_blocks"})
                if (!sample->contains(field) || sample->at(field) != frame.at(field)) Invalid(std::string("mismatched sample ") + field);
            if (sample->value("execution_mode", "") != "isolated-frame" || sample->value("completion", "") != "owned_fence" ||
                sample->value("start_stage", "") != "TOP_OF_PIPE" || sample->value("end_stage", "") != "BOTTOM_OF_PIPE")
                Invalid("unrecognized sample scope or synchronization");
            if (sample->value("clock_domain", "") != "calibrated_device" &&
                !(frame.at("first_submit") == frame.at("last_submit") && sample->value("clock_domain", "") == "same_submission"))
                Invalid("timestamp domain is not comparable");
            const auto bits = Unsigned(*sample, "timestamp_valid_bits");
            if (!bits || bits > 64) Invalid("timestamp valid bits are unsupported");
            const auto start = Unsigned(*sample, "start_tick"), end = Unsigned(*sample, "end_tick");
            const double period = sample->at("timestamp_period_ns").get<double>();
            if (!(period > 0) || !std::isfinite(period)) Invalid("invalid timestamp period");
            const auto timeout = Unsigned(request, "timeout_ns");
            const long double wrapNs = std::ldexp(static_cast<long double>(period), static_cast<int>(bits));
            if (!timeout || static_cast<long double>(timeout) >= wrapNs) Invalid("ambiguous timestamp wrap bound");
            const uint64_t mask = bits == 64 ? UINT64_MAX : (uint64_t(1) << bits) - 1;
            const long double calculated = static_cast<long double>((end - start) & mask) * period;
            const double ns = sample->at("elapsed_ns").get<double>(), ms = sample->at("elapsed_ms").get<double>();
            if (!std::isfinite(ns) || !std::isfinite(ms) || ns < 0 || ms < 0 || calculated > timeout ||
                std::abs(static_cast<long double>(ns) - calculated) > std::max(0.001L, calculated * 1e-9L) ||
                std::abs(ms * 1e6 - ns) > std::max(0.001, ns * 1e-9)) Invalid("GPU duration does not match valid timestamp inputs");
            if (!sample->value("start_available", false) || !sample->value("end_available", false)) Invalid("timestamp endpoint availability is missing");
        }
        const auto& replacements = report.at("overrides");
        if (!replacements.is_array() || replacements.size() != request.at("overrides").size()) Invalid("replacement evidence count does not match request");
        for (const auto& replacement : request.at("overrides")) {
            size_t matches = 0;
            for (const auto& evidence : replacements) if (evidence.at("module") == replacement.at("module")) {
                ++matches;
                if (!evidence.value("applied", false) || evidence.value("creation_result", -1) != 0 ||
                    evidence.at("sha256") != replacement.at("sha256") ||
                    evidence.value("actual_sha256", std::string{}) != replacement.at("sha256")) Invalid("shader replacement was not successfully applied with requested bytes");
            }
            if (matches != 1) Invalid("shader replacement evidence is missing or duplicated");
        }
    } catch (const Json::exception& error) {
        Invalid(std::string("malformed evidence: ") + error.what());
    }
}

Json ValidateFrameCounterWindow(const Json& request, const Json& report) {
    if (!request.contains("counter_window")) return Json();
    Json window = report.value("counter_window", Json::object());
    try {
        const auto& options = request.at("counter_window");
        if (options.value("protocol", 0) != 1 || request.at("frames").size() != 1) Invalid("counter windows require protocol 1 and one frame");
        const auto periodNs = Unsigned(options, "period_ns");
        if (!periodNs || periodNs > 1000000000ull) Invalid("invalid counter sampling period");
        if (!window.is_object() || window.value("protocol", 0) != 1 ||
            window.at("frame") != request.at("frames").front().at("frame") || Unsigned(window, "period_ns") != periodNs)
            Invalid("counter window identity or sampling period differs");
        if (window.value("status", "") == "unavailable") {
            if (window.value("reason", std::string{}).empty()) Invalid("unavailable counter window lacks a reason");
            return window;
        }
        if (window.value("status", "") != "available") Invalid("unknown counter window status");
        const auto start = Unsigned(window, "start_boot_ns"), end = Unsigned(window, "end_boot_ns");
        if (!start || end < start) Invalid("invalid counter window endpoints");
        const auto& sample = report.at("samples").front();
        if (Unsigned(sample, "start_boot_ns") != start || Unsigned(sample, "end_boot_ns") != end) Invalid("counter window differs from GPU sample endpoints");
        const auto& bridge = window.at("clock_bridge");
        if (bridge.value("status", "") != "available" || bridge.value("source", "") != "owned_queue_timestamp_bracket" ||
            bridge.value("counter_domain", "") != "BOOTTIME" || bridge.value("host_domain", "") != "CLOCK_BOOTTIME" ||
            bridge.value("completion", "") != "owned_fence" || !bridge.value("marker_query_available", false))
            Invalid("counter clock bridge lacks a completed owned GPU marker");
        const auto bits = Unsigned(bridge, "timestamp_valid_bits");
        const auto period = bridge.at("timestamp_period_ns").get<double>();
        if (!bits || bits > 64 || !std::isfinite(period) || period <= 0 ||
            bits != Unsigned(sample, "timestamp_valid_bits") || period != sample.at("timestamp_period_ns").get<double>())
            Invalid("counter bridge timestamp units differ from GPU sample");
        const auto before = Unsigned(bridge, "marker_before_boot_ns"), after = Unsigned(bridge, "marker_after_boot_ns");
        const auto midpoint = Unsigned(bridge, "host_timestamp_ns"), uncertainty = Unsigned(bridge, "uncertainty_ns");
        const auto probes = Unsigned(bridge, "probe_count");
        if (!before || after < before || midpoint != before + (after - before) / 2 ||
            static_cast<long double>(uncertainty) < std::ceil(static_cast<long double>(after - before) / 2) + std::ceil(period) ||
            !probes || probes > 3 || !bridge.value("offset_bounds_overlap", false) ||
            bridge.at("host_to_boot_offset_ns") != 0 || bridge.at("queue_id") != sample.at("queue_id"))
            Invalid("counter bridge bracket or uncertainty evidence is inconsistent");
        const auto reference = Unsigned(bridge, "gpu_tick");
        const uint64_t mask = bits == 64 ? UINT64_MAX : (uint64_t(1) << bits) - 1;
        for (const auto& endpoint : {std::pair<const char*, uint64_t>{"start_tick", start}, {"end_tick", end}}) {
            const auto tick = Unsigned(sample, endpoint.first) & mask, marker = reference & mask;
            const auto forward = (tick - marker) & mask;
            const auto delta = forward > mask / 2 ? -static_cast<long double>((marker - tick) & mask) : static_cast<long double>(forward);
            const auto mapped = static_cast<long double>(midpoint) + delta * period;
            const auto tolerance = std::max(1.0L, std::abs(mapped) * std::numeric_limits<long double>::epsilon() * 4);
            if (!std::isfinite(mapped) || mapped < 0 || mapped > INT64_MAX || std::abs(static_cast<long double>(endpoint.second) - mapped) > tolerance)
                Invalid("counter endpoint does not match its owned marker mapping");
        }
        const auto entry = Unsigned(window, "entry_boot_ns"), completed = Unsigned(window, "completion_boot_ns");
        if (!entry || completed < entry || (start < entry && entry - start > uncertainty) || (end > completed && end - completed > uncertainty))
            Invalid("counter endpoints fall outside observed host frame bounds");
        return window; // Wide uncertainty is reported, rather than rejected against period_ns.
    } catch (const std::exception& error) {
        if (!window.is_object()) window = Json::object();
        window["protocol"] = 1; window["status"] = "unavailable"; window["reason"] = error.what();
        try { window["frame"] = request.at("frames").front().at("frame"); window["period_ns"] = request.at("counter_window").at("period_ns"); } catch (const Json::exception&) {}
        if (!window.contains("clock_bridge")) window["clock_bridge"] = {{"status", "unavailable"}};
        window["start_boot_ns"] = nullptr; window["end_boot_ns"] = nullptr;
        window["validation"] = "counter_evidence_rejected_gpu_timing_retained";
        return window;
    }
}

Json VerifyReplaySwapchain(const std::string& requested, const std::string& log,
                          bool requireConfirmation) {
    if (requested != "virtual" && requested != "captured" && requested != "offscreen")
        throw std::runtime_error("Unsupported replay swapchain mode: " + requested);
    static const std::regex marker("GfxReconGui effective swapchain mode: ([a-z]+)");
    std::string effective;
    for (std::sregex_iterator i(log.begin(), log.end(), marker), end; i != end; ++i) {
        const auto observed = (*i)[1].str();
        if (observed != requested)
            throw std::runtime_error("Replay backend selected swapchain mode " + observed + " instead of requested " + requested);
        effective = observed;
    }
    if (effective.empty() && requireConfirmation)
        throw std::runtime_error("Replay backend did not confirm effective swapchain mode " + requested + "; rebuild/update the bundled backend");
    return {{"requested", requested}, {"effective", effective.empty() ? Json(nullptr) : Json(effective)},
        {"source", effective.empty() ? "unavailable" : "backend_swapchain_class"}};
}
}
