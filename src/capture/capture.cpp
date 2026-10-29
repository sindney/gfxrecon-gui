#include "capture/capture.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <cstdlib>

namespace gfx {
namespace {
const std::set<std::string> kSettings{"enable_gpu_debug_layers", "gpu_debug_app", "gpu_debug_layers", "gpu_debug_layer_app"};
const std::set<std::string> kProperties{"debug.gfxrecon.capture_file", "debug.gfxrecon.capture_frames",
    "debug.gfxrecon.capture_file_timestamp", "debug.gfxrecon.log_file", "debug.gfxrecon.log_file_flush_after_write",
    "debug.gfxrecon.log_level", "debug.gfxrecon.capture_trigger", "debug.gfxrecon.capture_process_name",
    "debug.gfxrecon.capture_android_trigger", "debug.gfxrecon.capture_trigger_frames", "debug.gfxrecon.capture_trigger_boundary"};
std::string Trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    const auto start = value.find_first_not_of(" \r\n\t");
    return start == std::string::npos ? "" : value.substr(start);
}
void Save(const std::filesystem::path& path, const Json& document) {
    auto temporary = path; temporary += ".tmp";
    { std::ofstream file(temporary, std::ios::binary); if (!(file << document.dump(2))) throw std::runtime_error("Cannot save capture journal"); }
    std::filesystem::rename(temporary, path);
}
Json Load(const std::filesystem::path& path) {
    std::ifstream input(path); if (!input) throw std::runtime_error("Cannot read capture journal");
    return Json::parse(input);
}
void CheckPackage(const std::string& package) {
    if (package.empty() || package.find('.') == std::string::npos ||
        !std::all_of(package.begin(), package.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_'; }))
        throw std::runtime_error("Choose a valid Android application package.");
}
void CheckDevice(const AdbConnection& adb) {
    if (adb.Shell({"dumpsys", "window", "policy"}).find("showing=true") != std::string::npos)
        throw std::runtime_error("Phone is locked. Unlock " + adb.serial + " before continuing.");
}
std::string Current(const AdbConnection& adb, const Json& entry, bool cleanup = false) {
    auto value = entry.at("kind") == "setting" ? adb.Shell({"settings", "get", "global", entry.at("name").get<std::string>()}, cleanup) :
        adb.Shell({"getprop", entry.at("name").get<std::string>()}, cleanup);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}
void Set(const AdbConnection& adb, const Json& entry, const std::string& value, bool cleanup = false) {
    const auto name = entry.at("name").get<std::string>();
    if (entry.at("kind") == "setting") {
        if (value == "null") adb.Shell({"settings", "delete", "global", name}, cleanup);
        else adb.Shell({"settings", "put", "global", name, value}, cleanup);
    } else adb.Shell({"setprop", name, value}, cleanup);
}
std::vector<std::filesystem::path> WindowsFiles(const std::filesystem::path& output) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(output))
        if (entry.is_regular_file() && entry.path().extension() == ".gfxr" && entry.file_size() > 16) files.push_back(entry.path());
    std::sort(files.begin(), files.end()); return files;
}
} // namespace
void ValidateCaptureFrames(const std::string& frames) {
    if (frames.empty()) throw std::runtime_error("Enter a bounded frame range, such as 1-3.");
    std::istringstream parts(frames); std::string range; uint64_t last = 0;
    if (frames.back() == ',') throw std::runtime_error("Invalid capture frame range");
    while (std::getline(parts, range, ',')) {
        const auto dash = range.find('-');
        auto number = [](const std::string& text) {
            if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                throw std::runtime_error("Frame ranges must contain positive numbers and optional hyphens.");
            return std::stoull(text);
        };
        const auto begin = number(range.substr(0, dash));
        const auto end = dash == std::string::npos ? begin : number(range.substr(dash + 1));
        if (!begin || end < begin || begin <= last || end > UINT32_MAX)
            throw std::runtime_error("Frame ranges must be positive, ascending, and non-overlapping.");
        last = end;
    }
}
RestoreDecision PlanRestore(const std::string& previous, const std::string& requested, const std::string& current) {
    if (current == previous) return RestoreDecision::AlreadyRestored;
    return current == requested ? RestoreDecision::Restore : RestoreDecision::Conflict;
}
AndroidInfo InspectAndroidDevice(const AdbConnection& adb) {
    CheckDevice(adb);
    AndroidInfo info; info.serial = adb.serial;
    info.model = Trim(adb.Shell({"getprop", "ro.product.model"}));
    info.androidVersion = Trim(adb.Shell({"getprop", "ro.build.version.release"}));
    info.abi = Trim(adb.Shell({"getprop", "ro.product.cpu.abilist"}));
    std::istringstream lines(adb.Shell({"pm", "list", "packages", "-3"})); std::string line;
    while (std::getline(lines, line)) if (line.rfind("package:", 0) == 0) info.packages.push_back(Trim(line.substr(8)));
    std::sort(info.packages.begin(), info.packages.end());
    return info;
}
void RestoreAndroidCapture(const AdbConnection& adb, const std::filesystem::path& path) {
    auto journal = Load(path);
    if (journal.at("serial") != adb.serial) throw std::runtime_error("Capture recovery journal belongs to another device.");
    if (journal.value("requires_game_exit", false) && adb.TryShell({"pidof", journal.at("package").get<std::string>()}, true).Success())
        throw std::runtime_error("Close the launched game before restoring its prior enabled runtime trigger.");
    bool conflicts = false;
    for (auto& entry : journal.at("changes")) {
        const auto kind = entry.at("kind").get<std::string>(), name = entry.at("name").get<std::string>();
        if ((kind != "setting" || !kSettings.count(name)) && (kind != "property" || !kProperties.count(name)))
            throw std::runtime_error("Unrecognized device setting in capture recovery journal.");
        if (entry.value("restored", false)) continue;
        const auto action = PlanRestore(entry.at("previous"), entry.at("value"), Current(adb, entry, true));
        if (action == RestoreDecision::Conflict) { conflicts = true; continue; }
        if (action == RestoreDecision::Restore) Set(adb, entry, entry.at("previous"), true);
        entry["restored"] = true; Save(path, journal);
    }
    journal["restored"] = !conflicts; Save(path, journal);
    if (conflicts) throw std::runtime_error("Device settings changed outside this capture session. Recovery conflict recorded in " + path.u8string());
}
CaptureResult CaptureAndroid(const AdbConnection& adb, const CaptureRequest& r) {
    if (r.progress) r.progress("Checking the application and device");
    CheckPackage(r.package); ValidateCaptureFrames(r.frames); CheckDevice(adb);
    if (adb.Shell({"pm", "path", "com.lunarg.gfxreconstruct.replay"}).find("package:") == std::string::npos)
        throw std::runtime_error("Install the compatible replay/layer APK before capture.");
    adb.Shell({"run-as", r.package, "id"}); // fails clearly for non-debuggable apps
    std::filesystem::create_directories(r.output);
    const auto id = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream token; token << std::hex << static_cast<uint32_t>(id);
    const auto relative = "files/g" + token.str();
    const auto remote = "/data/data/" + r.package + "/" + relative;
    if ((remote + "/c.gfxr").size() > 91) throw std::runtime_error("Application-private capture path exceeds the Android property size limit.");
    if (adb.TryShell({"run-as", r.package, "test", "-e", relative}).Success())
        throw std::runtime_error("Capture staging path already exists. Start a new capture to allocate another session.");
    adb.Shell({"run-as", r.package, "mkdir", "-p", relative});
    Json journal{{"schema", 1}, {"serial", adb.serial}, {"package", r.package}, {"remote", remote}, {"restored", false}, {"changes", Json::array()}};
    auto setting = [&](const char* kind, const char* name, const std::string& value) {
        Json entry{{"kind", kind}, {"name", name}, {"value", value}, {"restored", false}};
        entry["previous"] = Current(adb, entry); journal["changes"].push_back(std::move(entry));
    };
    setting("setting", "enable_gpu_debug_layers", "1");
    setting("setting", "gpu_debug_app", r.package);
    setting("setting", "gpu_debug_layers", "VK_LAYER_LUNARG_gfxreconstruct");
    setting("setting", "gpu_debug_layer_app", "com.lunarg.gfxreconstruct.replay");
    setting("property", "debug.gfxrecon.capture_file", remote + "/c.gfxr");
    setting("property", "debug.gfxrecon.capture_frames", r.frames);
    setting("property", "debug.gfxrecon.capture_file_timestamp", "false");
    setting("property", "debug.gfxrecon.log_file", remote + "/l.txt");
    setting("property", "debug.gfxrecon.log_file_flush_after_write", "true");
    setting("property", "debug.gfxrecon.log_level", "info");
    setting("property", "debug.gfxrecon.capture_trigger", "");
    setting("property", "debug.gfxrecon.capture_process_name", r.package);
    CaptureResult result; result.journal = r.output / "device-journal.json";
    Save(result.journal, journal);
    bool launched = false;
    try {
        if (r.progress) r.progress("Preparing capture settings");
        for (const auto& entry : journal["changes"]) Set(adb, entry, entry.at("value"));
        auto activity = Trim(adb.Shell({"cmd", "package", "resolve-activity", "--brief", r.package}));
        const auto newline = activity.find_last_of('\n'); if (newline != std::string::npos) activity = activity.substr(newline + 1);
        if (activity.find('/') == std::string::npos) throw std::runtime_error("No launchable activity found for " + r.package);
        adb.Stop(r.package);
        std::vector<std::string> args{"am", "start", "-W", "-n", activity};
        if (!r.arguments.empty()) args.insert(args.end(), {"--es", "args", r.arguments});
        launched = true;
        const auto started = adb.Shell(args);
        if (started.find("Error:") != std::string::npos || started.find("Exception") != std::string::npos)
            throw std::runtime_error("Could not launch capture target:\n" + started);
        Save(r.output / "launch.json", Json{{"activity", activity}, {"output", started}});
        if (r.progress) r.progress("Recording the selected frames");
        const auto deadline = std::chrono::steady_clock::now() + r.timeout;
        bool complete = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (adb.cancel && adb.cancel->load()) throw std::runtime_error("Capture cancelled.");
            const auto log = adb.TryShell({"run-as", r.package, "cat", relative + "/l.txt"});
            if (log.cancelled || log.timedOut) throw std::runtime_error("Capture interrupted; restoring device settings.");
            if (log.Success() && log.output.find("Finished recording graphics API capture") != std::string::npos) { complete = true; break; }
            auto running = adb.TryShell({"pidof", r.package});
            if (!running.Success() && Trim(running.output).empty()) {
                const auto finalLog = adb.TryShell({"run-as", r.package, "cat", relative + "/l.txt"});
                complete = finalLog.Success() && finalLog.output.find("Finished recording graphics API capture") != std::string::npos;
                if (!complete) throw std::runtime_error("Application exited before capture completed. Check Vulkan support and the capture log.");
                break;
            }
            if (!log.Success() && log.output.find("No such file") == std::string::npos)
                throw std::runtime_error("Capture device connection failed:\n" + log.output);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        adb.Stop(r.package); launched = false;
        if (!complete) throw std::runtime_error("Capture did not finish its frame range. Check app state and Vulkan support.");
        if (r.progress) r.progress("Retrieving the completed capture");
        const auto files = adb.Shell({"run-as", r.package, "find", relative, "-type", "f", "-name", "*.gfxr"});
        std::istringstream lines(files); std::string file;
        while (std::getline(lines, file)) {
            file = Trim(file);
            if (file.rfind(relative + "/", 0) != 0 || file.find("..") != std::string::npos || file.find('\\') != std::string::npos)
                throw std::runtime_error("Unexpected capture output path.");
            const auto path = r.output / std::filesystem::path(file).filename();
            adb.Get(r.package, file, path); result.files.push_back(path);
        }
        if (result.files.empty()) throw std::runtime_error("No capture produced. Check that the app uses Vulkan and reaches the requested frames.");
        std::sort(result.files.begin(), result.files.end());
        const auto log = adb.TryShell({"run-as", r.package, "cat", relative + "/l.txt"});
        if (log.Success()) { std::ofstream out(r.output / "capture.log"); out << log.output; }
        if (r.progress) r.progress("Restoring previous device settings");
        RestoreAndroidCapture(adb, result.journal);
        return result;
    } catch (const std::exception& error) {
        const std::string reason = error.what();
        if (launched) { try { adb.Stop(r.package); } catch (...) {} }
        try { RestoreAndroidCapture(adb, result.journal); }
        catch (const std::exception& recovery) { throw std::runtime_error(reason + "\nRecovery pending: " + recovery.what()); }
        throw std::runtime_error(reason);
    }
}
CaptureResult CaptureWindows(const ProcessRequest& p, const std::string& layerDirectory, const std::string& frames,
                              const std::filesystem::path& output, const std::atomic_bool* cancel) {
    ValidateCaptureFrames(frames);
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(layerDirectory) / "VkLayer_gfxreconstruct.json"))
        throw std::runtime_error("Configure a capture layer directory containing VkLayer_gfxreconstruct.json.");
    std::filesystem::create_directories(output);
    auto process = p;
    const auto inheritedPath = std::getenv("VK_LAYER_PATH");
    const auto inheritedLayers = std::getenv("VK_INSTANCE_LAYERS");
    process.environment["VK_LAYER_PATH"] = layerDirectory + (inheritedPath && *inheritedPath ? std::string(";") + inheritedPath : "");
    process.environment["VK_INSTANCE_LAYERS"] = std::string("VK_LAYER_LUNARG_gfxreconstruct") +
        (inheritedLayers && *inheritedLayers ? std::string(";") + inheritedLayers : "");
    process.environment["GFXRECON_CAPTURE_FILE"] = (output / "capture.gfxr").u8string();
    process.environment["GFXRECON_CAPTURE_FRAMES"] = frames;
    process.environment["GFXRECON_CAPTURE_FILE_TIMESTAMP"] = "false";
    process.environment["GFXRECON_QUIT_AFTER_CAPTURE_FRAMES"] = "true";
    process.timeout = std::chrono::seconds(120);
    const auto result = RunProcess(process, cancel);
    { std::ofstream log(output / "capture.log"); log << result.output; }
    if (result.cancelled || result.timedOut) throw std::runtime_error("Capture interrupted; any partial output has not been imported.");
    CaptureResult captured; captured.files = WindowsFiles(output);
    if (captured.files.empty()) throw std::runtime_error("No capture was produced. Verify Vulkan/layer compatibility and run the GUI without elevation.\n" + result.output);
    if (!result.Success()) throw std::runtime_error("Capture process failed; inspect capture.log before using incomplete output.");
    return captured;
}
} // namespace gfx
