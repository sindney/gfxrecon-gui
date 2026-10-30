#include "cli/cli.h"
#include "capture/live_capture.h"
#include "core/artifacts.h"
#include "platform/adb.h"
#include <algorithm>
#include <fstream>
#include <thread>

namespace gfx::cli {
namespace {
using Seconds = std::chrono::seconds;
Seconds Timeout(const Context& context, Seconds fallback) { return context.timeout.count() ? context.timeout : fallback; }
bool Cancelled(const Context& context) { return context.cancel && context.cancel->load(); }
void Sleep(const Context& context, std::chrono::milliseconds time) {
    const auto end = std::chrono::steady_clock::now() + time;
    while (!Cancelled(context) && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
std::string RequiredTool(const Context& context, const std::string& name) {
    const auto path = ToolPath(context, name);
    if (path.empty()) throw MissingTool(name, "Configure the " + name + " tool for capture.");
    return path;
}
// Device selection for commands that start a session: --serial, else the only authorized device.
AdbConnection SelectDevice(const Context& context) {
    const auto adb = RequiredTool(context, "adb");
    const auto devices = QueryAndroidDevices(adb, context.cancel);
    for (const auto& device : devices) if ((context.serial.empty() || device.serial == context.serial) && device.state == "unauthorized")
        throw CliError(kHumanAction, "authorize_device", "Authorize USB debugging on " + device.serial, {{"human_action", "authorize_device"}, {"serial", device.serial}});
    try { return {adb, SelectReplayTarget(ReplayMode::Android, true, context.serial, devices).serial, context.cancel}; }
    catch (const std::exception& error) { throw CliError(kFailed, "no_device", error.what()); }
}
// Device for an existing session directory: the serial recorded when it was armed.
AdbConnection SessionDevice(const Context& context, const std::string& serial) {
    if (!context.serial.empty() && context.serial != serial)
        throw Usage("Session belongs to device " + serial + " but --serial " + context.serial + " was given");
    return {RequiredTool(context, "adb"), serial, context.cancel};
}
std::filesystem::path SessionArgument(const Args& args) {
    const auto directory = std::filesystem::absolute(std::filesystem::u8path(args.Get("session")));
    if (!std::filesystem::is_directory(directory)) throw CliError(kFailed, "session_not_found", "Capture session directory does not exist: " + directory.u8string(), {{"session", directory.u8string()}});
    return directory;
}
uint32_t FrameCount(const Args& args, uint32_t fallback) {
    const auto frames = args.Number("frames", fallback);
    if (frames < 1 || frames > 1000000) throw Usage("--frames must be 1 to 1,000,000");
    return static_cast<uint32_t>(frames);
}
[[noreturn]] void RethrowConflict(const AndroidRestoreConflict& conflict, const std::string& serial) {
    throw CliError(kHumanAction, "restore_conflict", conflict.what(),
        {{"human_action", "resolve_restore_conflict"}, {"serial", serial}, {"journal", conflict.journal.u8string()}, {"conflicts", conflict.conflicts},
         {"hint", "Settings were changed outside the capture session. Review them, then run 'capture restore --session <dir>' again."}});
}
// Converts and indexes a retrieved capture through the shared cache; the file is always kept.
Json Verify(const Context& context, const std::filesystem::path& file, uint32_t armedFrames = 0) {
    Json entry{{"path", file.u8string()}, {"bytes", std::filesystem::file_size(file)}, {"sha256", Sha256File(file, context.cancel)}};
    if (armedFrames) entry["frames_per_trigger"] = armedFrames;
    try {
        const auto imported = OpenCapture(context, file);
        entry["verified"] = true; entry["frames"] = imported.capture->frames; entry["draw_dispatch_executions"] = imported.capture->executions.size();
        entry["events"] = imported.capture->events.size();
    } catch (const CliError& error) {
        if (error.exit == kMissingTool) throw;
        entry["verified"] = false; entry["error"] = error.what();
    } catch (const std::exception& error) {
        if (Cancelled(context)) throw;
        entry["verified"] = false; entry["error"] = error.what();
    }
    return entry;
}
Json VerifyAll(const Context& context, const std::vector<std::filesystem::path>& files, const Json& extra, uint32_t armedFrames = 0) {
    Json captures = Json::array(); std::string failure;
    for (const auto& file : files) {
        auto entry = Verify(context, file, armedFrames);
        if (!entry.value("verified", false) && failure.empty()) failure = entry.value("error", std::string("verification failed"));
        captures.push_back(std::move(entry));
    }
    Json result = extra; result["captures"] = captures;
    if (!failure.empty()) {
        result["captures"] = captures;
        throw CliError(kFailed, "capture_unverified", "Capture was retrieved but could not be converted or indexed (file kept): " + failure, result);
    }
    return result;
}

Json Arm(Context& context, const Args& args) {
    const auto directory = args.Has("out") ? std::filesystem::absolute(std::filesystem::u8path(args.Get("out"))) : context.NewSession("capture");
    AndroidArmOptions options; options.directory = directory; options.package = args.Get("package");
    options.arguments = args.Get("args"); options.frames = FrameCount(args, 3); options.launch = !args.Has("no-launch");
    options.timeout = Timeout(context, Seconds(120));
    const auto device = SelectDevice(context);
    const auto state = ArmAndroidCapture(device, options, [&](const std::string& text) { context.Progress(text); });
    Json result{{"session", directory.u8string()}, {"serial", state.serial}, {"package", state.package}, {"frames_per_trigger", state.frames},
        {"journal", state.Journal().u8string()}, {"launched", state.launched}, {"armed", state.launched},
        {"next", "gfxrecon-cli capture trigger --session \"" + directory.u8string() + "\""}};
    if (!state.launched) result["note"] = "Settings are applied and journaled but nothing was launched; start " + state.package + " now so the layer loads at process start, then trigger. Run 'capture end' to restore settings.";
    return result;
}
Json Trigger(Context& context, const Args& args) {
    const auto directory = SessionArgument(args);
    const auto session = LoadAndroidSession(directory);
    const auto device = SessionDevice(context, session.serial);
    const auto frames = args.Has("frames") ? FrameCount(args, 0) : 0;
    std::vector<AndroidCaptureFile> files;
    files = TriggerAndroidCapture(device, directory, frames, Timeout(context, Seconds(180)), [&](const std::string& text) { context.Progress(text); });
    std::vector<std::filesystem::path> paths; for (const auto& file : files) paths.push_back(file.path);
    return VerifyAll(context, paths, {{"session", directory.u8string()}, {"serial", session.serial}, {"package", session.package}}, session.frames);
}
Json Status(Context& context, const Args& args) {
    const auto directory = SessionArgument(args);
    return AndroidCaptureStatus(SessionDevice(context, LoadAndroidSession(directory).serial), directory);
}
Json End(Context& context, const Args& args) {
    const auto directory = SessionArgument(args);
    const auto session = LoadAndroidSession(directory);
    try { return EndAndroidCapture(SessionDevice(context, session.serial), directory, args.Has("close-game")); }
    catch (const AndroidRestoreConflict& conflict) { RethrowConflict(conflict, session.serial); }
}
Json Restore(Context& context, const Args& args) {
    auto path = std::filesystem::absolute(std::filesystem::u8path(args.Get("session")));
    if (std::filesystem::is_directory(path)) path /= "device-journal.json";
    if (!std::filesystem::is_regular_file(path)) throw CliError(kFailed, "session_not_found", "No device-journal.json at " + path.u8string(), {{"session", args.Get("session")}});
    Json journal; { std::ifstream in(path); journal = Json::parse(in); }
    const auto serial = journal.value("serial", std::string{});
    if (serial.empty()) throw std::runtime_error("Journal has no device serial: " + path.u8string());
    try { RestoreAndroidJournal(SessionDevice(context, serial), path); }
    catch (const AndroidRestoreConflict& conflict) { RethrowConflict(conflict, serial); }
    return {{"journal", path.u8string()}, {"serial", serial}, {"restored", true}};
}
Json AndroidScheduled(Context& context, const Args& args) {
    const auto device = SelectDevice(context);
    RequireAndroidUnlocked(device); // exit 3 instead of a generic failure
    const auto directory = args.Has("out") ? std::filesystem::absolute(std::filesystem::u8path(args.Get("out"))) : context.NewSession("capture");
    CaptureRequest request; request.output = directory; request.package = args.Get("package"); request.arguments = args.Get("args");
    request.frames = args.Get("frames"); request.timeout = Timeout(context, Seconds(90));
    request.progress = [&](const std::string& text) { context.Progress(text); };
    CaptureResult captured;
    try { captured = CaptureAndroid(device, request); }
    catch (const AndroidRestoreConflict& conflict) { RethrowConflict(conflict, device.serial); }
    catch (const std::runtime_error& error) { // CaptureAndroid appends restore failures to its message
        const std::string text = error.what();
        if (text.find("Recovery pending: Device settings changed outside") != std::string::npos)
            throw CliError(kHumanAction, "restore_conflict", text, {{"human_action", "resolve_restore_conflict"}, {"serial", device.serial}, {"journal", (directory / "device-journal.json").u8string()}});
        throw;
    }
    return VerifyAll(context, captured.files, {{"session", directory.u8string()}, {"serial", device.serial}, {"package", request.package},
        {"frame_range", request.frames}, {"journal", captured.journal.u8string()}, {"settings_restored", true}});
}
ProcessRequest WindowsProcess(const Args& args) {
    ProcessRequest process; process.executable = args.Get("exe"); process.arguments = SplitProcessArguments(args.Get("args"));
    process.workingDirectory = args.Has("cwd") ? args.Get("cwd") : std::filesystem::absolute(std::filesystem::u8path(process.executable)).parent_path().u8string();
    return process;
}
Json Windows(Context& context, const Args& args) {
    if (!args.positional.empty() && args.positional.front() != "run") throw Usage("Unknown capture windows mode '" + args.positional.front() + "'; use 'capture windows run' or omit it for a scheduled frame range");
    const bool onDemand = !args.positional.empty() || args.Has("trigger-after") || args.Has("trigger-file");
    const auto layer = RequiredTool(context, "layer");
    const auto directory = args.Has("out") ? std::filesystem::absolute(std::filesystem::u8path(args.Get("out"))) : context.NewSession("capture");
    auto process = WindowsProcess(args);
    if (!std::filesystem::is_regular_file(std::filesystem::u8path(process.executable))) throw CliError(kFailed, "exe_not_found", "Executable does not exist: " + process.executable);
    if (!onDemand) {
        if (!args.Has("frames")) throw Usage("Scheduled capture requires --frames <range>, e.g. 100-102; or use 'capture windows run' with --trigger-after/--trigger-file");
        context.Progress("Capturing frames " + args.Get("frames") + " of " + process.executable);
        const auto captured = CaptureWindows(process, layer, args.Get("frames"), directory, context.cancel);
        return VerifyAll(context, captured.files, {{"session", directory.u8string()}, {"mode", "scheduled"}, {"frame_range", args.Get("frames")}});
    }
    if (args.Has("trigger-after") == args.Has("trigger-file")) throw Usage("'capture windows run' requires exactly one of --trigger-after SEC or --trigger-file PATH");
    const auto frames = FrameCount(args, 3);
    const auto delay = args.Number("trigger-after", 0);
    const auto triggerFile = std::filesystem::u8path(args.Get("trigger-file"));
    LiveCaptureOptions options; options.android = false; options.frames = frames; options.layerDirectory = layer;
    options.process = process; options.capture.output = directory; options.capture.progress = [&](const std::string& text) { context.Progress(text); };
    LiveCaptureSession session(options);
    auto wait = [&](Seconds limit, const char* what, auto done) {
        const auto end = std::chrono::steady_clock::now() + limit;
        for (;;) {
            const auto state = session.State();
            if (state.phase == LivePhase::Failed) throw std::runtime_error(state.detail);
            if (done(state)) return state;
            if (Cancelled(context)) throw std::runtime_error("Capture cancelled.");
            if (std::chrono::steady_clock::now() > end) throw std::runtime_error(std::string("Timed out waiting for ") + what + ": " + state.detail);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };
    auto close = [&] {
        try { session.End(true); wait(Seconds(30), "the game to close", [](const LiveCaptureState& s) { return s.phase == LivePhase::Ended && !s.gameRunning; }); } catch (...) {}
    };
    try {
        wait(Timeout(context, Seconds(180)), "the capture layer to arm", [](const LiveCaptureState& s) { return s.phase == LivePhase::Armed; });
        if (args.Has("trigger-after")) { context.Progress("Armed; capturing in " + std::to_string(delay) + " s"); Sleep(context, std::chrono::seconds(delay)); }
        else {
            context.Progress("Armed; waiting for trigger file " + triggerFile.u8string());
            const auto end = std::chrono::steady_clock::now() + Timeout(context, Seconds(600));
            while (!std::filesystem::exists(triggerFile)) {
                if (Cancelled(context)) throw std::runtime_error("Capture cancelled.");
                if (std::chrono::steady_clock::now() > end) throw std::runtime_error("Timed out waiting for trigger file " + triggerFile.u8string());
                const auto state = session.State();
                if (state.phase == LivePhase::Failed || state.phase == LivePhase::Ended || !state.gameRunning) throw std::runtime_error("Game exited before the trigger file appeared. " + state.detail);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        if (Cancelled(context)) throw std::runtime_error("Capture cancelled.");
        session.Capture();
        wait(Timeout(context, Seconds(300)), "the capture to finish", [](const LiveCaptureState& s) { return s.phase == LivePhase::Armed && !s.captures.empty(); });
    } catch (...) { close(); throw; }
    const auto captures = session.State().captures;
    close();
    return VerifyAll(context, captures, {{"session", directory.u8string()}, {"mode", "on_demand"}, {"frames_per_trigger", frames}, {"game_closed", true}});
}
} // namespace
void RegisterCaptureCommands() {
    const Option session{"session", "DIR", "Capture session directory printed by 'capture arm'", false, true};
    const Option out{"out", "DIR", "Output/session directory (default: a new directory under <settings>/sessions)"};
    Register({"capture", "arm", "Android: apply journaled capture settings and launch the app with capture tracking armed (nothing is recorded yet)", {},
        {{"package", "PKG", "Debuggable Android package", false, true}, {"frames", "N", "Frames recorded per trigger, fixed at launch", false, false, "3"},
         {"no-launch", "", "Only apply settings; the next launch of the app is captured"}, {"args", "TEXT", "Launch arguments passed to the activity"}, out}, Arm});
    Register({"capture", "trigger", "Android: record the armed frame count in the running game, retrieve it, hash-verify and index it (repeatable; exit 3 when the phone is locked)", {},
        {session, {"frames", "N", "Must equal the armed frame count (fixed at arm time)"}}, Trigger});
    Register({"capture", "status", "Android: report session state, device lock, game and layer status and retrieved captures", {}, {session}, Status});
    Register({"capture", "end", "Android: restore every journaled setting and mark the session ended (exit 3 on restore conflicts)", {},
        {session, {"close-game", "", "Force-stop the game before restoring"}}, End});
    Register({"capture", "restore", "Android: restore settings from a session directory or device-journal.json (also for sessions interrupted before 'end', and GUI sessions)", {},
        {session}, Restore});
    Register({"capture", "android", "Android: scheduled frame-range capture (launch, record, retrieve, restore settings, verify)", {},
        {{"package", "PKG", "Debuggable Android package", false, true}, {"frames", "RANGE", "Frame range such as 100-102 or 1-3,10", false, true}, {"args", "TEXT", "Launch arguments"}, out}, AndroidScheduled});
    Register({"capture", "windows", "Windows: scheduled frame range (--frames RANGE), or 'run' for a blocking on-demand capture (--trigger-after SEC | --trigger-file PATH)", {"[run]"},
        {{"exe", "FILE", "Game executable", false, true}, {"args", "TEXT", "Game arguments"}, {"cwd", "DIR", "Working directory (default: the executable's directory)"},
         {"frames", "N|RANGE", "Scheduled: frame range such as 100-102. On-demand: frames to record (default 3)"},
         {"trigger-after", "SEC", "On-demand: seconds after the layer arms"}, {"trigger-file", "PATH", "On-demand: capture when this file appears"}, out}, Windows});
}
} // namespace gfx::cli
