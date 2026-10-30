#include "cli/cli.h"
#include "platform/adb.h"
#include "platform/device_lock.h"
#include "core/tools.h"
#include "core/settings.h"
#include "core/log.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace gfx::cli {
uint64_t Args::Number(const std::string& name, uint64_t fallback) const {
    const auto text = Get(name);
    if (text.empty()) return fallback;
    try { size_t used = 0; const auto value = std::stoull(text, &used, 0); if (used == text.size()) return value; } catch (...) {}
    throw Usage("--" + name + " expects a non-negative integer, got '" + text + "'");
}
void Context::Progress(const std::string& text) const { if (!quiet) std::cerr << "[gfxrecon-cli] " << text << std::endl; }
std::filesystem::path Context::NewSession(const std::string& label) const {
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto path = SessionsDir() / ("cli-" + label + "-" + std::to_string(stamp + attempt));
        if (std::filesystem::create_directories(path)) return path;
    }
    throw std::runtime_error("Could not create a unique session directory");
}
std::string ToolPath(const Context& context, const std::string& name) {
    const auto found = context.tools.find(name); return found == context.tools.end() ? "" : found->second;
}
ImportResult OpenCapture(const Context& context, const std::filesystem::path& capture) {
    if (!std::filesystem::is_regular_file(capture)) throw CliError(kFailed, "capture_not_found", "Capture file does not exist: " + capture.u8string(), {{"capture", capture.u8string()}});
    if (capture.extension() == ".gfxr" && ToolPath(context, "convert").empty()) throw MissingTool("convert", "Configure gfxrecon-convert to open .gfxr captures.");
    context.Progress("Opening " + capture.u8string());
    auto result = ImportCapture(std::filesystem::absolute(capture), context.CaptureCache(), context.tools, context.cancel);
    context.Progress(result.message);
    return result;
}
ReplayTarget ResolveTarget(const Context& context, const Capture& capture) {
    const auto mode = context.target == "local" ? ReplayMode::Local : context.target == "android" ? ReplayMode::Android : ReplayMode::Automatic;
    if (mode == ReplayMode::Local || (mode == ReplayMode::Automatic && !capture.android)) {
        if (ToolPath(context, "replay").empty()) throw MissingTool("replay", "Configure the local gfxrecon-replay executable or choose an Android target.");
        return {};
    }
    if (ToolPath(context, "adb").empty()) throw MissingTool("adb", "Configure ADB to replay on an Android device.");
    const auto devices = QueryAndroidDevices(ToolPath(context, "adb"), context.cancel);
    for (const auto& device : devices) if ((context.serial.empty() || device.serial == context.serial) && device.state == "unauthorized")
        throw CliError(kHumanAction, "authorize_device", "Authorize USB debugging on " + device.serial, {{"human_action", "authorize_device"}, {"serial", device.serial}});
    try { return SelectReplayTarget(mode, capture.android, context.serial, devices); }
    catch (const std::exception& error) { throw CliError(kFailed, "no_device", error.what()); }
}
size_t FindExecution(const Capture& capture, uint64_t block, size_t occurrence) {
    size_t seen = 0;
    for (size_t i = 0; i < capture.executions.size(); ++i)
        if (capture.events[capture.executions[i].event].block == block && seen++ == occurrence) return i;
    throw CliError(kFailed, "draw_not_found", "No submitted draw/dispatch execution at block " + std::to_string(block) +
        (occurrence ? " (occurrence " + std::to_string(occurrence) + ")" : ""), {{"block", block}});
}
std::vector<Command>& Commands() { static std::vector<Command> commands; return commands; }
void Register(Command command) { Commands().push_back(std::move(command)); }
const std::vector<Option>& GlobalOptions() {
    static const std::vector<Option> options{
        {"settings-dir", "DIR", "Settings, sessions and caches root (default: executable directory, shared with gfxrecon-gui)"},
        {"tools", "FILE", "Extra tool configuration JSON merged after gfxrecon_tools.json"},
        {"serial", "SERIAL", "Android device serial"},
        {"target", "auto|local|android", "Replay target", false, false, "auto"},
        {"timeout", "SEC", "Operation timeout in seconds"},
        {"wait-device", "SEC", "Wait up to SEC for a device held by another GUI/CLI operation", false, false, "0"},
        {"quiet", "", "Suppress progress on stderr"},
    };
    return options;
}
namespace {
const Option* FindOption(const std::vector<Option>& options, const std::string& name) {
    for (const auto& option : options) if (option.name == name) return &option;
    return nullptr;
}
} // namespace
Parsed Parse(const std::vector<std::string>& argv) {
    Parsed parsed;
    std::vector<std::string> words;
    // Global options may appear anywhere; split them out first.
    for (size_t i = 0; i < argv.size(); ++i) {
        const auto& word = argv[i];
        if (word.rfind("--", 0) == 0) {
            auto name = word.substr(2), value = std::string{};
            const auto equals = name.find('='); const bool inlineValue = equals != std::string::npos;
            if (inlineValue) { value = name.substr(equals + 1); name = name.substr(0, equals); }
            if (const auto* option = FindOption(GlobalOptions(), name)) {
                if (!option->value.empty() && !inlineValue) {
                    if (i + 1 >= argv.size()) throw Usage("--" + name + " requires " + option->value);
                    value = argv[++i];
                }
                parsed.global.values[name].push_back(option->value.empty() ? "true" : value);
                continue;
            }
        }
        words.push_back(word);
    }
    if (words.empty()) throw Usage("No command given. Run 'gfxrecon-cli describe' for the command list.");
    for (const auto& command : Commands()) {
        if (command.group != words[0]) continue;
        if (!command.verb.empty() && (words.size() < 2 || command.verb != words[1])) continue;
        parsed.command = &command; break;
    }
    if (!parsed.command) {
        std::string verbs;
        for (const auto& command : Commands()) if (command.group == words[0] && !command.verb.empty()) verbs += (verbs.empty() ? "" : ", ") + command.verb;
        throw Usage(verbs.empty() ? "Unknown command '" + words[0] + "'" : "Unknown or missing verb for '" + words[0] + "'; expected one of: " + verbs);
    }
    const auto& command = *parsed.command;
    for (size_t i = command.verb.empty() ? 1 : 2; i < words.size(); ++i) {
        const auto& word = words[i];
        if (word.rfind("--", 0) != 0 || word == "--") { parsed.args.positional.push_back(word); continue; }
        auto name = word.substr(2), value = std::string{};
        const auto equals = name.find('='); const bool inlineValue = equals != std::string::npos;
        if (inlineValue) { value = name.substr(equals + 1); name = name.substr(0, equals); }
        const auto* option = FindOption(command.options, name);
        if (!option) throw Usage("Unknown option --" + name + " for '" + command.Name() + "'");
        if (option->value.empty()) {
            if (inlineValue) throw Usage("--" + name + " is a flag and takes no value");
            value = "true";
        } else if (!inlineValue) {
            const bool omitted = i + 1 >= words.size() || words[i + 1].rfind("--", 0) == 0;
            if (omitted && option->implicitValue.empty()) throw Usage("--" + name + " requires " + option->value);
            value = omitted ? option->implicitValue : words[++i];
        }
        auto& slot = parsed.args.values[name];
        if (!slot.empty() && !option->repeatable) throw Usage("--" + name + " may be given only once");
        slot.push_back(value);
    }
    for (const auto& option : command.options) {
        if (option.required && !parsed.args.Has(option.name)) throw Usage("'" + command.Name() + "' requires --" + option.name + " " + option.value);
        if (!option.defaultValue.empty() && !parsed.args.Has(option.name)) parsed.args.values[option.name] = {option.defaultValue};
    }
    size_t required = 0; bool variadic = false;
    for (const auto& name : command.positional) {
        if (name.size() > 3 && name.compare(name.size() - 3, 3, "...") == 0) variadic = true;
        else if (name.empty() || name.front() != '[') ++required;
    }
    if (parsed.args.positional.size() < required) throw Usage("'" + command.Name() + "' expects " + std::to_string(required) + " argument(s)");
    if (!variadic && parsed.args.positional.size() > command.positional.size())
        throw Usage("Unexpected argument '" + parsed.args.positional[command.positional.size()] + "' for '" + command.Name() + "'");
    return parsed;
}
Json Describe() {
    auto options = [](const std::vector<Option>& list) {
        Json result = Json::array();
        for (const auto& o : list) {
            Json item{{"name", "--" + o.name}, {"help", o.help}, {"flag", o.value.empty()}};
            if (!o.value.empty()) item["value"] = o.value;
            if (o.repeatable) item["repeatable"] = true;
            if (o.required) item["required"] = true;
            if (!o.defaultValue.empty()) item["default"] = o.defaultValue;
            if (!o.implicitValue.empty()) item["implicit_value"] = o.implicitValue;
            result.push_back(item);
        }
        return result;
    };
    Json commands = Json::array();
    for (const auto& c : Commands())
        commands.push_back({{"name", c.Name()}, {"summary", c.summary}, {"arguments", c.positional}, {"options", options(c.options)}});
    return {{"schema", kCliSchema}, {"dump_schema", kDumpSchema}, {"usage", "gfxrecon-cli <command> [<verb>] [arguments] [--options]"},
        {"output", "Commands: stdout is one JSON document {ok, command, result | error{code,message,detail}}; stderr: progress and logs. Help is plain text."},
        {"help", "gfxrecon-cli help [command [verb]], --help or -h"},
        {"global_options", options(GlobalOptions())}, {"commands", commands},
        {"exit_codes", {{"0", "success"}, {"1", "operation failed"}, {"2", "usage error"}, {"3", "human action required (unlock/authorize device, restore conflict)"},
                        {"4", "missing tool or capability"}, {"5", "timeout or cancelled"}}}};
}
namespace {
void HelpOptions(std::ostream& out,const std::vector<Option>& options){
    for(const auto& option:options){
        const auto label="--"+option.name+(option.value.empty()?"":" "+option.value);
        out<<"  "<<std::left<<std::setw(30)<<label<<"  "<<option.help;
        if(option.required)out<<" (required)";
        if(!option.defaultValue.empty())out<<" [default: "<<option.defaultValue<<"]";
        if(option.repeatable)out<<" [repeatable]";
        out<<'\n';
    }
}
void Help(const std::vector<std::string>& argv,std::ostream& out){
    std::vector<std::string> topic;
    for(size_t i=0;i<argv.size();++i){
        const auto& word=argv[i];
        if(word=="help"||word=="--help"||word=="-h")continue;
        if(word.rfind("--",0)==0){
            const auto key=word.substr(2,word.find('=')==std::string::npos?std::string::npos:word.find('=')-2);
            const Option* option=FindOption(GlobalOptions(),key);
            if(!option)for(const auto& command:Commands())if((option=FindOption(command.options,key)))break;
            if(option&&!option->value.empty()&&word.find('=')==std::string::npos&&i+1<argv.size()&&argv[i+1].rfind("--",0)!=0)++i;
            continue;
        }
        topic.push_back(word);
    }
    out<<"GFXReconstruct Inspector CLI\n\n";
    if(topic.empty()){
        out<<"Usage: gfxrecon-cli <command> [<verb>] [arguments] [options]\n\nCommands:\n";
        for(const auto& command:Commands())out<<"  "<<std::left<<std::setw(24)<<command.Name()<<command.summary<<'\n';
    }else{
        const Command* selected=nullptr;bool group=false;
        for(const auto& command:Commands())if(command.group==topic[0]){
            group=true;
            if(command.verb.empty()||(topic.size()>1&&command.verb==topic[1]))selected=&command;
        }
        if(!group)throw Usage("Unknown help topic '"+topic[0]+"'");
        if(selected){
            out<<"Usage: gfxrecon-cli "<<selected->Name();
            for(const auto& argument:selected->positional)out<<' '<<argument;
            out<<" [options]\n\n"<<selected->summary<<"\n\nOptions:\n";HelpOptions(out,selected->options);
        }else{
            if(topic.size()>1)throw Usage("Unknown help topic '"+topic[0]+" "+topic[1]+"'");
            out<<"Usage: gfxrecon-cli "<<topic[0]<<" <verb> [options]\n\n";
            for(const auto& command:Commands())if(command.group==topic[0])out<<"  "<<std::left<<std::setw(24)<<command.Name()<<command.summary<<'\n';
        }
    }
    out<<"\nGlobal options:\n";HelpOptions(out,GlobalOptions());
    out<<"\nTry the indexed cube:\n"
       <<"  gfxrecon-cli dump --capture samples/indexed-cube.gfxr --out decoded\n"
       <<"  gfxrecon-cli events --capture samples/indexed-cube.gfxr --draws-only\n"
       <<"  gfxrecon-cli replay draw --capture samples/indexed-cube.gfxr --execution 0 --full\n"
       <<"\nWorkflow: dump static data, narrow passes/draws with replay, then inspect shaders and readbacks.\n"
       <<"Use 'gfxrecon-cli <command> --help' for command details.\n"
       <<"Commands return JSON; 'describe' provides the machine-readable command schema.\n"
       <<"Exit codes: 0 success, 1 failed, 2 usage, 3 device action, 4 missing tool, 5 cancelled/timeout.\n";
}
}
std::pair<int, Json> ErrorJson(const std::exception_ptr& error) {
    try { std::rethrow_exception(error); }
    catch (const CliError& e) { Json j{{"code", e.code}, {"message", e.what()}}; if (!e.detail.is_null()) j["detail"] = e.detail; if(e.exit==kUsage)j["hint"]="Run gfxrecon-cli --help or gfxrecon-cli <command> --help.";return {e.exit, j}; }
    catch (const AndroidUnlockRequired& e) {
        return {kHumanAction, {{"code", "unlock_device"}, {"message", e.what()},
            {"detail", {{"human_action", "unlock_device"}, {"serial", e.state.serial}, {"secure", e.state.secure}, {"locked", e.state.locked}}}}};
    }
    catch (const DeviceBusy& e) { return {kFailed, {{"code", "device_busy"}, {"message", e.what()}, {"detail", {{"serial", e.serial}}}}}; }
    catch (const Json::exception& e) { return {kFailed, {{"code", "invalid_json"}, {"message", e.what()}}}; }
    catch (const std::exception& e) {
        const std::string text = e.what();
        auto has = [&](const char* needle) { return text.find(needle) != std::string::npos; };
        if (has("cancelled") || has("Cancelled")) return {kCancelled, {{"code", "cancelled"}, {"message", text}}};
        if (has("timed out") || has("timeout")) return {kCancelled, {{"code", "timeout"}, {"message", text}}};
        if (has("unauthorized")) return {kHumanAction, {{"code", "authorize_device"}, {"message", text}, {"detail", {{"human_action", "authorize_device"}}}}};
        if (has("Configure ") || has("not configured") || has("validation layer") || has("VK_LAYER_KHRONOS_validation was not enabled"))
            return {kMissingTool, {{"code", "missing_tool"}, {"message", text}}};
        return {kFailed, {{"code", "failed"}, {"message", text}}};
    }
    catch (...) { return {kFailed, {{"code", "failed"}, {"message", "Unknown error"}}}; }
}
int Main(const std::vector<std::string>& argv, std::ostream& out, std::atomic_bool& cancel) {
    RegisterAll();
    std::string name;
    auto emit = [&](bool ok, const Json& body) {
        Json document{{"ok", ok}, {"command", name}};
        document[ok ? "result" : "error"] = body;
        out << document.dump(2) << std::endl;
    };
    try {
        if(argv.empty()||argv.front()=="help"||std::find(argv.begin(),argv.end(),"--help")!=argv.end()||std::find(argv.begin(),argv.end(),"-h")!=argv.end()){
            name="help";std::ostringstream help;Help(argv,help);out<<help.str();return kOk;
        }
        const auto parsed = Parse(argv);
        name = parsed.command->Name();
        Context context;
        context.exeDir = std::filesystem::u8path(tpl::ExeDirectory());
        context.settingsDir = parsed.global.Has("settings-dir") ? std::filesystem::absolute(std::filesystem::u8path(parsed.global.Get("settings-dir"))) : context.exeDir;
        context.quiet = parsed.global.Has("quiet");
        context.serial = parsed.global.Get("serial");
        context.target = parsed.global.Get("target", "auto");
        if (context.target != "auto" && context.target != "local" && context.target != "android") throw Usage("--target must be auto, local or android");
        context.timeout = std::chrono::seconds(parsed.global.Number("timeout", 0));
        context.waitDevice = std::chrono::seconds(parsed.global.Number("wait-device", 0));
        context.cancel = &cancel;
        context.tools = ResolveTools(context.settingsDir, context.exeDir);
        if (parsed.global.Has("tools")) {
            std::ifstream input(std::filesystem::u8path(parsed.global.Get("tools")), std::ios::binary);
            if (!input) throw Usage("Cannot read --tools file " + parsed.global.Get("tools"));
            const auto extra = Json::parse(input);   // keep alive: items() of a temporary dangles in a range-for
            for (const auto& [key, value] : extra.items()) if (value.is_string()) context.tools[key] = value.get<std::string>();
        }
        ConfigureDeviceLocks(context.settingsDir / "locks", context.waitDevice);
        auto result = parsed.command->run(context, parsed.args);
        if (cancel.load()) throw CliError(kCancelled, "cancelled", "Operation cancelled");
        emit(true, result);
        return kOk;
    } catch (...) {
        auto [code, error] = ErrorJson(std::current_exception());
        if (cancel.load() && code != kUsage) { code = kCancelled; error["code"] = "cancelled"; }
        emit(false, error);
        return code;
    }
}
} // namespace gfx::cli
