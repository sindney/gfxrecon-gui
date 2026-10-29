#include "gui/fonts.h"

#include "core/log.h"
#include "platform/platform.h"

#include <imgui.h>

#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tpl {

float SystemDpiScale() {
#if PLATFORM_WINDOWS
    float scale = (float)GetDpiForSystem() / 96.0f;
    return scale < 1.0f ? 1.0f : scale;
#else
    return 1.0f;
#endif
}

void LoadUiFonts(float dpiScale, float baseSize) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig config;
    config.SizePixels = baseSize * dpiScale;
    if (!(io.BackendFlags & ImGuiBackendFlags_RendererHasTextures))
        config.RasterizerDensity = io.DisplayFramebufferScale.x;
    // 1.92: AddFontFromFileTTF asserts on a missing file unless this is set.
    config.Flags |= ImFontFlags_NoLoadError;

#if PLATFORM_WINDOWS
    const char* candidates[] = {"C:/Windows/Fonts/segoeui.ttf",
                                "C:/Windows/Fonts/msyh.ttc",
                                "C:/Windows/Fonts/arial.ttf"};
    ImFont* font = nullptr;
    for (const char* path : candidates) {
        font = io.Fonts->AddFontFromFileTTF(path, baseSize * dpiScale, &config);
        LOGI("font") << path << " -> " << (font ? "ok" : "FAILED");
        if (font)
            break;
    }
#elif PLATFORM_MACOS
    ImFont* font = io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/SFNS.ttf",
                                                baseSize * dpiScale, &config);
    if (!font)
        font = io.Fonts->AddFontFromFileTTF(
            "/System/Library/Fonts/Helvetica.ttc", baseSize * dpiScale, &config);
#else
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", baseSize * dpiScale,
        &config);
#endif
    if (!font) {
        LOGW("font") << "system font not found, using imgui default at "
                     << baseSize * dpiScale << "px";
        font = io.Fonts->AddFontDefault(&config);
    }
    LOGI("font") << "loaded font: " << (font ? font->GetDebugName() : "<null>");
    // 1.92+: a NULL return leaves no default font and the next frame asserts.
    IM_ASSERT(font != nullptr);
    // No io.Fonts->Build(): 1.92 backends with RendererHasTextures rebuild on demand.
}

} // namespace tpl
