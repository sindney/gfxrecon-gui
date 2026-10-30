#include "core/settings.h"

#include "platform/platform.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif PLATFORM_MACOS
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace tpl {

Settings LoadSettings(const std::string& path) {
    Settings s;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        int value = 0;
        try {
            value = std::stoi(line.substr(eq + 1));
        } catch (...) {
            continue;
        }
        if (key == "theme") s.themeIndex = value;
        else if (key == "ui_defaults_version") s.uiDefaultsVersion = value;
        else if (key == "log_level") s.logLevel = value;
        else if (key == "log_to_file") s.logToFile = value != 0;
    }
    return s;
}

void SaveSettings(const Settings& s, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    out << "theme=" << s.themeIndex << "\n"
        << "ui_defaults_version=" << s.uiDefaultsVersion << "\n"
        << "log_level=" << s.logLevel << "\n"
        << "log_to_file=" << (s.logToFile ? 1 : 0) << "\n";
}

std::string ExeDirectory() {
#if PLATFORM_WINDOWS
    wchar_t wpath[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, wpath, MAX_PATH);
    std::string path;
    if (len > 0) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wpath, (int)len, nullptr, 0, nullptr, nullptr);
        path.resize(size);
        WideCharToMultiByte(CP_UTF8, 0, wpath, (int)len, path.data(), size, nullptr, nullptr);
    }
#elif PLATFORM_MACOS
    char buf[1024];
    uint32_t size = sizeof(buf);
    std::string path;
    if (_NSGetExecutablePath(buf, &size) == 0)
        path = buf;
#else
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string path;
    if (len > 0) {
        buf[len] = '\0';
        path = buf;
    }
#endif
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? "." : path.substr(0, pos);
}

std::string SettingsDirectory() {
#if PLATFORM_MACOS
    const auto home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("HOME is required to locate macOS application settings");
    return (std::filesystem::u8path(home) / "Library/Application Support/GFXReconstruct Inspector").u8string();
#else
    return ExeDirectory();
#endif
}

std::string ResourceDirectory() {
    const auto executable = std::filesystem::u8path(ExeDirectory());
#if PLATFORM_MACOS
    if (executable.filename() == "MacOS" && executable.parent_path().filename() == "Contents")
        return (executable.parent_path() / "Resources").u8string();
#endif
    return executable.u8string();
}

} // namespace tpl
