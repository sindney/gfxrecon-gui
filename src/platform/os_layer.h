#pragma once

#include <imgui.h>
#include <string>

// Backend-neutral OS/windowing seam. Implemented once per backend
// (oslayer_sdl3.cpp / oslayer_sfml.cpp); application code above uses only this.
namespace os {
#ifdef IMGUI_TEMPLATE_TESTS
constexpr unsigned kSyntheticTextTag = 0x47465854;
#endif

struct WindowConfig {
    const char* title = "GFXReconstruct Inspector";
    int width = 1600;
    int height = 960;
};

// Creates the window + GL context. `glslVersionOut` receives the GLSL version
// string (e.g. "#version 330") the app must pass to ImGui_ImplOpenGL3_Init.
bool Init(const WindowConfig& config, const char*& glslVersionOut);
// Backend ImGui init - call after ImGui::CreateContext().
bool InitBackend(const char* glslVersion);
// Backend ImGui teardown - call while the imgui context is still alive,
// before ImGui::DestroyContext().
void ShutdownBackend();
// True when ShutdownBackend() already destroyed the imgui context (SFML's
// imgui-SFML owns it); the app must skip its own ImGui::DestroyContext().
bool BackendDestroyedImGuiContext();
void Shutdown();

// Feeds events to ImGui. Returns false when a quit was requested.
bool PumpEvents();
void NewFrame();
// Renders the current ImGui draw data and presents.
void Render(const ImVec4& clearColor);

// Drawable pixels (differs from window size on Retina).
void FramebufferSize(int& w, int& h);
// 2.0 on a Retina display, 1.0 elsewhere. Polled so monitor moves stay correct.
float ContentScale();
// For NFD parenting: HWND on Windows, NSWindow* on macOS.
void* NativeWindowHandle();
void SetWindowTitle(const std::string& title);
void* GlFunction(const char* name);

} // namespace os
