#include "gui/themes.h"

#include <imgui.h>

namespace tpl {

namespace {

// Theme bodies copied from fury3d's EditorThemes (originally TheAncientOwl's
// collection in imgui issue #707).
#include "gui/themes_inline.cpp.inc"

} // namespace

const ThemeEntry kThemes[] = {
    {"Programmer",       &SetupImGuiProgrammerStyle},
    {"Forest Green",     &SetupForestGreenStyle},
    {"Amethyst",         &SetupImGuiAmethystStyle},
    {"Sapphire",         &SetupImGuiSapphireStyle},
    {"Amber Yellow",     &SetupImGuiAmberYellowStyle},
    {"Dracula",          &SetupImGuiDraculaStyle},
    {"Catppuccin Mocha", &SetupImGuiCatppuccinMochaStyle},
    {"Gruvbox Hard",     &SetupImGuiGruvboxHardStyle},
    {"Crimson Vesuvius", &SetupImGuiCrimsonVesuviusStyle},
    {"Rose Quartz",      &SetupImGuiRoseQuartzStyle},
    {"Cyberpunk",        &SetupImGuiCyberpunkStyle},
    {"Paper And Ink",    &SetupImGuiPaperAndInkStyle},
    {"Dark",             &SetupImGuiDarkStyle},
};

const std::size_t kThemesCount = sizeof(kThemes) / sizeof(kThemes[0]);

namespace {
int s_currentThemeIndex = 0;
}

void ApplyTheme(int index) {
    if (index < 0 || index >= (int)kThemesCount)
        index = 0;
    s_currentThemeIndex = index;
    kThemes[index].apply();
}

int CurrentThemeIndex() { return s_currentThemeIndex; }

void SetCurrentThemeIndex(int index) {
    if (index < 0 || index >= (int)kThemesCount)
        index = 0;
    s_currentThemeIndex = index;
}

} // namespace tpl
