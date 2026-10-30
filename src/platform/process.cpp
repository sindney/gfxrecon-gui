#include "platform/process.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <future>
#include <fstream>

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
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif

namespace gfx {
struct GameProcess::Impl {};
GameProcess::GameProcess(const ProcessRequest&) { throw std::runtime_error("Game launching is currently supported on Windows"); }
GameProcess::~GameProcess()=default;
bool GameProcess::Running() const { return false; }
void GameProcess::Stop() {}
void GameProcess::SetCaptureEnabled(bool) { throw std::runtime_error("Windows capture control is unavailable"); }
std::string FindExecutablePath(const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos) return {};
    const auto executable = [](const std::filesystem::path& file) {
        return std::filesystem::is_regular_file(file) && access(file.c_str(), X_OK) == 0;
    };
    if (name.find('/') != std::string::npos)
        return executable(std::filesystem::u8path(name)) ? std::filesystem::absolute(std::filesystem::u8path(name)).u8string() : "";
    const char* path = std::getenv("PATH");
    if (!path) return {};
    const std::string search(path);
    for (size_t begin = 0;;) {
        const auto end = search.find(':', begin);
        const auto directory = search.substr(begin, end == std::string::npos ? end : end - begin);
        const auto file = std::filesystem::u8path(directory.empty() ? "." : directory) / name;
        if (executable(file)) return std::filesystem::absolute(file).u8string();
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}
std::vector<std::string> NativeArguments() {
#ifdef __APPLE__
    return std::vector<std::string>(*_NSGetArgv(), *_NSGetArgv() + *_NSGetArgc());
#else
    std::ifstream input("/proc/self/cmdline", std::ios::binary);
    std::vector<std::string> result;
    for (std::string word; std::getline(input, word, '\0');) result.push_back(std::move(word));
    return result;
#endif
}
std::vector<std::string> SplitProcessArguments(const std::string& arguments) {
    if (arguments.find('\0') != std::string::npos) throw std::runtime_error("NUL in process arguments");
    std::vector<std::string> result;
    std::string word;
    char quote = 0;
    bool started = false, escaped = false;
    for (char c : arguments) {
        if (escaped) { word += c; escaped = false; started = true; }
        else if (c == '\\' && quote != '\'') { escaped = true; started = true; }
        else if (quote) { if (c == quote) quote = 0; else word += c; }
        else if (c == '\'' || c == '"') { quote = c; started = true; }
        else if (c == ' ' || c == '\t' || c == '\n') {
            if (started) { result.push_back(std::move(word)); word.clear(); started = false; }
        } else { word += c; started = true; }
    }
    if (quote || escaped) throw std::runtime_error("Unterminated process argument");
    if (started) result.push_back(std::move(word));
    return result;
}
ProcessResult RunProcess(const ProcessRequest& r, const std::atomic_bool* cancel) {
    if (r.executable.empty()) throw std::runtime_error("No executable configured");
    const auto validate = [](const std::string& value) {
        if (value.find('\0') != std::string::npos) throw std::runtime_error("NUL in process request");
    };
    validate(r.executable); validate(r.workingDirectory);
    for (const auto& argument : r.arguments) validate(argument);
    ProcessResult result;
    if (cancel && cancel->load()) { result.cancelled = true; return result; }
    const auto executable = FindExecutablePath(r.executable);
    if (executable.empty()) throw std::runtime_error("Cannot find executable " + r.executable);

    std::map<std::string, std::string> environment;
#ifdef __APPLE__
    auto inherited = *_NSGetEnviron();
#else
    auto inherited = environ;
#endif
    for (auto p = inherited; *p; ++p) {
        const std::string entry(*p);
        const auto equals = entry.find('=');
        if (equals != std::string::npos) environment[entry.substr(0, equals)] = entry.substr(equals + 1);
    }
    for (const auto& [key, value] : r.environment) {
        validate(key); validate(value);
        if (key.empty() || key.find('=') != std::string::npos) throw std::runtime_error("Invalid process environment override");
        environment[key] = value;
    }
    std::vector<std::string> words{r.executable}, entries;
    words.insert(words.end(), r.arguments.begin(), r.arguments.end());
    for (const auto& [key, value] : environment) entries.push_back(key + "=" + value);
    const auto pointers = [](std::vector<std::string>& strings) {
        std::vector<char*> out;
        for (auto& value : strings) out.push_back(value.data());
        out.push_back(nullptr); return out;
    };
    auto argv = pointers(words), env = pointers(entries);
    const auto check = [](int error) {
        if (error) throw std::runtime_error(std::string("Process setup failed: ") + std::strerror(error));
    };
    struct Pipe {
        int read = -1, write = -1;
        ~Pipe() { if (read >= 0) close(read); if (write >= 0) close(write); }
    } pipe;
    int descriptors[2];
    if (::pipe(descriptors) != 0) check(errno);
    pipe.read = descriptors[0]; pipe.write = descriptors[1];
    if (fcntl(pipe.read, F_SETFD, FD_CLOEXEC) == -1 || fcntl(pipe.write, F_SETFD, FD_CLOEXEC) == -1) check(errno);
    const auto flags = fcntl(pipe.read, F_GETFL);
    if (flags == -1 || fcntl(pipe.read, F_SETFL, flags | O_NONBLOCK) == -1) check(errno);
    struct Actions {
        posix_spawn_file_actions_t value;
        Actions() { if (posix_spawn_file_actions_init(&value)) throw std::runtime_error("Cannot initialize process actions"); }
        ~Actions() { posix_spawn_file_actions_destroy(&value); }
    } actions;
    struct Attributes {
        posix_spawnattr_t value;
        Attributes() { if (posix_spawnattr_init(&value)) throw std::runtime_error("Cannot initialize process attributes"); }
        ~Attributes() { posix_spawnattr_destroy(&value); }
    } attributes;
    check(posix_spawn_file_actions_addopen(&actions.value, STDIN_FILENO,
        r.stdinFile.empty() ? "/dev/null" : r.stdinFile.c_str(), O_RDONLY, 0));
    if (r.stdoutFile.empty()) check(posix_spawn_file_actions_adddup2(&actions.value, pipe.write, STDOUT_FILENO));
    else check(posix_spawn_file_actions_addopen(&actions.value, STDOUT_FILENO, r.stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666));
    check(posix_spawn_file_actions_adddup2(&actions.value, pipe.write, STDERR_FILENO));
    check(posix_spawn_file_actions_addclose(&actions.value, pipe.read));
    check(posix_spawn_file_actions_addclose(&actions.value, pipe.write));
    if (!r.workingDirectory.empty()) check(posix_spawn_file_actions_addchdir_np(&actions.value, r.workingDirectory.c_str()));
    check(posix_spawnattr_setflags(&attributes.value, POSIX_SPAWN_SETPGROUP));
    check(posix_spawnattr_setpgroup(&attributes.value, 0));
    pid_t pid;
    check(posix_spawn(&pid, executable.c_str(), &actions.value, &attributes.value, argv.data(), env.data()));
    close(pipe.write); pipe.write = -1;
    struct Child {
        pid_t pid;
        ~Child() { if (pid > 0) { kill(-pid, SIGKILL); while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} } }
    } child{pid};
    const auto started = std::chrono::steady_clock::now();
    auto exitedAt = started;
    bool exited = false, eof = false, terminated = false;
    int status = 0;
    while (!exited || !eof) {
        char buffer[8192];
        for (int batch = 0; batch < 64; ++batch) {
            const auto count = read(pipe.read, buffer, sizeof(buffer));
            if (count > 0) {
                const auto retain = std::min<size_t>(count, r.outputLimit - result.output.size());
                result.output.append(buffer, retain); result.outputTruncated |= retain != static_cast<size_t>(count);
            } else {
                if (count == 0) eof = true;
                else if (errno == EINTR) continue;
                else if (errno != EAGAIN && errno != EWOULDBLOCK) check(errno);
                break;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (!exited) {
            const auto waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) { exited = true; exitedAt = now; child.pid = -1; }
            else if (waited < 0 && errno != EINTR) check(errno);
        }
        if (!exited && !terminated) {
            result.cancelled = cancel && cancel->load();
            result.timedOut = r.timeout.count() > 0 && now - started >= r.timeout;
            if (result.cancelled || result.timedOut) { kill(-pid, SIGKILL); terminated = true; }
        }
        if (exited && !eof && now - exitedAt >= std::chrono::milliseconds(100)) {
            kill(-pid, SIGKILL);
            break;
        }
        if (!exited || !eof) { pollfd fd{pipe.read, POLLIN, 0}; poll(&fd, 1, 10); }
    }
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return result;
}
} // namespace gfx
#endif
