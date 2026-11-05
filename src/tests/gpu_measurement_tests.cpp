#include "replay/gpu_measurement.h"
#include "core/artifacts.h"
#include "../../cmake/gui_gpu_profiling_sha256.h"

#include <fstream>
#include <stdexcept>

namespace gfx::tests {
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <class F> void Fails(F run, const char* expected) {
    try { run(); }
    catch (const std::exception& error) {
        Require(std::string(error.what()).find(expected) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error(std::string("GPU measurement unexpectedly accepted: ") + expected);
}
Json Submit(uint64_t index, uint64_t queue) {
    return {{"index", index}, {"function", {{"name", "vkQueueSubmit"}, {"return", "VK_SUCCESS"},
        {"args", {{"queue", queue}, {"submitCount", 1}, {"pSubmits", Json::array({{{"pNext", nullptr}, {"pCommandBuffers", Json::array({9})}}})}}}}}};
}
Json Present(uint64_t index) { return {{"index", index}, {"function", {{"name", "vkQueuePresentKHR"}, {"args", {{"queue", 7}}}, {"return", "VK_SUCCESS"}}}}; }
Capture Fixture(const std::filesystem::path& file, const std::vector<Json>& records) {
    { std::ofstream stream(file); for (const auto& record : records) stream << record.dump() << '\n'; }
    auto capture = ReadCapture(file);
    capture->fingerprint = Sha256File(file);
    return *capture;
}
Json Request() {
    return {{"protocol", 1}, {"mode", "isolated-frame"}, {"swapchain_mode", "virtual"}, {"request_id", "unique"}, {"capture_sha256", "capture"},
        {"backend_sha256", "backend"}, {"timeout_ns", 1000000}, {"overrides", Json::array({{{"module", 12}, {"sha256", "shader"}}})},
        {"frames", Json::array({{{"frame", 1}, {"queue_id", 7}, {"first_submit", 10}, {"last_submit", 12}, {"submit_blocks", Json::array({10, 12})}}})}};
}
Json Report(const Json& request) {
    Json sample = request.at("frames").front();
    sample.update({{"valid", true}, {"start_tick", 250}, {"end_tick", 5}, {"timestamp_period_ns", 2.0},
        {"timestamp_valid_bits", 8}, {"elapsed_ns", 22.0}, {"elapsed_ms", 0.000022}, {"start_available", true},
        {"end_available", true}, {"execution_mode", "isolated-frame"}, {"completion", "owned_fence"},
        {"start_stage", "TOP_OF_PIPE"}, {"end_stage", "BOTTOM_OF_PIPE"}, {"clock_domain", "calibrated_device"}});
    Json result = request;
    result["status"] = "complete";
    result["swapchain"] = {{"requested", request.at("swapchain_mode")}, {"effective", request.at("swapchain_mode")}, {"source", "backend_swapchain_class"}};
    result["device"] = {{"device_name", "Test GPU"}, {"vendor_id", 1}, {"device_id", 2}, {"api_version", 3}, {"driver_version", 4}};
    result["samples"] = Json::array({sample});
    result["overrides"] = Json::array({{{"module", 12}, {"sha256", "shader"}, {"actual_sha256", "shader"}, {"applied", true}, {"creation_result", 0}, {"execution_coverage", "unknown"}}});
    return result;
}
}

void GpuMeasurementTests(const std::filesystem::path& root) {
    Require(VerifyReplaySwapchain("virtual", "old backend without mode evidence", false)["effective"].is_null(),
        "Older default CPU replay invented effective mode evidence");
    const auto mode = VerifyReplaySwapchain("offscreen", "[gfxrecon] INFO - GfxReconGui effective swapchain mode: offscreen\n", true);
    Require(mode["requested"] == "offscreen" && mode["effective"] == "offscreen", "Offscreen backend evidence missing");
    Fails([&] { VerifyReplaySwapchain("offscreen", "old backend", true); }, "did not confirm");
    Fails([&] { VerifyReplaySwapchain("offscreen", "GfxReconGui effective swapchain mode: virtual", true); }, "instead of requested");
    Require(gfx_gui_gpu::ShaderSha256("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Empty backend shader hash failed");
    Require(gfx_gui_gpu::ShaderSha256("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "Backend shader hash failed");
    const std::string padding = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Require(gfx_gui_gpu::ShaderSha256(padding.data(), padding.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "Backend two-block padding hash failed");
    const std::string blocks(1000000, 'a');
    Require(gfx_gui_gpu::ShaderSha256(blocks.data(), blocks.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "Backend multi-block shader hash failed");
    const auto file = root / "gpu-plan.jsonl";
    const Json header{{"header", {{"source-path", file.u8string()}}}};
    auto capture = Fixture(file, {header, {{"state", {{"marker_type", "BeginMarker"}}}}, Submit(2, 99),
        {{"state", {{"marker_type", "EndMarker"}}}}, Submit(10, 7), Submit(12, 7), Present(13),
        {{"frame", {{"marker_type", "EndMarker"}, {"frame_number", 3410}}}}, Submit(20, 7), Present(21)});
    const auto plan = BuildGpuMeasurementPlan(capture, 1, 2);
    Require(plan.at("frames").size() == 2 && plan.at("frames")[0].at("first_submit") == 10 &&
        plan.at("frames")[0].at("last_submit") == 12 && plan.at("frames")[1].at("first_submit") == 20,
        "GPU plan included trimmed-state initialization or double-counted frame markers");
    capture = Fixture(file, {header, Submit(10, 7), Submit(12, 8), Present(13)});
    Fails([&] { BuildGpuMeasurementPlan(capture, 1, 1); }, "multiple command queues");
    capture = Fixture(file, {header, Submit(10, 7)});
    Fails([&] { BuildGpuMeasurementPlan(capture, 1, 1); }, "complete captured boundary");
    auto failedPresent = Present(13); failedPresent["function"]["return"] = "VK_ERROR_OUT_OF_DATE_KHR";
    capture = Fixture(file, {header, Submit(10, 7), failedPresent, Submit(20, 7), Present(21)});
    Fails([&] { BuildGpuMeasurementPlan(capture, 1, 1); }, "failed frame presentation");
    auto extension = Submit(10, 7); extension["function"]["args"]["pSubmits"][0]["pNext"] = {{"sType", "VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO"}};
    capture = Fixture(file, {header, extension, Present(13)});
    Fails([&] { BuildGpuMeasurementPlan(capture, 1, 1); }, "extension chains");
    capture = Fixture(file, {header, {{"index", 2}, {"function", {{"name", "vkImportSemaphoreFdKHR"}, {"args", Json::object()}}}}, Submit(10, 7), Present(13)});
    Fails([&] { BuildGpuMeasurementPlan(capture, 1, 1); }, "vkImportSemaphoreFdKHR");

    auto request = Request(); request["timeout_ns"] = 100; // 8-bit test domain wraps every 512 ns.
    auto report = Report(request);
    VerifyGpuMeasurementReport(request, report); // Valid single wrap: (5 - 250) & 255 == 11 ticks.
    auto unverified = report; unverified.erase("swapchain");
    Fails([&] { VerifyGpuMeasurementReport(request, unverified); }, "lacks effective swapchain evidence");
    unverified = report; unverified["swapchain"]["effective"] = "offscreen";
    Fails([&] { VerifyGpuMeasurementReport(request, unverified); }, "swapchain modes do not match");
    unverified = report; unverified["swapchain"]["source"] = "requested_arguments";
    Fails([&] { VerifyGpuMeasurementReport(request, unverified); }, "swapchain modes do not match");
    auto changed = report; changed["request_id"] = "stale";
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "mismatched request_id");
    changed = report; changed["samples"][0]["elapsed_ns"] = 21.0;
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "valid timestamp inputs");
    changed = report; changed["samples"][0]["end_available"] = false;
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "availability");
    changed = report; changed["samples"][0].erase("start_available");
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "availability");
    changed = report; changed["samples"][0]["error"] = {{"message", "query failed"}};
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "also contains an error");
    changed = report; changed["error"] = {{"message", "backend failed after recording samples"}};
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "completed report also contains an error");
    changed = report; changed["samples"][0]["clock_domain"] = "same_submission";
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "domain");
    changed = report; changed["samples"][0]["timestamp_valid_bits"] = 0;
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "valid bits");
    changed = report; changed["overrides"][0]["applied"] = false;
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "replacement");
    changed = report; changed["overrides"][0]["sha256"] = "different";
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "replacement");
    changed = report; changed["overrides"][0]["actual_sha256"] = "different";
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "replacement");
    changed = report; changed["samples"].push_back(changed["samples"][0]);
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "sample count");
    changed = report; changed["status"] = "failed"; changed["error"] = {{"code", "timeout"}, {"message", "completion timed out"}};
    Fails([&] { VerifyGpuMeasurementReport(request, changed); }, "completion timed out");
    request["timeout_ns"] = 512;
    Fails([&] { VerifyGpuMeasurementReport(request, report); }, "wrap bound");
    request["timeout_ns"] = 100;
    auto legacy = request; legacy["sustained"] = Json::object();
    Fails([&] { VerifyGpuMeasurementReport(legacy, Report(legacy)); }, "fresh outer replays");
    auto counterRequest = request;
    counterRequest["counter_window"] = {{"protocol",1},{"period_ns",1}};
    auto counterReport = Report(counterRequest);
    auto& counterSample = counterReport["samples"][0];
    counterSample["start_boot_ns"] = 1000; counterSample["end_boot_ns"] = 1022;
    counterReport["counter_window"] = {{"protocol",1},{"status","available"},{"frame",1},{"period_ns",1},
        {"start_boot_ns",1000},{"end_boot_ns",1022},{"entry_boot_ns",900},{"completion_boot_ns",1100},
        {"clock_bridge",{{"status","available"},{"source","owned_queue_timestamp_bracket"},{"counter_domain","BOOTTIME"},
            {"host_domain","CLOCK_BOOTTIME"},{"completion","owned_fence"},{"marker_query_available",true},
            {"timestamp_valid_bits",8},{"timestamp_period_ns",2.0},{"marker_before_boot_ns",900},{"marker_after_boot_ns",1100},
            {"host_timestamp_ns",1000},{"host_to_boot_offset_ns",0},{"gpu_tick",250},{"uncertainty_ns",102},
            {"queue_id",7},{"probe_count",3},{"offset_bounds_overlap",true}}}};
    VerifyGpuMeasurementReport(counterRequest,counterReport);
    const auto wideWindow = ValidateFrameCounterWindow(counterRequest,counterReport);
    Require(wideWindow.at("status")=="available"&&wideWindow.at("clock_bridge").at("uncertainty_ns")==102,
        "Approximate counter alignment must retain uncertainty wider than its sampling period");
    auto clockFailure = counterReport;
    clockFailure["counter_window"]["clock_bridge"]["host_timestamp_ns"] = 29000001000ull;
    VerifyGpuMeasurementReport(counterRequest,clockFailure); // GPU duration remains independently valid.
    const auto rejectedClock = ValidateFrameCounterWindow(counterRequest,clockFailure);
    Require(rejectedClock.at("status")=="unavailable"&&!rejectedClock.at("reason").get<std::string>().empty()&&
        clockFailure.at("samples")[0].at("elapsed_ns")==22.0,"Invalid counter mapping must preserve valid GPU timing");
    clockFailure = counterReport; clockFailure["counter_window"]["clock_bridge"]["host_timestamp_ns"]=1000;
    clockFailure["counter_window"]["start_boot_ns"]=997; clockFailure["samples"][0]["start_boot_ns"]=997;
    Require(ValidateFrameCounterWindow(counterRequest,clockFailure).at("status")=="unavailable","Incorrect raw GPU tick mapping must downgrade counters");
    auto unavailable = counterReport;
    unavailable["counter_window"]={{"protocol",1},{"status","unavailable"},{"frame",1},{"period_ns",1},{"reason","driver clock unavailable"}};
    unavailable["samples"][0].erase("start_boot_ns");unavailable["samples"][0].erase("end_boot_ns");
    VerifyGpuMeasurementReport(counterRequest,unavailable);
    Require(ValidateFrameCounterWindow(counterRequest,unavailable).at("status")=="unavailable","Explicit counter-clock failure must retain usable GPU duration");
    unavailable.erase("counter_window");
    Require(ValidateFrameCounterWindow(counterRequest,unavailable).at("status")=="unavailable","Missing optional counter evidence must downgrade rather than discard GPU time");
    Require(ValidateFrameCounterWindow(request,report).is_null(),"GPU-only replay should not invent a counter window");
    std::filesystem::remove(file);
}
}
