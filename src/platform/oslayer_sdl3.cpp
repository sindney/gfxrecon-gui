#include "platform/os_layer.h"
#include "platform/platform.h"
#include "gui/editor_input.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#ifdef IMGUI_TEMPLATE_TESTS
#include <imgui_internal.h>
#endif
#if PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <imm.h>
#endif

#if PLATFORM_MACOS
#include <objc/objc.h>
#endif

namespace {

SDL_Window* g_window = nullptr;
SDL_GLContext g_glContext = nullptr;
#if PLATFORM_WINDOWS
WNDPROC g_previousProcedure = nullptr;
HWND g_imeWindow = nullptr;
void (*g_previousIme)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*) = nullptr;
LRESULT CALLBACK EditorWindowProcedure(HWND window, UINT message, WPARAM word, LPARAM value) {
    if (message == WM_IME_STARTCOMPOSITION) gfx::SetEditorComposition(true);
    if (message == WM_IME_ENDCOMPOSITION || message == WM_KILLFOCUS) gfx::SetEditorComposition(false);
    return CallWindowProcW(g_previousProcedure, window, message, word, value);
}
void EditorImeCallback(ImGuiContext* context, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
    if (g_previousIme) g_previousIme(context, viewport, data);
    if (!g_imeWindow || !data->WantVisible) return;
    const auto ime = ImmGetContext(g_imeWindow); if (!ime) return;
    COMPOSITIONFORM form{}; form.dwStyle = CFS_POINT;
    form.ptCurrentPos = {static_cast<LONG>(data->InputPos.x - viewport->Pos.x), static_cast<LONG>(data->InputPos.y - viewport->Pos.y + data->InputLineHeight)};
    ImmSetCompositionWindow(ime, &form); ImmReleaseContext(g_imeWindow, ime);
}
#endif

#if PLATFORM_MACOS
// NSWindow* from the SDL window properties, for NFD parenting.
void* CocoaWindow(SDL_Window* window) {
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                  SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}
#endif

} // namespace

namespace os {
void* GlFunction(const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }

void SetWindowTitle(const std::string& title) { if (g_window) SDL_SetWindowTitle(g_window, title.c_str()); }

bool Init(const WindowConfig& config, const char*& glslVersionOut) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

#if PLATFORM_MACOS
    // macOS OpenGL 3.2 core uses GLSL 150.
    glslVersionOut = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    glslVersionOut = "#version 330";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    g_window = SDL_CreateWindow(config.title, config.width, config.height,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                    SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
#if PLATFORM_WINDOWS
    const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    const auto instance = GetModuleHandleW(nullptr);
    const auto iconSmall = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    const auto iconLarge = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR));
    if (iconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
    if (iconLarge) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge));
    const DWORD corners = 2; // DWMWCP_ROUND; ignored by older Windows versions.
    DwmSetWindowAttribute(hwnd, static_cast<DWMWINDOWATTRIBUTE>(33), &corners, sizeof(corners));
#endif

    g_glContext = SDL_GL_CreateContext(g_window);
    if (!g_glContext) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(g_window, g_glContext);
    SDL_GL_SetSwapInterval(1);
    return true;
}

bool InitBackend(const char* glslVersion) {
    ImGui_ImplSDL3_InitForOpenGL(g_window, g_glContext);
    ImGui_ImplOpenGL3_Init(glslVersion);
#if PLATFORM_WINDOWS
    g_imeWindow = static_cast<HWND>(NativeWindowHandle());
    g_previousProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_imeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditorWindowProcedure)));
    g_previousIme = ImGui::GetPlatformIO().Platform_SetImeDataFn;
    ImGui::GetPlatformIO().Platform_SetImeDataFn = EditorImeCallback;
#endif
    return true;
}

void ShutdownBackend() {
#if PLATFORM_WINDOWS
    if (g_previousProcedure) SetWindowLongPtrW(g_imeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_previousProcedure));
    ImGui::GetPlatformIO().Platform_SetImeDataFn = g_previousIme;
#endif
    // ImGui backend teardown - requires a live imgui context.
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

bool BackendDestroyedImGuiContext() { return false; }

void Shutdown() {
    // ImGui backends are shut down by the app before this is called (they need
    // a live imgui context); we only tear down the OS window and GL context.
    SDL_GL_DestroyContext(g_glContext);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    g_glContext = nullptr;
    g_window = nullptr;
}

bool PumpEvents() {
    gfx::BeginEditorInputFrame();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_TEXT_EDITING) gfx::SetEditorComposition(event.edit.text && *event.edit.text);
        if (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) gfx::SetEditorComposition(false);
#ifdef IMGUI_TEMPLATE_TESTS
        const auto textStart = GImGui->InputEventsQueue.Size;
#endif
        ImGui_ImplSDL3_ProcessEvent(&event);
#ifdef IMGUI_TEMPLATE_TESTS
        // Preserve this test-owned SDL packet through Test Engine's OS-input filter.
        if (event.type == SDL_EVENT_TEXT_INPUT && event.common.reserved == kSyntheticTextTag)
            for (int i = textStart; i < GImGui->InputEventsQueue.Size; ++i) GImGui->InputEventsQueue[i].AddedByTestEngine = true;
#endif
        if (event.type == SDL_EVENT_QUIT)
            return false;
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            event.window.windowID == SDL_GetWindowID(g_window))
            return false;
    }
    if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(10);
        return true; // skip rendering while minimized
    }
    return true;
}

void NewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Render(const ImVec4& clearColor) {
    ImGui::Render();
    int w, h;
    FramebufferSize(w, h);
    glViewport(0, 0, w, h);
    glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_window);
}

void FramebufferSize(int& w, int& h) {
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
}

float ContentScale() {
    // SDL_GetWindowSizeInPixels / SDL_GetWindowSize is the HiDPI factor.
    int winW, winH, pxW, pxH;
    SDL_GetWindowSize(g_window, &winW, &winH);
    SDL_GetWindowSizeInPixels(g_window, &pxW, &pxH);
    if (winW <= 0)
        return 1.0f;
    return (float)pxW / (float)winW;
}

void* NativeWindowHandle() {
#if PLATFORM_WINDOWS
    return SDL_GetPointerProperty(SDL_GetWindowProperties(g_window),
                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif PLATFORM_MACOS
    return CocoaWindow(g_window);
#else
    return nullptr;
#endif
}

} // namespace os
