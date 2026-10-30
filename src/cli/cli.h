#pragma once
// gfxrecon-cli command framework: table-driven parsing, JSON envelope and exit codes.
#include "analysis/capture.h"
#include "analysis/import.h"
#include "replay/replay.h"
#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace gfx::cli {
enum Exit { kOk = 0, kFailed = 1, kUsage = 2, kHumanAction = 3, kMissingTool = 4, kCancelled = 5 };
constexpr const char* kDumpSchema = "gfxrecon-dump/1";
constexpr const char* kCliSchema = "gfxrecon-cli/1";

// Any error with a stable machine code; `detail` goes to error.detail.
struct CliError : std::runtime_error {
    int exit; std::string code; Json detail;
    CliError(int exitCode, std::string errorCode, const std::string& message, Json extra = {})
        : std::runtime_error(message), exit(exitCode), code(std::move(errorCode)), detail(std::move(extra)) {}
};
inline CliError Usage(const std::string& message) { return {kUsage, "usage", message}; }
inline CliError MissingTool(const std::string& tool, const std::string& message) { return {kMissingTool, "missing_tool", message, {{"tool", tool}}}; }

struct Option {
    std::string name;        // without leading dashes
    std::string value;       // value placeholder, empty for a flag
    std::string help;
    bool repeatable = false;
    bool required = false;
    std::string defaultValue;
    std::string implicitValue; // non-empty: the value may be omitted ("--dynamic" alone means "--dynamic <implicitValue>")
};
struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> values; // flags store "true"
    bool Has(const std::string& name) const { return values.count(name) != 0; }
    std::string Get(const std::string& name, const std::string& fallback = {}) const {
        const auto found = values.find(name); return found == values.end() || found->second.empty() ? fallback : found->second.back();
    }
    const std::vector<std::string>& All(const std::string& name) const {
        static const std::vector<std::string> none; const auto found = values.find(name); return found == values.end() ? none : found->second;
    }
    uint64_t Number(const std::string& name, uint64_t fallback) const;
};
struct Context;
struct Command {
    std::string group, verb;          // verb empty for single-word commands ("info")
    std::string summary;
    std::vector<std::string> positional; // names of positional arguments, "NAME..." for variadic, "[NAME]" optional
    std::vector<Option> options;
    std::function<Json(Context&, const Args&)> run;
    std::string Name() const { return verb.empty() ? group : group + " " + verb; }
};
// Shared per-invocation state built from global options.
struct Context {
    std::filesystem::path settingsDir, exeDir;
    std::map<std::string, std::string> tools;
    std::string serial, target = "auto";
    std::chrono::seconds timeout{0};    // 0: command default
    std::chrono::seconds waitDevice{0};
    bool quiet = false;
    std::atomic_bool* cancel = nullptr;
    void Progress(const std::string& text) const;
    std::filesystem::path SessionsDir() const { return settingsDir / "sessions"; }
    std::filesystem::path CaptureCache() const { return SessionsDir() / "cache"; }
    std::filesystem::path ReplayCache() const { return settingsDir / "replay-cache"; }
    std::filesystem::path NewSession(const std::string& label) const;
};

// Shared helpers for command groups.
// Imports/indexes through the capture cache shared with the GUI (<settings>/sessions/cache).
ImportResult OpenCapture(const Context& context, const std::filesystem::path& capture);
// Applies --target/--serial; Android targets for Android captures under auto. Throws CliError for missing tools.
ReplayTarget ResolveTarget(const Context& context, const Capture& capture);
// Execution index for a submitted draw/dispatch block (first occurrence unless `occurrence` given). Throws CliError.
size_t FindExecution(const Capture& capture, uint64_t block, size_t occurrence = 0);
std::string ToolPath(const Context& context, const std::string& name); // "" when not configured

// Registry. Command groups call Register from their Register*Commands function.
std::vector<Command>& Commands();
void Register(Command command);
const std::vector<Option>& GlobalOptions();

struct Parsed { const Command* command = nullptr; Args args; Args global; };
Parsed Parse(const std::vector<std::string>& argv); // throws CliError(kUsage)
Json Describe();
// Commands write one JSON document; help writes text. Returns the exit code.
int Main(const std::vector<std::string>& argv, std::ostream& out, std::atomic_bool& cancel);
// Maps any exception to {exit, error json}.
std::pair<int, Json> ErrorJson(const std::exception_ptr& error);

void RegisterCoreCommands();    // describe, doctor
void RegisterQueryCommands();   // info, frames, events, draw, pipeline, object
void RegisterDumpCommands();    // dump
void RegisterReplayCommands();  // replay frame/draw, compare, shader *, perf
void RegisterCaptureCommands(); // capture *
inline void RegisterAll() {
    if (!Commands().empty()) return;
    RegisterCoreCommands(); RegisterQueryCommands(); RegisterDumpCommands(); RegisterReplayCommands(); RegisterCaptureCommands();
}
} // namespace gfx::cli
