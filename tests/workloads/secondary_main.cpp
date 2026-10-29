// Validation harness for the MIT-licensed LunarG triangle derivative generated
// by cmake/validation_targets.cmake. No upstream tracked sources are modified.
#include <triangle_app.h>
#include <application/application.h>
#include <util/logging.h>
#include <memory>
#include <cstdio>

int main() {
#ifdef GFXRECON_GUI_MESH
    const char* workloadName = "indexed-cube";
#else
    const char* workloadName = "secondary-triangle";
#endif
    gfxrecon::util::Log::Init();
    try {
        auto application = std::make_unique<gfxrecon::application::Application>(
            workloadName, nullptr, "VK_KHR_win32_surface", nullptr);
        auto app = std::make_unique<gfxrecon::test_app::triangle::App>();
        app->SetApplication(std::move(application));
        app->run(workloadName);
        gfxrecon::util::Log::Release();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what()); return 1;
    }
}
