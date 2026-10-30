#include "app.h"
#include "core/log.h"
#include "core/settings.h"
#include "gui/fonts.h"
#include "platform/crash.h"
#include "platform/os_layer.h"
#include "platform/platform.h"
#include "platform/process.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <filesystem>

#ifdef IMGUI_TEMPLATE_OSLAYER_SFML
#include <imgui-SFML.h>
#endif

#ifdef IMGUI_TEMPLATE_TESTS
#include <imgui_te_engine.h>
#include <imgui_internal.h>
#endif

namespace {

bool WantsTests(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--test") == 0)
            return true;
    return false;
}

} // namespace

#ifdef IMGUI_TEMPLATE_TESTS
namespace tpl {
void RegisterAppTests(ImGuiTestEngine* engine, App* app);
void EnableTestScreenCapture(ImGuiTestEngine* engine);
#if PLATFORM_MACOS
void ConfigureMacTestCoroutines(ImGuiTestEngine* engine);
#endif
ImGuiTest* RegisterThemeRecording(ImGuiTestEngine* engine, const char* outputPath,
                                const char* encoderPath);
}
#endif

int main(int argc, char** argv) {
    tpl::InstallCrashHandler();

#ifdef IMGUI_TEMPLATE_TESTS
    std::string recordingPath;
    std::string encoderPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--record-themes") == 0 ||
            std::strcmp(argv[i], "--video-encoder") == 0) {
            const bool output = std::strcmp(argv[i], "--record-themes") == 0;
            if (i + 1 == argc || argv[i + 1][0] == '-') {
                std::fprintf(stderr, "%s requires a path\n", argv[i]);
                return 1;
            }
            (output ? recordingPath : encoderPath) = argv[++i];
        }
    }
    if (!recordingPath.empty()) {
        if (WantsTests(argc, argv) || encoderPath.empty() ||
            !std::filesystem::is_regular_file(encoderPath) ||
            std::filesystem::path(recordingPath).extension() != ".mp4" ||
            recordingPath.size() >= 256 || encoderPath.size() >= 256) {
            std::fprintf(stderr, "Use --record-themes output.mp4 --video-encoder /path/to/ffmpeg; "
                                 "run --test separately. Paths must be shorter than 256 bytes.\n");
            return 1;
        }
    }
#endif

    tpl::Log& log = tpl::Log::Instance();

    const auto nativeArgs = gfx::NativeArguments();
    std::string settingsDir = tpl::SettingsDirectory();
    for (size_t i = 1; i < nativeArgs.size(); ++i) {
        if (nativeArgs[i] != "--settings-dir") continue;
        if (i + 1 == nativeArgs.size() || nativeArgs[i + 1].empty()) {
            std::fprintf(stderr, "--settings-dir requires a directory\n");
            return 1;
        }
        settingsDir = std::filesystem::absolute(std::filesystem::u8path(nativeArgs[++i])).u8string();
    }
    std::filesystem::create_directories(std::filesystem::u8path(settingsDir));
    const std::string settingsPath = settingsDir + "/gfxrecon_gui.ini";
    const std::string layoutPath = settingsDir + "/gfxrecon_layout.ini";

    LOGI("app") << "GFXReconstruct Inspector starting";

    os::WindowConfig windowConfig;
    const char* glslVersion = nullptr;
    if (!os::Init(windowConfig, glslVersion)) {
        LOGE("app") << "os layer init failed";
        return 1;
    }

#ifdef IMGUI_TEMPLATE_OSLAYER_SFML
    // imgui-SFML::Init creates and owns the imgui context (it must: its
    // per-window registry destroys it at shutdown).
    if (!os::InitBackend(glslVersion)) {
        LOGE("app") << "imgui backend init failed";
        return 1;
    }
    IMGUI_CHECKVERSION();
#else
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Backend init must run before fonts so 1.92's dynamic texture system is
    // in the mode the backend will use for the whole session.
    if (!os::InitBackend(glslVersion)) {
        LOGE("app") << "imgui backend init failed";
        return 1;
    }
#endif

    ImGui::GetIO().IniFilename = layoutPath.c_str();
    ImGui::StyleColorsDark();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable |
                                  ImGuiConfigFlags_NavEnableKeyboard;

    float dpiScale = tpl::SystemDpiScale();
#if !PLATFORM_MACOS
    dpiScale *= os::ContentScale();
#endif
    LOGI("app") << "dpi scale: system=" << tpl::SystemDpiScale()
                << " content=" << os::ContentScale() << " effective=" << dpiScale;
    ImGui::GetStyle().ScaleAllSizes(dpiScale);
    tpl::LoadUiFonts(dpiScale);

#ifdef IMGUI_TEMPLATE_OSLAYER_SFML
    // imgui-SFML owns the font texture on this layer; rebuild it after loading.
    ImGui::SFML::UpdateFontTexture();
#endif

    tpl::App app;
#ifdef IMGUI_TEMPLATE_TESTS
    if (WantsTests(argc, argv)) {
        const auto testDir = std::filesystem::u8path(tpl::ExeDirectory()) / "test-state";
        std::filesystem::create_directories(testDir);
        app.Init((testDir / "gfxrecon_gui.ini").u8string());
        ImGui::GetIO().IniFilename = nullptr;
        app.RequestDefaultLayout();
    } else
#endif
        app.Init(settingsPath);
    const bool quitWhenReady = std::find(nativeArgs.begin(), nativeArgs.end(), "--quit-when-ready") != nativeArgs.end();
    for (size_t i = 1; i + 1 < nativeArgs.size(); ++i)
        if (nativeArgs[i] == "--open") app.Workspace().Open(nativeArgs[++i]);
    // Missing layout settings means first run: ask App to build
    // the default dock layout once the first frame begins.
    if (ImGui::GetIO().IniFilename) {
        FILE* ini = std::fopen(ImGui::GetIO().IniFilename, "r");
        if (!ini)
            app.RequestDefaultLayout();
        else
            std::fclose(ini);
    }

    // Debug hooks: --quit-after N exits cleanly after N frames; --crash-test
    // forces a null deref after 2 frames (crash-handler verification).
    int quitAfter = 0;
    const bool crashTest = [argc, argv] {
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--crash-test") == 0)
                return true;
        return false;
    }();
    if (crashTest)
        std::fprintf(stderr, "[crash-test] armed\n");
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--quit-after") == 0)
            quitAfter = std::atoi(argv[i + 1]);

#ifdef IMGUI_TEMPLATE_TESTS
    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
#if PLATFORM_MACOS
    tpl::ConfigureMacTestCoroutines(engine);
#endif
    ImGuiTestEngine_GetIO(engine).ConfigLogToTTY = true;
    ImGuiTestEngine_GetIO(engine).ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    ImGuiTestEngine_GetIO(engine).ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    if (std::getenv("GFXRECON_GUI_CAPTURE_DEVICE") || std::getenv("GFXRECON_GUI_ANDROID_CAPTURE")) {
        ImGuiTestEngine_GetIO(engine).ConfigWatchdogWarning = 90.0f;
        ImGuiTestEngine_GetIO(engine).ConfigWatchdogKillTest = 180.0f;
    }
    if (std::getenv("GFXRECON_GUI_SCREENSHOT") || std::getenv("GFXRECON_GUI_MESH_SCREENSHOT") || std::getenv("GFXRECON_GUI_REVIEW_DIR")) tpl::EnableTestScreenCapture(engine);
    tpl::RegisterAppTests(engine, &app);
    ImGuiTest* recordingTest = nullptr;
    if (!recordingPath.empty()) {
        // Keep the recording independent of the user's saved dock layout.
        ImGui::GetIO().IniFilename = nullptr;
        app.RequestDefaultLayout();
        recordingTest = tpl::RegisterThemeRecording(engine, recordingPath.c_str(), encoderPath.c_str());
    }
    const bool runTests = WantsTests(argc, argv) || recordingTest != nullptr;
    if (runTests) {
        ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
        if (recordingTest)
            ImGuiTestEngine_QueueTest(engine, recordingTest, ImGuiTestRunFlags_RunFromCommandLine);
        else
            ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Tests, nullptr,
                                      ImGuiTestRunFlags_RunFromCommandLine);
    }
#endif

    bool running = true;
    int frameCount = 0;
#ifdef IMGUI_TEMPLATE_TESTS
    unsigned imguiErrorReports=0,idConflictFrames=0;
#endif
    while (running) {
        ++frameCount;
        running = os::PumpEvents();
        if (quitAfter > 0 && frameCount >= quitAfter)
            running = false;
        if (crashTest && frameCount == 2) {
            std::fprintf(stderr, "[crash-test] forcing crash at frame %d\n", frameCount);
            volatile int* p = nullptr;
            *p = 0;
        }

        os::NewFrame();
        if (!app.Draw())
            running = false;
        os::Render(ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
#ifdef IMGUI_TEMPLATE_TESTS
        imguiErrorReports+=static_cast<unsigned>(GImGui->ErrorCountCurrentFrame);
        if(GImGui->DebugDrawIdConflictsId!=0)++idConflictFrames;
#endif
        if (quitWhenReady && app.Workspace().CurrentCapture() && !app.Workspace().Busy())
            running = false;

#ifdef IMGUI_TEMPLATE_TESTS
        if (runTests) {
            ImGuiTestEngine_PostSwap(engine);
            // Quit ~2s after the queue drains so late frames settle.
            static int emptyFrames = 0;
            if (ImGuiTestEngine_IsTestQueueEmpty(engine)) {
                if (++emptyFrames > 120)
                    running = false;
            } else {
                emptyFrames = 0;
            }
        }
#endif
    }

    app.Shutdown();
    LOGI("app") << "GFXReconstruct Inspector exiting";

    int exitCode = 0;
#ifdef IMGUI_TEMPLATE_TESTS
    if (runTests) {
        ImGuiTestEngine_TryAbortEngine(engine); // clears any Running status
        int tested = 0, succeeded = 0;
        ImGuiTestEngine_GetResult(engine, tested, succeeded);
        exitCode = (tested > 0 && tested == succeeded) ? 0 : 1;
        const bool loggedErrors=std::strstr(GImGui->DebugLogBuf.c_str(),"[imgui-error]")!=nullptr;
        std::fprintf(stderr,"[gui-diagnostics] %u ImGui error reports; %u frames with ID conflicts; error log %s\n",imguiErrorReports,idConflictFrames,loggedErrors?"present":"clean");
        if(imguiErrorReports||idConflictFrames||loggedErrors)exitCode=1;
        ImGuiTestEngine_Stop(engine);
    }
#endif

    // Backend teardown while the imgui context is alive, then context, then
    // the test engine (which requires the imgui context already destroyed).
    os::ShutdownBackend();
    std::fprintf(stderr, "[shutdown] backend done\n");
    if (!os::BackendDestroyedImGuiContext())
        ImGui::DestroyContext();
    std::fprintf(stderr, "[shutdown] context done\n");
#ifdef IMGUI_TEMPLATE_TESTS
    ImGuiTestEngine_DestroyContext(engine);
#endif
    os::Shutdown();
    std::fprintf(stderr, "[shutdown] window done\n");
    return exitCode;
}
