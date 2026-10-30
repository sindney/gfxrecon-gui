#pragma once

#include <string>

// Minimal key=value settings file (theme, log level, ...).
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
// macOS uses per-user Application Support so signed app bundles stay immutable.
// Other platforms retain the portable executable-directory default.
std::string SettingsDirectory();
// Bundle Resources on macOS; executable directory for portable builds elsewhere.
std::string ResourceDirectory();

} // namespace tpl
