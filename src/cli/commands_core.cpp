#include "cli/cli.h"
#include "core/tools.h"
#include "replay/replay.h"
#include "platform/adb.h"

namespace gfx::cli {
namespace {
Json Doctor(Context& context, const Args& args) {
    Json tools = Json::object();
    std::map<std::string, bool> ready;
    std::map<std::string, std::string> versions;
    if (args.Has("no-versions")) ready = ToolAvailability(context.tools);
    else versions = ProbeToolVersions(context.tools, ready, context.cancel);
    for (const auto& [name, path] : context.tools) {
        Json entry{{"path", path}, {"available", ready[name]}};
        if (versions.count(name)) {
            auto text = versions[name];
            if (const auto newline = text.find_first_of("\r\n"); newline != std::string::npos) text = text.substr(0, newline);
            entry["version"] = text;
        }
        tools[name] = entry;
    }
    Json devices = Json::array();
    std::string deviceError;
    const auto adb = context.tools.count("adb") ? context.tools.at("adb") : "";
    if (!adb.empty()) {
        try {
            for (const auto& device : QueryAndroidDevices(adb, context.cancel)) {
                Json entry{{"serial", device.serial}, {"state", device.state}, {"details", device.details}};
                if (device.state == "device") {
                    const AdbConnection connection{adb, device.serial, context.cancel};
                    const auto installed = connection.TryShell({"pm", "path", "com.lunarg.gfxreconstruct.replay"});
                    entry["replay_apk_installed"] = installed.Success() && installed.output.find("package:") != std::string::npos;
                    const auto policy = connection.TryShell({"dumpsys", "window", "policy"});
                    if (policy.Success()) {
                        const auto lock = ParseAndroidLockState(policy.output, device.serial);
                        entry["locked"] = lock.known ? Json(lock.locked) : Json("unknown");
                    }
                }
                devices.push_back(entry);
            }
        } catch (const std::exception& error) { deviceError = error.what(); }
    } else deviceError = "ADB is not configured";
    Json result{{"settings_dir", context.settingsDir.u8string()}, {"executable_dir", context.exeDir.u8string()},
        {"tools", tools}, {"devices", devices},
        {"validation_layer", {{"path", context.tools.count("validation_layer") ? context.tools.at("validation_layer") : ""},
                              {"available", ready["validation_layer"]}}},
        {"caches", {{"capture_index", context.CaptureCache().u8string()}, {"replay", context.ReplayCache().u8string()}}}};
    if (!deviceError.empty()) result["device_error"] = deviceError;
    return result;
}
} // namespace
void RegisterCoreCommands() {
    Register({"describe", "", "Print every command, option, exit code and the dump schema version", {}, {},
        [](Context&, const Args&) { return Describe(); }});
    Register({"doctor", "", "Report tools with availability/versions, Android devices, replay APK and validation layer", {},
        {{"no-versions", "", "Skip running each tool's version command"}},
        Doctor});
}
} // namespace gfx::cli
