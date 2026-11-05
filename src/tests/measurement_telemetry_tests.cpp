#include "replay/measurement_telemetry.h"

#include <stdexcept>

namespace gfx::tests {
namespace {
void Require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
const char* ThermalFixture = R"(Thermal Status: 0
Cached temperatures:
    Temperature{mValue=92.6, mType=0, mName=CPU7, mStatus=0}
    Temperature{mValue=56.2, mType=1, mName=GPU7, mStatus=0}
HAL Ready: true
Current temperatures from HAL:
    Temperature{mValue=28.6, mType=2, mName=battery, mStatus=0}
    Temperature{mValue=30.6, mType=1, mName=GPU0, mStatus=0}
    Temperature{mValue=3.02e1, mType=1, mName=GPU1, mStatus=1}
    Temperature{mValue=NaN, mType=1, mName=GPU2, mStatus=0}
Current cooling devices from HAL:
    Temperature{mValue=99.0, mType=1, mName=not_a_temperature, mStatus=0}
)";
}

void MeasurementTelemetryTests() {
    const auto parsed = ParseAndroidThermalTelemetry(ThermalFixture);
    Require(parsed["status"] == "available", "Current HAL sensors were not parsed");
    Require(parsed["thermal_status"]["value"] == 0, "Thermal severity zero was not preserved");
    const auto& sensors = parsed["gpu_temperatures"]["sensors"];
    Require(sensors.size() == 2, "Thermal parser mixed cached, battery or invalid sensors with GPU readings");
    Require(sensors[0]["name"] == "GPU0" && sensors[0]["value"] == 30.6,
            "Cached GPU temperature was incorrectly labelled current");
    Require(sensors[1]["value"] == 30.2 && sensors[1]["thermal_status"] == 1,
            "Scientific temperature or per-sensor status lost");
    Require(parsed["gpu_temperatures"]["cached_readings_ignored"] == true,
            "Cached-reading freshness limitation was lost");
    Require(parsed["gpu_temperatures"]["invalid_sensor_records"] == 1,
            "Invalid nonfinite GPU sensor was not recorded");

    const auto cached = ParseAndroidThermalTelemetry(
        "Cached temperatures:\n Temperature{mValue=56, mType=1, mName=GPU0, mStatus=0}\n");
    Require(cached["status"] == "unsupported" && cached["gpu_temperatures"]["sensors"].empty(),
            "Cached-only temperatures were reported as current");
    Require(!cached["thermal_status"].contains("value"), "Missing severity became fabricated zero");

    unsigned calls = 0;
    const auto denied = QueryMeasurementTelemetry("fake-adb", "phone-a", nullptr,
        [&](const ProcessRequest& request, const std::atomic_bool*) {
            ++calls;
            Require(request.allowChildServices, "Telemetry must preserve the shared ADB server");
            Require(request.executable == "fake-adb" && request.arguments.size() == 4 &&
                    request.arguments[0] == "-s" && request.arguments[1] == "phone-a" &&
                    request.arguments[2] == "shell", "Telemetry escaped selected ADB target");
            Require(request.timeout <= std::chrono::seconds(3) && request.timeout.count() > 0,
                    "Optional telemetry command has no bounded timeout");
            if (calls == 1) return ProcessResult{0, ThermalFixture};
            Require(request.arguments[3].rfind("'cat' '/sys/class/kgsl/kgsl-3d0/", 0) == 0,
                    "Clock discovery issued a command outside the read-only whitelist");
            return ProcessResult{1, "cat: source: Permission denied\n"};
        });
    Require(calls == 3 && denied["gpu_clock"]["status"] == "permission_denied",
            "Denied clock availability was not retained");
    Require(!denied["gpu_clock"].contains("value") && denied["thermal"]["status"] == "available",
            "Denied clock became fake zero or invalidated usable temperatures");
    Require(denied["gpu_clock"]["attempts"].size() == 2 && denied["continuous"] == false,
            "Telemetry snapshots omit source attempts or claim continuous monitoring");
    Require(denied["observation"].contains("host_started_unix_ms") &&
            denied["thermal"]["observation"].contains("host_finished_unix_ms"),
            "Host observation intervals missing");

    calls = 0;
    const auto readable = QueryMeasurementTelemetry("fake-adb", "phone-a", nullptr,
        [&](const ProcessRequest&, const std::atomic_bool*) {
            ++calls;
            if (calls == 1) return ProcessResult{1, "Can't find service: thermalservice"};
            return ProcessResult{0, "700000000\n"};
        });
    Require(calls == 2 && readable["gpu_clock"]["value"] == 700000000 &&
            readable["gpu_clock"]["unit"] == "hertz", "Readable KGSL GPU clock was not retained");
    Require(readable["thermal"]["status"] == "unsupported", "Missing thermal service invalidated clock query");

    calls = 0;
    const auto deniedDump = QueryMeasurementTelemetry("fake-adb", "phone-a", nullptr,
        [&](const ProcessRequest&, const std::atomic_bool*) {
            ++calls;
            return ProcessResult{calls == 1 ? 0 : 1,
                calls == 1 ? "Permission Denial: requires android.permission.DUMP\n"
                           : "cat: No such file or directory\n"};
        });
    Require(deniedDump["thermal"]["status"] == "permission_denied",
            "Successful dumpsys exit hid a permission denial");

    const auto local = QueryMeasurementTelemetry("", "");
    Require(local["thermal"]["status"] == "unsupported" && local["gpu_clock"]["status"] == "unsupported",
            "Local target lacks explicit telemetry unavailability");

    calls = 0;
    const auto timedOut = QueryMeasurementTelemetry("fake-adb", "phone-a", nullptr,
        [&](const ProcessRequest&, const std::atomic_bool*) {
            ++calls;
            ProcessResult result; result.timedOut = true; return result;
        });
    Require(calls == 3 && timedOut["thermal"]["reason"] == "query_timed_out" &&
            timedOut["gpu_clock"]["status"] == "error", "Timed out optional telemetry became available");

    calls = 0;
    bool cancelled = false;
    try {
        QueryMeasurementTelemetry("fake-adb", "phone-a", nullptr,
            [&](const ProcessRequest&, const std::atomic_bool*) {
                ++calls; ProcessResult result; result.cancelled = true; return result;
            });
    } catch (const std::runtime_error&) { cancelled = true; }
    Require(cancelled && calls == 1, "Cancelled telemetry silently queried more sources");

    std::atomic_bool cancel{true};
    cancelled = false;
    try { QueryMeasurementTelemetry("", "", &cancel); }
    catch (const std::runtime_error&) { cancelled = true; }
    Require(cancelled, "Cancelled local telemetry was ignored");
}
} // namespace gfx::tests
