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
#include "replay/perf_session.h"
#include "gui/image_viewer.h"
#include "core/image_io.h"
#ifdef IMGUI_TEMPLATE_OSLAYER_SDL3
#include <SDL3/SDL.h>
#endif

#include <imgui_te_context.h>
#include <imgui_te_engine.h>
#include <imgui_internal.h>
#include <imgui_capture_tool.h>
#include <implot.h>
#include <implot_internal.h>

#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef LoadImage
#undef Yield
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

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
std::vector<std::string> PerfLogSnapshot() {
    std::vector<std::string> result;
    for (const auto& record : Log::Instance().RecentRecords())
        if (record.category == "perf") result.push_back(record.formatted);
    return result;
}
bool PerfLogAdded(const std::vector<std::string>& before) {
    for (const auto& record : Log::Instance().RecentRecords())
        if (record.category == "perf" && std::find(before.begin(), before.end(), record.formatted) == before.end()) return true;
    return false;
}
bool PerfWarningAdded(const std::vector<std::string>& before) {
    for (const auto& record : Log::Instance().RecentRecords())
        if (record.category == "perf" && record.level == LogLevel::Warn &&
            std::find(before.begin(), before.end(), record.formatted) == before.end()) return true;
    return false;
}
std::string VisibleUiText(ImGuiTestContext* ctx) {
    ImGui::LogToBuffer(0);
    // Capture the complete frames, including tooltip windows. The default
    // logger stops and clears its buffer when the current window ends.
    GImGui->LogWindow = nullptr; ctx->Yield(2);
    const std::string text = GImGui->LogBuffer.c_str(); ImGui::LogFinish(); return text;
}
void ReviewScreenshot(ImGuiTestContext* ctx,const char* suffix){
    const auto folder=std::getenv("GFXRECON_GUI_REVIEW_DIR");if(!folder)return;
    std::filesystem::create_directories(std::filesystem::u8path(folder));ctx->Yield(4);
    const auto path=std::filesystem::u8path(folder)/(std::string(suffix)+".png");const auto* viewport=ImGui::GetMainViewport();ctx->CaptureReset();
    const int flags=std::string(suffix)=="phone-unlock-notice"||(std::string(suffix)=="settings-dialog"||std::string(suffix)=="settings-tools"||std::string(suffix)=="replay-device")||std::string(suffix).rfind("perf-popup",0)==0?ImGuiCaptureFlags_IncludePopups:0;
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
void ReplayDeviceAction(ImGuiTestContext* ctx,gfx::Workspace& workspace,const char* target){
    ctx->SetRef("//Events");ctx->ItemClick("##event-replay-device");WaitForWorkspace(ctx,workspace);
    ctx->SetRef("//Replay device");ctx->ItemClick(target);ctx->ItemClick("Close");
    FrameAction(ctx,"Render frame");
}

ImGuiID SettingsItem(ImGuiTestContext* ctx,const char* label){
    return ctx->GetID(label,ctx->WindowInfo("//Settings/settings-content").Window->ID);
}
void SettingsCombo(ImGuiTestContext* ctx,const char* value){
    ctx->SetRef(ctx->WindowInfo("//Settings/settings-content").Window);ctx->ComboClick(value);ctx->SetRef("//Settings");
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
struct ImageEncodingFixture {
    gfx::ImageViewer viewer, baseline, modified, thumbnail;
    std::filesystem::path directory;
    std::string error;
    int source = 0;
    bool initialized = false, finish = false, cleaned = false, uploadedMatches = false, loadSaved = false;
};
bool UploadedImageMatchesDisplay(const gfx::ImageViewer& image) {
    // Inspect the texture submitted by the real Draw() path. All GL calls are
    // made by GuiFunc on the application's main context, never TestFunc.
    for (auto* child : ImGui::GetCurrentWindow()->DC.ChildWindows) {
        for (const auto& command : child->DrawList->CmdBuffer) {
            if (!command.ElemCount || command.UserCallback || command.TexRef._TexData) continue;
            const auto texture = static_cast<GLuint>(command.GetTexID());
            if (!texture || !glIsTexture(texture)) continue;
            GLint previous = 0, width = 0, height = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous); glBindTexture(GL_TEXTURE_2D, texture);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
            if (width != image.Width() || height != image.Height()) { glBindTexture(GL_TEXTURE_2D, previous); continue; }
            GLint alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0;
            glGetIntegerv(GL_PACK_ALIGNMENT, &alignment); glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows); glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
            glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            std::vector<unsigned char> uploaded(static_cast<size_t>(width) * height * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, uploaded.data());
            glPixelStorei(GL_PACK_ALIGNMENT, alignment); glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
            glPixelStorei(GL_PACK_SKIP_ROWS, skipRows); glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
            glBindTexture(GL_TEXTURE_2D, previous);
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const auto displayed = image.DisplayPixel(x, y); const auto offset = (static_cast<size_t>(y) * width + x) * 4;
                if (!std::equal(displayed.begin(), displayed.end(), uploaded.begin() + offset)) return false;
            }
            return true;
        }
    }
    return false;
}
}

#if PLATFORM_MACOS
void RegisterMacTests(ImGuiTestEngine* engine);
#endif

void RegisterAppTests(ImGuiTestEngine* engine, App* app) {
    (void)app;
    ImGuiTest* t = nullptr;

    t = IM_REGISTER_TEST(engine, "image", "source encoding display range thumbnails and difference");
    t->SetVarsDataType<ImageEncodingFixture>();
    t->GuiFunc = [](ImGuiTestContext* ctx) {
        auto& vars = ctx->GetVars<ImageEncodingFixture>();
        if (vars.finish) {
            vars.viewer.Release(); vars.baseline.Release(); vars.modified.Release(); vars.thumbnail.Release();
            std::error_code error; std::filesystem::remove_all(vars.directory, error); vars.cleaned = true; return;
        }
        if (!vars.initialized) {
            vars.initialized = true;
            try {
                vars.directory = std::filesystem::temp_directory_path() / ("gfx-image-encoding-gui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                std::filesystem::create_directories(vars.directory);
                const gfx::Image linear{2, 1, {128,128,128,37, 192,192,192,211}};
                const gfx::Image encoded{2, 1, {188,188,188,37, 224,224,224,211}};
                const gfx::Image modified{2, 1, {64,64,64,11, 128,128,128,29}};
                const gfx::Image depth{2, 1, {64,64,64,255, 191,191,191,255}};
                gfx::WriteBmp(vars.directory / "linear.bmp", linear); gfx::WritePng(vars.directory / "linear.png", linear);
                gfx::WriteBmp(vars.directory / "encoded.bmp", encoded); gfx::WritePng(vars.directory / "encoded.png", encoded);
                gfx::WritePng(vars.directory / "modified.png", modified); gfx::WriteBmp(vars.directory / "depth.bmp", depth);
                { const float values[]{0.25f, 0.75f}; std::ofstream sidecar(vars.directory / "depth.bmp.depth.bin", std::ios::binary);
                  sidecar.write(reinterpret_cast<const char*>(values), sizeof(values)); }
                vars.baseline.LoadPng(vars.directory / "linear.png", "VK_FORMAT_R16G16B16A16_SFLOAT");
                vars.modified.LoadPng(vars.directory / "modified.png", "VK_FORMAT_R16G16B16A16_SFLOAT");
                vars.viewer.LoadBmp(vars.directory / "linear.bmp", "COLOR", "VK_FORMAT_R16G16B16A16_SFLOAT");
            } catch (const std::exception& error) { vars.error = error.what(); }
        }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::min(900.0f, viewport->WorkSize.x - 40), std::min(520.0f, viewport->WorkSize.y - 40)), ImGuiCond_Always);
        ImGui::Begin("Image encoding fixture", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
        try {
            if (vars.loadSaved) {
                vars.loadSaved = false;
                vars.viewer.LoadBmp(std::filesystem::u8path(std::getenv("GFXRECON_GUI_IMAGE_PREVIEW")), "COLOR", "VK_FORMAT_B10G11R11_UFLOAT_PACK32");
            }
            ImGui::SetNextItemWidth(160);
            if (ImGui::Combo("Source", &vars.source, "Linear BMP\0Linear PNG\0sRGB PNG\0Presentation\0Depth\0Difference\0Empty\0")) {
                switch (vars.source) {
                case 0: vars.viewer.LoadBmp(vars.directory / "linear.bmp", "COLOR", "VK_FORMAT_R16G16B16A16_SFLOAT"); break;
                case 1: vars.viewer.LoadPng(vars.directory / "linear.png", "VK_FORMAT_R16G16B16A16_SFLOAT"); break;
                case 2: vars.viewer.LoadPng(vars.directory / "encoded.png", "VK_FORMAT_R8G8B8A8_SRGB"); break;
                case 3: vars.viewer.LoadPng(vars.directory / "encoded.png"); break;
                case 4: vars.viewer.LoadBmp(vars.directory / "depth.bmp", "DEPTH", "VK_FORMAT_D32_SFLOAT"); break;
                case 5: vars.viewer.AbsoluteDifference(vars.baseline, vars.modified); break;
                case 6: vars.viewer.Release(); break;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load thumbnail")) {
                const bool encoded = vars.source == 2 || vars.source == 3;
                vars.thumbnail.LoadThumbnailBmp(vars.directory / (encoded ? "encoded.bmp" : "linear.bmp"), "COLOR",
                    encoded ? "VK_FORMAT_R8G8B8A8_SRGB" : "VK_FORMAT_R16G16B16A16_SFLOAT");
            }
            ImGui::SameLine(); if (ImGui::Button("Make thumbnail")) vars.thumbnail.MakeThumbnail(vars.viewer);
            if (vars.error.empty()) { vars.viewer.Draw(); vars.uploadedMatches = UploadedImageMatchesDisplay(vars.viewer); }
            else ImGui::TextWrapped("%s", vars.error.c_str());
        } catch (const std::exception& error) { vars.error = error.what(); }
        ImGui::End();
    };
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& vars = ctx->GetVars<ImageEncodingFixture>(); ctx->Yield(3); ctx->SetRef("//Image encoding fixture");
        IM_CHECK(vars.error.empty()); IM_CHECK(vars.viewer.SrgbDisplay());
        IM_CHECK(vars.viewer.SourceEncoding() == gfx::ImageSourceEncoding::Linear);
        IM_CHECK_EQ(vars.viewer.Pixel(0,0)[0], 128); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 188); IM_CHECK(vars.uploadedMatches);
        ctx->ItemUncheck("sRGB"); ctx->Yield(); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 128); IM_CHECK_EQ(vars.viewer.Pixel(0,0)[0], 128); IM_CHECK(vars.uploadedMatches);
        ctx->ItemCheck("sRGB"); ctx->ComboClick("Source/Linear PNG"); ctx->Yield();
        IM_CHECK_EQ(vars.viewer.Pixel(0,0)[3], 37); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[3], 37); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 188); IM_CHECK(vars.uploadedMatches);
        ctx->ItemClick("Auto"); const auto linearRange = vars.viewer.Range();
        IM_CHECK(std::abs(linearRange.minimum - 128.0f/255) < 0.00001f && std::abs(linearRange.maximum - 192.0f/255) < 0.00001f);
        ctx->ItemUncheck("sRGB"); ctx->ItemClick("Reset"); ctx->ItemClick("Auto");
        IM_CHECK(std::abs(vars.viewer.Range().minimum - linearRange.minimum) < 0.00001f && std::abs(vars.viewer.Range().maximum - linearRange.maximum) < 0.00001f);
        IM_CHECK_EQ(vars.viewer.Pixel(0,0)[0], 128); IM_CHECK_EQ(vars.viewer.Pixel(0,0)[3], 37); IM_CHECK(vars.uploadedMatches);
        ctx->ItemClick("Reset"); ctx->ItemCheck("sRGB"); ctx->ItemClick("Make thumbnail");
        IM_CHECK(vars.thumbnail.SourceEncoding() == gfx::ImageSourceEncoding::Srgb); IM_CHECK_EQ(vars.thumbnail.Pixel(0,0)[0], 188); IM_CHECK_EQ(vars.thumbnail.DisplayPixel(0,0)[0], 188);
        IM_CHECK_EQ(vars.thumbnail.DisplayPixel(0,0)[3], 37);
        ctx->ItemClick("Load thumbnail"); IM_CHECK(vars.thumbnail.SourceEncoding() == gfx::ImageSourceEncoding::Linear); IM_CHECK_EQ(vars.thumbnail.DisplayPixel(0,0)[0], 188);
        ctx->ComboClick("Source/sRGB PNG"); ctx->Yield(); IM_CHECK(vars.viewer.SourceEncoding() == gfx::ImageSourceEncoding::Srgb);
        IM_CHECK_EQ(vars.viewer.Pixel(0,0)[0], 188); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 188); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[3], 37); IM_CHECK(vars.uploadedMatches);
        ctx->ItemUncheck("sRGB"); ctx->Yield(); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 128); IM_CHECK_EQ(vars.viewer.Pixel(0,0)[0], 188); IM_CHECK(vars.uploadedMatches);
        ctx->ItemClick("Auto"); const auto encodedRange = vars.viewer.Range();
        IM_CHECK(std::abs(encodedRange.minimum - 0.5028865f) < 0.00001f && std::abs(encodedRange.maximum - 0.7454042f) < 0.00001f);
        ctx->ItemCheck("sRGB"); ctx->ItemClick("Reset"); ctx->ItemClick("Auto");
        IM_CHECK(std::abs(vars.viewer.Range().minimum - encodedRange.minimum) < 0.00001f && std::abs(vars.viewer.Range().maximum - encodedRange.maximum) < 0.00001f);
        ctx->ItemClick("Reset"); ctx->ItemClick("Load thumbnail"); IM_CHECK(vars.thumbnail.SourceEncoding() == gfx::ImageSourceEncoding::Srgb); IM_CHECK_EQ(vars.thumbnail.DisplayPixel(0,0)[0], 188);
        ctx->ComboClick("Source/Presentation"); ctx->Yield(); IM_CHECK(vars.viewer.SourceEncoding() == gfx::ImageSourceEncoding::Srgb); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 188);
        ctx->ComboClick("Source/Depth"); ctx->Yield(); IM_CHECK(vars.viewer.HasDepthValues()); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 64);
        IM_CHECK((ctx->ItemInfo("sRGB").ItemFlags & ImGuiItemFlags_Disabled) != 0); ctx->ItemClick("Auto");
        IM_CHECK(std::abs(vars.viewer.Range().minimum - 0.25f) < 0.00001f && std::abs(vars.viewer.Range().maximum - 0.75f) < 0.00001f);
        const auto baselineRaw = vars.baseline.Pixel(0,0); ctx->ComboClick("Source/Difference"); ctx->Yield();
        IM_CHECK(!vars.viewer.HasDepthValues()); IM_CHECK(vars.viewer.SourceEncoding() == gfx::ImageSourceEncoding::Linear);
        IM_CHECK_EQ(vars.viewer.Range().minimum, 0.0f); IM_CHECK_EQ(vars.viewer.Range().maximum, 1.0f);
        IM_CHECK((ctx->ItemInfo("sRGB").ItemFlags & ImGuiItemFlags_Disabled) == 0);
        IM_CHECK_EQ(vars.viewer.Pixel(0,0)[0], 64); IM_CHECK_EQ(vars.viewer.DisplayPixel(0,0)[0], 137);
        IM_CHECK(vars.baseline.Pixel(0,0) == baselineRaw); IM_CHECK(vars.uploadedMatches); IM_CHECK(vars.error.empty());
        if (std::getenv("GFXRECON_GUI_IMAGE_PREVIEW")) {
            vars.loadSaved = true; ctx->Yield(3);
            IM_CHECK(vars.error.empty()); IM_CHECK(vars.uploadedMatches);
            ctx->ItemCheck("sRGB"); ctx->Yield(); IM_CHECK(vars.uploadedMatches);
            ReviewScreenshot(ctx, "saved-attachment-srgb");
            ctx->ItemUncheck("sRGB"); ctx->Yield(); IM_CHECK(vars.uploadedMatches);
            ReviewScreenshot(ctx, "saved-attachment-linear");
        }
        ctx->ComboClick("Source/Empty"); ctx->Yield();
        for (const auto* control : {"R", "G", "B", "sRGB", "##range-min", "##range-max", "Auto", "Reset", "##rotate-left", "##rotate-right", "Flip H", "Flip V", "Fit", "##scale"})
            IM_CHECK(!ctx->ItemExists(control));
        vars.finish = true; ctx->Yield(2); IM_CHECK(vars.cleaned);
    };

    t = IM_REGISTER_TEST(engine, "performance", "toolbar dockable window and dense plots");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto* app = static_cast<App*>(ctx->Test->UserData);
        ctx->MenuClick("//##MainMenuBar/View/Reset Layout"); ctx->Yield(4);
        using gfx::Json;
        const uint64_t origin = 1000000000000ull;
        Json result{{"schema", "gfxrecon-perf/1"}, {"status", "complete"}, {"mode", "single-frame"}, {"runs_requested", 2},
            {"policy", {{"frame", 3}, {"counter_profile", "precision"}, {"execution_model", "fresh_replay_per_run"}}},
            {"metrics", Json::array()}, {"runs", Json::array()}};
        std::map<std::string, std::vector<double>> baseline, modified;
        const std::vector<int> ids{26, 132, 143, 145, 9999};
        const std::vector<std::string> names{"Full fragment ALU instructions", "Half fragment ALU instructions", "Memory read bandwidth", "Time ALUs working", "Additional vendor activity"};
        for (int variant = 0; variant != 2; ++variant) {
            Json samples = Json::array(), counters = Json::array();
            auto& values = variant ? modified : baseline;
            for (int i = 0; i < 1; ++i) {
                const auto time = origin;
                const double elapsed = 10 + std::sin(i * 0.3) + variant * 0.2;
                samples.push_back({{"frame", 3}, {"valid", true}, {"phase", "measurement"},
                    {"start_boot_ns", time}, {"end_boot_ns", time + uint64_t(elapsed * 1e6)}, {"elapsed_ms", elapsed}});
                values["gpu_time"].push_back(elapsed);
            }
            for (size_t metric = 0; metric < ids.size(); ++metric) {
                Json points = Json::array(); const auto key = "counter_" + std::to_string(ids[metric]);
                for (int i = 0; i < 3000; ++i) {
                    if (i >= 1400 && i < 1415) continue;
                    const auto time = origin + uint64_t(i) * 10000ull;
                    const double scale = metric < 3 ? 1e9 : 1;
                    const double value = (50 + 20 * std::sin(i * 0.013 + metric) + variant * 3) * scale * (i == 1901 ? 2 : 1);
                    points.push_back({{"timestamp_boot_ns", time}, {"value", value}});
                    if (time >= origin) values[key].push_back(value);
                }
                counters.push_back({{"id", ids[metric]}, {"name", names[metric]}, {"unit", metric < 2 ? "INSTRUCTION/SECOND" : metric == 2 ? "BYTE/SECOND" : "PERCENT"},
                    {"samples", points}, {"summary", {{"coverage_estimate", 0.99}, {"gap_threshold_ns", 40000}}}});
            }
            result["runs"].push_back({{"variant", variant ? "modified" : "baseline"}, {"index", 1},
                {"gpu", {{"device", {{"device_name", "UI fixture GPU"}, {"driver_name", "Fixture driver"}, {"driver_version", 1}}},
                    {"swapchain", {{"effective", "offscreen"}}}, {"samples", samples},
                    {"counter_window", {{"start_boot_ns", origin}, {"end_boot_ns", origin + 30000000ull}, {"status", "available"}}}}},
                {"counters", {{"period_ns", 10000}, {"window_begin_boot_ns", origin}, {"window_end_boot_ns", origin + 30000000ull}, {"counters", counters}}}});
        }
        const auto firstRuns = result["runs"];
        for (auto run : firstRuns) { run["index"] = 2; result["runs"].push_back(std::move(run)); }
        for (auto* group : {&baseline, &modified}) for (auto& [key, values] : *group) {
            const auto repeated = values; values.insert(values.end(), repeated.begin(), repeated.end());
        }
        for (const auto& [key, values] : baseline) {
            int id = key == "gpu_time" ? 0 : std::stoi(key.substr(8));
            auto found = std::find(ids.begin(), ids.end(), id); const auto metric = size_t(found - ids.begin());
            Json row{{"key", key}, {"name", id ? names[metric] : "GPU frame elapsed"}, {"unit", id ? metric < 2 ? "INSTRUCTION/SECOND" : metric == 2 ? "BYTE/SECOND" : "PERCENT" : "ms"},
                {"baseline", gfx::SummarizePerfValues(values)}, {"modified", gfx::SummarizePerfValues(modified[key])},
                {"status", "available"}, {"scope", id ? "system-wide whole-loop" : "isolated render interval"}};
            if (id) row["counter_id"] = id;
            result["metrics"].push_back(row);
        }
        auto& workspace = app->Workspace(); workspace.SetPerformanceFixture(std::move(result));
        // Simulate the old floating-window preference while Perf is closed.
        // Opening it must dock immediately, without a visible move/resize step.
        if(auto* saved=ImGui::FindWindowSettingsByID(ImHashStr("Perf"))){saved->DockId=0;saved->WantApply=true;}
        ctx->SetRef("//Events");
        const auto arrow = ctx->ItemInfo("Filters"); const auto perf = ctx->ItemInfo("##event-perf");
        IM_CHECK(perf.RectFull.Min.x >= arrow.RectFull.Max.x);
        ctx->ItemClick("##event-perf"); ctx->Yield(5);
        ctx->SetRef("//Perf");
        auto* perfWindow = ctx->WindowInfo("//Perf").Window;
        IM_CHECK(perfWindow != nullptr && workspace.PerformanceOpen());
        IM_CHECK((perfWindow->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoDocking)) == 0);
        IM_CHECK(perfWindow->HasCloseButton);
        IM_CHECK(perfWindow->DockIsActive&&perfWindow->DockId==ctx->WindowInfo("//Pipeline").Window->DockId);
        IM_CHECK(!ctx->ItemExists("Close"));
        IM_CHECK(ctx->ItemExists("Probe") && ctx->ItemExists("Open...") && ctx->ItemExists("Settings"));
        const auto tableId=ImHashStr("perf-metrics",0,perfWindow->ID);
        const auto* table=ImGui::TableFindByID(tableId);IM_CHECK(table!=nullptr&&table->ColumnsCount==5);
        IM_CHECK(ctx->ItemExists("**/Mean")&&ctx->ItemExists("**/Min")&&ctx->ItemExists("**/Max"));
        IM_CHECK(!ctx->ItemExists("**/Mean B")&&!ctx->ItemExists("**/Coverage / scope"));
        IM_CHECK(!ctx->ItemExists("**/Plot"));
        const auto extraRow=ImHashStr("###metric-row",0,ImHashStr("counter_9999",0,tableId));
        const auto memoryRow=ImHashStr("###metric-row",0,ImHashStr("counter_143",0,tableId));
        IM_CHECK((ctx->ItemInfo(extraRow).ItemFlags&ImGuiItemFlags_Disabled)==0);
        ctx->ItemClick(extraRow);ctx->Yield(3);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().count("counter_9999")==1);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().size()==6);
        IM_CHECK(ctx->ItemExists("##perf-metric-filter")&&!ctx->ItemExists("Filter") && !ctx->ItemExists("Warm-up") && !ctx->ItemExists("Fit"));
        const auto filterWidth=ctx->ItemInfo("##perf-metric-filter").RectFull.GetWidth();
        IM_CHECK(filterWidth>=ImGui::GetFontSize()*12&&filterWidth<ImGui::GetFontSize()*16);
        ctx->ItemInputValue("##perf-metric-filter","Memory");ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(4);
        auto* plots=ctx->WindowInfo("//Perf/perf-plots").Window;IM_CHECK(plots!=nullptr&&plots->ContentSize.y>100&&plots->ContentSize.y<200);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().size()==6);
        ctx->ItemInputValue("##perf-metric-filter","no_matching_metric");ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(4);
        IM_CHECK(ctx->ItemInfo("##perf-metric-filter").RectFull.GetWidth()==filterWidth);
        IM_CHECK(ctx->WindowInfo("//Perf/perf-plots").Window->ContentSize.y<20);
        ctx->ItemInputValue("##perf-metric-filter","Additional,Memory");ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(4);
        ctx->ItemClick(extraRow);ctx->Yield(3);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().count("counter_9999")==0&&workspace.PerformanceSelectedMetricsForTests().count("counter_143")==1);
        ctx->KeyDown(ImGuiMod_Shift);ctx->ItemClick(memoryRow);ctx->KeyUp(ImGuiMod_Shift);ctx->Yield(3);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().count("counter_143")==1&&workspace.PerformanceSelectedMetricsForTests().count("counter_9999")==1);
        ctx->ItemClick(extraRow);ctx->Yield(3);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().count("counter_9999")==0);
        ctx->ItemClick(extraRow);ctx->Yield(3);
        const auto tableMenu=[&](const char* action){ctx->ItemClick(extraRow,ImGuiMouseButton_Right);ctx->SetRef("//$FOCUSED");ctx->ItemClick(action);ctx->Yield(3);ctx->SetRef("//Perf");};
        tableMenu("Deselect all");
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().count("counter_9999")==0&&workspace.PerformanceSelectedMetricsForTests().count("counter_143")==0);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().count("gpu_time")==1);
        tableMenu("Select all");
        ctx->ItemInputValue("##perf-metric-filter","");ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(4);
        IM_CHECK(workspace.PerformanceSelectedMetricsForTests().size()==6);
        tableMenu("Deselect all");IM_CHECK(workspace.PerformanceSelectedMetricsForTests().empty());
        IM_CHECK(ctx->WindowInfo("//Perf/perf-plots").Window->ContentSize.y<20);
        tableMenu("Select all");IM_CHECK(workspace.PerformanceSelectedMetricsForTests().size()==6);
        IM_CHECK(!ctx->ItemExists("Cancel"));
        for (const auto* old : {"Probe capabilities", "Start measurement", "Cancel measurement", "Load result...", "Load Result", "Session settings", "Show warm-up samples", "Device, cadence and sampling"})
            IM_CHECK(!ctx->ItemExists(old));
        IM_CHECK((ctx->ItemInfo("Start").ItemFlags & ImGuiItemFlags_Disabled) != 0);
        ctx->MouseMove("Open...");
        IM_CHECK(VisibleUiText(ctx).find("UI fixture GPU") == std::string::npos);
        IM_CHECK(!ctx->ItemExists("##perf-device-info"));
        ctx->SetRef("//Events");
        const auto phone=ctx->ItemInfo("##event-replay-device");
        IM_CHECK(phone.RectFull.Min.x>=ctx->ItemInfo("##event-perf").RectFull.Max.x);
        ctx->MouseMoveToPos(phone.RectFull.GetCenter());ctx->Yield(60);
        IM_CHECK(VisibleUiText(ctx).find(workspace.ReplayDeviceLabel())!=std::string::npos);
        ctx->SetRef("//Perf");ctx->MouseMove("Open...");ctx->Yield(3);
        const auto graphMenu = [&] {
            ctx->SetRef("//Perf");
            ImPlotPlot* graph = nullptr;
            for (int i = 0; i < GImPlot->Plots.GetBufSize(); ++i) {
                auto* plot = GImPlot->Plots.GetByIndex(i);
                if (plot->TitleOffset >= 0 && std::string(plot->GetTitle()) == "GPU frame elapsed") { graph = plot; break; }
            }
            IM_CHECK(graph != nullptr);
            ctx->MouseSetViewport(ctx->WindowInfo("//Perf").Window);
            ctx->MouseMoveToPos(ImVec2(graph->PlotRect.Max.x - 24, graph->PlotRect.GetCenter().y));
            ctx->MouseClick(ImGuiMouseButton_Right); ctx->Yield(2);
            ctx->SetRef("//$FOCUSED");
            IM_CHECK(!ctx->ItemExists("Warm-up") && ctx->ItemExists("Fit"));
        };
        graphMenu(); ctx->ItemClick("Fit"); ctx->Yield(4); ctx->SetRef("//Perf");
        ImPlotPlot* timing=nullptr;
        for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto* plot=GImPlot->Plots.GetByIndex(i);if(plot->TitleOffset>=0&&std::string(plot->GetTitle())=="GPU frame elapsed"){timing=plot;break;}}
        IM_CHECK(timing!=nullptr);IM_CHECK(!timing->Axes[ImAxis_X1].HasLabel());
        const auto oldX=timing->Axes[ImAxis_X1].Range,oldY=timing->Axes[ImAxis_Y1].Range;
        ctx->MouseMoveToPos(timing->PlotRect.GetCenter());ctx->MouseWheelY(2);
        const auto zoomDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(timing->Axes[ImAxis_X1].Range.Size()>=oldX.Size()&&std::chrono::steady_clock::now()<zoomDeadline&&!ctx->IsError()){
            ctx->Yield();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        IM_CHECK(timing->Axes[ImAxis_X1].Range.Size()<oldX.Size());
        IM_CHECK(timing->Axes[ImAxis_Y1].Range.Min==oldY.Min&&timing->Axes[ImAxis_Y1].Range.Max==oldY.Max);
        ctx->MouseMoveToPos(timing->Axes[ImAxis_X1].HoverRect.GetCenter());ctx->Yield(4);
        IM_CHECK(VisibleUiText(ctx).find("Time from frame GPU start (milliseconds)")!=std::string::npos);
        graphMenu();ctx->ItemClick("Fit");ctx->Yield(4);ctx->SetRef("//Perf");
        IM_CHECK(VisibleUiText(ctx).find("GB/s") != std::string::npos);
        for (const auto* window : GImGui->Windows) if (window->Active)
            IM_CHECK(window->DrawList->VtxBuffer.Size < 65536);
        int checkedLegends = 0;
        IM_CHECK(GImPlot != nullptr);
        for (int i = 0; i < GImPlot->Plots.GetBufSize(); ++i) {
            auto* plot = GImPlot->Plots.GetByIndex(i);
            if (plot->TitleOffset < 0 || plot->Items.GetLegendCount() == 0) continue;
            const std::string title = plot->GetTitle();
            if (title != "GPU frame elapsed" && std::find(names.begin(), names.end(), title) == names.end()) continue;
            if(title=="Memory read bandwidth") IM_CHECK(plot->Axes[ImAxis_Y1].Range.Max < 1000);
            ++checkedLegends;
            IM_CHECK(plot->Items.GetLegendCount() == 4);
            IM_CHECK(plot->Axes[ImAxis_X1].Range.Min >= -0.01 && plot->Axes[ImAxis_X1].Range.Max > 25 && plot->Axes[ImAxis_X1].Range.Max <= 35);
            for (int entry = 0; entry < plot->Items.GetLegendCount(); ++entry) {
                const std::string label = plot->Items.GetLegendLabel(entry);
                IM_CHECK(label == "baseline 1" || label == "modified 1" || label == "baseline 2" || label == "modified 2");
            }
        }
        IM_CHECK(checkedLegends > 0);
        ReviewScreenshot(ctx, "perf-window");
        IM_CHECK(perfWindow->DockIsActive && perfWindow->DockId != 0);
        const auto dockId = perfWindow->DockId;
        ctx->WindowClose("//Perf"); ctx->Yield(3);
        IM_CHECK(!workspace.PerformanceOpen());
        workspace.OpenPerformance(); ctx->Yield(3);
        IM_CHECK(workspace.PerformanceResult().at("metrics").size() == 6);
        IM_CHECK(workspace.PerformanceOpen());
        IM_CHECK(ctx->WindowInfo("//Perf").Window->DockId == dockId);
        ctx->WindowClose("//Perf");
    };

    t = IM_REGISTER_TEST(engine, "performance", "unavailable counter clock retains GPU duration");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        using gfx::Json;
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        workspace.SetPerformanceFixture(Json{}, true); workspace.OpenPerformance(); ctx->Yield(2);
        workspace.StartPerformanceJobFixture(20); WaitForWorkspace(ctx, workspace);
        IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Success);
        workspace.ClosePerformance(); ctx->Yield(2);
        const auto gpuStats = gfx::SummarizePerfValues({2.5});
        const Json missing{{"mean", nullptr}, {"count", 0}, {"p95", nullptr}, {"stddev", nullptr}};
        Json report{{"schema", "gfxrecon-perf/1"}, {"status", "complete"}, {"mode", "single-frame"}, {"runs_requested", 1},
            {"policy", {{"frame", 1}, {"counter_profile", "bandwidth"}}},
            {"metrics", Json::array({{{"key", "gpu_time"}, {"name", "GPU frame elapsed"}, {"unit", "ms"}, {"baseline", gpuStats}},
                {{"key", "counter_5001"}, {"counter_id", 5001}, {"name", "Read Total"}, {"unit", "BYTE/SECOND"},
                    {"baseline", missing}, {"status", "unavailable"}}})},
            {"runs", Json::array({{{"variant", "baseline"}, {"index", 1},
                {"gpu", {{"status", "complete"}, {"samples", Json::array({{{"frame", 1}, {"valid", true}, {"elapsed_ms", 2.5},
                    {"start_boot_ns", uint64_t(18000000000000000000ull)}, {"end_boot_ns", nullptr}}})},
                    {"counter_window", {{"status", "unavailable"}, {"start_boot_ns", nullptr}, {"end_boot_ns", nullptr}, {"reason", "clock mapping rejected"}}}}},
                {"counters", {{"status", "unavailable"}, {"reason", "clock mapping rejected"}, {"window_begin_boot_ns", nullptr}, {"window_end_boot_ns", nullptr},
                    {"counters", Json::array({{{"id", 5001}, {"status", "unavailable"}, {"samples", Json::array({{{"timestamp_boot_ns", 2000}, {"value", 1e90}}})},
                        {"summary", {{"mean", nullptr}, {"coverage_estimate", nullptr}}}}})}}}}})}};
        const auto logs = PerfLogSnapshot();
        for (int variant = 0; variant < 3; ++variant) {
            auto input = report;
            if (variant == 1) input["runs"][0]["gpu"]["counter_window"]["status"] = "available"; // Null endpoints are invalid even with this status.
            if (variant == 2) {
                input["runs"][0]["gpu"]["counter_window"]["start_boot_ns"] = 1000;
                input["runs"][0]["gpu"]["counter_window"]["end_boot_ns"] = uint64_t(17000000000000000000ull);
                input["runs"][0]["counters"].update({{"status", "complete"}, {"window_begin_boot_ns", 1000},
                    {"window_end_boot_ns", uint64_t(17000000000000000000ull)}});
            }
            workspace.SetPerformanceFixture(std::move(input), true); workspace.OpenPerformance(); ctx->Yield(5); ctx->SetRef("//Perf");
            IM_CHECK(workspace.PerformanceOpen());
            IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Success);
            IM_CHECK(workspace.PerformanceResult()["metrics"][0]["baseline"]["mean"] == 2.5);
            IM_CHECK(VisibleUiText(ctx).find("2.5") != std::string::npos);
            bool checked = false;
            for (int i = 0; i < GImPlot->Plots.GetBufSize(); ++i) {
                const auto* plot = GImPlot->Plots.GetByIndex(i);
                if (plot->TitleOffset < 0) continue;
                const std::string title = plot->GetTitle();
                if (title == "GPU frame elapsed") {
                    checked = true;
                    IM_CHECK(plot->Items.GetLegendCount() == 1);
                    IM_CHECK(plot->Axes[ImAxis_X1].Range.Min >= -0.01 && plot->Axes[ImAxis_X1].Range.Max >= 2.4 && plot->Axes[ImAxis_X1].Range.Max <= 2.6);
                }
                if (title == "Read Total") IM_CHECK(plot->Items.GetLegendCount() == 0);
            }
            IM_CHECK(checked);
            for (const auto& record : Log::Instance().RecentRecords()) if (record.category == "perf" && record.level == LogLevel::Error)
                IM_CHECK(std::find(logs.begin(), logs.end(), record.formatted) != logs.end());
            ctx->WindowClose("//Perf"); ctx->Yield(2);
        }
    };

    t = IM_REGISTER_TEST(engine, "performance", "cancellation and stale selection");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        workspace.SetPerformanceFixture(gfx::Json{}); workspace.OpenPerformance(); ctx->Yield(3);
        workspace.StartPerformanceJobFixture(); ctx->Yield(2);
        IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Running);
        ctx->SetRef("//Perf"); IM_CHECK(ctx->ItemExists("Cancel") && !ctx->ItemExists("Start")); ctx->ItemClick("Cancel");
        WaitForWorkspace(ctx, workspace);
        IM_CHECK(!workspace.Busy()); IM_CHECK(workspace.PerformanceResult().is_null());
        IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Inactive);
        IM_CHECK(ctx->ItemExists("Start") && !ctx->ItemExists("Cancel"));
        workspace.StartPerformanceJobFixture(); workspace.SupersedePerformanceFixtureSelection();
        WaitForWorkspace(ctx, workspace);
        IM_CHECK(!workspace.Busy()); IM_CHECK(workspace.PerformanceResult().is_null());
        IM_CHECK(workspace.Status().find("previous selection") != std::string::npos);
        IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Inactive);
        ctx->WindowClose("//Perf");
    };

    t = IM_REGISTER_TEST(engine, "performance", "closing active window retains job and logs completion");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        workspace.SetPerformanceFixture(gfx::Json{}); workspace.OpenPerformance(); ctx->Yield(3);
        const auto previousLogs = PerfLogSnapshot();
        workspace.StartPerformanceJobFixture(2000); ctx->Yield(2);
        IM_CHECK(workspace.Busy()); IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Running);
        ctx->WindowClose("//Perf"); ctx->Yield(2);
        IM_CHECK(!workspace.PerformanceOpen());
        IM_CHECK(workspace.Busy());
        WaitForWorkspace(ctx, workspace);
        IM_CHECK(!workspace.Busy()); IM_CHECK(workspace.PerformanceResult().at("status") == "complete");
        IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Success);
        IM_CHECK(PerfLogAdded(previousLogs));
        workspace.OpenPerformance(); ctx->Yield(3);
        IM_CHECK(workspace.PerformanceOpen());
        IM_CHECK(workspace.PerformanceResult().at("status") == "complete");
        ctx->WindowClose("//Perf");
    };

    t = IM_REGISTER_TEST(engine, "performance", "settings remain runnable without another probe");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        workspace.ClearPerformanceFixture(); workspace.SetReplayMode(gfx::ReplayMode::Local);
        workspace.Open((std::filesystem::current_path() / "samples/windows-triangle.gfxr").u8string()); WaitForWorkspace(ctx, workspace);
        auto capture = workspace.CurrentCapture(); IM_CHECK(capture != nullptr);
        workspace.ConfigurePerformanceCaptureForTests(capture, 1, "fixture-only");
        workspace.SetPerformanceFixture(gfx::Json{}, true); workspace.OpenPerformance(); ctx->Yield(4);
        ctx->SetRef("//Perf");
        const auto canStart = [&] { return (ctx->ItemInfo("Start").ItemFlags & ImGuiItemFlags_Disabled) == 0; };
        IM_CHECK(canStart());
        IM_CHECK(!ctx->ItemExists("Mode") && !ctx->ItemExists("FPS") && !ctx->ItemExists("Warm-up ms") && !ctx->ItemExists("Measure ms"));
        IM_CHECK(!ctx->ItemExists("Counter us"));
        ctx->ItemInputValue("Runs", 2); ctx->Yield(2); IM_CHECK(canStart());
        ctx->ItemInputValue("Runs", 0); ctx->Yield(2); IM_CHECK(!canStart());
        ctx->ItemInputValue("Runs", 2); ctx->Yield(2); IM_CHECK(canStart());
        IM_CHECK(!ctx->ItemExists("Profile"));
        ctx->ItemCheck("Counters"); ctx->Yield(2); IM_CHECK(canStart());
        IM_CHECK(ctx->ItemExists("Counter us"));
        const auto counters=ctx->ItemInfo("Counters"),overrides=ctx->ItemInfo("Shader Override");
        IM_CHECK(std::abs(counters.RectFull.GetCenter().y-overrides.RectFull.GetCenter().y)<3);
        IM_CHECK(!ctx->ItemExists("Compare applied shader overrides"));
        ctx->ItemInputValue("Counter us", 2000); ctx->Yield(2); IM_CHECK(canStart());
        ctx->ItemInputValue("Counter us", 0); ctx->Yield(2); IM_CHECK(!canStart());
        ctx->ItemInputValue("Counter us", 1000); ctx->Yield(2); IM_CHECK(canStart());
        ctx->ItemInputValue("Frame",0);ctx->Yield(2);IM_CHECK(!canStart());
        ctx->ItemInputValue("Frame",1);ctx->Yield(2);IM_CHECK(canStart());
        ctx->ItemUncheck("Counters"); ctx->Yield(2); IM_CHECK(canStart());
        ctx->ItemCheck("Counters");ctx->Yield(2);IM_CHECK(canStart());
        ctx->ItemUncheck("Counters");ctx->Yield(2);IM_CHECK(canStart());
        IM_CHECK(!ctx->ItemExists("Counter us"));
        ctx->MouseMove("Shader Override");
        const auto tooltipDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        bool overrideTooltipVisible=false;
        do {
            overrideTooltipVisible=VisibleUiText(ctx).find("Compare applied shader overrides")!=std::string::npos;
            if(!overrideTooltipVisible)std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while(!overrideTooltipVisible&&std::chrono::steady_clock::now()<tooltipDeadline&&!ctx->IsError());
        IM_CHECK(overrideTooltipVisible);
        ReviewScreenshot(ctx,"perf-settings-ready");
        ctx->WindowClose("//Perf");
    };

    t = IM_REGISTER_TEST(engine, "performance", "local timing uses shared session core");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        const auto file = std::filesystem::current_path() / "samples/windows-triangle.gfxr";
        IM_CHECK(std::filesystem::is_regular_file(file));
        workspace.ClearPerformanceFixture(); workspace.SetReplayMode(gfx::ReplayMode::Local);
        workspace.Open(file.u8string()); WaitForWorkspace(ctx, workspace);
        IM_CHECK(workspace.CurrentCapture() != nullptr);
        workspace.OpenPerformance(); WaitForWorkspace(ctx, workspace);
        ctx->SetRef("//Perf");
        ctx->ItemCheck("Counters"); WaitForWorkspace(ctx, workspace);
        const auto probeLogs = PerfLogSnapshot();
        ctx->ItemClick("Probe"); WaitForWorkspace(ctx, workspace);
        IM_CHECK(workspace.StateFor("Perf") != gfx::PanelState::Failed);
        IM_CHECK((ctx->ItemInfo("Start").ItemFlags & ImGuiItemFlags_Disabled) == 0);
        IM_CHECK(PerfLogAdded(probeLogs));
        ctx->ItemUncheck("Counters");
        ctx->Yield(2);
        IM_CHECK((ctx->ItemInfo("Start").ItemFlags & ImGuiItemFlags_Disabled) == 0);
        ctx->ItemClick("Start"); WaitForWorkspace(ctx, workspace);
        IM_CHECK(workspace.PerformanceResult().at("status") == "complete");
        IM_CHECK(workspace.PerformanceResult().at("mode") == "single-frame");
        IM_CHECK(workspace.PerformanceResult().at("runs")[0].at("gpu").at("samples")[0].at("valid") == true);
        IM_CHECK(workspace.StateFor("Perf") == gfx::PanelState::Success);
        ctx->WindowClose("//Perf"); workspace.SetReplayMode(gfx::ReplayMode::Automatic);
    };

    if (std::getenv("GFXRECON_GUI_PERF_CAPTURE") && std::getenv("GFXRECON_GUI_PERF_INDEX") && std::getenv("GFXRECON_GUI_PERF_SERIAL")) {
        t = IM_REGISTER_TEST(engine, "performance-device", "fresh frame counters from performance window"); t->UserData = app;
        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
            auto capture = gfx::ReadCapture(std::filesystem::u8path(std::getenv("GFXRECON_GUI_PERF_INDEX")));
            capture->source = std::filesystem::u8path(std::getenv("GFXRECON_GUI_PERF_CAPTURE")); capture->fingerprint = gfx::Sha256File(capture->source);
            workspace.ConfigurePerformanceCaptureForTests(capture, 3, std::getenv("GFXRECON_GUI_PERF_SERIAL"));
            workspace.OpenPerformance(); WaitForWorkspace(ctx, workspace); ctx->SetRef("//Perf");
            ctx->ItemCheck("Counters"); ctx->ItemClick("Probe"); WaitForWorkspace(ctx, workspace);
            IM_CHECK((ctx->ItemInfo("Start").ItemFlags & ImGuiItemFlags_Disabled) == 0);
            ctx->ItemClick("Start"); WaitForWorkspace(ctx, workspace);
            if (!workspace.PerformanceResult().is_object()) { IM_CHECK(false && "Device performance run failed; inspect workspace status and retained artifacts"); return; }
            IM_CHECK(workspace.PerformanceResult().at("status") == "complete");
            IM_CHECK(workspace.PerformanceResult().at("mode") == "single-frame");
            const auto& run = workspace.PerformanceResult().at("runs")[0];
            IM_CHECK(run.at("gpu").at("samples").size() == 1);
            IM_CHECK(run.at("counters").at("status") == "complete" || run.at("counters").at("status") == "sparse" || run.at("counters").at("status") == "unavailable");
            ctx->Yield(5); ReviewScreenshot(ctx, "perf-window-device"); ctx->WindowClose("//Perf");
        };
    }

    if (std::getenv("GFXRECON_GUI_PERF_RESULT")) {
        t = IM_REGISTER_TEST(engine, "performance-retained", "real result in dockable window"); t->UserData = app;
        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
            WaitForWorkspace(ctx, workspace);
            const auto reportPath = std::filesystem::u8path(std::getenv("GFXRECON_GUI_PERF_RESULT"));
            std::ifstream input(reportPath);
            IM_CHECK(input.good());
            auto report = gfx::Json::parse(input);
            bool qualityWarning = false;
            for (const auto& run : report.value("runs", gfx::Json::array())) {
                if (run.contains("gpu") && run.at("gpu").is_object() && run.at("gpu").contains("sustained") && run.at("gpu").at("sustained").is_object())
                    qualityWarning = qualityWarning || run.at("gpu").at("sustained").value("missed_deadlines", uint64_t(0)) > 0;
            }
            for (const auto& comparison : report.value("comparisons", gfx::Json::array()))
                qualityWarning = qualityWarning || comparison.value("status", "") != "comparable";
            const auto logs = PerfLogSnapshot();
            workspace.LoadPerformanceReport(reportPath);
            // Open while the real loader owns the job, so merely viewing a
            // saved report cannot launch a device capability probe.
            workspace.OpenPerformance(); WaitForWorkspace(ctx, workspace); ctx->Yield(4);
            IM_CHECK(!workspace.Busy());
            IM_CHECK(workspace.StateFor("Perf") == (qualityWarning ? gfx::PanelState::Warning : gfx::PanelState::Success));
            IM_CHECK(PerfLogAdded(logs));
            if (qualityWarning) IM_CHECK(PerfWarningAdded(logs));
            IM_CHECK(workspace.PerformanceOpen());
            auto* window = ctx->WindowInfo("//Perf").Window;
            IM_CHECK(window != nullptr && (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoDocking)) == 0);
            IM_CHECK(window->DockIsActive&&window->DockId==ctx->WindowInfo("//Pipeline").Window->DockId);
            IM_CHECK(workspace.PerformanceResult().at("status") == "complete");
            ctx->SetRef("//Perf");
            IM_CHECK(ctx->ItemExists("Open...") && ctx->ItemExists("Start") && !ctx->ItemExists("Cancel"));
            IM_CHECK(!ctx->ItemExists("Load result...") && !ctx->ItemExists("Device, cadence and sampling"));
            ReviewScreenshot(ctx, "perf-window-retained");
            ctx->WindowClose("//Perf");
        };
    }

    t = IM_REGISTER_TEST(engine, "image-toolbar", "compact mode and scale placement"); t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        workspace.ClearPerformanceFixture(); workspace.SetReplayMode(gfx::ReplayMode::Local);
        if (!workspace.CurrentCapture()) {
            ctx->WindowFocus("//Image"); ctx->Yield(3); ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);
            for (const auto* control : {"##draw-visualization", "##indirect-subdraw", "R", "sRGB", "Fit", "##scale"})
                IM_CHECK(!ctx->ItemExists(control));
            ReviewScreenshot(ctx, "image-toolbar-empty");
        }
        workspace.Open((std::filesystem::current_path() / "samples/windows-triangle.gfxr").u8string()); WaitForWorkspace(ctx, workspace);
        workspace.SelectExecution(0, true); WaitForWorkspace(ctx, workspace);
        IM_CHECK(workspace.SelectedExecution() == 0);
        IM_CHECK(workspace.CurrentCapture()->events[workspace.SelectedEvent()].name.rfind("vkCmdDraw", 0) == 0);
        ctx->WindowFocus("//Image"); ctx->DockClear("Image", nullptr);
        ctx->WindowResize("//Image", ImVec2(1550, 850)); ctx->WindowMove("//Image", ImVec2(20, 40)); ctx->Yield(4);
        ctx->WindowCollapse("//Image", false); ctx->Yield(3);
        ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);
        ReviewScreenshot(ctx, "image-toolbar-before-controls");
        const auto checkRows = [&](bool indirect) {
            const auto mode = ctx->ItemInfo("##draw-visualization"), subdraw = ctx->ItemInfo("##indirect-subdraw"), red = ctx->ItemInfo("R");
            if (ctx->IsError()) return;
            IM_CHECK(mode.RectFull.Max.x < subdraw.RectFull.Min.x);
            IM_CHECK(std::abs(mode.RectFull.GetCenter().y - subdraw.RectFull.GetCenter().y) < 3);
            IM_CHECK(std::abs(mode.RectFull.GetWidth() - ImGui::GetFontSize()*8) < 1);
            IM_CHECK(std::abs(mode.RectFull.GetWidth() - subdraw.RectFull.GetWidth()) < 1);
            IM_CHECK(((subdraw.ItemFlags & ImGuiItemFlags_Disabled) == 0) == indirect);
            IM_CHECK(red.RectFull.Min.y > mode.RectFull.Max.y);
            for (const auto* control : {"G", "B", "sRGB", "##range-min", "##range-max", "Auto", "Reset"})
                IM_CHECK(std::abs(ctx->ItemInfo(control).RectFull.GetCenter().y - red.RectFull.GetCenter().y) < 3);
            const auto scale = ctx->ItemInfo("##scale"), flip = ctx->ItemInfo("Flip V"), fit = ctx->ItemInfo("Fit");
            IM_CHECK(std::abs(scale.RectFull.GetCenter().y - mode.RectFull.GetCenter().y) < 3);
            const auto rotate = ctx->ItemInfo("##rotate-left");
            IM_CHECK(rotate.RectFull.Min.x - subdraw.RectFull.Max.x >= ImGui::GetStyle().ItemSpacing.x * 3);
            IM_CHECK(flip.RectFull.Max.x < fit.RectFull.Min.x && fit.RectFull.Max.x < scale.RectFull.Min.x);
            IM_CHECK(fit.RectFull.Min.x - flip.RectFull.Max.x >= ImGui::GetStyle().ItemSpacing.x * 3);
            for (const auto* control : {"##rotate-left", "##rotate-right", "Flip H", "Flip V", "Fit"})
                IM_CHECK(std::abs(ctx->ItemInfo(control).RectFull.GetCenter().y - scale.RectFull.GetCenter().y) < 3);
        };
        checkRows(false); if (ctx->IsError()) return;
        if (!workspace.SelectedImage().Width()) { IM_CHECK(false && "Draw output image was not loaded"); return; }
        const auto original = workspace.SelectedImage().Pixel(0, 0);
        ctx->ItemClick("Flip V"); IM_CHECK(workspace.SelectedImage().Orientation().vertical);
        ctx->ItemClick("Flip V"); IM_CHECK(!workspace.SelectedImage().Orientation().vertical);
        IM_CHECK(workspace.SelectedImage().Pixel(0, 0) == original);
        ctx->ComboClick("##draw-visualization/Solid coverage"); WaitForWorkspace(ctx, workspace);
        IM_CHECK(workspace.DrawVisualization() == 1);
        ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window); ctx->ComboClick("##draw-visualization/Original"); WaitForWorkspace(ctx, workspace);
        IM_CHECK(workspace.DrawVisualization() == 0);
        ReviewScreenshot(ctx, "image-toolbar-compact");
        workspace.Open((std::filesystem::current_path() / "samples/indirect-triangles.gfxr").u8string()); WaitForWorkspace(ctx, workspace);
        workspace.SelectExecution(0, true); WaitForWorkspace(ctx, workspace);
        ctx->WindowFocus("//Image"); ctx->Yield(3); ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);
        checkRows(true); if (ctx->IsError()) return;
        ctx->ComboClick("##indirect-subdraw/###subdraw-1"); WaitForWorkspace(ctx, workspace); IM_CHECK_EQ(workspace.SelectedSubdraw(), 1);
        ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window); checkRows(true);
        ReviewScreenshot(ctx, "image-toolbar-indirect");
        ctx->WindowResize("//Image", ImVec2(600, 850)); ctx->Yield(4);
        const auto document = ctx->WindowInfo("//Image/image-document").Window;
        IM_CHECK(document->ScrollbarX && document->ContentSize.x > document->InnerRect.GetWidth());
        ctx->SetRef(document);
        IM_CHECK(std::abs(ctx->ItemInfo("##scale").RectFull.GetCenter().y - ctx->ItemInfo("Flip V").RectFull.GetCenter().y) < 3);
        ReviewScreenshot(ctx, "image-toolbar-narrow");
    };

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

    t = IM_REGISTER_TEST(engine, "replay-policy", "memory translation setting persists"); t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace(); WaitForWorkspace(ctx, workspace);
        const auto original = workspace.MemoryTranslation();
        ctx->MenuClick("//##MainMenuBar/File/Settings..."); ctx->SetRef("//Settings");
        IM_CHECK(ctx->ItemExists(SettingsItem(ctx,"Memory translation")));
        SettingsCombo(ctx,"Memory translation/Remap"); ctx->Yield(2);
        IM_CHECK(workspace.MemoryTranslation() == "remap");
        std::ifstream saved(std::filesystem::u8path(ExeDirectory()) / "test-state/gfxrecon_preferences.json");
        IM_CHECK(gfx::Json::parse(saved).at("memory_translation") == "remap");
        const std::map<std::string, std::string> labels{{"default","Default"},{"none","None"},{"remap","Remap"},{"realign","Realign"},{"rebind","Rebind"}};
        SettingsCombo(ctx,("Memory translation/" + labels.at(original)).c_str());
        IM_CHECK(workspace.MemoryTranslation() == original);
        ReviewScreenshot(ctx, "settings-dialog"); ctx->ItemClick("Close");
    };

    t = IM_REGISTER_TEST(engine, "shell", "theme switch applies");
    t->TestFunc = [](ImGuiTestContext* ctx) {
        const int original = CurrentThemeIndex();
        int next = (CurrentThemeIndex() + 1) % (int)kThemesCount;
        ctx->MenuClick("//##MainMenuBar/File/Settings...");
        ctx->SetRef("//Settings");
        char label[128];
        std::snprintf(label, sizeof(label), "Theme/%s", kThemes[next].displayName);
        SettingsCombo(ctx,label);
        IM_CHECK_EQ(CurrentThemeIndex(), next);
        std::snprintf(label, sizeof(label), "Theme/%s", kThemes[original].displayName);
        SettingsCombo(ctx,label);
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
                ReplayDeviceAction(ctx, workspace, gfx::LocalReplayLabel()); wait();
                ReplayDeviceAction(ctx, workspace, "Automatic (based on capture)"); wait();
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
                    ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window); ctx->ComboClick((std::string("##draw-visualization/")+mode).c_str());wait();
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
                ReplayDeviceAction(ctx, workspace, gfx::LocalReplayLabel()); wait();
                FrameAction(ctx, "Render frame"); wait();
                IM_CHECK(workspace.Status().find("local GPU lacks a Vulkan extension") != std::string::npos);
                IM_CHECK_EQ(workspace.BaselineImage().Width(), originalWidth);
                IM_CHECK(workspace.StateFor("Frames") == gfx::PanelState::Failed);
                IM_CHECK(workspace.FrameStateFor(1) == gfx::PanelState::Failed);
                ReplayDeviceAction(ctx, workspace, "Automatic (based on capture)"); wait();
            }
            ctx->SetRef("//Frames"); FrameAction(ctx, "Render frame"); wait();
            IM_CHECK(workspace.Status().find("Replay artifacts:") != std::string::npos);
            IM_CHECK(workspace.LastReplayTarget().find(android ? "Android " : gfx::LocalReplayLabel()) == 0);
            IM_CHECK(workspace.StateFor("Frames") == gfx::PanelState::Success);
            IM_CHECK(workspace.FrameStateFor(1) == gfx::PanelState::Success);
            IM_CHECK(workspace.StateFor("Image") == gfx::PanelState::Success);
            ImGui::SetWindowFocus("Image"); ctx->Yield(2); ctx->SetRef("//Image");
            const auto scale = workspace.BaselineImage().Zoom();
            ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);
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
            // Change selection before yielding to Poll: a warm cache hit can
            // finish during the UI helper's mouse-click frames.
            workspace.StartFrameReplayForTests(); IM_CHECK(workspace.Busy());
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
            ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");SettingsCombo(ctx,"Draw replay/Full (all views)");
            IM_CHECK(w.FullDrawReplay());IM_CHECK(readPreference());ctx->ItemClick("Close");
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);
            IM_CHECK(w.CachedDrawModeCount()>=5u);IM_CHECK_EQ(replayCount(),before+9);
            ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");SettingsCombo(ctx,"Draw replay/Light (on demand)");
            IM_CHECK(!w.FullDrawReplay());IM_CHECK(!readPreference());ctx->ItemClick("Close");
        };
    }
    if(const auto indirect=std::getenv("GFXRECON_GUI_INDIRECT_CAPTURE")) {
        t=IM_REGISTER_TEST(engine,"integration","native indirect batch visualization and latest subdraw selection");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();
            w.SetReplayMode(gfx::ReplayMode::Local);w.Open(std::getenv("GFXRECON_GUI_INDIRECT_CAPTURE"));WaitForWorkspace(ctx,w);
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);ctx->WindowFocus("//Image");ctx->Yield(3);
            ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);
            IM_CHECK(ctx->ItemExists("##indirect-subdraw"));
            auto orange=[&](){const auto& image=w.SelectedImage();size_t count=0;for(int y=0;y<image.Height();++y)for(int x=0;x<image.Width();++x){const auto p=image.Pixel(x,y);if(p[0]>250&&p[1]>120&&p[1]<240&&p[2]<5)++count;}return count;};
            ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);ctx->ComboClick("##draw-visualization/Solid coverage");WaitForWorkspace(ctx,w);IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);
            const auto all=orange();IM_CHECK(all>10000);size_t sum=0;
            for(int i=0;i<3;++i){ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);ctx->ComboClick(("##indirect-subdraw/###subdraw-"+std::to_string(i)).c_str());WaitForWorkspace(ctx,w);
                IM_CHECK_EQ(w.SelectedSubdraw(),i);IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);const auto count=orange();IM_CHECK(count>10000);IM_CHECK(count<all);sum+=count;}
            IM_CHECK_EQ(sum,all);
            w.SetDrawVisualization(1,0);w.SetDrawVisualization(2,1);w.SetDrawVisualization(1,2);WaitForWorkspace(ctx,w);
            IM_CHECK_EQ(w.SelectedSubdraw(),2);IM_CHECK_EQ(w.DrawVisualization(),1);IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);IM_CHECK(orange()<all);
            ReviewScreenshot(ctx,"indirect-draw-visualization");
            ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);ctx->ComboClick("##indirect-subdraw/All 3 draws");WaitForWorkspace(ctx,w);IM_CHECK_EQ(orange(),all);
            ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);ctx->ComboClick("##draw-visualization/Original");WaitForWorkspace(ctx,w);IM_CHECK_EQ(orange(),0u);
            ctx->SetRef(ctx->WindowInfo("//Image/image-document").Window);ctx->ComboClick("##draw-visualization/Orange outline");WaitForWorkspace(ctx,w);IM_CHECK(orange()>0);IM_CHECK(orange()<all/5);IM_CHECK(!w.Busy());ReviewScreenshot(ctx,"orange-outline");
        };
    }

    if (desktopFixture) {
        t=IM_REGISTER_TEST(engine,"integration","completed draw inspections restore locally and preserve geometry");t->UserData=app;
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();
            const bool full=w.FullDrawReplay();
            struct Restore{gfx::Workspace& w;bool full;~Restore(){w.SetFullDrawReplay(full);}} restore{w,full};
            w.SetFullDrawReplay(false);w.SetReplayMode(gfx::ReplayMode::Local);
            w.Open(std::getenv("GFXRECON_GUI_TEST_CAPTURE"));WaitForWorkspace(ctx,w);
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);IM_CHECK(w.ExecutionState(0)==gfx::PanelState::Success);
            const auto vertices=w.Mesh().InputVertexCount();
            w.SetDrawVisualization(1);WaitForWorkspace(ctx,w);IM_CHECK(w.CachedDrawModeCount()>=2u);
            w.SetDrawVisualization(0);w.SelectExecution(1,true);WaitForWorkspace(ctx,w);
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);
            IM_CHECK(w.Status().find("Restored draw 0 from memory")!=std::string::npos);
            IM_CHECK_EQ(w.DisplayedExecution(),0);IM_CHECK_EQ(w.Mesh().InputVertexCount(),vertices);
            IM_CHECK(w.CachedDrawModeCount()>=2u);
            w.SelectExecution(0,true);IM_CHECK(!w.Busy()&&!w.HasPendingWork());
            w.SetDrawVisualization(1);IM_CHECK(!w.Busy()&&!w.HasPendingWork());
            w.StartFrameReplayForTests();WaitForWorkspace(ctx,w);IM_CHECK_EQ(w.DisplayedExecution(),-1);
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);IM_CHECK_EQ(w.DisplayedExecution(),0);
            IM_CHECK(w.Status().find("Restored draw 0 from memory")!=std::string::npos);
            w.SetDrawVisualization(0);w.Open(std::getenv("GFXRECON_GUI_TEST_CAPTURE"));WaitForWorkspace(ctx,w);
            w.SelectExecution(0,true);WaitForWorkspace(ctx,w);
            IM_CHECK(w.Status().find("Restored draw")==std::string::npos);
        };
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
            w.RequestTools();ctx->Yield(2);ctx->SetRef("//Settings");ctx->ItemOpen(SettingsItem(ctx,"Tool configuration"));
            ctx->ItemInputValue(SettingsItem(ctx,"replay"),helper.u8string().c_str());ctx->ItemClick("Close");
            w.SetReplayMode(gfx::ReplayMode::Local);w.SelectFrame(1);ctx->Yield(3);IM_CHECK(w.Busy());
            const auto started=std::chrono::steady_clock::now();
            w.LoadShader(w.CurrentCapture()->pipelines.begin()->second.front());
            WaitForWorkspace(ctx,w);
            IM_CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<4);
            IM_CHECK(!w.Editor().GetText().empty());
            w.RequestTools();ctx->Yield(2);ctx->SetRef("//Settings");ctx->ItemOpen(SettingsItem(ctx,"Tool configuration"));
            ctx->ItemInputValue(SettingsItem(ctx,"replay"),replay.c_str());ctx->ItemClick("Close");
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
            ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");ctx->ItemClick(SettingsItem(ctx,"Vulkan validation"));ctx->ItemClick("Close");
            FrameAction(ctx,"Render frame");WaitForWorkspace(ctx,w);
            IM_CHECK(w.ValidationReport().is_object());IM_CHECK(w.ValidationReport().value("enabled",false));
            ImGui::SetWindowFocus("Resources");ctx->Yield(3);ctx->SetRef("//Resources");ctx->ItemClick("resource-tabs/Validation");
            IM_CHECK(ctx->ItemExists("resource-tabs/Validation"));ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");ctx->ItemClick(SettingsItem(ctx,"Vulkan validation"));ctx->ItemClick("Close");
            FrameAction(ctx,"Render frame");WaitForWorkspace(ctx,w);
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

    t=IM_REGISTER_TEST(engine,"shell","Settings keeps user size while tab content scrolls");t->UserData=app;
    t->TestFunc=[](ImGuiTestContext* ctx){
        ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");ctx->Yield(3);
        auto* dialog=ctx->WindowInfo("//Settings").Window;const auto original=dialog->Size;
        const auto sameSize=[](ImVec2 a,ImVec2 b){return std::abs(a.x-b.x)<1&&std::abs(a.y-b.y)<1;};
        IM_CHECK((dialog->Flags&(ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoResize))==0);
        ctx->ItemClick("settings-tabs/Tools and devices");ctx->Yield(3);
        ctx->ItemOpen(SettingsItem(ctx,"Available operations"));ctx->ItemOpen(SettingsItem(ctx,"Tool configuration"));ctx->Yield(3);
        IM_CHECK(sameSize(dialog->Size,original));
        const ImVec2 requested(original.x-60,original.y-100);
        ctx->WindowResize("//Settings",requested);ctx->Yield(3);const auto resized=dialog->Size;
        IM_CHECK(resized.x<original.x-30&&resized.y<original.y-50);
        auto* content=ctx->WindowInfo("//Settings/settings-content").Window;
        IM_CHECK(content->ScrollMax.y>0);
        ctx->MouseMoveToPos(content->InnerRect.GetCenter());ctx->MouseWheelY(-8);ctx->Yield(5);
        IM_CHECK(content->Scroll.y>0);IM_CHECK(sameSize(dialog->Size,resized));
        const auto close=ctx->ItemInfo("Close").RectFull;
        IM_CHECK(close.Min.y>=dialog->Pos.y&&close.Max.y<=dialog->Pos.y+dialog->Size.y);
        ReviewScreenshot(ctx,"settings-tools");
        ctx->ItemClick("settings-tabs/General");ctx->Yield(3);IM_CHECK(sameSize(dialog->Size,resized));
        ctx->ItemClick("settings-tabs/Tools and devices");ctx->Yield(3);IM_CHECK(sameSize(dialog->Size,resized));
        ctx->ItemClick("Close");ctx->MenuClick("//##MainMenuBar/File/Settings...");ctx->SetRef("//Settings");ctx->Yield(3);
        IM_CHECK(sameSize(dialog->Size,resized));ctx->WindowResize("//Settings",original);ctx->ItemClick("Close");
    };

    t=IM_REGISTER_TEST(engine,"shell","settings owns tools and Events selects the shared replay device");t->UserData=app;
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& w=static_cast<App*>(ctx->Test->UserData)->Workspace();WaitForWorkspace(ctx,w);
        const auto adb=w.ConfiguredTools().at("adb");
        struct Restore{gfx::Workspace& w;std::string adb;~Restore(){w.ConfigureAdbFixture(adb);w.SetReplayDevicesForTests({});w.SetReplayMode(gfx::ReplayMode::Automatic);}} restore{w,adb};
        w.RequestTools();ctx->Yield(3);ctx->SetRef("//Settings");
        IM_CHECK(ctx->ItemExists("settings-tabs/Tools and devices"));
        ctx->ItemOpen(SettingsItem(ctx,"Tool configuration"));IM_CHECK(ctx->ItemExists(SettingsItem(ctx,"adb")));IM_CHECK(ctx->ItemExists(SettingsItem(ctx,"astcenc")));
        IM_CHECK(!ctx->ItemExists("##replay-device"));ReviewScreenshot(ctx,"settings-tools");ctx->ItemClick("Close");
        w.ConfigureAdbFixture("");w.SetReplayDevicesForTests({{"phone-a","device","model:Fixture"},{"phone-b","unauthorized",""}});
        ctx->SetRef("//Events");
        const auto perf=ctx->ItemInfo("##event-perf"),phone=ctx->ItemInfo("##event-replay-device");
        IM_CHECK_EQ(perf.RectFull.GetWidth(),phone.RectFull.GetWidth());IM_CHECK(phone.RectFull.Min.x>=perf.RectFull.Max.x);
        ctx->ItemClick("##event-replay-device");ctx->Yield(3);ctx->SetRef("//Replay device");
        IM_CHECK((ctx->ItemInfo("phone-b (unauthorized)").ItemFlags&ImGuiItemFlags_Disabled)!=0);
        ctx->ItemClick("phone-a (device)");IM_CHECK_EQ(w.ReplayDeviceLabel(),"Android phone-a");
        const auto preferences=std::filesystem::u8path(ExeDirectory())/"test-state/gfxrecon_preferences.json";
        {std::ifstream file(preferences);const auto j=gfx::Json::parse(file);IM_CHECK_EQ(j.at("android_serial").get<std::string>(),"phone-a");IM_CHECK_EQ(j.at("replay_mode").get<int>(),2);}
        ReviewScreenshot(ctx,"replay-device");ctx->ItemClick("Close");ctx->SetRef("//Events");
        ctx->MouseMove("##event-replay-device");ctx->Yield(60);IM_CHECK(VisibleUiText(ctx).find("Android phone-a")!=std::string::npos);
        ctx->ItemClick("##event-replay-device");ctx->SetRef("//Replay device");ctx->ItemClick(gfx::LocalReplayLabel());
        IM_CHECK_EQ(w.ReplayDeviceLabel(),gfx::LocalReplayLabel());ctx->ItemClick("Close");
        ctx->SetRef("//Events");ctx->ItemClick("##event-replay-device");ctx->SetRef("//Replay device");
        ctx->ItemClick("Android (single authorized phone)");ctx->ItemClick("Automatic (based on capture)");ctx->ItemClick("Close");
    };

    t = IM_REGISTER_TEST(engine, "inspection", "missing optional tools preserve offline inspection");
    t->UserData = app;
    t->TestFunc = [](ImGuiTestContext* ctx) {
        auto& workspace = static_cast<App*>(ctx->Test->UserData)->Workspace();
        const auto tools = workspace.ConfiguredTools();
        workspace.RequestTools(); ctx->Yield(2); ctx->SetRef("//Settings");
        ctx->ItemOpen(SettingsItem(ctx,"Tool configuration"));
        for (const auto* name : {"replay", "compiler", "validator", "extract", "disassembler"}) ctx->ItemInputValue(SettingsItem(ctx,name), "");
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
        workspace.RequestTools(); ctx->Yield(2); ctx->SetRef("//Settings");
        for (const auto* name : {"replay", "compiler", "validator", "extract", "disassembler"}) ctx->ItemInputValue(SettingsItem(ctx,name), tools.at(name).c_str());
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
