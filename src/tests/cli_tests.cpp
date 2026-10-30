#include "cli/cli.h"
#include "platform/adb.h"
#include "platform/device_lock.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace gfx::tests {
namespace {
void Require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error("CLI check failed: " + message); }
struct Outcome { int exit; Json document; };
Outcome Invoke(const std::vector<std::string>& args) {
    std::ostringstream out; std::atomic_bool cancel{false};
    const auto exit = cli::Main(args, out, cancel);
    return {exit, Json::parse(out.str())}; // exactly one JSON document or parse throws
}
} // namespace
void RunCliTests(const std::filesystem::path& root) {
    using namespace gfx::cli;
    RegisterAll();
    const auto settings = (root / "cli-settings").u8string();
    for(const auto& args:std::vector<std::vector<std::string>>{{},{"--help"},{"-h"},{"help"},{"replay","--help"},{"replay","draw","--help"},{"help","shader","show"},{"dump","--help"}}){
        std::ostringstream help;std::atomic_bool stopped{false};
        Require(Main(args,help,stopped)==kOk,"help exit");Require(help.str().find("Usage:")!=std::string::npos&&help.str().find("indexed-cube.gfxr")!=std::string::npos,"discoverable help and sample workflow");
        if(args.size()>1&&args[0]=="replay"&&args[1]=="draw")Require(help.str().find("--full")!=std::string::npos,"draw-specific help");
    }
    const auto badHelp=Invoke({"help","unknown-command"});Require(badHelp.exit==kUsage&&badHelp.document["error"]["code"]=="usage","unknown help topic remains one JSON error");
    // Parser: globals anywhere, inline values, flags, unknown options and missing values.
    const auto parsed = Parse({"--quiet", "doctor", "--no-versions", "--settings-dir=" + settings});
    Require(parsed.command && parsed.command->Name() == "doctor" && parsed.args.Has("no-versions"), "doctor parse");
    Require(parsed.global.Has("quiet") && parsed.global.Get("settings-dir") == settings, "global options");
    auto usage = [](const std::vector<std::string>& args) {
        try { Parse(args); return false; } catch (const CliError& e) { return e.exit == kUsage && e.code == "usage"; }
    };
    Require(usage({}), "empty command");
    Require(usage({"doctor", "--bogus"}), "unknown option");
    Require(usage({"doctor", "--no-versions=1"}), "flag with value");
    Require(usage({"doctor", "extra"}), "unexpected positional");
    Require(usage({"--settings-dir"}), "missing global value");
    Require(usage({"no-such-command"}), "unknown command");
    Args numbers; numbers.values["n"] = {"12x"};
    bool rejected = false; try { numbers.Number("n", 0); } catch (const CliError& e) { rejected = e.exit == kUsage; }
    Require(rejected && numbers.Number("missing", 7) == 7, "number parsing");
    // describe is generated from the registry: every registered command is listed, nothing else.
    const auto described = Invoke({"describe", "--quiet", "--settings-dir", settings});
    Require(described.exit == kOk && described.document["ok"] == true && described.document["command"] == "describe", "describe envelope");
    const auto& listed = described.document["result"]["commands"];
    Require(listed.size() == Commands().size(), "describe lists every command");
    for (size_t i = 0; i < Commands().size(); ++i) Require(listed[i]["name"] == Commands()[i].Name(), "describe order");
    Require(described.document["result"]["dump_schema"] == kDumpSchema && described.document["result"]["exit_codes"].size() == 6, "describe schema/exit codes");
    // Failure envelope and exit code mapping.
    const auto bad = Invoke({"no-such-command", "--quiet"});
    Require(bad.exit == kUsage && bad.document["ok"] == false && bad.document["error"]["code"] == "usage", "usage envelope");
    const auto [unlockExit, unlock] = ErrorJson(std::make_exception_ptr(AndroidUnlockRequired(ParseAndroidLockState("showing=true\nsecure=true", "phone-x"))));
    Require(unlockExit == kHumanAction && unlock["code"] == "unlock_device" && unlock["detail"]["serial"] == "phone-x", "unlock maps to 3");
    Require(ErrorJson(std::make_exception_ptr(MissingTool("replay", "no replay"))).first == kMissingTool, "missing tool maps to 4");
    Require(ErrorJson(std::make_exception_ptr(std::runtime_error("Replay cancelled."))).first == kCancelled, "cancel maps to 5");
    Require(ErrorJson(std::make_exception_ptr(std::runtime_error("Local replay timed out. Check"))).first == kCancelled, "timeout maps to 5");
    Require(ErrorJson(std::make_exception_ptr(DeviceBusy("s", "busy"))).second["code"] == "device_busy", "device busy code");
    Require(ErrorJson(std::make_exception_ptr(std::runtime_error("boom"))).first == kFailed, "generic failure maps to 1");
    std::ostringstream out; std::atomic_bool cancel{true};
    Require(Main({"describe", "--quiet", "--settings-dir", settings}, out, cancel) == kCancelled, "pre-set cancel exits 5");
    Require(Json::parse(out.str())["error"]["code"] == "cancelled", "cancel envelope");
    // A missing capture is an operation failure with a stable code (once query commands exist).
    if (std::any_of(Commands().begin(), Commands().end(), [](const Command& c) { return c.Name() == "info"; })) {
        const auto missing = Invoke({"info", "--quiet", "--settings-dir", settings, "--capture", (root / "missing.gfxr").u8string()});
        Require(missing.exit == kFailed && missing.document["ok"] == false && missing.document["error"].contains("code"), "missing capture");
    }
    std::cout << "CLI contract checks passed (" << Commands().size() << " commands)\n";
}
} // namespace gfx::tests
