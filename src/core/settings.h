#pragma once

#include <string>

// Minimal key=value settings file next to the exe (theme, log level, ...).
namespace tpl {

struct Settings {
    int themeIndex = 0; // Programmer, the first entry in the persisted theme table.
    int uiDefaultsVersion = 0;
    int logLevel = 2; // tpl::LogLevel as int
    bool logToFile = false;
};

// Loads from `path`; missing file or keys keep defaults.
Settings LoadSettings(const std::string& path);
void SaveSettings(const Settings& settings, const std::string& path);

// Directory containing the running executable (UTF-8).
std::string ExeDirectory();

} // namespace tpl
