// Opt-in host diagnostic; never invoked by normal GUI/CLI startup.
#include "core/artifacts.h"
#include "platform/adb.h"
#include "platform/device_lock.h"
#include "replay/gpu_counters.h"
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using gfx::Json;
static Json Read(const fs::path &path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot read " + path.u8string());
    return Json::parse(input);
}
struct RemoteCleanup {
    gfx::AdbConnection &adb;
    std::string remote;
    ~RemoteCleanup() {
        for (const char *name :
             {"precision-workload", "precision.vert.spv", "precision_full.frag.spv",
              "precision_half.frag.spv", "full-a.ready", "full-a.go", "full-a.json", "half.ready", "half.go",
              "half.json", "full-b.ready", "full-b.go", "full-b.json"})
            try {
                adb.TryShell({"rm", "-f", remote + "/" + name}, true);
            } catch (...) {
            }
        try {
            adb.TryShell({"rmdir", remote}, true);
        } catch (...) {
        }
    }
};

struct Options {
    std::filesystem::path workload, output, tools = "local-tools.json";
    std::string serial;
    uint32_t frames = 30, iterations = 256;
    double fps = 30;
};
static Options Parse(int argc, char **argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        if (i + 1 >= argc)
            throw std::runtime_error("Missing value for " + name);
        const std::string value = argv[++i];
        if (name == "--serial")
            result.serial = value;
        else if (name == "--tools")
            result.tools = std::filesystem::u8path(value);
        else if (name == "--workload-dir")
            result.workload = std::filesystem::u8path(value);
        else if (name == "--output")
            result.output = std::filesystem::u8path(value);
        else if (name == "--frames" || name == "--iterations") {
            size_t used = 0;
            const auto number = std::stoull(value, &used);
            if (used != value.size() || number > UINT32_MAX)
                throw std::runtime_error("Invalid integer: " + name);
            (name == "--frames" ? result.frames : result.iterations) = static_cast<uint32_t>(number);
        } else if (name == "--fps") {
            size_t used = 0;
            result.fps = std::stod(value, &used);
            if (used != value.size())
                throw std::runtime_error("Invalid FPS");
        } else
            throw std::runtime_error("Unknown argument " + name);
    }
    if (result.serial.empty() || result.workload.empty() || result.output.empty())
        throw std::runtime_error("Required: --serial DEVICE --tools FILE "
                                 "--workload-dir DIR --output NEW_DIR");
    if (!result.frames || result.frames > 120 || !result.iterations || result.iterations > 8192 ||
        !std::isfinite(result.fps) || result.fps <= 0 || result.fps > 120 || result.frames / result.fps > 10)
        throw std::runtime_error("Calibration must use 1..120 frames, 1..8192 iterations, FPS in "
                                 "(0,120], at most ten seconds per variant");
    result.tools = std::filesystem::absolute(result.tools);
    result.workload = std::filesystem::absolute(result.workload);
    result.output = std::filesystem::absolute(result.output);
    if (std::filesystem::exists(result.output) &&
        (!std::filesystem::is_directory(result.output) || !std::filesystem::is_empty(result.output)))
        throw std::runtime_error("Calibration output must be a new empty directory");
    return result;
}

static int Run(const Options &options) {
    const auto base = options.workload;
    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const auto output = options.output;
    fs::create_directories(output);
    const std::string remote = "/data/local/tmp/gfxrecon_precision_" + stamp;
    Json summary{{"status", "running"},
                 {"scope", "controlled offscreen fragment arithmetic; counters "
                           "remain system-wide sampled estimates"},
                 {"runs", Json::array()}};
    auto tools = Read(options.tools);
    for (const char *key : {"adb", "gpu_counter_producer"}) {
        const auto value = fs::u8path(tools.at(key).get<std::string>());
        if (value.is_relative() && value.has_parent_path())
            tools[key] = (options.tools.parent_path() / value).lexically_normal().u8string();
    }
    std::atomic_bool cancel{false};
    gfx::ConfigureDeviceLocks(output / "locks");
    try {
        gfx::DeviceLock lease(options.serial, &cancel);
        gfx::AdbConnection adb{tools.at("adb").get<std::string>(), options.serial, &cancel};
        const auto replay = adb.TryShell({"pidof", "com.lunarg.gfxreconstruct.replay"});
        if (replay.Success() && replay.output.find_first_of("0123456789") != std::string::npos)
            throw std::runtime_error("Replay process active; refusing calibration");
        const auto before = adb.Shell({"perfetto", "--query"});
        std::ofstream(output / "perfetto-before.txt") << before;
        std::ofstream(output / "keyguard-observation.txt") << adb.Shell({"dumpsys", "window", "policy"});
        adb.Shell({"mkdir", "-p", remote});
        RemoteCleanup cleanup{adb, remote};
        const auto buildProvenance = Read(base / "build-provenance.json");
        summary["build_provenance"] = buildProvenance;
        for (const char *name : {"precision-workload", "precision.vert.spv", "precision_full.frag.spv",
                                 "precision_half.frag.spv"}) {
            const auto file = base / name;
            if (gfx::Sha256File(file) != buildProvenance.at("hashes").at(name))
                throw std::runtime_error("Calibration build provenance does not match workload/shader bytes");
            const auto pushed = adb.Run({"push", file.u8string(), remote + "/" + name});
            if (!pushed.Success())
                throw std::runtime_error(pushed.output);
            if (adb.Shell({"sha256sum", remote + "/" + name}).substr(0, 64) != gfx::Sha256File(file))
                throw std::runtime_error("Calibration transfer hash mismatch");
        }
        adb.Shell({"chmod", "700", remote + "/precision-workload"});
        for (const std::string label : {"full-a", "half", "full-b"}) {
            const auto directory = output / label;
            fs::create_directories(directory);
            const std::string mode = label == "half" ? "half" : "full";
            const std::string ready = remote + "/" + label + ".ready", go = remote + "/" + label + ".go",
                              result = remote + "/" + label + ".json";
            gfx::AndroidGpuCounters counters(adb.executable, adb.serial, directory / "counters",
                                             fs::u8path(tools.at("gpu_counter_producer").get<std::string>()),
                                             &cancel);
            counters.DiscoverPrecision(1000000);
            gfx::ProcessRequest process{
                adb.executable,
                {"-s", adb.serial, "shell",
                 gfx::AndroidCommand({remote + "/precision-workload", mode, remote + "/precision.vert.spv",
                                      remote + "/precision_" + mode + ".frag.spv", ready, go, result,
                                      std::to_string(options.iterations), std::to_string(options.frames),
                                      std::to_string(options.fps)})}};
            process.timeout = std::chrono::seconds(100);
            process.stdoutFile = directory / "native-console.txt";
            std::atomic_bool nativeCancel{false};
            auto running =
                std::async(std::launch::async, [&] { return gfx::RunProcess(process, &nativeCancel); });
            uint64_t nativePid = 0;
            try {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (!adb.TryShell({"test", "-e", ready}).Success()) {
                    if (running.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                        throw std::runtime_error("Native initialization ended before "
                                                 "ready; inspect output JSON");
                    if (std::chrono::steady_clock::now() > deadline)
                        throw std::runtime_error("Native ready timeout");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                auto fetched = adb.Run({"exec-out", "cat", ready}, {}, directory / "ready.json");
                if (!fetched.Success())
                    throw std::runtime_error(fetched.output);
                const auto capabilities = Read(directory / "ready.json");
                nativePid = capabilities.at("pid").get<uint64_t>();
                counters.Start(uint64_t(std::ceil(options.frames / options.fps * 1000)) + 5000);
                adb.Shell({"touch", go});
                const auto finished = running.get();
                if (!finished.Success())
                    throw std::runtime_error("Native workload failed: " + finished.output);
                fetched = adb.Run({"exec-out", "cat", result}, {}, directory / "native-result.json");
                if (!fetched.Success())
                    throw std::runtime_error(fetched.output);
                const auto measured = Read(directory / "native-result.json");
                if (measured.at("status") != "complete")
                    throw std::runtime_error(measured.dump());
                if (measured.at("fragment_sha256") !=
                        buildProvenance.at("hashes").at("precision_" + mode + ".frag.spv") ||
                    measured.at("vertex_sha256") != buildProvenance.at("hashes").at("precision.vert.spv") ||
                    !measured.at("shaderFloat16_enabled").get<bool>())
                    throw std::runtime_error(
                        "Native shader/feature evidence does not match controlled inputs");
                auto sampled = counters.StopAndDecode(measured.at("window_begin_boot_ns").get<uint64_t>(),
                                                      measured.at("window_end_boot_ns").get<uint64_t>());
                Json row{{"label", label},
                         {"workload", measured},
                         {"counter_definitions", sampled.at("selected_definition_sha256")},
                         {"counters", Json::array()}};
                for (const auto &counter : sampled.at("counters"))
                    row["counters"].push_back({{"id", counter.at("id")},
                                               {"semantic", counter.at("semantic")},
                                               {"name", counter.at("name")},
                                               {"summary", counter.at("summary")}});
                summary["runs"].push_back(std::move(row));
                gfx::AtomicJson(output / "calibration-result.json", summary);
            } catch (...) {
                try {
                    adb.Run({"exec-out", "cat", result}, {}, directory / "native-failure.json", true);
                    const auto failure = Read(directory / "native-failure.json");
                    if (failure.value("status", "") == "unsupported")
                        summary["status"] = "unsupported";
                } catch (...) {
                }
                if (!nativePid) {
                    auto ids = adb.TryShell({"pidof", "precision-workload"}, true);
                    std::istringstream candidates(ids.output);
                    uint64_t candidate;
                    while (candidates >> candidate) {
                        auto identity =
                            adb.TryShell({"cat", "/proc/" + std::to_string(candidate) + "/cmdline"}, true);
                        if (identity.Success() && identity.output.substr(0, identity.output.find('\0')) ==
                                                      remote + "/precision-workload") {
                            nativePid = candidate;
                            break;
                        }
                    }
                }
                if (nativePid) {
                    auto identity =
                        adb.TryShell({"cat", "/proc/" + std::to_string(nativePid) + "/cmdline"}, true);
                    if (identity.Success() && identity.output.substr(0, identity.output.find('\0')) ==
                                                  remote + "/precision-workload")
                        adb.TryShell({"kill", "-TERM", std::to_string(nativePid)}, true);
                }
                nativeCancel = true;
                if (running.valid())
                    running.wait();
                throw;
            }
        }
        auto metric = [&](size_t run, const std::string &semantic) {
            for (const auto &c : summary["runs"][run]["counters"])
                if (c["semantic"] == semantic)
                    return c["summary"]["mean"].get<double>();
            throw std::runtime_error("Metric missing");
        };
        const auto fullReference = (metric(0, "fragment_full") + metric(2, "fragment_full")) / 2;
        const auto halfReference = (metric(0, "fragment_half") + metric(2, "fragment_half")) / 2;
        const bool directional =
            metric(1, "fragment_half") > halfReference && metric(1, "fragment_full") < fullReference;
        bool comparable = true;
        for (const auto &row : summary["runs"])
            comparable = comparable && row["workload"]["frames_completed"] == options.frames &&
                         row["workload"]["iterations_per_fragment"] == options.iterations &&
                         row["workload"]["fps_requested"] == options.fps &&
                         row["workload"]["missed_deadlines"] == 0 &&
                         row["workload"]["device"] == summary["runs"][0]["workload"]["device"] &&
                         row["counter_definitions"] == summary["runs"][0]["counter_definitions"];
        summary["same_work_driver_cadence_counter_definitions"] = comparable;
        summary["qualitative_counter_response_supported"] = directional && comparable;
        summary["status"] = "complete";
        summary["directional_full_to_half_response"] = directional;
        summary["full_reference_mean_rate"] = fullReference;
        summary["half_reference_mean_rate"] = halfReference;
        summary["full_a_b_pixels_equal"] =
            summary["runs"][0]["workload"]["pixel_sha256"] == summary["runs"][2]["workload"]["pixel_sha256"];
        summary["qualitative_counter_response_supported"] =
            directional && comparable && summary["full_a_b_pixels_equal"].get<bool>();
        summary["interpretation"] = "Descriptive response to explicit fragment FP16 controls; no magic "
                                    "ratio certifies units, no exact shader/process attribution, no proof "
                                    "for a separate RelaxedPrecision edit.";
        for (const char *name :
             {"precision-workload", "precision.vert.spv", "precision_full.frag.spv",
              "precision_half.frag.spv", "full-a.ready", "full-a.go", "full-a.json", "half.ready", "half.go",
              "half.json", "full-b.ready", "full-b.go", "full-b.json"})
            adb.TryShell({"rm", "-f", remote + "/" + name}, true);
        adb.TryShell({"rmdir", remote}, true);
        std::ofstream(output / "perfetto-after.txt") << adb.Shell({"perfetto", "--query"});
        gfx::AtomicJson(output / "calibration-result.json", summary);
        std::cout << output.u8string() << '\n' << summary.dump(2) << '\n';
        return 0;
    } catch (const std::exception &error) {
        if (summary["status"] != "unsupported")
            summary["status"] = "failed";
        summary["error"] = error.what();
        gfx::AtomicJson(output / "calibration-result.json", summary);
        std::cerr << output.u8string() << "\n" << error.what() << '\n';
        return 1;
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Opt-in fragment precision counter validation\n"
                     "--serial DEVICE --tools FILE --workload-dir DIR --output NEW_DIR\n"
                     "[--frames 30 --iterations 256 --fps 30]\n";
        return 0;
    }
    try {
        return Run(Parse(argc, argv));
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 2;
    }
}
