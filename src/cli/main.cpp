// gfxrecon-cli: agent-facing console front end over the shared gfxrecon_core library.
#include "cli/cli.h"
#include "core/log.h"
#include "platform/process.h"

#include <csignal>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
std::atomic_bool cancelled{false};
#ifdef _WIN32
BOOL WINAPI OnConsole(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) { cancelled = true; return TRUE; }
    return FALSE;
}
#else
void OnSignal(int) { cancelled = true; }
#endif
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleCtrlHandler(OnConsole, TRUE);
    SetConsoleOutputCP(CP_UTF8);
    // UTF-8 arguments regardless of the console code page.
    auto words = gfx::NativeArguments();
    if (!words.empty()) words.erase(words.begin());
#else
    std::signal(SIGINT, OnSignal); std::signal(SIGTERM, OnSignal);
    std::vector<std::string> words(argv + 1, argv + argc);
#endif
    (void)argc; (void)argv;
    // stdout carries exactly one JSON document; all logging goes to stderr.
    tpl::Log::Instance().SetConsoleStream(&std::cerr);
    tpl::Log::Instance().SetMinLevel(tpl::LogLevel::Warn);
    return gfx::cli::Main(words, std::cout, cancelled);
}
