#include "replay/android_replay_failure.h"
#include "replay/replay.h"
#include "replay/gpu_measurement.h"
#include "replay/measurement_telemetry.h"
#include "platform/android_display.h"
#include "platform/process.h"
#include "core/artifacts.h"
#include "core/image_io.h"
#include "platform/device_lock.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <optional>
#include <regex>
#include <cstdlib>
#include <cctype>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef LoadImage
#endif

namespace gfx {
std::string ResolveMemoryTranslation(const std::string& requested, bool android) {
    if (requested == "none" || requested == "remap" || requested == "realign" || requested == "rebind") return requested;
    if (requested != "default") throw std::runtime_error("Unsupported memory translation policy: " + requested);
#ifdef __APPLE__
    if (!android) return "rebind";
#else
    (void)android;
#endif
    return "none";
}
Json ReplayMemoryTranslationPolicy(const std::string& requested, bool android) {
    return {{"requested", requested}, {"effective", ResolveMemoryTranslation(requested, android)},
        {"source", "backend_arguments"}, {"backend_confirmed", false}};
}
namespace {
constexpr const char* kPackage = "com.lunarg.gfxreconstruct.replay";
std::vector<std::string> AndroidFields(const std::string& output, size_t count) {
    std::vector<std::string> fields;
    std::istringstream input(output); std::string field;
    while (std::getline(input, field, '\0')) fields.push_back(field);
    if (fields.size() != count || output.empty() || output.back() != '\0')
        throw std::runtime_error("Incomplete Android replay setup response");
    return fields;
}
std::string ReplayDeviceIdentity() {
#ifdef _WIN32
    std::string data;
    for (DWORD ordinal = 0;; ++ordinal) {
        DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, ordinal, &device, 0)) break;
        data.append(reinterpret_cast<const char*>(device.DeviceID), sizeof(device.DeviceID));
        data.append(reinterpret_cast<const char*>(device.DeviceString), sizeof(device.DeviceString));
        const std::wstring prefix = L"\\Registry\\Machine\\";
        const std::wstring keyName = device.DeviceKey;
        if (keyName.rfind(prefix, 0) == 0) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyName.substr(prefix.size()).c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
                FILETIME modified{};
                RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &modified);
                data.append(reinterpret_cast<const char*>(&modified), sizeof(modified));
                wchar_t version[256]{}; DWORD bytes = sizeof(version);
                if (RegQueryValueExW(key, L"DriverVersion", nullptr, nullptr, reinterpret_cast<BYTE*>(version), &bytes) == ERROR_SUCCESS)
                    data.append(reinterpret_cast<const char*>(version), bytes);
                RegCloseKey(key);
            }
        }
    }
    return Sha256Text(data);
#else
    return "unsupported";
#endif
}
// Rendered output is retained in the completed per-run session, never in a
// persistent cross-session result cache. Warm PSO caches are intentionally
// separate: they do not substitute for a real replay or its readback.
std::string LocalReplayDeviceIdentity() {
#ifdef _WIN32
    static const std::string identity = [] {
        DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
        return EnumDisplayDevicesW(nullptr, 0, &device, 0) ? ReplayDeviceIdentity() : std::string{};
    }();
    return identity;
#else
    return ReplayDeviceIdentity();
#endif
}
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!(file << text)) throw std::runtime_error("Cannot write replay artifact " + path.u8string());
}
std::string Command(const std::vector<std::string>& words) {
    std::string command;
    for (const auto& word : words) { if (!command.empty()) command += ' '; command += QuoteAndroidArgument(word); }
    return command;
}
std::string Checked(const ProcessResult& result, const std::string& operation) {
    if (result.cancelled) throw std::runtime_error("Replay cancelled.");
    if (result.timedOut) throw std::runtime_error(operation + " timed out. Check the selected device connection.");
    if (!result.Success()) throw std::runtime_error(operation + " failed:\n" + result.output);
    return result.output;
}
Json ReadJson(const std::filesystem::path& file) {
    std::ifstream input(file); return Json::parse(input);
}
bool CacheCapable(const std::filesystem::path& binary, const std::string& hash) {
    try {
        const auto capability = ReadJson(std::filesystem::u8path(binary.u8string() + ".capabilities.json"));
        return capability.value("pipeline_cache_protocol", 0) == 1 && capability.value("binary_sha256", "") == hash;
    } catch (...) { return false; }
}
bool GpuCapable(const std::filesystem::path& binary, const std::string& hash) {
    try {
        const auto capability = ReadJson(std::filesystem::u8path(binary.u8string() + ".capabilities.json"));
        return capability.value("gpu_profiling_protocol", 0) == 1 && capability.value("binary_sha256", "") == hash;
    } catch (...) { return false; }
}
std::string Trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return text;
}
Json PrepareOptimized(ReplayRequest& replay, const std::string& sourceHash,
                      const std::map<std::string,std::string>& tools, const std::atomic_bool* cancel) {
    const auto tool=tools.find("optimize");
    if(!replay.optimize || replay.cacheDirectory.empty() || tool==tools.end() || tool->second.empty())
        return {{"status", "original"}};
    const auto executable=std::filesystem::u8path(tool->second);
    if(!std::filesystem::is_regular_file(executable))return {{"status","unavailable"}};
    const auto optimizerHash=CachedSha256File(executable,cancel);
    // Full hashes are verified in the manifest; short directory names keep the
    // optimizer's native Windows file APIs usable from extracted portable paths.
    const auto root=replay.cacheDirectory/"optimized"/(sourceHash.substr(0,24)+"-"+optimizerHash.substr(0,16));
    const auto derived=root/"capture.gfxr";
    try {
        auto metadata=ReadJson(root/"manifest.json");
        if(metadata.value("owner","")=="gfxrecon_gui" && metadata.value("complete",false) &&
            metadata.at("source_sha256")==sourceHash && metadata.at("optimizer_sha256")==optimizerHash) {
            if(metadata.value("status","")=="unchanged")return metadata;
            if(!std::filesystem::is_symlink(derived) && CachedSha256File(derived,cancel)==metadata.at("derived_sha256")) {
                replay.capture=derived;metadata["cache_hit"]=true;return metadata;
            }
        }
    } catch (...) {if(cancel&&cancel->load())throw;}
    std::filesystem::create_directories(root);
    const auto pending=root/"capture.partial";
    if(std::filesystem::exists(pending))std::filesystem::remove(pending);
    ProcessRequest process{tool->second,{replay.capture.u8string(),pending.u8string()}};
    process.timeout=replay.timeout;
    const auto result=RunProcess(process,cancel);Write(root/"optimizer.log",result.output);
    if(result.cancelled)throw std::runtime_error("Optimization cancelled");
    if(!result.Success())return {{"status","failed; using original"},{"log",(root/"optimizer.log").u8string()}};
    if(CachedSha256File(replay.capture,cancel)!=sourceHash)throw std::runtime_error("Original capture changed during optimization");
    Json metadata{{"owner","gfxrecon_gui"},{"schema",1},{"complete",true},
        {"source",replay.capture.u8string()},{"source_sha256",sourceHash},
        {"optimizer_sha256",optimizerHash},{"log",(root/"optimizer.log").u8string()}};
    if(std::filesystem::is_regular_file(pending)) {
        metadata["derived_sha256"]=CachedSha256File(pending,cancel);
        metadata["original_bytes"]=std::filesystem::file_size(replay.capture);
        metadata["optimized_bytes"]=std::filesystem::file_size(pending);
        metadata["status"]="optimized";
        if(std::filesystem::exists(derived))std::filesystem::remove(derived);
        std::filesystem::rename(pending,derived);replay.capture=derived;
    } else metadata["status"]="unchanged";
    AtomicJson(root/"manifest.json",metadata);
    return metadata;
}
bool HasVisualization(const ReplayRequest& r){return r.visualization!="original"||r.indirectSubdraw>=0;}
Json VisualizationConfig(const ReplayRequest& r){return {{"protocol",r.visualization=="silhouette"?3:r.visualization=="uv"?2:1},{"mode",r.visualization},{"pipeline",r.visualizationPipeline},{"subdraw",r.indirectSubdraw},{"draw_id_safe",r.indirectDrawIdSafe},{"uv_shader_sha256",r.uvShader.empty()?std::string{}:CachedSha256File(r.uvShader)},
    {"coverage_variant",r.coverageVariant},
    {"outline_base_sha256",r.outlineBaseImage.empty()?std::string{}:CachedSha256File(r.outlineBaseImage)},{"outline_coverage_sha256",r.outlineCoverageImage.empty()?std::string{}:CachedSha256File(r.outlineCoverageImage)}};}
Json DumpOptions(const ReplayRequest& r, const std::string& output) {
    return {{"OutputDir", output}, {"ImageFormat", r.imageFormat},
        {"DumpAllDescriptors", !r.outputsOnly || r.descriptorImages || r.drawInspection}, {"DumpVertexIndexBuffer", !r.outputsOnly || r.drawInspection},
        {"DumpDepth", true}, {"DumpBeforeCommand", false}, {"DumpAllImageSubresources", r.allImageSubresources}};
}
ReplayResult RunLocal(const ReplayRequest& r, const std::map<std::string, std::string>& tools, const std::string& identity,
                       const std::atomic_bool* cancel) {
    std::vector<std::string> args{"--screenshots", std::to_string(r.frame), "--screenshot-format", "bmp",
        "--screenshot-dir", r.output.u8string(), "--screenshot-prefix", r.prefix,
        "--quit-after-frame", std::to_string(r.frame), "--omit-pipeline-cache-data"};
    if (r.outputsOnly) args.erase(args.begin(), args.begin() + 8);
    if (r.measurement.enabled) {
        args.erase(args.begin(), args.end() - 1); // no screenshots; the measurement range replaces --quit-after-frame (keeps --omit-pipeline-cache-data)
        args.insert(args.end(), {"--measurement-frame-range", r.measurement.frameRange, "--measurement-file", (r.output / "measurement.json").u8string()});
        if (r.measurement.quitAfter) args.push_back("--quit-after-measurement-range");
    }
    if (r.measurement.gpu) args.insert(args.end(), {"--gpu-measurement-request", (r.output / "gpu-measurement-request.json").u8string()});
    if (!r.measurement.counterWindow.is_null())
        throw std::runtime_error("Frame counter collection currently requires an Android target");
    args.insert(args.end(), {"--swapchain", r.swapchainMode});
    args.insert(args.end(), {"--memory-translation", ResolveMemoryTranslation(r.memoryTranslation, false)});
    std::filesystem::path pendingCache, readyCache;
    bool cacheLoaded = false;
    const auto backend = tools.find("replay");
    if (!r.validation && !r.cacheDirectory.empty() && backend != tools.end() && CacheCapable(std::filesystem::u8path(backend->second), CachedSha256File(std::filesystem::u8path(backend->second), cancel))) {
        const auto directory = r.cacheDirectory / "pipelines"; std::filesystem::create_directories(directory);
        readyCache = directory / (identity + ".bin");
        if (std::filesystem::is_regular_file(readyCache)) {
            args.erase(std::find(args.begin(), args.end(), "--omit-pipeline-cache-data"));
            args.insert(args.end(), {"--load-pipeline-cache", readyCache.u8string(), "--add-new-pipeline-caches"});
            cacheLoaded = true;
        } else {
            pendingCache = r.output / "pipeline-cache.partial";
            args.insert(args.end(), {"--save-pipeline-cache", pendingCache.u8string(), "--add-new-pipeline-caches"});
        }
    }
    if (!r.dump.is_null()) {
        auto dump = r.dump;
        dump["DumpResourcesOptions"] = DumpOptions(r, r.output.u8string());
        Write(r.output / "dump-request.json", dump.dump(2));
        if(HasVisualization(r))Write(r.output/"gui-visualization.json",VisualizationConfig(r).dump(2));
        if(!r.uvShader.empty())std::filesystem::copy_file(r.uvShader,r.output/"gui-uv.spv");
        args.insert(args.end(), {"--dump-resources", (r.output / "dump-request.json").u8string()});
    }
    if (!r.overrides.empty()) {
        const auto directory = r.output / "overrides";
        std::filesystem::create_directory(directory);
        for (const auto& [module, file] : r.overrides)
            std::filesystem::copy_file(file, directory / ("sh" + std::to_string(module)));
        args.insert(args.end(), {"--replace-shaders", directory.u8string()});
    }
    args.push_back(r.capture.u8string());
    const auto tool = tools.find("replay");
    if (tool == tools.end() || tool->second.empty()) throw std::runtime_error("Configure the local replay executable or choose an Android replay device.");
    ProcessRequest process{tool->second, args}; process.timeout = r.timeout;
    if(r.validation){
        process.arguments.insert(process.arguments.begin(),{"--validate","--log-file",(r.output/"validation-full.log").u8string()});
        const auto layer=tools.find("validation_layer");
        if(layer!=tools.end()&&!layer->second.empty()) {
            const auto previous=std::getenv("VK_LAYER_PATH");
            process.environment["VK_LAYER_PATH"]=layer->second+(previous&&*previous?std::string(";")+previous:"");
        }
    }
    auto result = RunProcess(process, cancel);
    Write(r.output / "replay.log", result.output);
    Json swapchain;
    if (result.Success()) swapchain = VerifyReplaySwapchain(r.swapchainMode, result.output, r.swapchainMode != "virtual");
    if (result.Success() && !pendingCache.empty() && std::filesystem::is_regular_file(pendingCache)) {
        std::filesystem::copy_file(pendingCache, readyCache, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(pendingCache);
    }
    if (!result.Success() && result.output.find("VK_ERROR_EXTENSION_NOT_PRESENT") != std::string::npos)
        throw std::runtime_error("The local GPU lacks a Vulkan extension required by this capture. For an Android capture, choose Automatic or Android device under Frames > Replay.\n" + result.output);
    ReplayResult local{{}, Checked(result, "Local replay"), LocalReplayLabel()};
    local.timings = {{"pipeline_cache_loaded", cacheLoaded}, {"screen_off", false}};
    local.timings["swapchain"] = std::move(swapchain);
    local.timings["memory_translation"] = ReplayMemoryTranslationPolicy(r.memoryTranslation, false);
    if (r.measurement.gpu) {
        local.timings["pipeline_cache_identity"] = identity;
        local.timings["pipeline_cache_saved"] = !pendingCache.empty() && std::filesystem::is_regular_file(readyCache);
        local.timings["pipeline_cache_sha256"] = !readyCache.empty() && std::filesystem::is_regular_file(readyCache) ? Json(CachedSha256File(readyCache, cancel)) : Json(nullptr);
        local.timings["telemetry"] = {{"before", QueryMeasurementTelemetry({}, {}, cancel)}, {"after", QueryMeasurementTelemetry({}, {}, cancel)}};
    }
    return local;
}

ReplayResult RunAndroid(const ReplayRequest& r, const ReplayTarget& target, const std::string& identity, const std::string& captureHash, bool pipelineCapable,
                         const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    const auto started = std::chrono::steady_clock::now();
    AdbConnection adb{tools.at("adb"), target.serial, cancel};
    EnsureAndroidReplayReady(adb);
    if(r.progress)r.progress("Preparing replay on Android "+target.serial);
    const auto id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const auto relative = "files/gui_replay_" + id;
    const auto remote = std::string("/data/data/") + kPackage + "/" + relative;
    Write(r.output / "device-session.json", Json{{"serial", target.serial}, {"remote", remote},
        {"capture", r.capture.u8string()}, {"frame", r.frame}}.dump(2));
    std::string captureFile = remote + "/capture.gfxr", pipelineFile, pendingPipeline;
    bool pipelineReady = false, captureReused = false;
    std::string setup = "set -e; mkdir -p " + QuoteAndroidArgument(relative);
    std::string base;
    if (!r.cacheDirectory.empty()) {
        base = "files/gui_cache_v2/" + captureHash;
        setup += "; mkdir -p " + QuoteAndroidArgument(base) + "; checksum=$(sha256sum " + QuoteAndroidArgument(base + "/capture.gfxr") + " 2>/dev/null || true); printf '%s\\000' \"$checksum\"";
        captureFile = std::string("/data/data/") + kPackage + "/" + base + "/capture.gfxr";
        if (pipelineCapable) {
            pipelineFile = std::string("/data/data/") + kPackage + "/" + base + "/pipeline_" + identity + ".bin";
            pendingPipeline = remote + "/pipeline.partial";
        }
    } else setup += "; printf '\\000'";
    setup += pipelineFile.empty() ? "; printf '0\\000'" : "; if test -s " + QuoteAndroidArgument(pipelineFile) + "; then printf '1\\000'; else printf '0\\000'; fi";
    const auto prepared = AndroidFields(adb.Shell({"run-as", kPackage, "sh", "-c", setup}), 2);
    captureReused = !base.empty() && prepared[0].rfind(captureHash, 0) == 0;
    pipelineReady = prepared[1] == "1";
    if (!base.empty()) {
        if (!captureReused) {
            adb.Put(kPackage, r.capture, base + "/capture.partial");
            // Put waits for the receiver's content acknowledgement. Verify actual
            // contents before atomically publishing the reusable capture cache.
            adb.Shell({"run-as", kPackage, "sh", "-c", "set -e; checksum=$(sha256sum " + QuoteAndroidArgument(base + "/capture.partial") +
                "); test \"${checksum%% *}\" = " + QuoteAndroidArgument(captureHash) + " || { echo 'Uploaded capture checksum did not match' >&2; exit 1; }; mv " +
                QuoteAndroidArgument(base + "/capture.partial") + " " + QuoteAndroidArgument(base + "/capture.gfxr")});
        }
    } else adb.Put(kPackage, r.capture, relative + "/capture.gfxr");
    const auto transportTool=tools.find("artifact_transport");
    const auto transport=transportTool==tools.end()?std::string{}:adb.PrepareArtifactTransport(std::filesystem::u8path(transportTool->second));
    const auto uploaded = std::chrono::steady_clock::now();
    std::string args = "--screenshots " + std::to_string(r.frame) + " --screenshot-format bmp --screenshot-dir " + remote +
        " --screenshot-prefix " + r.prefix + " --quit-after-frame " + std::to_string(r.frame) +
        " --omit-pipeline-cache-data --log-file " + remote + "/replay.log";
    if (r.outputsOnly) args = "--quit-after-frame " + std::to_string(r.frame) + " --omit-pipeline-cache-data --log-file " + remote + "/replay.log";
    if (r.measurement.enabled) {
        // Single-space separated, no leading option removed below: the SDK parser rejects empty arguments.
        args = "--measurement-frame-range " + r.measurement.frameRange + " --measurement-file " + remote + "/measurement.json" +
            (r.measurement.quitAfter ? " --quit-after-measurement-range" : "") + " --omit-pipeline-cache-data --log-file " + remote + "/replay.log";
    }
    if (!pipelineFile.empty()) {
        if (pipelineReady) {
            // Android's SDK parser treats consecutive spaces as empty positional
            // arguments. Remove the leading separator together with the option.
            const std::string omit = " --omit-pipeline-cache-data"; args.erase(args.find(omit), omit.size());
            args += " --load-pipeline-cache " + pipelineFile;
        } else args += " --save-pipeline-cache " + pendingPipeline;
        args += " --add-new-pipeline-caches";
    }
    if (!r.dump.is_null()) {
        auto dump = r.dump;
        dump["DumpResourcesOptions"] = DumpOptions(r, remote);
        Write(r.output / "dump-request.json", dump.dump(2));
        adb.Put(kPackage, r.output / "dump-request.json", relative + "/dump-request.json");
        if(HasVisualization(r)){Write(r.output/"gui-visualization.json",VisualizationConfig(r).dump(2));adb.Put(kPackage,r.output/"gui-visualization.json",relative+"/gui-visualization.json");}
        if(!r.uvShader.empty()){std::filesystem::copy_file(r.uvShader,r.output/"gui-uv.spv");adb.Put(kPackage,r.uvShader,relative+"/gui-uv.spv");}
        args += " --dump-resources " + remote + "/dump-request.json";
    }
    if (!r.overrides.empty()) {
        adb.Shell({"run-as", kPackage, "mkdir", "-p", relative + "/overrides"});
        for (const auto& [module, file] : r.overrides)
            adb.Put(kPackage, file, relative + "/overrides/sh" + std::to_string(module));
        args += " --replace-shaders " + remote + "/overrides";
    }
    if (r.measurement.gpu) {
        auto config = ReadJson(r.output / "gpu-measurement-request.json");
        config["output_file"] = remote + "/gpu-measurement.json";
        if (!r.measurement.counterWindow.is_null()) {
            config["counter_window"]["ready_file"] = remote + "/perf-ready.json";
            config["counter_window"]["continue_file"] = remote + "/perf-continue.json";
        }
        AtomicJson(r.output / "gpu-measurement-request.json", config);
        adb.Put(kPackage, r.output / "gpu-measurement-request.json", relative + "/gpu-measurement-request.json");
        args += " --gpu-measurement-request " + remote + "/gpu-measurement-request.json";
    }
    args += " --swapchain " + r.swapchainMode;
    args += " --memory-translation " + ResolveMemoryTranslation(r.memoryTranslation, true);
    args += " " + captureFile;
    if(r.validation)args="--validate "+args;
    Write(r.output / "launch-arguments.json", Json{{"args", args}}.dump(2));
    if (!r.measurement.counterWindow.is_null()) {
        const auto active = adb.TryShell({"pidof", kPackage});
        if (active.Success() && active.output.find_first_not_of(" \r\n\t") != std::string::npos)
            throw DeviceBusy(target.serial, "Replay process already active; frame profiling will not interrupt another session");
    }
    adb.Stop(kPackage);
    bool running = false;
    Json telemetryBefore, telemetryAfter;
    AndroidReplayDisplay display(adb,r.output/"display-journal.json");
    try {
        EnsureAndroidReplayReady(adb);
        if(r.screenOff)display.TurnOff();
        if (r.measurement.gpu) {
            telemetryBefore = QueryMeasurementTelemetry(tools.at("adb"), target.serial, cancel);
            AtomicJson(r.output / "measurement-telemetry-before.json", telemetryBefore);
        }
        running = true;
        // NativeActivity can complete replay while Android's activity-launch/
        // visibility acknowledgment is still pending after keyguard dismissal. Its completion log,
        // rather than `am start -W`, is the replay readiness/completion signal.
        std::vector<std::string> launch{"am","start"};
        launch.insert(launch.end(),{"-n",std::string(kPackage)+"/android.app.NativeActivity","--es","args",args});
        const auto launched=adb.Shell(launch);
        if(r.progress)r.progress("Android replay launched; waiting for replay completion");
        if(r.screenOff)display.Observe();
        Write(r.output / "launch.log", launched);
        if (launched.find("Error:") != std::string::npos || launched.find("Exception") != std::string::npos)
            throw std::runtime_error("Android replay could not start:\n" + launched);
        const auto launchedAt = std::chrono::steady_clock::now();
        const auto deadline = launchedAt + r.timeout;
        const auto describeFailure = [&](const std::string& fallback = std::string{}) {
            const auto exitInfo = adb.TryShell({"dumpsys", "activity", "exit-info", kPackage});
            Write(r.output / "exit-info.txt", exitInfo.output);
            std::string reason;
            try {
                const auto crash = adb.Run({"logcat", "-b", "crash", "-d", "-v", "threadtime", "-t", "500"}, {}, {}, true);
                Write(r.output / "native-crash-logcat.txt", crash.output);
                if (crash.Success()) reason = AndroidNativeCrashReason(exitInfo.output, crash.output, kPackage);
            } catch (const std::exception& error) {
                Write(r.output / "native-crash-logcat.txt", std::string("Crash log unavailable: ") + error.what());
            }
            std::string message = reason.empty() ? (fallback.empty() ? DescribeAndroidReplayExit(exitInfo.output) : fallback) :
                "Android native replay failed: " + reason;
            if (reason.find("VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS") != std::string::npos)
                message += " Cross-device allocation may require --memory-translation remap (Settings > Replay > Memory translation).";
            return message + " See " + (r.output / "exit-info.txt").u8string() + ", native-crash-logcat.txt and replay.log.";
        };
        std::string log;
        bool complete = false;
        bool measurementStarted = r.measurement.counterWindow.is_null();
        auto displayChecked=std::chrono::steady_clock::now();
        auto processChecked=launchedAt;
        auto lastProgress=launchedAt;
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel && cancel->load()) throw std::runtime_error("Replay cancelled.");
            if (!measurementStarted) {
                const auto ready = adb.TryShell({"run-as", kPackage, "cat", relative + "/perf-ready.json"});
                if (ready.Success()) {
                    const auto state = Json::parse(ready.output);
                    const auto config = ReadJson(r.output / "gpu-measurement-request.json");
                    if (state.value("request_id", "") != config.at("request_id").get<std::string>() || state.value("status", "") != "ready")
                        throw std::runtime_error("Frame counter ready handshake identity/status mismatch");
                    AtomicJson(r.output / "perf-ready.json", state);
                    if (!r.measurementReady) throw std::runtime_error("Frame counter collection requires a start callback");
                    if (r.progress) r.progress("Starting optional GPU counters before the selected frame");
                    r.measurementReady(state);
                    const auto ack = r.output / "perf-continue.json";
                    AtomicJson(ack, {{"request_id", config.at("request_id")}, {"status", "continue"}});
                    adb.Put(kPackage, ack, relative + "/perf-continue.partial.json");
                    adb.Shell({"run-as", kPackage, "mv", relative + "/perf-continue.partial.json", relative + "/perf-continue.json"});
                    measurementStarted = true;
                }
            }
            if(r.screenOff&&std::chrono::steady_clock::now()-displayChecked>std::chrono::seconds(1)){display.Observe();displayChecked=std::chrono::steady_clock::now();}
            const auto read = adb.Run({"shell", Command({"run-as", kPackage, "cat", relative + "/replay.log"})});
            if (read.cancelled || read.timedOut) Checked(read, "Android replay status");
            const bool logNotCreated = read.output.find("replay.log") != std::string::npos &&
                read.output.find("No such file") != std::string::npos;
            if (!read.Success() && !logNotCreated) Checked(read, "Android replay connection");
            if (logNotCreated && std::chrono::steady_clock::now() - launchedAt > std::chrono::seconds(3) &&
                !adb.TryShell({"pidof", kPackage}).Success()) {
                const auto diagnostics = adb.Run({"logcat", "-d", "-s", "gfxrecon:V"});
                Write(r.output / "startup-logcat.log", diagnostics.output);
                throw std::runtime_error(describeFailure("Android replay exited before creating its log."));
            }
            if (read.Success()) {
                auto incoming=read.output;
                if(read.outputTruncated){
                    const auto tail=adb.TryShell({"run-as",kPackage,"tail","-c","65536",relative+"/replay.log"});
                    incoming+="\n[GUI: log preview truncated]\n"+tail.output;
                }
                if(log!=incoming)lastProgress=std::chrono::steady_clock::now();
                log=std::move(incoming);
                Write(r.output / "replay.log", log);
                if (log.find("[gfxrecon] FATAL") != std::string::npos || log.find("Replay has encountered a fatal error") != std::string::npos)
                    throw std::runtime_error("Android replay failed:\n" + log);
                if (log.find("Exiting android_main") != std::string::npos) { complete = true; break; }
            }
            if(std::chrono::steady_clock::now()-launchedAt>std::chrono::seconds(3)&&std::chrono::steady_clock::now()-processChecked>std::chrono::seconds(1)){
                processChecked=std::chrono::steady_clock::now();const auto alive=adb.TryShell({"pidof",kPackage});
                if(!r.screenOff&&std::chrono::steady_clock::now()-lastProgress>std::chrono::seconds(5))EnsureAndroidReplayReady(adb);
                if(alive.cancelled||alive.timedOut)Checked(alive,"Replay process check");
                if(!alive.Success()&&alive.output.find_first_not_of(" \r\n\t")==std::string::npos){
                    const auto final=adb.TryShell({"run-as",kPackage,"cat",relative+"/replay.log"});
                    if(final.Success()){
                        log=final.output;
                        if(final.outputTruncated){const auto tail=adb.TryShell({"run-as",kPackage,"tail","-c","65536",relative+"/replay.log"});if(tail.Success())log+="\n[GUI: final log preview truncated]\n"+tail.output;}
                        Write(r.output/"replay.log",log);if(log.find("Exiting android_main")!=std::string::npos){complete=true;break;}
                    }
                    throw std::runtime_error(describeFailure());
                }
                if(!alive.Success()&&!alive.output.empty())Checked(alive,"Replay process check");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (!complete) throw std::runtime_error("Android replay did not complete before its timeout. Check device state and " + (r.output / "replay.log").u8string());
        const auto swapchain = VerifyReplaySwapchain(r.swapchainMode, log, r.swapchainMode != "virtual");
        const auto replayed = std::chrono::steady_clock::now();
        if (r.measurement.gpu) {
            telemetryAfter = QueryMeasurementTelemetry(tools.at("adb"), target.serial, cancel);
            AtomicJson(r.output / "measurement-telemetry-after.json", telemetryAfter);
        }
        if(r.validation)adb.Get(kPackage,relative+"/replay.log",r.output/"validation-full.log");
        if (!pipelineFile.empty() && !pipelineReady && adb.TryShell({"run-as", kPackage, "test", "-s", pendingPipeline}).Success())
            adb.Shell({"run-as", kPackage, "mv", pendingPipeline, pipelineFile});
        if(r.progress)r.progress("Android replay completed; retrieving images and resource data");
        ArtifactTransferStats transfer;
        const auto received = adb.GetReplayFiles(kPackage, relative, r.output, !r.descriptorImages || r.drawInspection, r.timeout,transport,&transfer);
        if(r.progress)r.progress("Replay artifact retrieval complete ("+std::to_string(received)+" files)");
        if(r.screenOff)display.Restore();
        running = false;
        ReplayResult result{{}, log, target.Label()};
        result.timings = {{"screen_off",r.screenOff},{"screen_off_observations",display.OffObservations()},{"capture_upload_reused", captureReused}, {"pipeline_cache_loaded", pipelineReady},
            {"prepare_seconds", std::chrono::duration<double>(uploaded - started).count()},
            {"replay_seconds", std::chrono::duration<double>(replayed - uploaded).count()},
            {"download_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - replayed).count()}};
        result.timings["artifact_transfer"]={{"codec",transfer.codec},{"integrity",transfer.integrity},{"wire_bytes",transfer.wireBytes},{"raw_bytes",transfer.rawBytes}};
        result.timings["swapchain"] = swapchain;
        result.timings["memory_translation"] = ReplayMemoryTranslationPolicy(r.memoryTranslation, true);
        if (r.measurement.gpu) {
            result.timings["pipeline_cache_identity"] = identity;
            result.timings["pipeline_cache_saved"] = !pipelineFile.empty() && !pipelineReady && adb.TryShell({"run-as", kPackage, "test", "-s", pipelineFile}).Success();
            const auto checksum = pipelineFile.empty() ? ProcessResult{} : adb.TryShell({"run-as", kPackage, "sha256sum", pipelineFile});
            result.timings["pipeline_cache_sha256"] = !pipelineFile.empty() && checksum.Success() && checksum.output.size() >= 64 ? Json(checksum.output.substr(0, 64)) : Json(nullptr);
            result.timings["telemetry"] = {{"before", telemetryBefore}, {"after", telemetryAfter}};
        }
        return result;
    } catch (...) {
        if (r.measurement.gpu && !(cancel && cancel->load())) try {
            AdbConnection diagnostics{tools.at("adb"), target.serial};
            diagnostics.Get(kPackage, relative + "/gpu-measurement.json", r.output / "gpu-measurement.json");
        } catch (...) {}
        if (running && !r.measurement.counterWindow.is_null()) {
            // Cancellation must not cancel cleanup itself. A lost transport can
            // prevent remote cleanup; retain that uncertainty instead of
            // treating an attempted force-stop as proof of completion.
            Json cleanup{{"status", "unknown"}, {"method", "owned_replay_package_force_stop"}, {"cancel_ignored", true}};
            try {
                const auto stopped = adb.TryShell({"am", "force-stop", kPackage}, true);
                const auto alive = adb.TryShell({"pidof", kPackage}, true);
                cleanup["stop_exit_code"] = stopped.exitCode; cleanup["stop_output"] = stopped.output;
                cleanup["process_check_exit_code"] = alive.exitCode; cleanup["process_check_output"] = alive.output;
                if (stopped.Success() && alive.exitCode == 1 && !alive.cancelled && !alive.timedOut &&
                    alive.output.find_first_not_of(" \r\n\t") == std::string::npos) cleanup["status"] = "stopped";
            } catch (const std::exception& error) { cleanup["error"] = error.what(); }
            try { AtomicJson(r.output / "replay-cleanup.json", cleanup); } catch (...) {}
        } else if (running) { try { adb.Stop(kPackage); } catch (...) {} }
        if(r.validation&&!(cancel&&cancel->load()))try{
            AdbConnection diagnostics{tools.at("adb"),target.serial};diagnostics.Get(kPackage,relative+"/replay.log",r.output/"validation-full.log");
        }catch(...){}
        throw;
    }
}
} // namespace

std::string DescribeAndroidReplayExit(const std::string& info){
    const auto first=info.find("ApplicationExitInfo #0:");const auto end=info.find("ApplicationExitInfo #1:",first==std::string::npos?0:first);
    const auto latest=info.substr(first==std::string::npos?0:first,end==std::string::npos?std::string::npos:end-(first==std::string::npos?0:first));
    if(latest.find("(LOW_MEMORY)")!=std::string::npos)return "Android terminated the replayer because it ran out of memory while reading replay resources.";
    return "The Android replayer exited before completing its resource readback.";
}
std::vector<AndroidDevice> ParseAdbDevices(const std::string& text) {
    std::vector<AndroidDevice> devices;
    std::istringstream lines(text); std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || line.rfind("List of devices", 0) == 0 || line[0] == '*') continue;
        std::istringstream fields(line); AndroidDevice device;
        if (!(fields >> device.serial >> device.state)) continue;
        if (device.state != "device" && device.state != "offline" && device.state != "unauthorized") continue;
        std::getline(fields, device.details);
        devices.push_back(std::move(device));
    }
    return devices;
}
std::vector<AndroidDevice> QueryAndroidDevices(const std::string& adb, const std::atomic_bool* cancel) {
    if (adb.empty()) throw std::runtime_error("Configure ADB in Settings > Tools and devices to replay this Android capture on a phone.");
    ProcessRequest request{adb, {"devices", "-l"}}; request.allowChildServices = true;
    return ParseAdbDevices(Checked(RunProcess(request, cancel), "Android device discovery"));
}
ReplayTarget SelectReplayTarget(ReplayMode mode, bool androidCapture, const std::string& serial,
                                 const std::vector<AndroidDevice>& devices) {
    if (mode == ReplayMode::Local || (mode == ReplayMode::Automatic && !androidCapture)) return {};
    if (!serial.empty()) {
        const auto found = std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.serial == serial; });
        if (found == devices.end() || found->state != "device")
            throw std::runtime_error("Selected Android device " + serial + " is disconnected or unauthorized. Connect/authorize it, or select another device with the Events replay-device button.");
        return {true, serial};
    }
    std::vector<std::string> online;
    for (const auto& device : devices) if (device.state == "device") online.push_back(device.serial);
    if (online.empty()) throw std::runtime_error("This capture needs Android replay. Connect an authorized phone and refresh devices with the Events replay-device button; offline inspection remains available.");
    if (online.size() > 1) throw std::runtime_error("Multiple Android devices are connected. Select the replay device with the Events replay-device button.");
    return {true, online.front()};
}
ReplayTarget ResolveReplayTarget(ReplayMode mode, bool androidCapture, const std::string& serial,
                                  const std::string& adb, const std::atomic_bool* cancel) {
    if (mode == ReplayMode::Local || (mode == ReplayMode::Automatic && !androidCapture)) return {};
    if (!serial.empty()) return {true, serial}; // Cached inspection does not require a connected phone.
    return SelectReplayTarget(mode, androidCapture, serial, QueryAndroidDevices(adb, cancel));
}
ReplayResult ReplayCapture(const ReplayRequest& request, const ReplayTarget& target,
                            const std::map<std::string, std::string>& tools, const std::atomic_bool* cancel) {
    auto replay=request;
    if(replay.validation){replay.optimize=false;replay.reuseArtifacts=false;}
    if(replay.measurement.enabled){
        replay.reuseArtifacts=false;
        static const std::regex range("[0-9]+-[0-9]+");
        if(!std::regex_match(replay.measurement.frameRange,range))throw std::runtime_error("Measurement frame range must be <start>-<end> (end exclusive)");
    }
    if (replay.measurement.gpu) {
        if (!replay.measurement.enabled) throw std::runtime_error("GPU profiling requires a measurement frame range");
        if (replay.validation || !replay.dump.is_null() || HasVisualization(replay))
            throw std::runtime_error("GPU profiling cannot include validation, resource readback or diagnostic visualization");
        replay.optimize = false;
        replay.reuseArtifacts = false;
        if (!replay.measurement.counterWindow.is_null()) {
            if (!target.android) throw std::runtime_error("Frame counter collection currently requires an Android target");
            replay.measurement.quitAfter = true;
        }
    }
    const auto& r=replay;
    if (r.swapchainMode != "virtual" && r.swapchainMode != "captured" && r.swapchainMode != "offscreen")
        throw std::runtime_error("Unsupported replay swapchain mode: " + r.swapchainMode);
    const auto memoryPolicy = ReplayMemoryTranslationPolicy(r.memoryTranslation, target.android);
    const auto started = std::chrono::steady_clock::now();
    std::optional<DeviceLock> deviceLock;
    if(r.visualization!="original"&&r.visualization!="solid"&&r.visualization!="wireframe"&&r.visualization!="depth"&&r.visualization!="uv"&&r.visualization!="silhouette")throw std::runtime_error("Unknown draw visualization");
    if(r.visualization=="uv"&&r.uvShader.empty())throw std::runtime_error("UV visualization requires a reflected UV shader");
    if(r.visualization=="silhouette"&&(r.outlineBaseImage.empty()||r.outlineCoverageImage.empty()))throw std::runtime_error("Orange outline requires original color and solid coverage from this selection");
    if(r.indirectSubdraw < -1||r.indirectSubdraw>65535)throw std::runtime_error("Invalid indirect subdraw index");
    if(HasVisualization(r)&&r.dump.is_null())throw std::runtime_error("Draw visualization requires a selected draw");
    if (r.prefix != "baseline" && r.prefix != "modified") throw std::runtime_error("Invalid replay output prefix");
    if (r.imageFormat != "bmp" && r.imageFormat != "png") throw std::runtime_error("Invalid replay image format");
    if (!r.frame || !std::filesystem::is_regular_file(r.capture)) throw std::runtime_error("Replay requires a capture and a positive frame number");
    const auto sourceHash = CachedSha256File(request.capture, cancel);
    if (!r.expectedCaptureHash.empty() && sourceHash != r.expectedCaptureHash)
        throw std::runtime_error("Capture contents changed after import. Reopen the capture before replaying; previous results were retained.");
    if (std::filesystem::exists(r.output) && !std::filesystem::is_empty(r.output))
        throw std::runtime_error("Replay output must be a new, empty session directory");
    const auto optimization=PrepareOptimized(replay,sourceHash,tools,cancel);
    const auto captureHash=r.capture==request.capture?sourceHash:CachedSha256File(r.capture,cancel);
    if (r.measurement.gpu) {
        const auto& plan = r.measurement.gpuPlan;
        if (!plan.is_object() || plan.value("mode", "") != "isolated-frame" || plan.value("capture_sha256", "") != sourceHash)
            throw std::runtime_error("GPU measurement plan does not match the original capture. Reimport and plan the selected frames.");
        const auto dash = r.measurement.frameRange.find('-');
        const auto first = std::stoull(r.measurement.frameRange.substr(0, dash)), end = std::stoull(r.measurement.frameRange.substr(dash + 1));
        const auto frames = plan.value("frames", Json::array());
        if (first == 0 || end <= first || !frames.is_array() || frames.size() != end - first)
            throw std::runtime_error("GPU measurement plan does not cover the requested frame range");
        for (size_t i = 0; i < frames.size(); ++i) {
            const auto& frame = frames.at(i);
            const auto blocks = frame.value("submit_blocks", Json::array());
            if (frame.value("frame", uint64_t(0)) != first + i || frame.value("queue_id", uint64_t(0)) == 0 ||
                !blocks.is_array() || blocks.empty() || frame.at("first_submit") != blocks.front() || frame.at("last_submit") != blocks.back())
                throw std::runtime_error("GPU measurement plan has invalid frame or submission boundaries");
            uint64_t previous = 0;
            for (const auto& block : blocks) {
                if (!block.is_number_unsigned() && !block.is_number_integer()) throw std::runtime_error("GPU submission block identity is invalid");
                const auto index = block.get<uint64_t>();
                if (!index || index <= previous) throw std::runtime_error("GPU submission blocks must increase strictly");
                previous = index;
            }
        }
    }
    Json overrides = Json::array();
    for (const auto& [module, file] : r.overrides) {
        if (!module) throw std::runtime_error("Replacement module identity is unavailable");
        overrides.push_back({{"module", module}, {"file", file.u8string()}, {"sha256", CachedSha256File(file, cancel)}});
    }
    std::filesystem::create_directories(r.output);
    Json manifest{{"owner", "gfxrecon_gui"}, {"schema", 3}, {"complete", false},
        {"capture", r.capture.u8string()}, {"capture_sha256", captureHash}, {"target", target.Label()},
        {"source_capture",request.capture.u8string()},{"source_capture_sha256",sourceHash},{"optimization",optimization},
        {"serial", target.serial}, {"frame", r.frame}, {"timeout_seconds",r.timeout.count()}, {"selection_generation", r.selectionGeneration},
        {"dump_request", r.dump}, {"overrides", overrides}, {"outputs_only", r.outputsOnly}, {"swapchain_mode", r.swapchainMode},
        {"memory_translation", memoryPolicy},
        {"captured_pipeline_cache_data", "excluded; only target-generated caches may be loaded"}};
    const auto backend = tools.find(target.android ? "replay_apk" : "replay");
    if (backend != tools.end()) {
        manifest["backend"] = backend->second;
        if (std::filesystem::is_regular_file(std::filesystem::u8path(backend->second)))
            manifest["backend_sha256"] = CachedSha256File(std::filesystem::u8path(backend->second), cancel);
    }
    if (r.measurement.gpu && (backend == tools.end() || !GpuCapable(std::filesystem::u8path(backend->second), manifest.value("backend_sha256", ""))))
        throw std::runtime_error("GPU profiling requires the bundled replay backend/APK with frame GPU profiling protocol 1 and an identity-matched capability file. Rebuild/update the backend.");
    if (!r.measurement.counterWindow.is_null()) {
        const auto capability = ReadJson(std::filesystem::u8path(backend->second + ".capabilities.json"));
        if (capability.value("frame_counter_window_protocol", 0) != 1)
            throw std::runtime_error("Replay backend lacks frame counter window protocol 1; update the bundled backend/APK and matching capability file");
    }
    if (target.android) deviceLock.emplace(target.serial, cancel);
    std::string deviceIdentity = target.android ? std::string{} : LocalReplayDeviceIdentity();
    Json deviceMetadata{{"target", target.Label()}, {"serial", target.serial}};
#ifdef _WIN32
    deviceMetadata["os"] = "Windows";
#elif defined(__APPLE__)
    deviceMetadata["os"] = "macOS";
#else
    deviceMetadata["os"] = "Linux";
#endif
    bool pipelineCapable = false;
    if (target.android && (!r.cacheDirectory.empty() || r.measurement.gpu)) {
        AdbConnection adb{tools.at("adb"), target.serial, cancel};
        // Preserve each command's output (including newlines) in fixed NUL fields,
        // while paying for a single host/device round trip.
        const auto metadata = AndroidFields(adb.Shell({"sh", "-c",
            "getprop ro.build.fingerprint; printf '\\000'; getprop ro.vendor.build.fingerprint; printf '\\000'; "
            "driver=$(getprop ro.gfx.driver.0); printf '%s\\000' \"$driver\"; "
            "if test -n \"$driver\"; then dumpsys package \"$driver\"; fi; printf '\\000'; "
            "installed=$(pm path " + std::string(kPackage) + " | head -n 1); installed=${installed#package:}; printf '%s\\000' \"$installed\"; "
            "if test -n \"$installed\"; then sha256sum \"$installed\" 2>/dev/null || true; fi; printf '\\000'; "
            "getprop ro.product.model; printf '\\000'; getprop ro.build.version.release; printf '\\000'"}), 8);
        const auto& build = metadata[0]; const auto& vendorBuild = metadata[1];
        deviceIdentity = build + vendorBuild;
        if (r.measurement.gpu) deviceMetadata = {{"os", "Android"}, {"serial", target.serial}, {"build_fingerprint", Trim(build)},
            {"vendor_build_fingerprint", Trim(vendorBuild)}, {"model", Trim(metadata[6])}, {"os_version", Trim(metadata[7])}};
        auto driverPackage = metadata[2];
        while (!driverPackage.empty() && (driverPackage.back() == '\n' || driverPackage.back() == '\r' || driverPackage.back() == ' ')) driverPackage.pop_back();
        deviceIdentity += driverPackage;
        if (r.measurement.gpu) deviceMetadata["configured_driver_package"] = driverPackage;
        std::istringstream lines(metadata[3]); std::string line;
        while (std::getline(lines, line)) if (line.find("versionCode=") != std::string::npos || line.find("lastUpdateTime=") != std::string::npos) deviceIdentity += line;
        const auto& installed = metadata[4];
        if (!installed.empty()) {
            const auto installedHash = metadata[5].size() >= 64 ? metadata[5].substr(0, 64) : installed;
            deviceIdentity += installedHash;
            pipelineCapable = backend != tools.end() && installedHash == manifest.value("backend_sha256", "") &&
                CacheCapable(std::filesystem::u8path(backend->second), installedHash);
            if (r.measurement.gpu) {
                deviceMetadata["installed_apk_sha256"] = installedHash;
                if (installedHash != manifest.value("backend_sha256", ""))
                    throw std::runtime_error("Installed replay APK differs from the GPU-capable configured APK. Install the newly built bundled APK before GPU measurement.");
            }
        }
        else throw std::runtime_error("Install the compatible GFXReconstruct replay APK before rendering");
    }
    Json pipelineRequest{{"schema", 1}, {"capture_sha256", captureHash}, {"target", target.Label()},
        {"device_build", deviceIdentity}, {"backend_sha256", manifest.value("backend_sha256", "")}, {"overrides", overrides},
        {"swapchain_mode", r.swapchainMode}, {"memory_translation", memoryPolicy.at("effective")}};
    if (r.measurement.gpu) {
        pipelineRequest["profiling_mode"] = "single-frame";
    }
    const auto pipelineIdentity = Sha256Text(pipelineRequest.dump());
    Json gpuRequest;
    if (r.measurement.gpu) {
        const auto timeoutSeconds = std::max<int64_t>(1, std::min<int64_t>(30, r.timeout.count() - 10));
        gpuRequest = {{"protocol", 1}, {"mode", "isolated-frame"}, {"swapchain_mode", r.swapchainMode}, {"capture_sha256", captureHash},
            {"memory_translation", memoryPolicy},
            {"backend_sha256", manifest.at("backend_sha256")}, {"frames", r.measurement.gpuPlan.at("frames")},
            {"output_file", (r.output / "gpu-measurement.json").u8string()},
            {"timeout_ns", uint64_t(timeoutSeconds) * 1000000000ull}, {"overrides", Json::array()}};
        if (!r.measurement.counterWindow.is_null()) {
            gpuRequest["counter_window"] = r.measurement.counterWindow;
            gpuRequest["counter_window"]["ready_file"] = (r.output / "perf-ready.json").u8string();
            gpuRequest["counter_window"]["continue_file"] = (r.output / "perf-continue.json").u8string();
        }
        for (const auto& replacement : overrides) gpuRequest["overrides"].push_back({{"module", replacement.at("module")}, {"sha256", replacement.at("sha256")}});
        gpuRequest["request_id"] = Sha256Text(captureHash + manifest.at("backend_sha256").get<std::string>() +
            r.output.u8string() + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + gpuRequest.dump());
        AtomicJson(r.output / "gpu-measurement-request.json", gpuRequest);
        if (!r.measurement.counterWindow.is_null()) pipelineRequest["counter_window"] = r.measurement.counterWindow;
    }
    manifest["device_identity_source"] = "current_target";
    manifest["current_device_verified"] = true;
    manifest["request_identity"] = {{"capture_sha256", captureHash}, {"target", target.Label()},
        {"device_build", deviceIdentity}, {"backend_sha256", manifest.value("backend_sha256", "")}, {"overrides", overrides},
        {"swapchain_mode", r.swapchainMode}, {"memory_translation", memoryPolicy.at("effective")}, {"memory_translation_policy", memoryPolicy}};
    // Rendered results are never restored or duplicated into a persistent cache.
    // GUI inspections retain process-local snapshots; both front ends reuse the
    // hash-checked target-generated PSO cache, including during fresh perf runs.
    AtomicJson(r.output / "request.json", manifest);
    try {
    ReplayResult result = target.android ? RunAndroid(r, target, pipelineIdentity, captureHash, pipelineCapable&&!r.validation, tools, cancel) : RunLocal(r, tools, pipelineIdentity, cancel);
    if(r.validation){
        std::string log=result.log;
        if(std::filesystem::exists(r.output/"validation-full.log")){std::ifstream input(r.output/"validation-full.log");log.assign(std::istreambuf_iterator<char>(input),{});}
        result.validationReport=ParseValidationLog(log);result.validationReport["frame"]=r.frame;result.validationReport["target"]=target.Label();
        result.validationReport["source_capture"]=request.capture.u8string();AtomicJson(r.output/"validation.json",result.validationReport);
        if(!result.validationReport.value("enabled",false))throw std::runtime_error("Vulkan validation was requested but VK_LAYER_KHRONOS_validation was not enabled. Install/configure the validation layer. See "+(r.output/"validation.json").u8string());
    }
    if (r.measurement.gpu) {
        const auto file = r.output / "gpu-measurement.json";
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("Replay completed without a GPU measurement report. See replay.log; no CPU timing fallback is available.");
        result.gpuMeasurement = ReadJson(file);
        VerifyGpuMeasurementReport(gpuRequest, result.gpuMeasurement);
        if (gpuRequest.contains("counter_window"))
            result.gpuMeasurement["counter_window"] = ValidateFrameCounterWindow(gpuRequest,result.gpuMeasurement);
        result.gpuMeasurement["memory_translation"] = memoryPolicy;
        const bool cacheEnabled = !r.cacheDirectory.empty() && (target.android ? pipelineCapable : CacheCapable(std::filesystem::u8path(backend->second), manifest.value("backend_sha256", "")));
        result.gpuMeasurement["provenance"] = {{"source_capture_sha256", sourceHash}, {"capture_sha256", captureHash},
            {"backend_sha256", manifest.at("backend_sha256")}, {"request_id", gpuRequest.at("request_id")},
            {"optimization", optimization}, {"device", deviceMetadata}, {"swapchain", result.gpuMeasurement.at("swapchain")}, {"memory_translation", memoryPolicy},
            {"cache_policy", {{"result_cache", "bypassed"}, {"captured_pipeline_cache", "excluded"},
                {"memory_translation", memoryPolicy.at("effective")},
                {"target_pipeline_cache", cacheEnabled ? "allowed; load if present, save if missing" : "disabled"},
                {"driver_cache_state", "unknown; uncontrolled"}, {"hardware_cache_state", "unknown; uncontrolled"}}},
            {"cache_observations", {{"pipeline_cache_loaded", result.timings.value("pipeline_cache_loaded", false)},
                {"pipeline_cache_saved", result.timings.value("pipeline_cache_saved", false)},
                {"pipeline_cache_identity", result.timings.value("pipeline_cache_identity", "")},
                {"pipeline_cache_sha256", result.timings.value("pipeline_cache_sha256", Json{})}}}};
        result.gpuMeasurement["telemetry"] = result.timings.value("telemetry", Json::object());
        AtomicJson(file, result.gpuMeasurement);
    }
    if(r.measurement.enabled){
        const auto file=r.output/"measurement.json";
        if(!std::filesystem::is_regular_file(file))throw std::runtime_error("Replay completed without a measurement file (is the measurement range inside the capture?). See "+(r.output/"replay.log").u8string());
        result.measurement=ReadJson(file);
    }
    if (!r.dump.is_null()) result.resources = CollectResources(r.output);
    if (r.outputsOnly) {
        result.image=PrimaryOutputImage(result.resources);
    } else if (!r.measurement.enabled) {
        result.image = r.output / (r.prefix + "_frame_" + std::to_string(r.frame) + ".bmp");
        if (!std::filesystem::is_regular_file(result.image) || std::filesystem::file_size(result.image) < 54)
            throw std::runtime_error("Replay completed without the requested frame image. See " + r.output.u8string());
    }
    for (const auto& [module, file] : r.overrides) {
        if (r.measurement.gpu) continue; // Structured, hash-matched creation evidence was verified above.
        std::istringstream lines(result.log); std::string line; bool found = false;
        const auto name = "sh" + std::to_string(module);
        while (std::getline(lines, line)) if (line.find("Replacement shader found:") != std::string::npos) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto at = line.find_last_of("/\\");
            if (at != std::string::npos && line.substr(at + 1) == name) found = true;
        }
        if (!found) throw std::runtime_error("Replay did not confirm replacement of module " + name + "; previous successful results retained");
    }
    if(HasVisualization(r)){
        const auto reportFile=r.output/"gui-visualization-result.json";
        if(!std::filesystem::is_regular_file(reportFile))throw std::runtime_error("Replay backend lacks draw visualization support. Install the bundled replay APK/tool.");
        const auto report=ReadJson(reportFile);
        if(report.value("protocol",0)!=VisualizationConfig(r).at("protocol").get<int>()||report.value("mode",std::string{})!=r.visualization||report.value("subdraw",-2)!=r.indirectSubdraw||report.value("pipeline",uint64_t(0))!=r.visualizationPipeline)
            throw std::runtime_error("Replay visualization provenance does not match the selected draw");
        if(report.value("coverage_variant",false)!=r.coverageVariant)throw std::runtime_error("Replay backend does not support diagnostic coverage variants; update the replay tool/APK");
        if(!report.value("applied",false)||!report.value("error",std::string{}).empty())throw std::runtime_error("Draw visualization unavailable: "+report.value("error",std::string("selected draw was not reached")));
    }
    if(r.visualization=="silhouette"){
        bool composed=false;
        for(const auto& asset:result.resources)if(asset.metadata.value("category","")=="output"&&asset.metadata.value("aspect","")=="COLOR"&&asset.metadata.value("location",0u)==0&&asset.file.extension()==".bmp"){
            const auto coverage=r.output/"outline-coverage.bmp";std::filesystem::copy_file(asset.file,coverage);
            WriteBmp(asset.file,ComposeDrawOutline(LoadImage(r.outlineBaseImage),LoadImage(r.outlineCoverageImage),LoadImage(asset.file),2,
                SourceEncodingForImage(asset.metadata.value("format",std::string{}))));composed=true;
        }
        if(!composed)throw std::runtime_error("Orange outline requires a readable COLOR 0 attachment");
        result.resources.push_back({r.output/"outline-coverage.bmp",std::filesystem::file_size(r.output/"outline-coverage.bmp"),"Diagnostic coverage",{{"category","coverage"}}});
    }
    if (CachedSha256File(r.capture, cancel) != captureHash) throw std::runtime_error("Capture changed during replay; artifacts were not published");
    if(r.capture!=request.capture && CachedSha256File(request.capture,cancel)!=sourceHash)
        throw std::runtime_error("Original capture changed during optimized replay; artifacts were not published");
    for (const auto& replacement : overrides) {
        const auto file = r.output / "overrides" / ("sh" + std::to_string(replacement.at("module").get<uint64_t>()));
        // Android copies candidates through ADB; retain an identical local snapshot for provenance.
        if (target.android) {
            std::filesystem::create_directories(file.parent_path());
            std::filesystem::copy_file(std::filesystem::u8path(replacement.at("file").get<std::string>()), file);
        }
        if (CachedSha256File(file, cancel) != replacement.at("sha256"))
            throw std::runtime_error("Shader candidate changed during replay; artifacts were not published");
    }
    manifest["artifacts"] = Json::array();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(r.output)) if (entry.is_regular_file() && entry.path().filename() != "request.json")
        manifest["artifacts"].push_back({{"file", entry.path().lexically_relative(r.output).generic_u8string()},
            {"bytes", entry.file_size()}, {"sha256", CachedSha256File(entry.path(), cancel)}});
    manifest["complete"] = true; manifest["image"] = result.image.filename().u8string();
    manifest["artifact_cache_hit"] = false;
    manifest["swapchain"] = result.timings.value("swapchain", Json{{"requested", r.swapchainMode}, {"effective", nullptr}, {"source", "unavailable"}});
    manifest["validation"]=result.validationReport;
    if(r.measurement.enabled)manifest["measurement"]=result.measurement;
    if(r.measurement.gpu)manifest["gpu_measurement"]=result.gpuMeasurement;
    manifest["backend_timing"] = result.timings;
    manifest["elapsed_seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    manifest["override_count"] = r.overrides.size();
    AtomicJson(r.output / "manifest.json", manifest);
    result.timings["artifact_cache_hit"] = false;
    result.timings["elapsed_seconds"] = manifest["elapsed_seconds"];
    result.timings["optimized_replay"]=optimization.value("status","")=="optimized";
    result.timings["optimizer_status"]=optimization.value("status","");
    if (target.android && backend != tools.end() && !pipelineCapable &&
        CacheCapable(std::filesystem::u8path(backend->second), manifest.value("backend_sha256", "")))
        result.timings["replay_update_required"] = true;
    return result;
    } catch (const std::exception& error) {
        if(r.validation&&std::filesystem::exists(r.output/"replay.log")){
            std::ifstream input(std::filesystem::exists(r.output/"validation-full.log")?r.output/"validation-full.log":r.output/"replay.log");std::string log{std::istreambuf_iterator<char>(input),{}};
            auto report=ParseValidationLog(log);report["status"]="failed";report["message"]=error.what();AtomicJson(r.output/"validation.json",report);
        }
        manifest["error"] = error.what();
        AtomicJson(r.output / "failure.json", manifest);
        throw;
    }
}
Json ParseValidationLog(const std::string& log){
    Json report{{"enabled",false},{"status","complete"},{"errors",0},{"warnings",0},{"messages",Json::array()},{"truncated",log.find("log preview truncated")!=std::string::npos}};
    std::istringstream input(log);std::string line;bool added=false;
    static const std::regex vuid("VUID-[A-Za-z0-9_-]+");
    while(std::getline(input,line)){
        if(line.find("Replay has added the following required layers")!=std::string::npos)added=true;
        else if(added&&line.find('\t')!=std::string::npos){if(line.find("VK_LAYER_KHRONOS_validation")!=std::string::npos)report["enabled"]=true;}
        else added=false;
        const bool diagnostic=line.find("[gfxrecon]")!=std::string::npos||line.find("Validation Error")!=std::string::npos||line.find("Validation Warning")!=std::string::npos;
        if(!diagnostic||(line.find("VUID-")==std::string::npos&&line.find("Validation Error")==std::string::npos&&line.find("DEBUG MESSENGER")==std::string::npos))continue;
        const bool error=line.find("ERROR")!=std::string::npos||line.find("Validation Error")!=std::string::npos;
        const auto severity=error?"error":"warning";report[error?"errors":"warnings"]=report[error?"errors":"warnings"].get<int>()+1;
        std::smatch match;std::regex_search(line,match,vuid);
        if(report["messages"].size()<10000)report["messages"].push_back({{"severity",severity},{"vuid",match.empty()?"":match.str()},{"message",line.substr(0,16384)}});
        else report["truncated"]=true;
    }
    if(log.find("Failed to enable validation layer")!=std::string::npos)report["enabled"]=false;
    if(!report["enabled"].get<bool>())report["status"]="unavailable";
    else if(report["errors"].get<int>()>0)report["status"]="completed with errors";
    return report;
}
} // namespace gfx
