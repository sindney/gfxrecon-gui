#include "app.h"

#include "core/log.h"
#include "gui/themes.h"
#include "platform/dialogs.h"
#include "platform/platform.h"
#include "platform/os_layer.h"

#include <imgui_internal.h> // DockBuilder for the default layout

namespace tpl {

void App::Init(const std::string& settingsPath) {
    settingsPath_ = settingsPath;
    settings_ = LoadSettings(settingsPath_);
    // Apply the new workspace once, including installations with the old stacked
    // layout. Subsequent theme/layout choices remain user preferences.
    if (settings_.uiDefaultsVersion < 1) {
        settings_.themeIndex = 0; // Programmer
        settings_.uiDefaultsVersion = 1;
        RequestDefaultLayout();
    }
    if (settings_.uiDefaultsVersion < 6) {
        settings_.uiDefaultsVersion = 6;
        RequestDefaultLayout();
    }
    workspace_.Init(std::filesystem::u8path(settingsPath_).parent_path());
    ApplyTheme(settings_.themeIndex);
    Log::Instance().SetMinLevel((LogLevel)settings_.logLevel);
    logToFile_ = settings_.logToFile;
    if (logToFile_)
        Log::Instance().SetFileOutput(logFilePath(), true);
}

void App::Shutdown() {
    workspace_.Shutdown();
    settings_.themeIndex = CurrentThemeIndex();
    settings_.logLevel = (int)Log::Instance().MinLevel();
    settings_.logToFile = logToFile_;
    SaveSettings(settings_, settingsPath_);
}

std::string App::logFilePath() const {
    // Keep logs in the settings directory, outside the signed bundle on macOS.
    size_t pos = settingsPath_.find_last_of("/\\");
    std::string dir = pos == std::string::npos ? "." : settingsPath_.substr(0, pos);
    return dir + "/logs/gfxrecon_gui.log";
}

bool App::Draw() {
    const ImGuiIO& io = ImGui::GetIO();
    const bool shortcutHeld =
#if PLATFORM_MACOS
        io.KeySuper;
#else
        io.KeyCtrl;
#endif
    if (shortcutHeld && !io.KeyShift && !io.KeyAlt &&
        ImGui::IsKeyPressed(ImGuiKey_P, false) && !ImGui::GetTopMostPopupModal())
        showCommandPalette_ = !showCommandPalette_;
    if (shortcutHeld && ImGui::IsKeyPressed(ImGuiKey_O, false) && !ImGui::GetTopMostPopupModal())
        pendingCommand_ = Command::OpenFile;

    DrawMenuBar();
    DrawDockspace();

    // Add your tool's commands here and handle them in ExecuteCommand().
    static const std::vector<CommandPaletteEntry> commands = {
        {static_cast<int>(Command::OpenFile), "Open File..."},
        {static_cast<int>(Command::Capture), "Capture application..."},
        {static_cast<int>(Command::SaveCapture), "Save capture"},
        {static_cast<int>(Command::SaveCaptureAs), "Save capture as..."},
        {static_cast<int>(Command::CancelJob), "Cancel current operation"},
        {static_cast<int>(Command::Tools), "Tools and devices..."},
        {static_cast<int>(Command::OpenWindowsSample), "Open Windows triangle sample"},
        {static_cast<int>(Command::OpenAndroidSample), "Open Android triangle sample"},
        {static_cast<int>(Command::OpenMeshSample), "Open indexed cube mesh sample"},
        {static_cast<int>(Command::OpenIndirectSample), "Open indirect draw visualization sample"},
        {static_cast<int>(Command::Settings), "Settings..."},
        {static_cast<int>(Command::ToggleConsole), "Toggle Console"},
        {static_cast<int>(Command::ResetLayout), "Reset Layout"},
        {static_cast<int>(Command::About), "About"},
        {static_cast<int>(Command::Exit), "Exit"},
    };
    if (auto command = commandPalette_.Draw(showCommandPalette_, commands))
        pendingCommand_ = static_cast<Command>(*command);
    // Dispatch outside menu/palette windows so popups share the root ID scope.
    if (pendingCommand_) {
        ExecuteCommand(*pendingCommand_);
        pendingCommand_.reset();
    }

    if (showConsole_)
        console_.Draw(&showConsole_);

    workspace_.Draw();
    if(activateFrames_){
        if(auto* frames=ImGui::FindWindowByName("Frames");frames&&frames->Active){
            ImGui::SetWindowFocus("Frames");activateFrames_=false;
        }
    }
    workspace_.DrawPanelStatuses();
    const auto& capture = workspace_.CurrentCapture();
    const auto title = capture ? capture->source.filename().u8string() + " - GFXReconstruct Inspector" : "GFXReconstruct Inspector";
    if (title != windowTitle_) { windowTitle_ = title; os::SetWindowTitle(title); }

    DrawSettings();

    AboutInfo about{"GFXReconstruct Inspector", "0.1.0", lastOpenedFile_};
    ShowAboutModal(about);

    return !quitRequested_;
}

void App::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            pendingCommand_ = Command::OpenFile;
        if (ImGui::MenuItem("Capture application...")) pendingCommand_ = Command::Capture;
        if (ImGui::MenuItem("Save capture",nullptr,false,workspace_.CanSaveCapture())) pendingCommand_=Command::SaveCapture;
        if (ImGui::MenuItem("Save capture as...",nullptr,false,workspace_.CanSaveCapture())) pendingCommand_=Command::SaveCaptureAs;
        if (ImGui::MenuItem("Cancel current operation", nullptr, false, workspace_.Busy() || workspace_.CaptureSessionActive())) pendingCommand_ = Command::CancelJob;
        if (ImGui::BeginMenu("Samples")) {
            const auto samples = std::filesystem::u8path(ResourceDirectory()) / "samples";
            if (ImGui::MenuItem("Windows triangle (3 frames)", nullptr, false, std::filesystem::is_regular_file(samples / "windows-triangle.gfxr")))
                pendingCommand_ = Command::OpenWindowsSample;
            if (ImGui::MenuItem("Android triangle (3 frames)", nullptr, false, std::filesystem::is_regular_file(samples / "android-triangle.gfxr")))
                pendingCommand_ = Command::OpenAndroidSample;
            if (ImGui::MenuItem("Indexed cube (mesh)", nullptr, false, std::filesystem::is_regular_file(samples / "indexed-cube.gfxr")))
                pendingCommand_ = Command::OpenMeshSample;
            if (ImGui::MenuItem("Indirect triangles (draw visualization)", nullptr, false, std::filesystem::is_regular_file(samples / "indirect-triangles.gfxr")))
                pendingCommand_ = Command::OpenIndirectSample;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Settings..."))
            pendingCommand_ = Command::Settings;
        if (ImGui::MenuItem("Command Palette...", PLATFORM_MACOS ? "Cmd+P" : "Ctrl+P"))
            showCommandPalette_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            pendingCommand_ = Command::Exit;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Tools and devices...")) pendingCommand_ = Command::Tools;
        if (ImGui::MenuItem("Console", nullptr, showConsole_))
            pendingCommand_ = Command::ToggleConsole;
        if (ImGui::MenuItem("Reset Layout"))
            pendingCommand_ = Command::ResetLayout;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About"))
            pendingCommand_ = Command::About;
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void App::ExecuteCommand(Command command) {
    switch (command) {
    case Command::OpenFile:
        if (auto path = OpenFileDialog("All files", "*")) {
            lastOpenedFile_ = *path;
            workspace_.Open(*path);
            LOGI("app") << "Open picked: " << *path;
        }
        break;
    case Command::Settings: showSettings_ = true; break;
    case Command::Capture: workspace_.RequestCapture(); break;
    case Command::SaveCapture: workspace_.SaveCapture(false); break;
    case Command::SaveCaptureAs: workspace_.SaveCapture(true); break;
    case Command::CancelJob: workspace_.CancelJob(); break;
    case Command::Tools: workspace_.RequestTools(); break;
    case Command::OpenWindowsSample:
    case Command::OpenAndroidSample:
    case Command::OpenMeshSample:
    case Command::OpenIndirectSample:
        lastOpenedFile_ = (std::filesystem::u8path(ResourceDirectory()) / "samples" /
            (command == Command::OpenWindowsSample ? "windows-triangle.gfxr" : command == Command::OpenMeshSample ? "indexed-cube.gfxr" : command == Command::OpenIndirectSample ? "indirect-triangles.gfxr" : "android-triangle.gfxr")).u8string();
        workspace_.Open(lastOpenedFile_); break;
    case Command::ToggleConsole: showConsole_ = !showConsole_; break;
    case Command::ResetLayout: RequestDefaultLayout(); break;
    case Command::About: OpenAboutPopup(); break;
    case Command::Exit: quitRequested_ = true; break;
    }
}

void App::DrawSettings() {
    if(showSettings_){ImGui::OpenPopup("Settings");showSettings_=false;}
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize()*24,0),ImGuiCond_Appearing);
    bool open=true;
    if(!ImGui::BeginPopupModal("Settings",&open,ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings))return;

    // Theme picker.
    if (ImGui::BeginCombo("Theme", kThemes[CurrentThemeIndex()].displayName)) {
        for (int i = 0; i < (int)kThemesCount; ++i) {
            if (ImGui::Selectable(kThemes[i].displayName, CurrentThemeIndex() == i)) {
                ApplyTheme(i);
                LOGI("app") << "theme: " << kThemes[i].displayName;
            }
        }
        ImGui::EndCombo();
    }

    int drawReplay=workspace_.FullDrawReplay()?1:0;
    if(ImGui::Combo("Draw replay",&drawReplay,"Light (on demand)\0Full (all views)\0"))workspace_.SetFullDrawReplay(drawReplay==1);
    ImGui::SetItemTooltip("Light loads the selected view and its required passes.\nFull preloads all six views for instant switching.\nFetched views stay cached with either setting.");

    // Logger minimum level.
    const char* levelNames[] = {"Error", "Warn", "Info", "Debug"};
    int level = (int)Log::Instance().MinLevel();
    if (ImGui::Combo("Log level", &level, levelNames, 4))
        Log::Instance().SetMinLevel((LogLevel)level);

    if (ImGui::Checkbox("Log to file", &logToFile_)) {
        if (logToFile_) {
            std::string path = logFilePath();
            if (Log::Instance().SetFileOutput(path, true))
                LOGI("app") << "logging to " << path;
            else {
                LOGE("app") << "cannot open log file " << path;
                logToFile_ = false;
            }
        } else {
            LOGI("app") << "log file output off";
            Log::Instance().ClearFileOutput();
        }
    }
    if (logToFile_)
        ImGui::TextDisabled("%s", logFilePath().c_str());

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
    if(ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void App::DrawDockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##dockspace-host", nullptr, flags);
    ImGui::PopStyleVar(2);

    ImGuiID dockspaceId = ImGui::GetID("MainDockspace");

    // Frame strip, event column, pipeline canvas, inspector tabs and diagnostics.
    if (resetLayout_) {
        resetLayout_ = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockspaceId, ImGui::GetCursorScreenPos());
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetContentRegionAvail());
        const ImGuiID rootId = dockspaceId;
        ImGuiID footerId = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Down,
                                                       0.24f, nullptr, &dockspaceId);
        ImGuiID leftId = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left,
                                                     0.22f, nullptr, &dockspaceId);
        ImGui::DockBuilderDockWindow("Events", leftId);
        for(const auto* name:{"Image","Pipeline","Shader","Mesh","Resources"})ImGui::DockBuilderDockWindow(name,dockspaceId);
        ImGui::DockBuilderDockWindow("Console", footerId);
        ImGui::DockBuilderDockWindow("Frames", footerId);
        ImGui::DockBuilderFinish(rootId);
        dockspaceId = rootId;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetFontSize(), ImGui::GetStyle().FramePadding.y));
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::PopStyleVar();
    ImGui::End();
}

} // namespace tpl
