#include "platform/crash.h"

#include "core/log.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#endif

namespace tpl {

#if PLATFORM_WINDOWS
namespace {

// StackWalk64-based trace (richer than RtlCaptureStackBackTrace: walks real
// frames so SymFromAddr/SymGetLineFromAddr64 resolve correctly in optimized
// builds). Log<0> may not be live on startup crashes, so mirror to stderr.
void LogStackTrace(HANDLE process, CONTEXT* context) {
    SymInitialize(process, nullptr, TRUE);

    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(),
                         &frame, context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
            break;
        DWORD64 addr = frame.AddrPC.Offset;
        if (!addr)
            break;

        char symbolBuf[sizeof(SYMBOL_INFO) + 256] = {};
        auto* symbol = (SYMBOL_INFO*)symbolBuf;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        std::string name = "<unknown>";
        DWORD64 displacement = 0;
        if (SymFromAddr(process, addr, &displacement, symbol))
            name = symbol->Name;

        IMAGEHLP_LINE64 lineInfo = {};
        lineInfo.SizeOfStruct = sizeof(lineInfo);
        DWORD lineDisp = 0;
        std::string location;
        if (SymGetLineFromAddr64(process, addr, &lineDisp, &lineInfo)) {
            std::string file = lineInfo.FileName;
            size_t pos = file.find_last_of("/\\");
            if (pos != std::string::npos)
                file = file.substr(pos + 1);
            location = " (" + file + ":" + std::to_string(lineInfo.LineNumber) + ")";
        }

        char buf[1024];
        std::snprintf(buf, sizeof(buf), "#%2d 0x%llx %s+0x%llx%s", i,
                      (unsigned long long)addr, name.c_str(),
                      (unsigned long long)displacement, location.c_str());
        LOGE("crash") << buf;
        std::fprintf(stderr, "%s\n", buf);
    }

    SymCleanup(process);
}

void WriteMiniDump(EXCEPTION_POINTERS* ep) {
    std::filesystem::path dir = std::filesystem::current_path() / "crash";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path file =
        dir / ("crash_" + std::to_string(GetCurrentProcessId()) + ".dmp");

    HANDLE hFile = CreateFileW(file.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LOGE("crash") << "failed to create dump file " << file.string();
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION info = {};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ep;
    info.ClientPointers = FALSE;

    BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), hFile,
        (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithHandleData), &info,
        nullptr, nullptr);
    CloseHandle(hFile);
    if (ok)
        LOGE("crash") << "minidump written to " << file.string();
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* ep) {
    LOGE("crash") << "unhandled exception 0x" << std::hex
                  << ep->ExceptionRecord->ExceptionCode << " at 0x"
                  << ep->ExceptionRecord->ExceptionAddress << std::dec;
    LogStackTrace(GetCurrentProcess(), ep->ContextRecord);
    WriteMiniDump(ep);
    std::fflush(stdout);
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void SignalHandler(int sig) {
    LOGE("crash") << "fatal signal " << sig << " - rerouting through the SEH"
                     " filter for a full context capture";
    std::fflush(stdout);
    std::fflush(stderr);
    // Re-raise as an SEH exception so UnhandledExceptionFilter runs with a
    // real CONTEXT (better stack walk + minidump than signal context allows).
    if (sig == SIGSEGV)
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    else
        RaiseException(0x4001000A, 0, 0, nullptr); // DBG_TERMINATE_PROCESS-ish
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

} // namespace

void InstallCrashHandler() {
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
    std::signal(SIGABRT, SignalHandler);
    std::signal(SIGSEGV, SignalHandler);
}

#elif PLATFORM_MACOS

namespace {

void SignalHandler(int sig) {
    LOGE("crash") << "fatal signal " << sig;
    std::fflush(stdout);
    std::fflush(stderr);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

} // namespace

void InstallCrashHandler() {
    std::signal(SIGSEGV, SignalHandler);
    std::signal(SIGABRT, SignalHandler);
}

#else

void InstallCrashHandler() {}

#endif

} // namespace tpl
