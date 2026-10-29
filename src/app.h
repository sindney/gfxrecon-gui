#pragma once

#include "core/settings.h"
#include "gui/about.h"
#include "gui/command_palette.h"
#include "gui/console_window.h"
#include "gui/workspace.h"

#include <imgui.h>

#include <string>

namespace tpl {

// Frame-level app state and UI: menu bar, dockspace, panels, console.
// `Draw` returns false when the app should quit (File->Exit).
class App {
public:
    void Init(const std::string& settingsPath);
    void Shutdown();

    // Build the default docking layout on the next frame (first run).
    void RequestDefaultLayout() { resetLayout_ = true; activateFrames_ = true; }

    bool Draw(); // one ImGui frame's worth of UI (inside NewFrame/Render)

    const Settings& GetSettings() const { return settings_; }
    gfx::Workspace& Workspace() { return workspace_; }

private:
    enum class Command { OpenFile, OpenWindowsSample, OpenAndroidSample, OpenMeshSample, OpenIndirectSample, Capture, SaveCapture, SaveCaptureAs, CancelJob, Tools, Settings, ToggleConsole, ResetLayout, About, Exit };
    void ExecuteCommand(Command command);
    void DrawMenuBar();
    void DrawDockspace();
    void DrawSettings();
    std::string logFilePath() const;

    Settings settings_;
    std::string settingsPath_;
    bool quitRequested_ = false;
    bool showConsole_ = true;
    bool showSettings_ = false;
    bool logToFile_ = false;
    bool resetLayout_ = false;
    bool activateFrames_ = true;
    bool showCommandPalette_ = false;
    std::optional<Command> pendingCommand_;

    CommandPalette commandPalette_;
    ConsoleWindow console_;
    std::string lastOpenedFile_;
    std::string windowTitle_;
    gfx::Workspace workspace_;
};

} // namespace tpl
