#include "platform/process.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <future>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

namespace gfx {
namespace {
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 process argument");
    std::wstring w(n, '\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string Utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
std::wstring Quote(const std::wstring& s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}
[[noreturn]] void Fail(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed (Windows error " + std::to_string(GetLastError()) + ")");
}
struct EnvLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};
std::vector<wchar_t> Environment(const std::map<std::string, std::string>& overrides) {
    std::map<std::wstring, std::wstring, EnvLess> values;
    auto block = GetEnvironmentStringsW();
    if (!block) Fail("GetEnvironmentStringsW");
    for (auto p = block; *p; p += wcslen(p) + 1) {
        std::wstring entry(p);
        const auto eq = entry.find(L'=', entry[0] == '=' ? 1 : 0);
        if (eq != std::wstring::npos) values[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    FreeEnvironmentStringsW(block);
    for (const auto& [key, value] : overrides) {
        if (key.empty() || key.find_first_of("=\0", 0, 2) != std::string::npos || value.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid process environment override");
        values[Wide(key)] = Wide(value);
    }
    std::vector<wchar_t> out;
    for (const auto& [key, value] : values) {
        const auto entry = key + L"=" + value;
        out.insert(out.end(), entry.begin(), entry.end());
        out.push_back(0);
    }
    out.push_back(0);
    return out;
}
} // namespace

std::string FindExecutablePath(const std::string& name) {
    const auto w = Wide(name);
    std::vector<wchar_t> path(32768);
    const DWORD n = SearchPathW(nullptr, w.c_str(), L".exe", static_cast<DWORD>(path.size()), path.data(), nullptr);
    return n && n < path.size() ? Utf8(std::wstring(path.data(), n)) : std::string{};
}
std::vector<std::string> NativeArguments() {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) Fail("CommandLineToArgvW");
    std::vector<std::string> result;
    for (int i = 0; i < count; ++i) result.push_back(Utf8(args[i]));
    LocalFree(args);
    return result;
}
std::vector<std::string> SplitProcessArguments(const std::string& arguments) {
    if (arguments.find('\0') != std::string::npos) throw std::runtime_error("NUL in process arguments");
    const auto command = L"program " + Wide(arguments);
    int count = 0; auto args = CommandLineToArgvW(command.c_str(), &count);
    if (!args) Fail("Parse process arguments");
    std::vector<std::string> result;
    for (int i = 1; i < count; ++i) result.push_back(Utf8(args[i]));
    LocalFree(args); return result;
}

struct GameProcess::Impl { Handle process, job, captureEvent; };
GameProcess::GameProcess(const ProcessRequest& r) : impl_(std::make_unique<Impl>()) {
    std::wstring command=Quote(Wide(r.executable));for(const auto& a:r.arguments)command+=L" "+Quote(Wide(a));
    const auto eventName="Local\\GfxReconGuiCapture_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    impl_->captureEvent.value=CreateEventW(nullptr,TRUE,FALSE,Wide(eventName).c_str());if(!impl_->captureEvent.value||GetLastError()==ERROR_ALREADY_EXISTS)Fail("Create private capture event");
    auto values=r.environment;values["GFXRECON_GUI_CONTROL_EVENT"]=eventName;
    auto env=Environment(values);const auto exe=Wide(r.executable),cwd=Wide(r.workingDirectory);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
    Handle output(CreateFileW(r.stdoutFile.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,0,nullptr));
    if(input.value==INVALID_HANDLE_VALUE||output.value==INVALID_HANDLE_VALUE)Fail("Open game log");
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=input.value;startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=output.value;
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> attributes(bytes);
    startup.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes))Fail("Initialize game handles");
    struct Guard{PPROC_THREAD_ATTRIBUTE_LIST list;~Guard(){DeleteProcThreadAttributeList(list);}} guard{startup.lpAttributeList};
    HANDLE handles[]={input.value,output.value};if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr))Fail("Set game handles");
    impl_->job.value=CreateJobObjectW(nullptr,nullptr);if(!impl_->job.value)Fail("Create game job");
    PROCESS_INFORMATION info{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,
        env.data(),cwd.empty()?nullptr:cwd.c_str(),&startup.StartupInfo,&info))Fail("Launch game");
    impl_->process.value=info.hProcess;Handle thread(info.hThread);
    if(!AssignProcessToJobObject(impl_->job.value,info.hProcess)){TerminateProcess(info.hProcess,1);Fail("Own game process");}
    if(ResumeThread(info.hThread)==DWORD(-1)){TerminateJobObject(impl_->job.value,1);Fail("Start game");}
}
GameProcess::~GameProcess()=default;
bool GameProcess::Running()const{JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};return QueryInformationJobObject(impl_->job.value,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)&&info.ActiveProcesses>0;}
void GameProcess::Stop(){TerminateJobObject(impl_->job.value,0);}
void GameProcess::SetCaptureEnabled(bool enabled){if(enabled)SetEvent(impl_->captureEvent.value);else ResetEvent(impl_->captureEvent.value);}

ProcessResult RunProcess(const ProcessRequest& r, const std::atomic_bool* cancel) {
    if (r.executable.empty()) throw std::runtime_error("No executable configured");
    if (r.executable.find('\0') != std::string::npos || r.workingDirectory.find('\0') != std::string::npos)
        throw std::runtime_error("NUL in process executable or working directory");
    if (cancel && cancel->load()) { ProcessResult result; result.cancelled = true; return result; }
    std::wstring command = Quote(Wide(r.executable));
    for (const auto& a : r.arguments) {
        if (a.find('\0') != std::string::npos) throw std::runtime_error("NUL in process argument");
        command += L" " + Quote(Wide(a));
    }
    auto env = Environment(r.environment);
    const auto exe = Wide(r.executable);
    const auto cwd = Wide(r.workingDirectory);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle read, write;
    if (!CreatePipe(&read.value, &write.value, &security, 0)) Fail("CreatePipe");
    if (!SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) Fail("SetHandleInformation");
    Handle input(CreateFileW(r.stdinFile.empty() ? L"NUL" : r.stdinFile.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) Fail("Open null input");
    Handle output;
    if (!r.stdoutFile.empty()) {
        output.value = CreateFileW(r.stdoutFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, 0, nullptr);
        if (output.value == INVALID_HANDLE_VALUE) Fail("Open process output file");
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = output.value ? output.value : write.value;
    startup.StartupInfo.hStdError = write.value;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> attrs(bytes);
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) Fail("InitializeProcThreadAttributeList");
    struct AttrGuard { PPROC_THREAD_ATTRIBUTE_LIST p; ~AttrGuard() { DeleteProcThreadAttributeList(p); } } attrGuard{startup.lpAttributeList};
    std::vector<HANDLE> inherited{write.value, input.value};
    if (output.value) inherited.push_back(output.value);
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited.data(), inherited.size() * sizeof(HANDLE), nullptr, nullptr)) Fail("Set inherited handles");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.value) Fail("CreateJobObjectW");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) Fail("Set job limits");
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                         env.data(), cwd.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &info)) Fail("CreateProcessW");
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job.value, process.value)) {
        TerminateProcess(process.value, 1);
        Fail("AssignProcessToJobObject");
    }
    CloseHandle(write.value); write.value = nullptr;
    if (output.value) { CloseHandle(output.value); output.value = nullptr; }
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) Fail("ResumeThread");
    ProcessResult result;
    // Drain the pipe through EOF on a reader thread. A poll-and-exit loop can
    // lose short child output around process termination on Windows.
    auto reader = std::async(std::launch::async, [&] {
        char buffer[8192];
        for (;;) {
            DWORD count = 0;
            const BOOL ok = ReadFile(read.value, buffer, sizeof(buffer), &count, nullptr);
            const auto retain = std::min<size_t>(count, r.outputLimit - result.output.size());
            result.output.append(buffer, retain);
            result.outputTruncated |= retain != count;
            if (!ok || !count) break;
        }
    });
    const auto started = std::chrono::steady_clock::now();
    bool terminated = false;
    while (WaitForSingleObject(process.value, 10) == WAIT_TIMEOUT) {
        if (!terminated) {
            result.cancelled = cancel && cancel->load();
            result.timedOut = r.timeout.count() > 0 && std::chrono::steady_clock::now() - started >= r.timeout;
            if (result.cancelled || result.timedOut) {
                TerminateJobObject(job.value, 1);
                terminated = true;
            }
        }
    }
    // Let final writes/helper shutdown reach EOF before closing this owned job.
    // Bound the grace period so a lingering inherited writer cannot hang us.
    if (reader.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
        TerminateJobObject(job.value, 0);
    reader.get();
    DWORD code = 0;
    GetExitCodeProcess(process.value, &code);
    result.exitCode = static_cast<int>(code);
    return result;
}
} // namespace gfx
#else
namespace gfx {
struct GameProcess::Impl {};
GameProcess::GameProcess(const ProcessRequest&) { throw std::runtime_error("Game launching is currently supported on Windows"); }
GameProcess::~GameProcess()=default;
bool GameProcess::Running() const { return false; }
void GameProcess::Stop() {}
void GameProcess::SetCaptureEnabled(bool) { throw std::runtime_error("Windows capture control is unavailable"); }
std::string FindExecutablePath(const std::string&) { return {}; }
std::vector<std::string> NativeArguments() { return {}; }
std::vector<std::string> SplitProcessArguments(const std::string&) { throw std::runtime_error("Windows argument parsing is unavailable"); }
ProcessResult RunProcess(const ProcessRequest&, const std::atomic_bool*) {
    throw std::runtime_error("Process backend is currently supported on Windows");
}
} // namespace gfx
#endif
