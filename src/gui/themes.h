#pragma once

#include <cstddef>

// Theme indices match the kThemes[] table in themes.cpp. Persisted as an
// integer index - append to the table, don't reorder.
namespace tpl {

struct ThemeEntry {
    const char* displayName;
    void (*apply)();
};

extern const ThemeEntry kThemes[];
extern const std::size_t kThemesCount;

void ApplyTheme(int index);

int CurrentThemeIndex();
void SetCurrentThemeIndex(int index);

} // namespace tpl
