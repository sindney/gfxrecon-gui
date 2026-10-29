#include "platform/os_layer.h"
#include "platform/platform.h"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/OpenGL.hpp>
#include <SFML/Window/Event.hpp>
#include <imgui-SFML.h>

#if PLATFORM_MACOS
#include "platform/display_mac.h"
#endif

namespace {

sf::RenderWindow* g_window = nullptr;
sf::Clock g_deltaClock;

} // namespace

namespace os {
void* GlFunction(const char* name) { return reinterpret_cast<void*>(sf::Context::getFunction(name)); }

bool Init(const WindowConfig& config, const char*& glslVersionOut) {
    sf::ContextSettings settings;
    settings.depthBits = 24;
    settings.stencilBits = 8;
#if PLATFORM_MACOS
    // macOS provides fixed-function OpenGL only in its 2.1 legacy profile.
    glslVersionOut = nullptr;
    settings.majorVersion = 2;
    settings.minorVersion = 1;
#else
    glslVersionOut = "#version 330";
    // Compatibility profile: imgui-SFML's renderer uses fixed-function GL
    // (glPushAttrib/glOrtho) which a core profile rejects.
    settings.majorVersion = 3;
    settings.minorVersion = 3;
#endif

    g_window = new sf::RenderWindow(
        sf::VideoMode(sf::Vector2u((unsigned)config.width, (unsigned)config.height)),
        config.title, sf::Style::Default, sf::State::Windowed, settings);
    g_window->setVerticalSyncEnabled(true);
    g_window->setActive(true);
#if PLATFORM_MACOS
    macdisplay::EnableHighDpi(g_window->getNativeHandle());
#endif
    return true;
}

bool InitBackend(const char* glslVersion) {
    (void)glslVersion; // imgui-SFML renders draw data itself
    if (!ImGui::SFML::Init(*g_window, false))
        return false;
    float scale = ContentScale();
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(scale, scale);
    return true;
}

void ShutdownBackend() {
    // imgui-SFML owns the imgui context: its WindowContext dtor calls
    // ImGui::DestroyContext(). Tearing it down here therefore also destroys
    // the context, and BackendDestroyedImGuiContext() tells the app to skip
    // its own DestroyContext().
    ImGui::SFML::Shutdown();
}

bool BackendDestroyedImGuiContext() { return true; }

void Shutdown() {
    // imgui-SFML's per-window state is torn down by ImGui::DestroyContext()
    // (its shutdown hook); we only destroy the OS window here.
    delete g_window;
    g_window = nullptr;
}

bool PumpEvents() {
    while (const std::optional event = g_window->pollEvent()) {
        ImGui::SFML::ProcessEvent(*g_window, *event);
        if (event->is<sf::Event::Closed>())
            return false;
    }
    return g_window->isOpen();
}

void NewFrame() {
#if PLATFORM_MACOS
    // Backing scale changes when the window crosses displays; re-poll each frame.
    float scale = ContentScale();
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(scale, scale);
#endif
    // imgui-SFML's Update calls ImGui::NewFrame() internally.
    ImGui::SFML::Update(*g_window, g_deltaClock.restart());
}

void Render(const ImVec4& clearColor) {
    ImGui::Render();
    g_window->setActive(true);
    glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui::SFML::Render(*g_window);
    g_window->display();
}

void FramebufferSize(int& w, int& h) {
    sf::Vector2u size = g_window->getSize();
    float scale = ContentScale();
    w = (int)(size.x * scale);
    h = (int)(size.y * scale);
}

float ContentScale() {
#if PLATFORM_MACOS
    return macdisplay::BackingScaleFactor(g_window->getNativeHandle());
#else
    return 1.0f;
#endif
}

void* NativeWindowHandle() {
    return (void*)g_window->getNativeHandle();
}

void SetWindowTitle(const std::string& title) { if (g_window) g_window->setTitle(title); }
} // namespace os
