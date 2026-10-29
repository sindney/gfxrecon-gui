// A watchable real-UI test, recorded by the Test Engine's own capture tool.
#include "gui/themes.h"
#include "platform/os_layer.h"
#include "platform/platform.h"

#include <imgui_te_context.h>
#include <imgui_te_engine.h>
#include <imgui_internal.h>

#if PLATFORM_WINDOWS
#include <windows.h>
#endif
#if PLATFORM_MACOS
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace tpl {
namespace {

// PostSwap runs with the main window's GL context current. The capture tool
// requests logical pixels; sample the physical framebuffer for HiDPI displays.
bool CaptureFrame(ImGuiID viewportId, int x, int y, int w, int h,
                  unsigned int* pixels, void*) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewportId != 0 && viewportId != viewport->ID)
        return false;
    int fbw, fbh;
    os::FramebufferSize(fbw, fbh);
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (size.x <= 0 || size.y <= 0 || w <= 0 || h <= 0)
        return false;
    const float sx = fbw / size.x, sy = fbh / size.y;
    const int px = (int)std::lround((x - viewport->Pos.x) * sx);
    const int py = (int)std::lround((y - viewport->Pos.y) * sy);
    const int pw = (int)std::lround(w * sx), ph = (int)std::lround(h * sy);
    if (px < 0 || py < 0 || pw <= 0 || ph <= 0 || px + pw > fbw || py + ph > fbh)
        return false;
    std::vector<unsigned int> raw((size_t)pw * ph);
    GLint readBuffer, alignment;
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(px, fbh - py - ph, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    glReadBuffer(readBuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    // OpenGL rows start at the bottom; video rows start at the top.
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col)
            pixels[(size_t)row * w + col] =
                raw[(size_t)(ph - 1 - row * ph / h) * pw + col * pw / w];
    return true;
}

struct RecordingVars {
    int checked = 0;
    bool complete = false;
};

} // namespace

void EnableTestScreenCapture(ImGuiTestEngine* engine) {
    auto& io = ImGuiTestEngine_GetIO(engine);
    io.ScreenCaptureFunc = CaptureFrame;
    io.ConfigCaptureEnabled = true;
}

ImGuiTest* RegisterThemeRecording(ImGuiTestEngine* engine, const char* outputPath,
                                const char* encoderPath) {
    auto& io = ImGuiTestEngine_GetIO(engine);
    io.ScreenCaptureFunc = CaptureFrame;
    io.ConfigRunSpeed = ImGuiTestRunSpeed_Normal;
    io.ConfigWatchdogWarning = 120;
    io.ConfigWatchdogKillTest = 180;
    std::snprintf(io.VideoCaptureEncoderPath, sizeof(io.VideoCaptureEncoderPath), "%s", encoderPath);
    std::snprintf(io.VideoCaptureEncoderParams, sizeof(io.VideoCaptureEncoderParams), "%s",
        "-hide_banner -loglevel error -f rawvideo -pixel_format rgba -video_size $WIDTHx$HEIGHT "
        "-framerate $FPS -i - -an -c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p "
        "-movflags +faststart -y \"$OUTPUT\"");

    ImGuiTest* test = IM_REGISTER_TEST(engine, "recording", "automated theme tour");
    test->SetVarsDataType<RecordingVars>();
    test->UserData = (void*)outputPath; // Owned by main until the engine shuts down.
    test->GuiFunc = [](ImGuiTestContext* ctx) {
        const auto& vars = ctx->GetVars<RecordingVars>();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 490,
                                      viewport->WorkPos.y + 38), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(450, 0));
        ImGui::Begin("Theme automation", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("DEAR IMGUI TEST ENGINE");
        ImGui::Separator();
        ImGui::TextUnformatted("Automated theme tour / 13 themes");
        ImGui::TextUnformatted("File > Settings... > Theme");
        ImGui::Spacing();
        ImGui::Text("Selected: %s", kThemes[CurrentThemeIndex()].displayName);
        ImGui::Text("Verified: %d / %d", vars.checked, (int)kThemesCount);
        ImGui::ProgressBar((float)vars.checked / (float)kThemesCount, ImVec2(-1, 0));
        ImGui::TextUnformatted(vars.complete ? "PASS - all theme selections verified" :
                               "MenuClick > ComboClick > IM_CHECK_EQ");
        ImGui::End();
    };
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& vars = ctx->GetVars<RecordingVars>();
        const int originalTheme = CurrentThemeIndex();
        ctx->CaptureReset();
        std::snprintf(ctx->CaptureArgs->InOutputFile, sizeof(ctx->CaptureArgs->InOutputFile),
                      "%s", (const char*)ctx->Test->UserData);
        IM_CHECK(ctx->CaptureBeginVideo());
        ctx->Sleep(2.0f);
        ctx->MenuClick("//##MainMenuBar/File/Settings...");
        ctx->SetRef("//Settings");
        ctx->Sleep(1.0f);
        for (int i = 0; i < (int)kThemesCount; ++i) {
            char path[128];
            std::snprintf(path, sizeof(path), "Theme/%s", kThemes[i].displayName);
            ctx->ComboClick(path);
            IM_CHECK_EQ(CurrentThemeIndex(), i);
            ++vars.checked;
            ctx->LogInfo("Verified theme %d/%d: %s", vars.checked, (int)kThemesCount,
                         kThemes[i].displayName);
            ctx->Sleep(1.8f);
        }
        ctx->ItemClick("Close");
        vars.complete = true;
        ctx->Sleep(3.0f);
        IM_CHECK(ctx->CaptureEndVideo());
        const auto path = std::filesystem::path((const char*)ctx->Test->UserData);
        IM_CHECK(std::filesystem::is_regular_file(path));
        IM_CHECK(std::filesystem::file_size(path) > 0);
        ApplyTheme(originalTheme);
    };
    return test;
}

} // namespace tpl
