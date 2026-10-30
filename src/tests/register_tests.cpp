// Smoke tests driving the real app UI via the Dear ImGui Test Engine.
#include "app.h"
#include "core/log.h"
#include "gui/about.h"
#include "gui/themes.h"
#include "platform/platform.h"
#include "platform/dialogs.h"
#include "core/artifacts.h"
#include "gui/editor_input.h"
#include "platform/os_layer.h"
#ifdef IMGUI_TEMPLATE_OSLAYER_SDL3
#include <SDL3/SDL.h>
#endif

#include <imgui_te_context.h>
#include <imgui_te_engine.h>
#include <imgui_internal.h>
#include <imgui_capture_tool.h>

#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <thread>

namespace tpl {
namespace {
constexpr ImGuiKeyChord kShortcutModifier = PLATFORM_MACOS ? ImGuiMod_Super : ImGuiMod_Ctrl;
std::optional<std::string> captureSaveChoice;
std::string captureSaveDefaultName,captureSaveDefaultFolder;
std::optional<std::string> CaptureSaveDialog(const char*,const char*,const char* name,const char* folder) {
    captureSaveDefaultName=name?name:"";captureSaveDefaultFolder=folder?folder:"";return captureSaveChoice;
}
ImGuiWindow* EditorWindow() {
    for (auto* window : GImGui->Windows)
        if (window->Active && std::string(window->Name).rfind("Shader/source-editor_", 0) == 0) return window;
    return nullptr;
}
void WaitForWorkspace(ImGuiTestContext* ctx, gfx::Workspace& workspace) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    do {
        ctx->Yield(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!workspace.Busy() && !workspace.HasPendingWork()) { ctx->Yield(2); if (!workspace.Busy() && !workspace.HasPendingWork()) return; }
    } while (std::chrono::steady_clock::now() < deadline && !ctx->IsError());
}
void ReviewScreenshot(ImGuiTestContext* ctx,const char* suffix){
    const auto folder=std::getenv("GFXRECON_GUI_REVIEW_DIR");if(!folder)return;
    std::filesystem::create_directories(std::filesystem::u8path(folder));ctx->Yield(4);
    const auto path=std::filesystem::u8path(folder)/(std::string(suffix)+".png");const auto* viewport=ImGui::GetMainViewport();ctx->CaptureReset();
    const int flags=std::string(suffix)=="phone-unlock-notice"||std::string(suffix)=="settings-dialog"?ImGuiCaptureFlags_IncludePopups:0;
    ctx->CaptureArgs->InCaptureRect=ImRect(viewport->Pos,ImVec2(viewport->Pos.x+viewport->Size.x,viewport->Pos.y+viewport->Size.y));
    std::snprintf(ctx->CaptureArgs->InOutputFile,sizeof(ctx->CaptureArgs->InOutputFile),"%s",path.u8string().c_str());IM_CHECK(ctx->CaptureScreenshot(flags));
}
void FrameAction(ImGuiTestContext* ctx, const char* action, int frame = 1) {
    ImGui::SetWindowFocus("Frames"); ctx->Yield(2);
    ctx->SetRef("//Frames");
    auto* strip = ctx->WindowInfo("//Frames/frame-strip").Window;
    IM_CHECK(strip != nullptr);
    const auto seed = ImHashData(&frame, sizeof(frame), strip->ID);
    ctx->ItemClick(ImHashStr("##Frame thumbnail", 0, seed), ImGuiMouseButton_Right);
    ctx->SetRef("//$FOCUSED");
    if (std::string(action).find('/') != std::string::npos) ctx->MenuClick(action);
    else ctx->ItemClick(action);
}
void ClickFrame(ImGuiTestContext* ctx, int frame) {
    ImGui::SetWindowFocus("Frames"); ctx->Yield(2);
    auto* strip = ctx->WindowInfo("//Frames/frame-strip").Window;
    IM_CHECK(strip != nullptr);
    const auto seed = ImHashData(&frame, sizeof(frame), strip->ID);
    ctx->ItemClick(ImHashStr("##Frame thumbnail", 0, seed));
}
void ShaderAction(ImGuiTestContext* ctx, const char* action) {
    ImGui::SetWindowFocus("Shader"); ctx->Yield(2);
    ctx->SetRef("//Shader"); ctx->ItemClick("More");
    ctx->SetRef("//$FOCUSED"); ctx->ItemClick(action); ctx->PopupCloseAll();
}
void ToggleSubmitted(ImGuiTestContext* ctx) {
    ctx->SetRef("//Events"); ctx->ItemClick("Filters");
    ctx->SetRef("//$FOCUSED"); ctx->ItemClick("Submitted executions"); ctx->PopupCloseAll();
    ctx->SetRef("//Events");
}
template<class F>bool WaitLive(ImGuiTestContext* ctx,gfx::Workspace& workspace,F predicate){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
    while(std::chrono::steady_clock::now()<deadline&&!ctx->IsError()){
        ctx->Yield();std::this_thread::sleep_for(std::chrono::milliseconds(5));const auto state=workspace.LiveState();
        if(state&&state->phase==gfx::LivePhase::Failed){ctx->LogError("%s",state->detail.c_str());return false;}
        if(state&&predicate(*state))return true;
    }return false;
}
}

#if PLATFORM_MACOS
void RegisterMacTests(ImGuiTestEngine* engine);
#endif

void RegisterAppTests(ImGuiTestEngine* engine, App* app) {
    (void)app;
    ImGuiTest* t = nullptr;

    t = IM_REGISTER_TEST(engine, "shell", "app starts and menu bar exists");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        // ItemInfo with full menu-bar path doubles as the existence check and
        // spins until the item appears (hooks arm a frame after the query).
        ImGuiTestItemInfo file =
            ctx->ItemInfo("//##MainMenuBar/##MenuBar/File", ImGuiTestOpFlags_NoError);
        IM_CHECK(file.ID != 0);
        ctx->MenuClick("//##MainMenuBar/View");
        ctx->MenuClick("//##MainMenuBar/Help");
        ctx->PopupCloseAll();
    };

    t = IM_REGISTER_TEST(engine, "shell", "about modal opens and closes");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        OpenAboutPopup(); // direct call; menu-path clicking covered by menu test
        ctx->Yield(2);
        ctx->SetRef("About");
        IM_CHECK(ctx->ItemExists("OK"));
        ctx->ItemClick("OK");
        ctx->PopupCloseAll();
    };

    t = IM_REGISTER_TEST(engine, "shell", "File capture opens a non-dockable dialog without device access");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->MenuClick("//##MainMenuBar/File/Capture application...");
        ctx->SetRef("//Capture application"); IM_CHECK(ctx->ItemExists("Next"));
        IM_CHECK((ImGui::FindWindowByName("Capture application")->Flags & ImGuiWindowFlags_NoDocking) != 0);
        ctx->Yield(3);const auto width=ImGui::FindWindowByName("Capture application")->Size.x;ctx->Yield(30);
        IM_CHECK(std::abs(ImGui::FindWindowByName("Capture application")->Size.x-width)<1);
        ctx->ItemClick("Close");
    };

    t = IM_REGISTER_TEST(engine, "shell", "theme switch applies");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        const int original = CurrentThemeIndex();
        int next = (CurrentThemeIndex() + 1) % (int)kThemesCount;
        ctx->MenuClick("//##MainMenuBar/File/Settings...");
        ctx->SetRef("//Settings");
        char label[128];
        std::snprintf(label, sizeof(label), "Theme/%s", kThemes[next].displayName);
        ctx->ComboClick(label);
        IM_CHECK_EQ(CurrentThemeIndex(), next);
        std::snprintf(label, sizeof(label), "Theme/%s", kThemes[original].displayName);
        ctx->ComboClick(label);
        ctx->ItemClick("Close");
    };

    t = IM_REGISTER_TEST(engine, "shell", "default workspace keeps primary panels visible separately");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto* app = static_cast<App*>(ctx->Test->UserData);
        app->RequestDefaultLayout();
        ctx->Yield(4);
        const char* panels[] = {"Events", "Pipeline", "Shader", "Image", "Resources", "Frames", "Console"};
        std::vector<ImGuiID> docks;
        for (const auto* name : panels) {
            auto* window = ImGui::FindWindowByName(name);
            IM_CHECK(window != nullptr);
            IM_CHECK(window->DockId != 0);
            IM_CHECK(window->Size.x > 120 && window->Size.y > 70);
            docks.push_back(window->DockId);
        }
        auto* events = ImGui::FindWindowByName("Events");
        auto* shader = ImGui::FindWindowByName("Shader");
        auto* image = ImGui::FindWindowByName("Image");
        auto* pipeline = ImGui::FindWindowByName("Pipeline");
        IM_CHECK(events->Pos.x < pipeline->Pos.x);
        IM_CHECK_EQ(pipeline->DockId,image->DockId);
        IM_CHECK(image->Size.x>events->Size.x*2);
        IM_CHECK_EQ(shader->DockId, image->DockId);
        IM_CHECK_EQ(ImGui::FindWindowByName("Resources")->DockId, image->DockId);
        IM_CHECK_EQ(ImGui::FindWindowByName("Frames")->DockId, ImGui::FindWindowByName("Console")->DockId);
        IM_CHECK(ImGui::FindWindowByName("Frames")->DockTabIsVisible);
        IM_CHECK(!ImGui::FindWindowByName("Console")->DockTabIsVisible);
        ImGui::SetWindowFocus("Console");ctx->Yield(4);
        IM_CHECK(ImGui::FindWindowByName("Console")->DockTabIsVisible);
        ctx->Yield(4);IM_CHECK(ImGui::FindWindowByName("Console")->DockTabIsVisible);
        app->RequestDefaultLayout();ctx->Yield(4);
        IM_CHECK(ImGui::FindWindowByName("Frames")->DockTabIsVisible);
        IM_CHECK(ImGui::FindWindowByName("Jobs") == nullptr || !ImGui::FindWindowByName("Jobs")->Active);
        auto* legacyCapture = ImGui::FindWindowByName("Capture & Tools");
        IM_CHECK(legacyCapture == nullptr || !legacyCapture->Active);
        IM_CHECK(ImGui::FindWindowByName("Frames")->Pos.y > events->Pos.y);
        IM_CHECK(ImGui::FindWindowByName("Console")->Pos.y > events->Pos.y);
        ctx->SetRef("//##dockspace-host"); IM_CHECK(!ctx->ItemExists("Open capture..."));
        IM_CHECK_EQ(Settings{}.themeIndex, 0);
        IM_CHECK_EQ(std::string(kThemes[0].displayName), std::string("Programmer"));
    };

    t = IM_REGISTER_TEST(engine, "shell", "console receives logged message");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        LOGI("test") << "console-smoke-marker";
        ctx->Yield(2);
        bool found = false;
        for (const LogRecord& r : Log::Instance().RecentRecords())
            if (r.message.find("console-smoke-marker") != std::string::npos)
                found = true;
        IM_CHECK(found);
    };

    t = IM_REGISTER_TEST(engine, "shell", "palette shortcut filters and dispatches settings");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->KeyPress(kShortcutModifier | ImGuiKey_P);
        ctx->SetRef("//Command Palette");
        IM_CHECK(ctx->ItemExists("##filter"));
        // The filter receives focus on opening and matches without case sensitivity.
        ctx->KeyChars("sEtTiNgS");
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("##filter"));
        ctx->SetRef("//Settings");
        IM_CHECK(ctx->ItemExists("Close"));
        const auto* settings=ImGui::FindWindowByName("Settings");IM_CHECK(settings!=nullptr);
        IM_CHECK((settings->Flags&ImGuiWindowFlags_Modal)!=0);IM_CHECK((settings->Flags&ImGuiWindowFlags_NoDocking)!=0);IM_CHECK(settings->DockNode==nullptr);
        ctx->ItemClick("Close");

        ctx->KeyPress(kShortcutModifier | ImGuiKey_P);
        ctx->SetRef("//Command Palette");
        IM_CHECK(ctx->ItemExists("##filter"));
        ctx->KeyPress(kShortcutModifier | ImGuiKey_P);
        IM_CHECK(!ctx->ItemExists("##filter"));
        ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->Yield(3);ctx->SetRef("//Settings");IM_CHECK(ctx->ItemExists("Close"));
        ReviewScreenshot(ctx,"settings-dialog");ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(3);IM_CHECK(!ctx->ItemExists("Close"));
    };

    t = IM_REGISTER_TEST(engine, "shell", "palette empty results and escape are safe");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->KeyPress(kShortcutModifier | ImGuiKey_P);
        ctx->SetRef("//Command Palette");
        ctx->KeyChars("no-command-matches-this");
        ctx->KeyPress(ImGuiKey_DownArrow);
        ctx->KeyPress(ImGuiKey_UpArrow);
        ctx->KeyPress(ImGuiKey_Enter);
        IM_CHECK(ctx->ItemExists("##filter"));
        ctx->KeyPress(ImGuiKey_Escape);
        ctx->Yield(2);
        IM_CHECK(!ctx->ItemExists("##filter"));

        // Reopening clears the old filter. Up wraps to Exit; Up again selects About.
        ctx->KeyPress(kShortcutModifier | ImGuiKey_P);
        ctx->KeyPress(ImGuiKey_UpArrow, 2);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->SetRef("//About");
        IM_CHECK(ctx->ItemExists("OK"));
        ctx->ItemClick("OK");
    };

    t = IM_REGISTER_TEST(engine, "shell", "palette menu entry and mouse dispatch");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        ctx->MenuClick("//##MainMenuBar/File/Command Palette...");
        ctx->SetRef("//Command Palette");
        ctx->ItemClick("##filter");
        ctx->KeyChars("About");
        ctx->SetRef(ctx->WindowInfo("//Command Palette/commands").Window);
        ctx->ItemClick("**/About");
        ctx->SetRef("//About");
        IM_CHECK(ctx->ItemExists("OK"));
        ctx->ItemClick("OK");
    };

#if PLATFORM_MACOS
    RegisterMacTests(engine);
#endif

    t = IM_REGISTER_TEST(engine, "inspection", "JSONL open and linked event selection");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto* app = static_cast<App*>(ctx->Test->UserData);
        const auto file = std::filesystem::temp_directory_path() / "gfxrecon-ui-fixture.jsonl";
        {
            std::ofstream out(file);
            out << R"({"header":{"gfxrecon-version":"fixture"}})" << '\n';
            out << R"({"index":4294967297,"function":{"name":"vkCmdDraw","thread":3,"args":{"commandBuffer":7,"vertexCount":3}}})" << '\n';
            out << R"({"index":4294967298,"function":{"name":"vkQueuePresentKHR","args":{}}})" << '\n';
        }
        app->Workspace().Open(file.u8string());
        WaitForWorkspace(ctx, app->Workspace());
        IM_CHECK(!app->Workspace().Busy());
        IM_CHECK(app->Workspace().CurrentCapture() != nullptr);
        IM_CHECK_EQ(app->Workspace().CurrentCapture()->events.size(), 2);
        IM_CHECK_EQ(app->Workspace().CurrentCapture()->events[0].block, 4294967297ull);
        app->Workspace().SelectEvent(1);
        ctx->Yield(2);
        IM_CHECK_EQ(app->Workspace().SelectedEvent(), 1);
        std::filesystem::remove(file);
    };

    const auto desktopFixture = std::getenv("GFXRECON_GUI_TEST_CAPTURE");
    const auto androidFixture = std::getenv("GFXRECON_GUI_ANDROID_CAPTURE");
    const auto captureDeviceFixture = std::getenv("GFXRECON_GUI_CAPTURE_DEVICE");
    const auto windowsCaptureFixture = std::getenv("GFXRECON_GUI_CAPTURE_WINDOWS");
    if (desktopFixture || androidFixture || captureDeviceFixture || windowsCaptureFixture) {
        t = IM_REGISTER_TEST(engine, "integration", "triangle inspect compile apply replay and revert");
        t->UserData = app;
        t->ArgVariant = desktopFixture ? 0 : androidFixture ? 1 : captureDeviceFixture ? 2 : 3;
        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto* app = static_cast<App*>(ctx->Test->UserData);
            auto& workspace = app->Workspace();
            auto wait = [&] {
                WaitForWorkspace(ctx, workspace);
            };
            const bool android = ctx->Test->ArgVariant == 1 || ctx->Test->ArgVariant == 2;
            workspace.SetReplayMode(gfx::ReplayMode::Automatic);
            if (ctx->Test->ArgVariant == 2) {
                ctx->MenuClick("//##MainMenuBar/File/Capture application...");
                ctx->SetRef("//Capture application");
                ctx->ItemClick("Android");
                ctx->ItemClick("Refresh devices"); wait();
                ctx->Yield(2);
                ctx->ItemClick("Device");
                const auto* popup = ctx->WindowInfo("//$FOCUSED").Window;
                IM_CHECK(popup != nullptr);
                const std::string devicePath = std::string("//") + popup->Name + "/**/" + std::getenv("GFXRECON_GUI_CAPTURE_DEVICE");
                ctx->ItemClick(devicePath.c_str());
                ctx->ItemClick("Next"); wait();
                IM_CHECK(workspace.Status().find("Device inspected:") != std::string::npos);
                ctx->ItemClick("Install replay APK"); wait();
                IM_CHECK(workspace.Status().find("Replay APK installed on ") != std::string::npos);
                ctx->ItemClick("Install test sample"); wait();
                IM_CHECK(workspace.Status().find("Sample APK installed on ") != std::string::npos);
                ctx->ItemClick("Next");
                ctx->ItemClick("Use Vulkan test sample");
                ctx->ComboClick("Capture timing/Scheduled frames");
                const auto previousCapture = workspace.CurrentCapture();
                ctx->ItemInputValue("Frames", "1000000-1000001");
                ctx->ItemClick("Start capture");
                for (int i = 0; i < 1200 && workspace.Busy() && !std::filesystem::exists(workspace.ActiveCaptureSession() / "device-journal.json"); ++i) ctx->Yield();
                IM_CHECK(workspace.Busy());
                ctx->ItemClick("Cancel capture"); wait();
                IM_CHECK(!workspace.Busy());
                IM_CHECK(workspace.CurrentCapture() == previousCapture);
                {
                    std::ifstream cancelled(workspace.ActiveCaptureSession() / "device-journal.json");
                    const auto recovered = gfx::Json::parse(cancelled);
                    IM_CHECK(recovered.at("restored").get<bool>());
                }
                ctx->ItemInputValue("Frames", "1-3");
                ctx->ItemClick("Start capture"); wait();
                IM_CHECK(workspace.CurrentCapture() != nullptr);
                IM_CHECK_EQ(workspace.CurrentCapture()->frames, 3);
                const auto journal = workspace.CurrentCapture()->source.parent_path() / "device-journal.json";
                std::ifstream input(journal); const auto state = gfx::Json::parse(input);
                IM_CHECK(state.at("restored").get<bool>());
                IM_CHECK_EQ(state.at("serial").get<std::string>(), std::string(std::getenv("GFXRECON_GUI_CAPTURE_DEVICE")));
                ctx->SetRef("//Capture application"); IM_CHECK(ctx->ItemExists("Open capture")); ctx->ItemClick("Open capture"); wait();
            } else if (ctx->Test->ArgVariant == 3) {
                ctx->MenuClick("//##MainMenuBar/File/Capture application...");
                ctx->SetRef("//Capture application");
                ctx->ItemClick("Local Windows");
                ctx->ItemInputValue("Executable", std::getenv("GFXRECON_GUI_CAPTURE_WINDOWS"));
                ctx->ItemClick("Next"); ctx->ItemClick("Next");
                ctx->ItemInputValue("Arguments", "triangle");
                ctx->ItemInputValue("Working directory", std::getenv("GFXRECON_GUI_CAPTURE_WORKDIR"));
                ctx->ComboClick("Capture timing/Scheduled frames");
                ctx->ItemInputValue("Frames", "1-3");
                ctx->ItemClick("Start capture"); wait();
                IM_CHECK(workspace.CurrentCapture() != nullptr);
                IM_CHECK_EQ(workspace.CurrentCapture()->frames, 3);
                ctx->SetRef("//Capture application"); ctx->ItemClick("Open capture"); wait();
            } else {
                workspace.Open(std::getenv(android ? "GFXRECON_GUI_ANDROID_CAPTURE" : "GFXRECON_GUI_TEST_CAPTURE")); wait();
            }
            IM_CHECK(!workspace.Busy());
            IM_CHECK(workspace.CurrentCapture() != nullptr);
            IM_CHECK(workspace.CurrentCapture()->executions.size() >= 1);
            IM_CHECK_EQ(workspace.CurrentCapture()->android, android);
            IM_CHECK_EQ(workspace.SelectedFrame(), 1u);
            IM_CHECK(workspace.BaselineImage().Width() > 0);
            IM_CHECK_EQ(workspace.ThumbnailCount(), 1u);
            ctx->Yield(60);
            IM_CHECK(!workspace.Busy()); IM_CHECK(!workspace.HasPendingWork());
            IM_CHECK_EQ(workspace.ThumbnailCount(), 1u);
            ClickFrame(ctx, 2); wait();
            IM_CHECK_EQ(workspace.SelectedFrame(), 2u);
            IM_CHECK_EQ(workspace.CurrentCapture()->events[workspace.SelectedEvent()].frame, 2u);
            ClickFrame(ctx, 1); wait();
            if (!android) {
                FrameAction(ctx, (std::string("Replay target/") + gfx::LocalReplayLabel()).c_str()); wait();
                FrameAction(ctx, "Replay target/Automatic"); wait();
            }
            ctx->SetRef("//Frames"); IM_CHECK(!ctx->ItemExists("##replay-target"));
            workspace.SelectExecution(0);
            const auto selected = workspace.CurrentCapture()->executions[0].event;
            const auto& selectedEvent = workspace.CurrentCapture()->events[selected];
            ctx->SetRef("//Events");
            ctx->ItemClick(("**/" + std::to_string(selectedEvent.block) + "  " + selectedEvent.name).c_str());
            IM_CHECK_EQ(workspace.SelectedEvent(), selected);
            IM_CHECK_EQ(workspace.SelectedExecution(), 0);
            wait();
            IM_CHECK_EQ(workspace.DisplayedExecution(), 0);
            IM_CHECK(workspace.BaselineImage().Width() > 0);
            IM_CHECK(workspace.ShaderDocumentCount()>=2);IM_CHECK(workspace.Mesh().ScreenVertexCount()>0);
            IM_CHECK(workspace.ExecutionState(0)==gfx::PanelState::Success);
            ImGui::SetWindowFocus("Image");ctx->Yield(3);ctx->SetRef("//Image");
            IM_CHECK(ctx->ItemExists("**/Frame presentation"));IM_CHECK(workspace.SelectedImage().Width()>0);
            if(!android){
                for(const auto* mode:{"Solid coverage","Wireframe","Depth pass & fail","Original"}){
                    ctx->ComboClick((std::string("##draw-visualization/")+mode).c_str());wait();
                    IM_CHECK(workspace.ExecutionState(0)==gfx::PanelState::Success);
                    IM_CHECK(workspace.SelectedImage().Width()>0);
                    const auto& image=workspace.SelectedImage();const auto center=image.Pixel(image.Width()/2,image.Height()/2);
                    if(std::string(mode)=="Solid coverage"){IM_CHECK_EQ(center[0],255);IM_CHECK(center[1]>100&&center[1]<240);IM_CHECK_EQ(center[2],0);}
                    if(std::string(mode)=="Depth pass & fail"){IM_CHECK_EQ(center[0],0);IM_CHECK_EQ(center[1],255);}
                    ctx->SetRef("//Image");
                }
                IM_CHECK_EQ(workspace.DrawVisualization(),0);
            }
            ImGui::SetWindowFocus("Pipeline");ctx->Yield(3);ctx->SetRef("//Pipeline");
            ctx->ItemClick("**/VTX");IM_CHECK_EQ(workspace.PipelineStage(),0);
            ctx->ItemClick("**/FS");IM_CHECK_EQ(workspace.PipelineStage(),6);
            IM_CHECK(ctx->ItemExists("**/View shader"));
            if (android) {
                const auto originalWidth = workspace.BaselineImage().Width();
                FrameAction(ctx, (std::string("Replay target/") + gfx::LocalReplayLabel()).c_str()); wait();
                FrameAction(ctx, "Render frame"); wait();
                IM_CHECK(workspace.Status().find("local GPU lacks a Vulkan extension") != std::string::npos);
                IM_CHECK_EQ(workspace.BaselineImage().Width(), originalWidth);
                IM_CHECK(workspace.StateFor("Frames") == gfx::PanelState::Failed);
                IM_CHECK(workspace.FrameStateFor(1) == gfx::PanelState::Failed);
                FrameAction(ctx, "Replay target/Automatic"); wait();
            }
            ctx->SetRef("//Frames"); FrameAction(ctx, "Render frame"); wait();
            IM_CHECK(workspace.Status().find("Replay artifacts:") != std::string::npos);
            IM_CHECK(workspace.LastReplayTarget().find(android ? "Android " : gfx::LocalReplayLabel()) == 0);
            IM_CHECK(workspace.StateFor("Frames") == gfx::PanelState::Success);
            IM_CHECK(workspace.FrameStateFor(1) == gfx::PanelState::Success);
            IM_CHECK(workspace.StateFor("Image") == gfx::PanelState::Success);
            ImGui::SetWindowFocus("Image"); ctx->Yield(2); ctx->SetRef("//Image");
            const auto scale = workspace.BaselineImage().Zoom();
            ctx->SetRef("//Image/image-document");
            auto* pixels = ctx->WindowInfo("//Image/image-document/pixels").Window;
            ctx->MouseMoveToPos(ImVec2(pixels->Pos.x + 80, pixels->Pos.y + 80)); ctx->MouseWheelY(2); ctx->Yield(3);
            IM_CHECK(workspace.BaselineImage().Zoom() > scale);
            ctx->SetRef(pixels->ParentWindow);
            ctx->ItemClick("sRGB"); IM_CHECK(!workspace.BaselineImage().SrgbDisplay());
            ctx->SetRef(pixels->ParentWindow);
            ctx->ItemClick("sRGB"); IM_CHECK(workspace.BaselineImage().SrgbDisplay());
            const auto originalPixel=workspace.BaselineImage().Pixel(0,0);
            ctx->ItemClick("##rotate-right");IM_CHECK_EQ(workspace.BaselineImage().Orientation().turns,1u);
            ctx->ItemClick("##rotate-left");IM_CHECK_EQ(workspace.BaselineImage().Orientation().turns,0u);
            ctx->ItemClick("Flip H");IM_CHECK(workspace.BaselineImage().Orientation().horizontal);ctx->ItemClick("Flip H");
            ctx->ItemClick("Flip V");IM_CHECK(workspace.BaselineImage().Orientation().vertical);ctx->ItemClick("Flip V");
            IM_CHECK(workspace.BaselineImage().Pixel(0,0)==originalPixel);
            IM_CHECK(std::abs(ctx->ItemInfo("Fit").RectFull.GetCenter().y - ctx->ItemInfo("##scale").RectFull.GetCenter().y) < 3);
            IM_CHECK(ctx->ItemInfo("##scale").RectFull.GetWidth()<=ImGui::GetFontSize()*14+1);
            IM_CHECK(ctx->ItemInfo("##range-bar").RectFull.GetWidth()<=ImGui::GetFontSize()*14+1);
            const auto baselinePixel = workspace.BaselineImage().Pixel(workspace.BaselineImage().Width() / 2, workspace.BaselineImage().Height() / 2);
            FrameAction(ctx, "Dump selected resources"); wait();
            IM_CHECK(workspace.Status().find("Replay artifacts:") != std::string::npos);
            IM_CHECK(!workspace.Resources().empty());
            IM_CHECK_EQ(workspace.Mesh().ScreenVertexCount(),3);
            IM_CHECK(std::abs(workspace.Mesh().ScreenVertex(0)[0])<.00001f);
            IM_CHECK(std::abs(workspace.Mesh().ScreenVertex(0)[1]+.5f)<.00001f);
            IM_CHECK(std::abs(workspace.Mesh().ScreenVertex(0)[3]-1)<.00001f);
            const auto imageResource = std::find_if(workspace.Resources().begin(), workspace.Resources().end(), [](const auto& artifact) { return artifact.file.extension() == ".bmp"; });
            IM_CHECK(imageResource != workspace.Resources().end());
            ImGui::SetWindowFocus("Resources"); ctx->Yield(2);
            ctx->SetRef("//Resources"); ctx->ItemClick("resource-tabs/Replay data");
            ctx->ItemClick(("**/" + imageResource->file.filename().u8string()).c_str()); wait();
            IM_CHECK(workspace.Status().find("Replay resource:") != std::string::npos);
            ctx->SetRef("//Image"); ctx->ItemClick("**/Frame presentation");
            const auto& event = workspace.CurrentCapture()->events[workspace.SelectedEvent()];
            const auto& stages = workspace.CurrentCapture()->pipelines.at(event.pipeline);
            auto fragment = std::find_if(stages.begin(), stages.end(), [](const auto& stage) {
                return stage.stage.find("FRAGMENT") != std::string::npos;
            });
            IM_CHECK(fragment != stages.end());
            ImGui::SetWindowFocus("Pipeline");ctx->Yield(3);ctx->SetRef("//Pipeline");
            ctx->ItemClick("**/FS");ctx->ItemClick("**/View shader");wait();
            IM_CHECK(workspace.Editor().GetText().find("#version") != std::string::npos);
            IM_CHECK(!ImGui::FindWindowByName("Shader")->Hidden);
            auto* code = EditorWindow(); IM_CHECK(code != nullptr);
            IM_CHECK(code->Size.x > ImGui::FindWindowByName("Shader")->InnerRect.GetWidth() - 40);
            ImGuiWindow* references = nullptr;
            for (auto* candidate : GImGui->Windows) if (candidate->Active && std::string(candidate->Name).rfind("Shader/pipeline-uses_",0)==0) references=candidate;
            IM_CHECK(references!=nullptr); IM_CHECK(references->Pos.y>=code->Pos.y+code->Size.y-2);
            const auto beforeJump = workspace.Editor().GetText();
            ctx->SetRef("//Shader"); ctx->ItemClick(("**/" + std::to_string(event.pipeline)).c_str()); ctx->Yield(2);
            IM_CHECK_EQ(workspace.Editor().GetText(), beforeJump);
            IM_CHECK_EQ(workspace.InspectedPipeline(), event.pipeline);
            ImGui::SetWindowFocus("Shader"); ctx->Yield(2);
            ctx->SetRef("//Shader"); ctx->ComboClick("##representation/SPIR-V assembly");
            IM_CHECK(workspace.Editor().GetText().find("OpEntryPoint") != std::string::npos);
            ctx->ComboClick("##representation/Debug metadata");
            IM_CHECK(workspace.Editor().GetText().find("binary_sha256") != std::string::npos);
            IM_CHECK(workspace.Editor().IsReadOnly());
            ctx->ComboClick("##representation/Reconstructed GLSL");
            ctx->SetRef("//Shader"); ctx->ItemClick("Edit");
            auto* window = EditorWindow();
            IM_CHECK(window != nullptr);
            ctx->MouseMoveToPos(ImVec2(window->Pos.x + 120, window->Pos.y + 50));
            ctx->MouseClick(ImGuiMouseButton_Left);
            ctx->KeyPress(kShortcutModifier | ImGuiKey_A);
            const char* lines[] = {"#version 450", "layout(location=0) in vec3 fragColor;", "layout(location=0) out vec4 outColor;",
                "void main(){outColor=vec4(1.0,0.0,1.0,1.0)+vec4(fragColor*0.0,0.0);}"};
            for (auto line : lines) { ctx->KeyChars(line); ctx->KeyPress(ImGuiKey_Enter); }
            ctx->SetRef("//Shader"); ctx->ItemClick("Compile"); wait();
            IM_CHECK(workspace.Status().find("Candidate compiled and validated:") != std::string::npos);
            ctx->ItemClick("Apply"); wait();
            IM_CHECK(workspace.Status().find("Baseline/modified replay complete") != std::string::npos);
            const auto& image = workspace.ModifiedImage();
            IM_CHECK(image.Width() > 0 && image.Height() > 0);
            const auto pixel = image.Pixel(image.Width() / 2, image.Height() / 2);
            IM_CHECK_EQ(pixel[0], 255); IM_CHECK_EQ(pixel[1], 0); IM_CHECK_EQ(pixel[2], 255);
            if (const char* screenshot = std::getenv("GFXRECON_GUI_SCREENSHOT")) {
                ShaderAction(ctx, "Top");
                ctx->Yield(3);
                const auto* viewport = ImGui::GetMainViewport();
                ctx->CaptureReset();
                ctx->CaptureArgs->InCaptureRect = ImRect(viewport->Pos,
                    ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y));
                std::snprintf(ctx->CaptureArgs->InOutputFile, sizeof(ctx->CaptureArgs->InOutputFile), "%s", screenshot);
                IM_CHECK(ctx->CaptureScreenshot());
                IM_CHECK(std::filesystem::is_regular_file(std::filesystem::u8path(screenshot)));
            }
            ctx->SetRef("//Frames"); FrameAction(ctx, "Render frame"); wait();
            IM_CHECK(workspace.BaselineImage().Pixel(workspace.BaselineImage().Width() / 2, workspace.BaselineImage().Height() / 2) == pixel);
            // A compile failure must leave the last working override and image intact.
            ImGui::SetWindowFocus("Shader"); ctx->Yield(2);
            window = EditorWindow();
            ctx->MouseMoveToPos(ImVec2(window->Pos.x + 120, window->Pos.y + 50)); ctx->MouseClick(ImGuiMouseButton_Left);
            ctx->KeyPress(kShortcutModifier | ImGuiKey_A); ctx->KeyChars("#version 450\nvoid main( syntax error");
            ctx->SetRef("//Shader"); ctx->ItemClick("Compile"); wait();
            IM_CHECK(workspace.Status().find("ERROR") != std::string::npos);
            IM_CHECK(workspace.StateFor("Shader") == gfx::PanelState::Failed);
            IM_CHECK((ctx->ItemInfo("Apply").ItemFlags & ImGuiItemFlags_Disabled) != 0);
            IM_CHECK(workspace.BaselineImage().Pixel(workspace.BaselineImage().Width() / 2, workspace.BaselineImage().Height() / 2) == pixel);
            ctx->SetRef("//Frames"); FrameAction(ctx, "Render frame");
            workspace.SelectExecution(workspace.CurrentCapture()->executions.size() - 1); wait();
            IM_CHECK(workspace.Status().find("previous selection") != std::string::npos);
            IM_CHECK(workspace.BaselineImage().Pixel(workspace.BaselineImage().Width() / 2, workspace.BaselineImage().Height() / 2) == pixel);
            workspace.SelectExecution(0);
            ImGui::SetWindowFocus("Shader");ctx->Yield(3);ctx->SetRef("//Shader");
#ifdef _WIN32
            {
                const auto authored=workspace.Editor().GetText();
                _putenv_s("GFXRECON_GUI_TEST_RESET_FAILURE","1");
                struct ClearResetFailure{~ClearResetFailure(){_putenv_s("GFXRECON_GUI_TEST_RESET_FAILURE","");}} clearResetFailure;
                ctx->ItemClick("Reset shader");wait();IM_CHECK(workspace.Status().find("Simulated reset replay failure")!=std::string::npos);
                IM_CHECK_EQ(workspace.Editor().GetText(),authored);IM_CHECK(workspace.HasUnsavedEdits());
                IM_CHECK(workspace.BaselineImage().Pixel(workspace.BaselineImage().Width()/2,workspace.BaselineImage().Height()/2)==pixel);
            }
#endif
            ImGui::SetWindowFocus("Shader");ctx->Yield(3);ctx->SetRef("//Shader");ctx->ItemClick("Reset shader");wait();
            IM_CHECK(workspace.Status().find("Shader reset to captured original and replayed") != std::string::npos);
            IM_CHECK(workspace.BaselineImage().Pixel(workspace.BaselineImage().Width() / 2, workspace.BaselineImage().Height() / 2) == baselinePixel);
            IM_CHECK(!workspace.HasUnsavedEdits());IM_CHECK(workspace.Editor().IsReadOnly());IM_CHECK(!workspace.Editor().CanUndo());IM_CHECK(!workspace.Editor().CanRedo());IM_CHECK_EQ(workspace.ModifiedImage().Width(),0);
            ImGui::SetWindowFocus("Shader");ctx->Yield(3);ctx->SetRef("//Shader");
            IM_CHECK((ctx->ItemInfo("Undo").ItemFlags&ImGuiItemFlags_Disabled)!=0);IM_CHECK((ctx->ItemInfo("Redo").ItemFlags&ImGuiItemFlags_Disabled)!=0);IM_CHECK((ctx->ItemInfo("Apply").ItemFlags&ImGuiItemFlags_Disabled)!=0);
            ReviewScreenshot(ctx,"shader-reset");
        };
        if (desktopFixture && androidFixture) {
            auto* androidTest = IM_REGISTER_TEST(engine, "integration", "Android capture automatic target render dump edit and revert");
            androidTest->UserData = app; androidTest->ArgVariant = 1; androidTest->TestFunc = t->TestFunc;
        }
        if (captureDeviceFixture && (desktopFixture || androidFixture)) {
            auto* captureTest = IM_REGISTER_TEST(engine, "integration", "Android GUI capture inspect render dump edit and revert");
            captureTest->UserData = app; captureTest->ArgVariant = 2; captureTest->TestFunc = t->TestFunc;
        }
        if (windowsCaptureFixture && (desktopFixture || androidFixture || captureDeviceFixture)) {
            auto* captureTest = IM_REGISTER_TEST(engine, "integration", "Windows GUI capture inspect render dump edit and revert");
            captureTest->UserData = app; captureTest->ArgVariant = 3; captureTest->TestFunc = t->TestFunc;
        }
    }

    if(std::getenv("GFXRECON_GUI_MESH_CAPTURE")) {
        t=IM_REGISTER_TEST(engine,"integration","indexed input geometry and post-VS uniform replay preview");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& workspace=static_cast<App*>(ctx->Test->UserData)->Workspace();
            workspace.SetReplayMode(gfx::ReplayMode::Local);workspace.Open(std::getenv("GFXRECON_GUI_MESH_CAPTURE"));WaitForWorkspace(ctx,workspace);
            IM_CHECK(workspace.CurrentCapture()!=nullptr);workspace.SelectExecution(0);
            ImGui::SetWindowFocus("Pipeline");ctx->Yield(3);ctx->SetRef("//Pipeline");ctx->ItemClick("**/VTX");ctx->ItemClick("**/Mesh");WaitForWorkspace(ctx,workspace);
            IM_CHECK_EQ(workspace.Mesh().InputVertexCount(),36);
            IM_CHECK_EQ(workspace.Mesh().ScreenVertexCount(),36);
            const auto clip=workspace.Mesh().ScreenVertex(0);
            IM_CHECK(std::abs(clip[0]+.65f)<.00001f);IM_CHECK(std::abs(clip[1]+.6f)<.00001f);
            IM_CHECK(std::abs(clip[2]-.3f)<.00001f);IM_CHECK(std::abs(clip[3]-.75f)<.00001f);
            ImGui::SetWindowFocus("Mesh");ctx->Yield(3);ctx->SetRef("//Mesh");
            ctx->ItemClick("mesh-views/Preview");ctx->Yield(2);
            IM_CHECK(workspace.Mesh().PreviewCenter()[2]>40);
            const auto fitted=workspace.Mesh().PreviewCenter();auto* meshWindow=ImGui::FindWindowByName("Mesh");const auto center=meshWindow->InnerRect.GetCenter();
            ctx->MouseMoveToPos(center);ctx->MouseDown(ImGuiMouseButton_Left);ctx->MouseMoveToPos(ImVec2(center.x+50,center.y+20));ctx->MouseUp(ImGuiMouseButton_Left);ctx->Yield(3);
            IM_CHECK(workspace.Mesh().PreviewCenter()!=fitted);ctx->ItemClick("Fit");ctx->Yield(3);IM_CHECK(workspace.Mesh().PreviewCenter()==fitted);
            ctx->ComboClick("##mesh-mode/Wireframe");ctx->ComboClick("##mesh-space/Screen (post-VS)");ctx->Yield(3);
            ctx->ComboClick("##mesh-space/Local (input)");ctx->ComboClick("##mesh-mode/Solid");
            if(const auto screenshot=std::getenv("GFXRECON_GUI_MESH_SCREENSHOT")){
                ctx->Yield(3);const auto* viewport=ImGui::GetMainViewport();ctx->CaptureReset();
                ctx->CaptureArgs->InCaptureRect=ImRect(viewport->Pos,ImVec2(viewport->Pos.x+viewport->Size.x,viewport->Pos.y+viewport->Size.y));
                std::snprintf(ctx->CaptureArgs->InOutputFile,sizeof(ctx->CaptureArgs->InOutputFile),"%s",screenshot);IM_CHECK(ctx->CaptureScreenshot());
                ctx->ComboClick("##mesh-space/Screen (post-VS)");ctx->ComboClick("##mesh-mode/Wireframe");ctx->Yield(3);
                const auto base=std::filesystem::u8path(screenshot);const auto wire=base.parent_path()/(base.stem().u8string()+"-wire.png");
                std::snprintf(ctx->CaptureArgs->InOutputFile,sizeof(ctx->CaptureArgs->InOutputFile),"%s",wire.u8string().c_str());IM_CHECK(ctx->CaptureScreenshot());
            }
        };
    }

    if(desktopFixture){
        t=IM_REGISTER_TEST(engine,"integration","Light draw replay fetches missing views and Full preloads supported views");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();WaitForWorkspace(ctx,w);
            const bool originalPolicy=w.FullDrawReplay();
            struct Restore{gfx::Workspace& w;bool full;~Restore(){w.SetFullDrawReplay(full);}} restore{w,originalPolicy};
            w.SetFullDrawReplay(false);w.SetReplayMode(gfx::ReplayMode::Local);
            w.Open(std::getenv("GFXRECON_GUI_TEST_CAPTURE"));WaitForWorkspace(ctx,w);
            const auto settings=std::filesystem::u8path(ExeDirectory())/"test-state";
            auto readPreference=[&](){std::ifstream file(settings/"gfxrecon_preferences.json");return gfx::Json::parse(file).at("full_draw_replay").get<bool>();};
            auto replayCount=[&](){size_t n=0;for(const auto& f:std::filesystem::recursive_directory_iterator(settings/"sessions"))if(f.is_regular_file()&&f.path().filename()=="manifest.json"){
                std::ifstream in(f.path());const auto j=gfx::Json::parse(in);const auto identity=j.value("request_identity",gfx::Json::object());
                if(!identity.value("dump",gfx::Json{}).is_null())++n;
            }return n;};
            IM_CHECK(!readPreference());const auto before=replayCount();
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);
            IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);IM_CHECK_EQ(w.CachedDrawModeCount(),1u);IM_CHECK_EQ(replayCount(),before+1);
            w.SetDrawVisualization(2);
            for(int i=0;i<300&&!w.Busy();++i){ctx->Yield();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            IM_CHECK(w.Busy());w.SetDrawVisualization(5);WaitForWorkspace(ctx,w);
            IM_CHECK_EQ(w.DrawVisualization(),5);IM_CHECK_EQ(w.CachedDrawModeCount(),4u);IM_CHECK_EQ(replayCount(),before+4);
            w.SetDrawVisualization(0);IM_CHECK(!w.Busy()&&!w.HasPendingWork());
            w.SetDrawVisualization(5);IM_CHECK(!w.Busy()&&!w.HasPendingWork());IM_CHECK_EQ(replayCount(),before+4);
            ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");ctx->ComboClick("Draw replay/Full (all views)");
            IM_CHECK(w.FullDrawReplay());IM_CHECK(readPreference());ctx->ItemClick("Close");
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);
            IM_CHECK(w.CachedDrawModeCount()>=5u);IM_CHECK_EQ(replayCount(),before+9);
            ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");ctx->ComboClick("Draw replay/Light (on demand)");
            IM_CHECK(!w.FullDrawReplay());IM_CHECK(!readPreference());ctx->ItemClick("Close");
        };
    }
    if(const auto indirect=std::getenv("GFXRECON_GUI_INDIRECT_CAPTURE")) {
        t=IM_REGISTER_TEST(engine,"integration","native indirect batch visualization and latest subdraw selection");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();
            w.SetReplayMode(gfx::ReplayMode::Local);w.Open(std::getenv("GFXRECON_GUI_INDIRECT_CAPTURE"));WaitForWorkspace(ctx,w);
            w.SelectExecution(0);WaitForWorkspace(ctx,w);ImGui::SetWindowFocus("Image");ctx->Yield(3);ctx->SetRef("//Image");
            IM_CHECK(ctx->ItemExists("##indirect-subdraw"));
            auto orange=[&](){const auto& image=w.SelectedImage();size_t count=0;for(int y=0;y<image.Height();++y)for(int x=0;x<image.Width();++x){const auto p=image.Pixel(x,y);if(p[0]>250&&p[1]>120&&p[1]<240&&p[2]<5)++count;}return count;};
            ctx->ComboClick("##draw-visualization/Solid coverage");WaitForWorkspace(ctx,w);IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);
            const auto all=orange();IM_CHECK(all>10000);size_t sum=0;
            for(int i=0;i<3;++i){ctx->SetRef("//Image");ctx->ComboClick(("##indirect-subdraw/###subdraw-"+std::to_string(i)).c_str());WaitForWorkspace(ctx,w);
                IM_CHECK_EQ(w.SelectedSubdraw(),i);IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);const auto count=orange();IM_CHECK(count>10000);IM_CHECK(count<all);sum+=count;}
            IM_CHECK_EQ(sum,all);
            w.SetDrawVisualization(1,0);w.SetDrawVisualization(2,1);w.SetDrawVisualization(1,2);WaitForWorkspace(ctx,w);
            IM_CHECK_EQ(w.SelectedSubdraw(),2);IM_CHECK_EQ(w.DrawVisualization(),1);IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);IM_CHECK(orange()<all);
            ReviewScreenshot(ctx,"indirect-draw-visualization");
            ctx->SetRef("//Image");ctx->ComboClick("##indirect-subdraw/All 3 draws");WaitForWorkspace(ctx,w);IM_CHECK_EQ(orange(),all);
            ctx->ComboClick("##draw-visualization/Original");WaitForWorkspace(ctx,w);IM_CHECK_EQ(orange(),0u);
            ctx->ComboClick("##draw-visualization/Orange outline");WaitForWorkspace(ctx,w);IM_CHECK(orange()>0);IM_CHECK(orange()<all/5);IM_CHECK(!w.Busy());ReviewScreenshot(ctx,"orange-outline");
        };
    }

    if (desktopFixture) {
        t=IM_REGISTER_TEST(engine,"integration","latest draw wins and shader preempts slow preview");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();
            w.Open(std::getenv("GFXRECON_GUI_TEST_CAPTURE"));WaitForWorkspace(ctx,w);
            IM_CHECK(w.CurrentCapture()->executions.size()>=3);
            ImGui::SetWindowFocus("Image");ctx->Yield(3);
            w.SelectExecution(0,true);w.SelectExecution(1,true);w.SelectExecution(2,true);
            IM_CHECK_EQ(w.SelectedExecution(),2);WaitForWorkspace(ctx,w);
            IM_CHECK_EQ(w.DisplayedExecution(),2);IM_CHECK(w.BaselineImage().Width()>0);
            ctx->Yield(30);IM_CHECK(!w.Busy());IM_CHECK(!w.HasPendingWork());
#ifdef _WIN32
            const auto replay=w.ConfiguredTools().at("replay");
            const auto helper=std::filesystem::u8path(gfx::NativeArguments()[0]).parent_path()/"gfxrecon_core_tests.exe";
            IM_CHECK(std::filesystem::is_regular_file(helper));
            _putenv_s("GFXRECON_GUI_TEST_SLOW_REPLAY","1");
            struct RestoreEnvironment {~RestoreEnvironment(){_putenv_s("GFXRECON_GUI_TEST_SLOW_REPLAY","");}} restore;
            w.RequestTools();ctx->Yield(2);ctx->SetRef("//Tools and devices");ctx->ItemOpen("Tool configuration");
            ctx->ItemInputValue("replay",helper.u8string().c_str());ctx->ItemClick("Close");
            w.SetReplayMode(gfx::ReplayMode::Local);w.SelectFrame(1);ctx->Yield(3);IM_CHECK(w.Busy());
            const auto started=std::chrono::steady_clock::now();
            w.LoadShader(w.CurrentCapture()->pipelines.begin()->second.front());
            WaitForWorkspace(ctx,w);
            IM_CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<4);
            IM_CHECK(!w.Editor().GetText().empty());
            w.RequestTools();ctx->Yield(2);ctx->SetRef("//Tools and devices");ctx->ItemOpen("Tool configuration");
            ctx->ItemInputValue("replay",replay.c_str());ctx->ItemClick("Close");
            w.ConfigureReplayFixture(helper.u8string());w.SelectExecution(0,true);
            for(int i=0;i<300&&!w.Busy();++i){ctx->Yield();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            IM_CHECK(w.Busy());
            ImGui::SetWindowFocus("Pipeline");ctx->Yield(4);
            IM_CHECK(w.Refreshing("Pipeline")&&w.Refreshing("Image")&&w.Refreshing("Shader")&&w.Refreshing("Mesh")&&w.Refreshing("Resources"));
            IM_CHECK(!w.Refreshing("Events")&&!w.Refreshing("Console")&&!w.Refreshing("Frames"));
            ctx->SetRef("//Pipeline");IM_CHECK(ctx->ItemExists("**/##block-input"));
            const int stageBefore=w.PipelineStage();ctx->MouseMoveToPos(ctx->ItemInfo("**/VS").RectFull.GetCenter());ctx->MouseClick();IM_CHECK_EQ(w.PipelineStage(),stageBefore);
            ReviewScreenshot(ctx,"refresh-overlay");
            w.SelectExecution(1,true);w.SelectExecution(2,true);
            IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Inactive);IM_CHECK_EQ(w.SelectedExecution(),2);
            w.ConfigureReplayFixture(replay);IM_CHECK(w.ConfiguredTools().at("replay")==replay);
            const auto supersededAt=std::chrono::steady_clock::now();w.SelectExecution(2,true);WaitForWorkspace(ctx,w);
            IM_CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now()-supersededAt).count()<6);
            IM_CHECK(w.ExecutionState(2)==gfx::PanelState::Success);IM_CHECK_EQ(w.DisplayedExecution(),2);
            IM_CHECK(!w.Refreshing("Pipeline")&&!w.Refreshing("Image"));
            w.ConfigureReplayFixture((std::filesystem::temp_directory_path()/"gfxrecon-missing-backend.exe").u8string());w.SelectExecution(2,true);WaitForWorkspace(ctx,w);
            IM_CHECK(w.ExecutionState(2)==gfx::PanelState::Failed);IM_CHECK(!w.Refreshing("Pipeline")&&!w.Refreshing("Image"));
            w.ConfigureReplayFixture(replay);w.SelectExecution(2,true);WaitForWorkspace(ctx,w);IM_CHECK(w.ExecutionState(2)==gfx::PanelState::Success);
#endif
        };
    }
    if(desktopFixture){
        t=IM_REGISTER_TEST(engine,"integration","locked replay notifies and retries without discarding capture");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();w.SetReplayMode(gfx::ReplayMode::Local);w.Open(std::getenv("GFXRECON_GUI_TEST_CAPTURE"));WaitForWorkspace(ctx,w);
#ifdef _WIN32
            _putenv_s("GFXRECON_GUI_TEST_LOCKED_REPLAY","1");
            struct RestoreLockFixture{~RestoreLockFixture(){_putenv_s("GFXRECON_GUI_TEST_LOCKED_REPLAY","");}} restore;
            FrameAction(ctx,"Render frame");WaitForWorkspace(ctx,w);
            ctx->SetRef("//Unlock Android device");IM_CHECK(ctx->ItemExists("Retry replay"));IM_CHECK(ctx->ItemExists("Continue inspecting"));
            IM_CHECK(w.Status().find("test-phone")!=std::string::npos);IM_CHECK(w.FrameStateFor(1)==gfx::PanelState::Failed);IM_CHECK(!w.Refreshing("Image"));
            ctx->Yield(3);const auto width=ImGui::FindWindowByName("Unlock Android device")->Size.x;ctx->Yield(30);
            IM_CHECK(std::abs(ImGui::FindWindowByName("Unlock Android device")->Size.x-width)<1);
            ReviewScreenshot(ctx,"phone-unlock-notice");ctx->ItemClick("Continue inspecting");ctx->Yield(3);
            IM_CHECK(w.CurrentCapture()!=nullptr);FrameAction(ctx,"Render frame");WaitForWorkspace(ctx,w);
            _putenv_s("GFXRECON_GUI_TEST_LOCKED_REPLAY","");ctx->SetRef("//Unlock Android device");ctx->ItemClick("Retry replay");WaitForWorkspace(ctx,w);
            IM_CHECK(w.FrameStateFor(1)==gfx::PanelState::Success);IM_CHECK(w.BaselineImage().Width()>0);
#endif
        };
    }
    if(desktopFixture&&std::getenv("GFXRECON_GUI_VALIDATE_FIXTURE")) {
        t=IM_REGISTER_TEST(engine,"integration","fresh replay enables Khronos validation and reports diagnostics");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();w.Open(std::getenv("GFXRECON_GUI_TEST_CAPTURE"));WaitForWorkspace(ctx,w);
            FrameAction(ctx,"Vulkan validation");WaitForWorkspace(ctx,w);
            IM_CHECK(w.ValidationReport().is_object());IM_CHECK(w.ValidationReport().value("enabled",false));
            ImGui::SetWindowFocus("Resources");ctx->Yield(3);ctx->SetRef("//Resources");ctx->ItemClick("resource-tabs/Validation");
            IM_CHECK(ctx->ItemExists("resource-tabs/Validation"));FrameAction(ctx,"Vulkan validation");WaitForWorkspace(ctx,w);
        };
    }
    if(desktopFixture) {
        t=IM_REGISTER_TEST(engine,"integration","capture Save As preserves original and updates filename");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();
            const auto folder=std::filesystem::temp_directory_path()/("gfx-save-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(folder);
            const auto original=folder/"source.gfxr";std::filesystem::copy_file(std::filesystem::u8path(std::getenv("GFXRECON_GUI_TEST_CAPTURE")),original);
            w.Open(original.u8string());WaitForWorkspace(ctx,w);const auto hash=gfx::Sha256File(original);
            w.SelectExecution(1);w.LoadShader(w.CurrentCapture()->pipelines.begin()->second.front());WaitForWorkspace(ctx,w);
            const auto selected=w.SelectedEvent();const auto shaderText=w.Editor().GetText();
            tpl::SetSaveDialogOverrideForTests(CaptureSaveDialog);
            struct ResetDialog{~ResetDialog(){tpl::SetSaveDialogOverrideForTests(nullptr);}} reset;
            captureSaveChoice.reset();ctx->MenuClick("//##MainMenuBar/File/Save capture as...");ctx->Yield(2);
            IM_CHECK(w.CurrentCapture()->source==original);IM_CHECK(!w.Busy());
            IM_CHECK(captureSaveDefaultFolder==original.parent_path().u8string());IM_CHECK(captureSaveDefaultName==original.filename().u8string());
            const auto saved=folder/std::filesystem::u8path(u8"renamed capture 中文.gfxr");captureSaveChoice=saved.u8string();
            ctx->MenuClick("//##MainMenuBar/File/Save capture as...");WaitForWorkspace(ctx,w);
            IM_CHECK(w.CurrentCapture()->source==saved);IM_CHECK(gfx::Sha256File(saved)==hash);IM_CHECK(gfx::Sha256File(original)==hash);
            IM_CHECK_EQ(w.SelectedEvent(),selected);IM_CHECK(w.Editor().GetText()==shaderText);
            FrameAction(ctx,"Revert and render baseline");WaitForWorkspace(ctx,w);
            IM_CHECK(w.Status().find("Replay artifacts:")!=std::string::npos);IM_CHECK(w.CurrentCapture()->source==saved);
#ifdef IMGUI_TEMPLATE_OSLAYER_SDL3
            const auto windowId=static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle));
            IM_CHECK(std::string(SDL_GetWindowTitle(SDL_GetWindowFromID(windowId))).find(saved.filename().u8string())!=std::string::npos);
#endif
            captureSaveChoice=(folder/"invalid.txt").u8string();ctx->MenuClick("//##MainMenuBar/File/Save capture as...");WaitForWorkspace(ctx,w);
            IM_CHECK(w.CurrentCapture()->source==saved);IM_CHECK(w.Status().find(".gfxr extension")!=std::string::npos);
            IM_CHECK(!std::filesystem::exists(folder/"invalid.txt"));IM_CHECK(gfx::Sha256File(original)==hash);
            w.Open(std::getenv("GFXRECON_GUI_TEST_CAPTURE"));WaitForWorkspace(ctx,w);
            IM_CHECK(!std::filesystem::is_symlink(folder)&&std::filesystem::equivalent(folder.parent_path(),std::filesystem::temp_directory_path()));std::filesystem::remove_all(folder);
        };
    }
    if(std::getenv("GFXRECON_GUI_LIVE_GAME")){
        t=IM_REGISTER_TEST(engine,"integration","on-demand game session captures without quitting and opens offline");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();
            struct Cleanup {
                gfx::Workspace& workspace;
                ~Cleanup() {
                    workspace.EndCaptureSession(true);
                    for (unsigned attempt=0;attempt<100;++attempt) {
                        const auto state=workspace.LiveState();
                        if (!state || !state->gameRunning) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
            } cleanup{w};
            ctx->MenuClick("//##MainMenuBar/File/Capture application...");ctx->SetRef("//Capture application");ctx->ItemClick("Local Windows");
            ctx->ItemInputValue("Executable",std::getenv("GFXRECON_GUI_LIVE_GAME"));ctx->ItemClick("Next");ctx->ItemClick("Next");
            ctx->ItemInputValue("Working directory",std::getenv("GFXRECON_GUI_LIVE_WORKDIR"));ctx->ItemInputValue("Arguments","");
            ctx->ComboClick("Capture timing/On demand: N frames");ctx->ItemInputValue("Frames per capture",3);ctx->ItemClick("Launch armed");
            IM_CHECK(WaitLive(ctx,w,[](const auto&s){return s.phase==gfx::LivePhase::Armed;}));
            IM_CHECK(w.LiveState()->captures.empty());ctx->ItemClick("Capture N frames");
            IM_CHECK(WaitLive(ctx,w,[](const auto&s){return s.phase==gfx::LivePhase::Armed&&s.captures.size()==1;}));IM_CHECK(w.LiveState()->gameRunning);
            const auto defaultCapture=w.LiveState()->captures.front();const auto defaultHash=gfx::Sha256File(defaultCapture);
            ctx->ItemClick("**/Save");WaitForWorkspace(ctx,w);IM_CHECK(ctx->ItemExists("**/Saved"));
            IM_CHECK(gfx::Sha256File(defaultCapture)==defaultHash);IM_CHECK(w.LiveState()->gameRunning);
            ctx->ItemClick("**/Open");WaitForWorkspace(ctx,w);IM_CHECK_EQ(w.CurrentCapture()->frames,3u);IM_CHECK_EQ(w.BaselineImage().Width(),0);
            ctx->MenuClick("//##MainMenuBar/File/Capture application...");ctx->SetRef("//Capture application");ctx->ItemClick("End session (keep game running)");
            IM_CHECK(WaitLive(ctx,w,[](const auto&s){return s.phase==gfx::LivePhase::Ended;}));IM_CHECK(w.LiveState()->gameRunning);
            ctx->ItemClick("Close launched game");IM_CHECK(WaitLive(ctx,w,[](const auto&s){return !s.gameRunning;}));
            ctx->ItemClick("New session");ctx->ItemClick("Next");ctx->ItemClick("Next");ctx->ComboClick("Capture timing/On demand: start-stop");ctx->ItemClick("Launch armed");
            IM_CHECK(WaitLive(ctx,w,[](const auto&s){return s.phase==gfx::LivePhase::Armed;}));ctx->ItemClick("Start recording");
            IM_CHECK(WaitLive(ctx,w,[](const auto&s){return s.phase==gfx::LivePhase::Recording;}));ctx->ItemClick("Stop capture");
            IM_CHECK(WaitLive(ctx,w,[](const auto&s){return s.phase==gfx::LivePhase::Armed&&!s.captures.empty();}));IM_CHECK(w.LiveState()->gameRunning);
            ctx->ItemClick("Close launched game");IM_CHECK(WaitLive(ctx,w,[](const auto&s){return !s.gameRunning;}));ctx->ItemClick("Hide controls");
            ctx->MenuClick("//##MainMenuBar/File/Capture application...");ctx->SetRef("//Capture application");
            IM_CHECK(ctx->ItemExists("**/Save As..."));ctx->ItemClick("Hide controls");
        };
    }

    if (std::getenv("GFXRECON_GUI_NAVIGATION_JSONL")) {
        t=IM_REGISTER_TEST(engine,"performance","large capture CPU navigation remains responsive without replay");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();
            w.Open(std::getenv("GFXRECON_GUI_NAVIGATION_JSONL"));WaitForWorkspace(ctx,w);
            IM_CHECK(w.CurrentCapture()!=nullptr);IM_CHECK(w.CurrentCapture()->events.size()>50000);
            IM_CHECK_EQ(w.ThumbnailCount(),0u);IM_CHECK(!w.HasPendingWork());
            const auto start=std::chrono::steady_clock::now();
            for(size_t index=0;index<std::min<size_t>(120,w.CurrentCapture()->executions.size());++index)w.SelectExecution(index);
            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            ctx->LogInfo("120 draw selections: %.6f seconds",elapsed);IM_CHECK(elapsed<1);
            ImGui::SetWindowFocus("Pipeline");ctx->Yield(60);
            IM_CHECK(!w.Busy());IM_CHECK(!w.HasPendingWork());IM_CHECK_EQ(w.ThumbnailCount(),0u);
        };
    }
    t = IM_REGISTER_TEST(engine, "inspection", "large GLSL editor uses custom tokenizer and read-only default");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto* app = static_cast<App*>(ctx->Test->UserData);
        const auto file = std::filesystem::temp_directory_path() / "gfxrecon-ui-large.frag";
        {
            std::ofstream out(file);
            out << "#version 450\n/* multiline\ncomment */\n";
            for (int i = 0; i < 10000; ++i) out << "vec4 value" << i << " = vec4(1.0); // shader\n";
        }
        app->Workspace().Open(file.u8string());
        WaitForWorkspace(ctx, app->Workspace());
        IM_CHECK(!app->Workspace().Busy());
        const double started = ImGui::GetTime();
        ctx->Yield(45);
        ctx->LogInfo("10,000-line editor opened and colored across 45 frames in %.3f seconds", ImGui::GetTime() - started);
        const auto& editor = app->Workspace().Editor();
        IM_CHECK(editor.IsReadOnly());
        IM_CHECK_EQ(editor.GetTotalLines(), 10004);
        IM_CHECK(editor.GetLanguageDefinition().mTokenize != nullptr);
        IM_CHECK(editor.GetLanguageDefinition().mTokenRegexStrings.empty());
        IM_CHECK(editor.GetText().find("comment */") != std::string::npos);
        ctx->SetRef("//Shader");
        ctx->ItemClick("Edit");
        IM_CHECK(!editor.IsReadOnly());
        IM_CHECK(ctx->ItemInfo("More").RectFull.Min.x>ctx->ItemInfo("Edit").RectFull.Min.x);
        IM_CHECK(ctx->ItemInfo("More").RectFull.Max.x<=ctx->ItemInfo("Undo").RectFull.Min.x);
        IM_CHECK((ctx->ItemInfo("Undo").ItemFlags&ImGuiItemFlags_Disabled)!=0);IM_CHECK((ctx->ItemInfo("Redo").ItemFlags&ImGuiItemFlags_Disabled)!=0);
        auto* window = EditorWindow();
        IM_CHECK(window != nullptr);
        ctx->MouseMoveToPos(ImVec2(window->Pos.x + 120, window->Pos.y + 50));
        ctx->MouseClick(ImGuiMouseButton_Left);
        ctx->Yield(3);
        IM_CHECK(GImGui->PlatformImeData.WantTextInput);
        IM_CHECK(GImGui->PlatformImeData.InputLineHeight>0);
        const auto parentScroll=window->ParentWindow->Scroll;const auto parentPos=window->ParentWindow->Pos;
        const auto caret=editor.GetCursorPosition();
        ctx->KeyPress(ImGuiKey_RightArrow);ctx->Yield(3);
        IM_CHECK(editor.GetCursorPosition()!=caret);
        IM_CHECK_EQ(window->ParentWindow->Scroll.x,parentScroll.x);IM_CHECK_EQ(window->ParentWindow->Scroll.y,parentScroll.y);
        IM_CHECK_EQ(window->ParentWindow->Pos.x,parentPos.x);IM_CHECK_EQ(window->ParentWindow->Pos.y,parentPos.y);
        ctx->SetRef("//Shader");ctx->ItemClick("Edit");IM_CHECK(editor.IsReadOnly());
        ctx->ItemClick("Edit");IM_CHECK(!editor.IsReadOnly());
        ctx->MouseMoveToPos(window->InnerRect.GetCenter());ctx->MouseClick(ImGuiMouseButton_Left);ctx->Yield(3);
#ifdef IMGUI_TEMPLATE_OSLAYER_SDL3
        const auto windowId=static_cast<SDL_WindowID>(reinterpret_cast<uintptr_t>(ImGui::GetMainViewport()->PlatformHandle));
        auto* native=SDL_GetWindowFromID(windowId);IM_CHECK(native!=nullptr);
        if(SDL_GetKeyboardFocus()==native)IM_CHECK(SDL_TextInputActive(native));
        ctx->KeyPress(kShortcutModifier|ImGuiKey_End);ctx->KeyPress(ImGuiKey_Enter);
        SDL_Event editing{};editing.type=SDL_EVENT_TEXT_EDITING;editing.edit.windowID=windowId;editing.edit.text="zhong";
        SDL_PushEvent(&editing);ctx->Yield(2);const auto beforeIme=editor.GetText();
        ctx->KeyPress(ImGuiKey_Backspace);ctx->KeyPress(ImGuiKey_Enter);IM_CHECK_EQ(editor.GetText(),beforeIme);
        SDL_Event committed{};committed.type=SDL_EVENT_TEXT_INPUT;committed.text.windowID=windowId;committed.text.text=u8"// native 中文 input";
        committed.common.reserved=os::kSyntheticTextTag;
        SDL_PushEvent(&committed);ctx->Yield(3);IM_CHECK(editor.GetText().find(u8"native 中文 input")!=std::string::npos);
#endif
        ctx->KeyPress(kShortcutModifier | ImGuiKey_End);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->KeyChars("/* edited");
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->KeyChars("line */");
        ctx->Yield(3);
        IM_CHECK(editor.GetText().find("/* edited\nline */") != std::string::npos);
        ctx->SetRef("//Shader");IM_CHECK((ctx->ItemInfo("Undo").ItemFlags&ImGuiItemFlags_Disabled)==0);
        const auto beforeUndo=editor.GetText();ctx->ItemClick("Undo");ctx->Yield(3);IM_CHECK(editor.GetText()!=beforeUndo);IM_CHECK((ctx->ItemInfo("Redo").ItemFlags&ImGuiItemFlags_Disabled)==0);
        ctx->ItemClick("Redo");ctx->Yield(3);IM_CHECK_EQ(editor.GetText(),beforeUndo);
        ctx->SetRef("//Shader");
        ShaderAction(ctx, "Revert text");
        IM_CHECK(editor.GetText().find("/* edited") == std::string::npos);
        std::filesystem::remove(file);
    };

    t = IM_REGISTER_TEST(engine, "inspection", "missing optional tools preserve offline inspection");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        const auto tools = workspace.ConfiguredTools();
        workspace.RequestTools(); ctx->Yield(2); ctx->SetRef("//Tools and devices");
        ctx->ItemOpen("Tool configuration");
        for (const auto* name : {"replay", "compiler", "validator", "extract", "disassembler"}) ctx->ItemInputValue(name, "");
        ctx->ItemClick("Close");
        const auto file = std::filesystem::temp_directory_path() / "gfxrecon-offline.jsonl";
        { std::ofstream out(file); out << R"({"index":1,"function":{"name":"vkCmdDraw","args":{"vertexCount":3}}})"; }
        workspace.Open(file.u8string());
        WaitForWorkspace(ctx, workspace);
        IM_CHECK_EQ(workspace.CurrentCapture()->events.size(), 1);
        IM_CHECK_EQ(workspace.CurrentCapture()->ReadEvent(0)["function"]["args"]["vertexCount"].get<int>(), 3);
        ctx->SetRef("//Frames");
        IM_CHECK(!ctx->ItemExists("Render frame"));
        if (const auto screenshot = std::getenv("GFXRECON_GUI_SCREENSHOT")) {
            ImGui::SetWindowFocus("Frames"); ctx->Yield(3);
            const auto base = std::filesystem::u8path(screenshot);
            const auto pending = base.parent_path() / (base.stem().u8string() + "-pending.png");
            const auto* viewport = ImGui::GetMainViewport(); ctx->CaptureReset();
            ctx->CaptureArgs->InCaptureRect = ImRect(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y));
            std::snprintf(ctx->CaptureArgs->InOutputFile, sizeof(ctx->CaptureArgs->InOutputFile), "%s", pending.u8string().c_str());
            IM_CHECK(ctx->CaptureScreenshot());
        }
        workspace.RequestTools(); ctx->Yield(2); ctx->SetRef("//Tools and devices");
        for (const auto* name : {"replay", "compiler", "validator", "extract", "disassembler"}) ctx->ItemInputValue(name, tools.at(name).c_str());
        ctx->ItemClick("Close"); std::filesystem::remove(file);
    };

    if (std::getenv("GFXRECON_GUI_STRESS_CAPTURE")) {
        t = IM_REGISTER_TEST(engine, "inspection", "100k import remains responsive cancels and pages cached records");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
            const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto file = std::filesystem::temp_directory_path() / ("gfxrecon-stress-" + std::to_string(token) + ".jsonl");
            {
                std::ifstream source(std::filesystem::u8path(std::getenv("GFXRECON_GUI_STRESS_CAPTURE")), std::ios::binary);
                std::ofstream copy(file, std::ios::binary);
                copy << "{\"header\":{\"test-run\":\"" << token << "\"}}\n" << source.rdbuf();
            }
            const auto previous = workspace.CurrentCapture();
            workspace.Open(file.u8string());
            const auto started = std::chrono::steady_clock::now();
            ctx->KeyPress(kShortcutModifier | ImGuiKey_P);
            ctx->SetRef("//Command Palette"); IM_CHECK(ctx->ItemExists("##filter"));
            const auto response = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            ctx->LogInfo("Import input response %.2f ms", response); IM_CHECK(response < 1000);
            ctx->KeyPress(ImGuiKey_Escape);
            IM_CHECK(workspace.Busy());
            IM_CHECK(workspace.StateFor("Events") == gfx::PanelState::Running);
            ctx->MenuClick("//##MainMenuBar/File/Cancel current operation");
            WaitForWorkspace(ctx, workspace);
            IM_CHECK(!workspace.Busy()); IM_CHECK(workspace.CurrentCapture() == previous);
            IM_CHECK(workspace.StateFor("Events") == gfx::PanelState::Inactive);
            workspace.Open(file.u8string());
            WaitForWorkspace(ctx, workspace);
            IM_CHECK(workspace.CurrentCapture() != nullptr);
            IM_CHECK_EQ(workspace.CurrentCapture()->events.size(), 100000);
            ctx->SetRef("//Events"); ToggleSubmitted(ctx);
            ctx->ItemInputValue("##event-filter", "100000");
            auto* eventWindow = ctx->WindowInfo("//Events/event-list").Window;
            ctx->SetRef(eventWindow);
            const int last = 99999;
            const auto rowId = ImHashData(&last, sizeof(last), eventWindow->ID);
            ctx->ItemClick(ImHashStr("100000  vkCmdSetViewport", 0, rowId));
            IM_CHECK_EQ(workspace.CurrentCapture()->ReadEvent(workspace.SelectedEvent())["index"].get<uint64_t>(), 100000ull);
            workspace.Open(file.u8string());
            WaitForWorkspace(ctx, workspace);
            IM_CHECK(workspace.Status().find("Loaded cached index:") != std::string::npos);
            ctx->SetRef("//Events"); ctx->ItemInputValue("##event-filter", ""); ToggleSubmitted(ctx);
            std::filesystem::remove(file);
        };
    }
}

} // namespace tpl
