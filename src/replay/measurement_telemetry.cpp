#include "replay/measurement_telemetry.h"
#include "platform/adb.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace gfx {
namespace {

using Clock = std::chrono::steady_clock;

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

int64_t HostMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

Json Interval(int64_t begin, Clock::time_point monotonic) {
    return {{"host_started_unix_ms", begin}, {"host_finished_unix_ms", HostMilliseconds()},
            {"elapsed_ms", std::chrono::duration<double, std::milli>(Clock::now() - monotonic).count()},
            {"device_timestamp", nullptr}};
}

void CheckCancelled(const std::atomic_bool* cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Operation cancelled.");
}

Json Unavailable(const std::string& status, const std::string& reason) {
    return {{"status", status}, {"reason", reason}};
}

Json Failure(const ProcessResult& result) {
    if (result.timedOut) return Unavailable("error", "query_timed_out");
    auto lower = result.output;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    Json failure;
    if (lower.find("permission denied") != std::string::npos ||
        lower.find("permission denial") != std::string::npos ||
        lower.find("not allowed") != std::string::npos)
        failure = Unavailable("permission_denied", "source_access_denied");
    else if (lower.find("no such file") != std::string::npos ||
             lower.find("not found") != std::string::npos ||
             lower.find("can't find service") != std::string::npos)
        failure = Unavailable("unsupported", "source_not_present");
    else
        failure = Unavailable("error", result.outputTruncated ? "query_output_truncated" : "query_failed");
    failure["detail"] = result.output.substr(0, 2048);
    failure["exit_code"] = result.exitCode;
    return failure;
}

struct Observation {
    ProcessResult result;
    Json interval;
};

Observation Probe(const std::string& adb, const std::string& serial,
                  const std::vector<std::string>& words, std::chrono::milliseconds timeout,
                  const std::atomic_bool* cancel, const MeasurementTelemetryTransport& transport) {
    CheckCancelled(cancel);
    ProcessRequest request{adb, {"-s", serial, "shell", AndroidCommand(words)}};
    request.allowChildServices = true;
    request.timeout = timeout;
    request.outputLimit = words.front() == "dumpsys" ? 128 * 1024 : 4096;
    const auto started = HostMilliseconds();
    const auto monotonic = Clock::now();
    ProcessResult result;
    try {
        result = transport ? transport(request, cancel) : RunProcess(request, cancel);
    } catch (const std::exception& error) {
        CheckCancelled(cancel);
        result.output = error.what();
    }
    if (result.cancelled) throw std::runtime_error("Operation cancelled.");
    CheckCancelled(cancel);
    return {std::move(result), Interval(started, monotonic)};
}

} // namespace

Json ParseAndroidThermalTelemetry(const std::string& text) {
    Json status = Unavailable("unsupported", "thermal_status_not_reported");
    Json sensors = Json::array();
    bool current = false, currentBlock = false, cachedBlock = false;
    size_t invalidSensors = 0;
    static const std::regex severity("^Thermal Status:\\s*([0-6])\\s*$");
    static const std::regex temperature(
        "Temperature\\{mValue=([^,}]+),\\s*mType=([^,}]+),\\s*mName=([^,}]+),\\s*mStatus=([^,}]+)\\}");
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const auto stripped = Trim(line);
        std::smatch match;
        if (std::regex_match(stripped, match, severity))
            status = {{"status", "available"}, {"value", std::stoi(match[1].str())},
                      {"unit", "android_thermal_severity"}, {"freshness", "service_report"}};
        if (stripped == "Cached temperatures:") cachedBlock = true;
        if (stripped == "Current temperatures from HAL:") {
            current = currentBlock = true;
            continue;
        }
        // Top-level section headings terminate the current-HAL sensor section.
        if (current && !stripped.empty() && !line.empty() &&
            !std::isspace(static_cast<unsigned char>(line.front())) &&
            stripped.rfind("Temperature{", 0) != 0)
            current = false;
        if (!current || !std::regex_search(stripped, match, temperature)) continue;
        try {
            size_t used = 0;
            const auto typeText = Trim(match[2].str());
            const auto type = std::stoi(typeText, &used);
            if (used != typeText.size() || type != 1) continue; // Android TYPE_GPU only.
            const auto valueText = Trim(match[1].str());
            const auto value = std::stod(valueText, &used);
            if (used != valueText.size() || !std::isfinite(value)) { ++invalidSensors; continue; }
            const auto severityText = Trim(match[4].str());
            const auto sensorStatus = std::stoi(severityText, &used);
            if (used != severityText.size() || sensorStatus < 0 || sensorStatus > 6) {
                ++invalidSensors; continue;
            }
            const auto name = Trim(match[3].str());
            if (name.empty()) { ++invalidSensors; continue; }
            sensors.push_back({{"name", name}, {"sensor_type", "gpu"}, {"sensor_type_id", type},
                               {"value", value}, {"unit", "celsius"}, {"thermal_status", sensorStatus},
                               {"freshness", "current_hal_report"}, {"device_timestamp", nullptr}});
        } catch (const std::exception&) { ++invalidSensors; }
    }
    Json temperatures = sensors.empty()
        ? Unavailable("unsupported", currentBlock ? "current_hal_gpu_temperatures_unavailable"
                                                  : "current_hal_block_not_reported")
        : Json{{"status", "available"}};
    temperatures["sensors"] = std::move(sensors);
    temperatures["freshness"] = currentBlock ? "current_hal_report" : "unavailable";
    temperatures["cached_readings_ignored"] = cachedBlock;
    temperatures["invalid_sensor_records"] = invalidSensors;
    const bool available = status["status"] == "available" || temperatures["status"] == "available";
    return {{"status", available ? "available" : "unsupported"},
            {"source", "dumpsys thermalservice"}, {"access_context", "adb_shell"},
            {"thermal_status", status}, {"gpu_temperatures", temperatures}};
}

Json QueryMeasurementTelemetry(const std::string& adb, const std::string& serial,
                               const std::atomic_bool* cancel,
                               const MeasurementTelemetryTransport& transport) {
    CheckCancelled(cancel);
    const auto started = HostMilliseconds();
    const auto monotonic = Clock::now();
    Json snapshot{{"schema", 1}, {"kind", "before_or_after_replay_snapshot"}, {"continuous", false},
                  {"time_domain", "host_unix_ms"}, {"device_serial", serial}};
    if (adb.empty() || serial.empty()) {
        snapshot["thermal"] = Unavailable("unsupported", "android_adb_telemetry_not_available_for_target");
        snapshot["gpu_clock"] = snapshot["thermal"];
        snapshot["observation"] = Interval(started, monotonic);
        return snapshot;
    }

    const auto thermal = Probe(adb, serial, {"dumpsys", "thermalservice"},
                               std::chrono::seconds(3), cancel, transport);
    // Some dumpsys services print a permission denial but still exit successfully.
    const auto thermalFailure = Failure(thermal.result);
    snapshot["thermal"] = thermal.result.Success() && !thermal.result.outputTruncated &&
                          thermalFailure["status"] != "permission_denied"
        ? ParseAndroidThermalTelemetry(thermal.result.output) : thermalFailure;
    snapshot["thermal"]["source"] = "dumpsys thermalservice";
    snapshot["thermal"]["access_context"] = "adb_shell";
    snapshot["thermal"]["observation"] = thermal.interval;

    // Fixed read-only KGSL candidates. No globbing, privileged reads or frequency controls.
    // gpuclk uses Hz in KGSL; the explicit clock_mhz attribute reports MHz.
    static const std::pair<const char*, const char*> paths[] = {
        {"/sys/class/kgsl/kgsl-3d0/gpuclk", "hertz"},
        {"/sys/class/kgsl/kgsl-3d0/clock_mhz", "megahertz"}};
    Json attempts = Json::array();
    Json selected;
    bool denied = false, errored = false;
    static const std::regex integer("[0-9]+");
    for (const auto& path : paths) {
        const auto clock = Probe(adb, serial, {"cat", path.first},
                                 std::chrono::seconds(1), cancel, transport);
        Json value;
        if (!clock.result.Success() || clock.result.outputTruncated) value = Failure(clock.result);
        else {
            const auto raw = Trim(clock.result.output);
            try {
                if (!std::regex_match(raw, integer)) throw std::runtime_error("invalid clock");
                value = {{"status", "available"}, {"value", std::stoull(raw)},
                         {"raw_value", raw}, {"unit", path.second}};
            } catch (const std::exception&) {
                value = Unavailable("error", "unexpected_clock_value");
                value["detail"] = raw.substr(0, 256);
            }
        }
        value["source"] = path.first;
        value["access_context"] = "adb_shell";
        value["freshness"] = value["status"] == "available" ? "sysfs_read" : "unavailable";
        value["observation"] = clock.interval;
        denied = denied || value["status"] == "permission_denied";
        errored = errored || value["status"] == "error";
        attempts.push_back(value);
        if (value["status"] == "available") { selected = value; break; }
    }
    snapshot["gpu_clock"] = selected.is_null()
        ? Unavailable(denied ? "permission_denied" : errored ? "error" : "unsupported",
                      "no_readable_validated_gpu_clock_source") : selected;
    snapshot["gpu_clock"]["attempts"] = std::move(attempts);
    snapshot["observation"] = Interval(started, monotonic);
    return snapshot;
}

} // namespace gfx
