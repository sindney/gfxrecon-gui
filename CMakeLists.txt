cmake_minimum_required(VERSION 3.24)

# macOS defaults must be set before project().
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "")
endif()

project(gfxrecon_gui VERSION 0.1.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
    configure_file(cmake/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
endif()

if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    string(REPLACE ";" "\n" _redistributables "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
    file(WRITE "${CMAKE_BINARY_DIR}/runtime-redistributables.txt" "${_redistributables}\n")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/distribution-version.txt" "${PROJECT_VERSION}\n")

set(IMGUI_TEMPLATE_OS_LAYER "SDL3" CACHE STRING "OS/windowing backend: SDL3 or SFML")
if(NOT IMGUI_TEMPLATE_OS_LAYER STREQUAL "SDL3" AND NOT IMGUI_TEMPLATE_OS_LAYER STREQUAL "SFML")
    message(FATAL_ERROR "IMGUI_TEMPLATE_OS_LAYER must be SDL3 or SFML (got '${IMGUI_TEMPLATE_OS_LAYER}')")
endif()
message(STATUS "OS layer: ${IMGUI_TEMPLATE_OS_LAYER}")

# nfd_cocoa.m / SFML macOS sources are Objective-C / Objective-C++.
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# --- Submodule presence checks ----------------------------------------------
# imgui and imgui_test_engine have no upstream CMakeLists; check for a marker file.
foreach(_pair imgui:imgui.h imgui_test_engine:imgui_test_engine/imgui_te_engine.h nativefiledialog-extended:CMakeLists.txt)
    string(REPLACE ":" ";" _parts "${_pair}")
    list(GET _parts 0 _sub)
    list(GET _parts 1 _marker)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${_sub}/${_marker}")
        message(FATAL_ERROR "thirdparty/${_sub} is missing - run: git submodule update --init --recursive")
    endif()
endforeach()
if(IMGUI_TEMPLATE_OS_LAYER STREQUAL "SDL3")
    set(_osl_sub SDL)
else()
    set(_osl_sub SFML)
endif()
foreach(_sub ${_osl_sub})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${_sub}/CMakeLists.txt")
        message(FATAL_ERROR "thirdparty/${_sub} is missing - run: git submodule update --init --recursive")
    endif()
endforeach()
if(IMGUI_TEMPLATE_OS_LAYER STREQUAL "SFML" AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui-sfml/CMakeLists.txt")
    message(FATAL_ERROR "thirdparty/imgui-sfml is missing - run: git submodule update --init --recursive")
endif()

# --- Third-party --------------------------------------------------------------
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/nativefiledialog-extended EXCLUDE_FROM_ALL)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)

# Plain imgui static lib; the SFML branch also routes ImGui-SFML through it.
add_library(imgui STATIC ${IMGUI_SOURCES})
if(IMGUI_TEMPLATE_OS_LAYER STREQUAL "SFML")
    # ImGui-SFML links `imgui` and its install(EXPORT) records the dependency;
    # exporting `imgui` too keeps that rule satisfied. BUILD/INSTALL generator
    # expressions are required for a target to appear in an export set.
    target_include_directories(imgui PUBLIC
        $<BUILD_INTERFACE:${IMGUI_DIR}>
        $<BUILD_INTERFACE:${IMGUI_DIR}/backends>
        $<INSTALL_INTERFACE:include/imgui>)
    install(TARGETS imgui EXPORT ImGui-SFML ARCHIVE DESTINATION lib)
    install(FILES ${IMGUI_DIR}/imgui.h ${IMGUI_DIR}/imconfig.h
        ${IMGUI_DIR}/imgui_internal.h ${IMGUI_DIR}/imstb_rectpack.h
        ${IMGUI_DIR}/imstb_textedit.h ${IMGUI_DIR}/imstb_truetype.h
        DESTINATION include/imgui)
else()
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
endif()

if(IMGUI_TEMPLATE_OS_LAYER STREQUAL "SDL3")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/SDL EXCLUDE_FROM_ALL)
    target_sources(imgui PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp)
    target_link_libraries(imgui PUBLIC SDL3::SDL3)
    set(OSLAYER_LIBS SDL3::SDL3)
    set(OSLAYER_DEFINE IMGUI_TEMPLATE_OSLAYER_SDL3)
    set(APP_IMGUI_LIB imgui)
else()
    set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/SFML EXCLUDE_FROM_ALL)
    set(IMGUI_SFML_FIND_SFML OFF CACHE BOOL "" FORCE)
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui" CACHE PATH "" FORCE)
    set(IMGUI_SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/imgui-sfml EXCLUDE_FROM_ALL)
    # Patches for imgui 1.92.x, applied to a build-dir copy of imgui-SFML.cpp:
    #  - ImFontAtlas::TexID is now an ImTextureRef union alias; the assert's
    #    direct != comparison no longer compiles. GetTexID() is equivalent.
    #  - (macOS) clip rects are already scaled by the renderer's per-command
    #    transform; ScaleClipRects would double-apply DisplayFramebufferScale.
    set(_isfml_orig "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui-sfml/imgui-SFML.cpp")
    file(READ "${_isfml_orig}" _isfml_src)
    string(REPLACE "assert(io.Fonts->TexID != (ImTextureID) nullptr);"
        "assert(io.Fonts->TexID.GetTexID() != (ImTextureID) nullptr);" _isfml_src "${_isfml_src}")
    if(APPLE)
        string(REPLACE "    draw_data->ScaleClipRects(io.DisplayFramebufferScale);" "" _isfml_src "${_isfml_src}")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/imgui-SFML.cpp" "${_isfml_src}")
    get_target_property(_isfml_sources ImGui-SFML SOURCES)
    list(REMOVE_ITEM _isfml_sources "imgui-SFML.cpp" "${_isfml_orig}")
    set_property(TARGET ImGui-SFML PROPERTY SOURCES "${_isfml_sources}")
    target_sources(ImGui-SFML PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/imgui-SFML.cpp")
    # ImGui-SFML's CMake compiles imgui sources into the lib; strip them and
    # link our own `imgui` target so the app uses a single imgui build.
    get_target_property(_isfml_all_sources ImGui-SFML SOURCES)
    foreach(_imgui_src imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp)
        list(REMOVE_ITEM _isfml_all_sources "${IMGUI_DIR}/${_imgui_src}")
    endforeach()
    set_property(TARGET ImGui-SFML PROPERTY SOURCES "${_isfml_all_sources}")
    # PRIVATE keeps `imgui` off ImGui-SFML's interface; `imgui` itself is added
    # to the ImGui-SFML export set above to satisfy upstream's install(EXPORT).
    target_link_libraries(ImGui-SFML PRIVATE imgui)
    set(OSLAYER_LIBS SFML::Graphics SFML::Window SFML::System ImGui-SFML::ImGui-SFML)
    set(OSLAYER_DEFINE IMGUI_TEMPLATE_OSLAYER_SFML)
    set(APP_IMGUI_LIB ImGui-SFML::ImGui-SFML)
endif()

# ImGui used by the test executable (test engine hooks must be compiled in).
add_library(imgui_tested STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_tested PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui_tested PUBLIC IMGUI_ENABLE_TEST_ENGINE)
if(IMGUI_TEMPLATE_OS_LAYER STREQUAL "SDL3")
    target_sources(imgui_tested PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp)
    target_link_libraries(imgui_tested PUBLIC SDL3::SDL3)
    set(TEST_IMGUI_LIB imgui_tested)
else()
    # imgui-SFML recompiled against the instrumented imgui so the test hooks
    # inside ImGui-SFML.cpp's imgui calls resolve to the tested build. Same
    # source patches as the app-side ImGui-SFML target above.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui-sfml/imgui-SFML.cpp" _isfml_test_src)
    string(REPLACE "assert(io.Fonts->TexID != (ImTextureID) nullptr);"
        "assert(io.Fonts->TexID.GetTexID() != (ImTextureID) nullptr);" _isfml_test_src "${_isfml_test_src}")
    if(APPLE)
        string(REPLACE "    draw_data->ScaleClipRects(io.DisplayFramebufferScale);" "" _isfml_test_src "${_isfml_test_src}")
    endif()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/imgui-SFML-tested.cpp" "${_isfml_test_src}")
    add_library(imgui_sfml_tested STATIC "${CMAKE_CURRENT_BINARY_DIR}/imgui-SFML-tested.cpp")
    target_include_directories(imgui_sfml_tested PUBLIC thirdparty/imgui-sfml)
    target_link_libraries(imgui_sfml_tested PUBLIC imgui_tested SFML::Graphics SFML::Window SFML::System OpenGL::GL)
    target_compile_definitions(imgui_sfml_tested PUBLIC IMGUI_ENABLE_TEST_ENGINE)
    set(TEST_IMGUI_LIB imgui_sfml_tested)
endif()

set(IMGUI_TE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui_test_engine/imgui_test_engine")
add_library(imgui_te STATIC
    ${IMGUI_TE_DIR}/imgui_te_context.cpp
    ${IMGUI_TE_DIR}/imgui_te_coroutine.cpp
    ${IMGUI_TE_DIR}/imgui_te_engine.cpp
    ${IMGUI_TE_DIR}/imgui_te_exporters.cpp
    ${IMGUI_TE_DIR}/imgui_te_perftool.cpp
    ${IMGUI_TE_DIR}/imgui_te_ui.cpp
    ${IMGUI_TE_DIR}/imgui_te_utils.cpp
    ${IMGUI_TE_DIR}/imgui_capture_tool.cpp
)
target_include_directories(imgui_te PUBLIC ${IMGUI_TE_DIR})
target_link_libraries(imgui_te PUBLIC ${TEST_IMGUI_LIB})
target_compile_definitions(imgui_te PUBLIC IMGUI_ENABLE_TEST_ENGINE
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1 IMGUI_TEST_ENGINE_ENABLE_CAPTURE=1)

# --- Shared core ---------------------------------------------------------------
# UI-independent capture, analysis, replay and shader services shared by the GUI,
# the agent CLI and the native tests.
set(CORE_SOURCES
    src/analysis/capture.cpp
    src/analysis/state.cpp
    src/analysis/index.cpp
    src/analysis/cache.cpp
    src/analysis/import.cpp
    src/analysis/export.cpp
    src/analysis/dump_dynamic.cpp
    src/analysis/resources.cpp
    src/analysis/mesh.cpp
    src/replay/replay.cpp
    src/capture/capture.cpp
    src/capture/live_capture.cpp
    src/capture/save_capture.cpp
    src/shaders/interface.cpp
    src/shaders/metadata.cpp
    src/shaders/decode.cpp
    src/core/log.cpp
    src/core/artifacts.cpp
    src/core/settings.cpp
    src/core/tools.cpp
    src/core/image_io.cpp
    src/core/image_decode.cpp
    src/platform/process.cpp
    src/platform/adb.cpp
    src/platform/android_display.cpp
    src/platform/device_lock.cpp
)

# --- App sources --------------------------------------------------------------
set(APP_SOURCES
    src/app.cpp
    src/gui/workspace.cpp
    src/gui/capture_window.cpp
    src/gui/capture_dialog.cpp
    src/gui/resource_window.cpp
    src/gui/pipeline_window.cpp
    src/gui/events_window.cpp
    src/gui/frame_window.cpp
    src/gui/panel_status.cpp
    src/gui/editor_input.cpp
    src/gui/mesh_viewer.cpp
    src/gui/mesh_window.cpp
    src/gui/image_viewer.cpp
    src/shaders/syntax.cpp
    src/gui/capture_save.cpp
    src/gui/image_inspection.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/TextEditor.cpp
    src/gui/about.cpp
    src/gui/command_palette.cpp
    src/gui/console_window.cpp
    src/gui/fonts.cpp
    src/gui/themes.cpp
    src/platform/crash.cpp
    src/platform/dialogs.cpp
)

# Keep the pinned upstream editor untouched. Limit incremental token coloring per
# frame; its default callback budget of 10,000 lines causes long shader stalls.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ImGuiColorTextEdit/TextEditor.cpp" _editor)
string(REPLACE "? 10 : 10000" "? 10 : 256" _editor "${_editor}")
string(REPLACE "if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Z))"
    "if (!gfx::BlockEditorCommands()) {\nif (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Z))" _editor "${_editor}")
string(REPLACE "if (!IsReadOnly() && !io.InputQueueCharacters.empty())"
    "}\nif (!IsReadOnly() && !io.InputQueueCharacters.empty())" _editor "${_editor}")
string(REPLACE "ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();"
    "ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();\nif (ImGui::IsWindowFocused() && !IsReadOnly()) gfx::RequestEditorInput(ImVec2(cursorScreenPos.x + mTextStart + TextDistanceToLineStart(GetActualCursorCoordinates()), cursorScreenPos.y + GetActualCursorCoordinates().mLine * mCharAdvance.y), mCharAdvance.y);" _editor "${_editor}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/TextEditor.cpp"
    "#include <cassert>\n#include <cfloat>\n#include <cstring>\n#include \"gui/editor_input.h\"\n${_editor}")

if(IMGUI_TEMPLATE_OS_LAYER STREQUAL "SDL3")
    list(APPEND APP_SOURCES src/platform/oslayer_sdl3.cpp)
else()
    list(APPEND APP_SOURCES src/platform/oslayer_sfml.cpp)
    if(APPLE)
        list(APPEND APP_SOURCES src/platform/display_mac.mm)
    endif()
endif()

set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/SPIRV-Cross EXCLUDE_FROM_ALL)

add_library(gfxrecon_core STATIC ${CORE_SOURCES})
target_include_directories(gfxrecon_core PUBLIC src thirdparty/json/include)
target_include_directories(gfxrecon_core PRIVATE thirdparty/gfxreconstruct/external/stb thirdparty/SDL/src/video)
target_link_libraries(gfxrecon_core PUBLIC Threads::Threads spirv-cross-glsl)
if(WIN32)
    target_link_libraries(gfxrecon_core PUBLIC shell32 bcrypt)
endif()
if(MSVC)
    target_compile_options(gfxrecon_core PRIVATE /utf-8 /Zc:__cplusplus /Zi)
    target_compile_definitions(gfxrecon_core PUBLIC NOMINMAX PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(APP_LIBS gfxrecon_core ${APP_IMGUI_LIB} ${OSLAYER_LIBS} OpenGL::GL Threads::Threads nfd::nfd spirv-cross-glsl)
if(WIN32)
    list(APPEND APP_LIBS dbghelp imm32 shell32 bcrypt)
endif()
# Tests exe links the instrumented imgui instead of the plain one.
set(TEST_APP_LIBS gfxrecon_core ${TEST_IMGUI_LIB} ${OSLAYER_LIBS} OpenGL::GL Threads::Threads nfd::nfd spirv-cross-glsl)
if(WIN32)
    list(APPEND TEST_APP_LIBS dbghelp imm32 shell32 bcrypt)
endif()

# --- Executables ----------------------------------------------------------------
if(WIN32)
    set(_os_bundle "")
elseif(APPLE)
    set(_os_bundle MACOSX_BUNDLE)
endif()

add_executable(gfxrecon_gui ${_os_bundle} src/main.cpp ${APP_SOURCES})
set_target_properties(gfxrecon_gui PROPERTIES OUTPUT_NAME gfxrecon-gui)
if(MSVC)
    # Launch as a native GUI on double-click while retaining redirected stdout
    # for CLI/package verification through the normal C/C++ main entry point.
    target_link_options(gfxrecon_gui PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()
add_custom_target(gfxrecon_samples ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/samples" "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/samples")
include(cmake/astcenc.cmake)
add_dependencies(gfxrecon_gui gfxrecon_samples gfxrecon_astcenc)
target_include_directories(gfxrecon_gui PRIVATE src thirdparty/ImGuiColorTextEdit thirdparty/json/include thirdparty/SDL/include)
target_link_libraries(gfxrecon_gui PRIVATE ${APP_LIBS})
target_compile_definitions(gfxrecon_gui PRIVATE ${OSLAYER_DEFINE})

add_executable(gfxrecon_gui_tests ${_os_bundle} src/main.cpp ${APP_SOURCES}
    src/tests/register_tests.cpp src/tests/theme_recording.cpp)
add_dependencies(gfxrecon_gui_tests gfxrecon_samples)
if(APPLE)
    target_sources(gfxrecon_gui_tests PRIVATE src/tests/platform_mac.mm)
endif()
target_include_directories(gfxrecon_gui_tests PRIVATE src thirdparty/ImGuiColorTextEdit thirdparty/json/include thirdparty/SDL/include)
target_link_libraries(gfxrecon_gui_tests PRIVATE imgui_te ${TEST_APP_LIBS})
target_compile_definitions(gfxrecon_gui_tests PRIVATE ${OSLAYER_DEFINE} IMGUI_TEMPLATE_TESTS IMGUI_ENABLE_TEST_ENGINE)

foreach(_t gfxrecon_gui gfxrecon_gui_tests)
    if(WIN32)
        target_sources(${_t} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
        target_link_libraries(${_t} PRIVATE dwmapi)
    endif()
    if(MSVC)
        # PDBs in all configs so the crash handler can symbolize Release traces.
        target_compile_options(${_t} PRIVATE /utf-8 /Zc:__cplusplus /Zi)
        target_link_options(${_t} PRIVATE /DEBUG /OPT:REF /OPT:ICF
            "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/cmake/app.manifest")
        target_compile_definitions(${_t} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
endforeach()

if(APPLE)
    foreach(_t gfxrecon_gui gfxrecon_gui_tests)
        set_target_properties(${_t} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "GFXReconstruct Inspector"
            MACOSX_BUNDLE_GUI_IDENTIFIER "app.imgui-template.${_t}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    endforeach()
endif()

enable_testing()
# Agent CLI: console executable over the shared core (same tools, caches and sessions as the GUI).
set(CLI_SOURCES
    src/cli/cli.cpp
    src/cli/commands_core.cpp
    src/cli/commands_query.cpp
    src/cli/commands_dump.cpp
    src/cli/commands_replay.cpp
    src/cli/commands_capture.cpp
)
add_library(gfxrecon_cli_lib STATIC ${CLI_SOURCES})
target_link_libraries(gfxrecon_cli_lib PUBLIC gfxrecon_core)
target_include_directories(gfxrecon_cli_lib PRIVATE thirdparty/gfxreconstruct/external/stb)
add_executable(gfxrecon_cli src/cli/main.cpp)
add_dependencies(gfxrecon_cli gfxrecon_astcenc)
set_target_properties(gfxrecon_cli PROPERTIES OUTPUT_NAME gfxrecon-cli)
target_link_libraries(gfxrecon_cli PRIVATE gfxrecon_cli_lib)
if(MSVC)
    target_compile_options(gfxrecon_cli_lib PRIVATE /utf-8 /Zc:__cplusplus /Zi)
    target_compile_options(gfxrecon_cli PRIVATE /utf-8 /Zc:__cplusplus /Zi)
    target_link_options(gfxrecon_cli PRIVATE /SUBSYSTEM:CONSOLE /DEBUG /OPT:REF /OPT:ICF)
endif()
add_executable(gfxrecon_core_tests src/tests/core_tests.cpp src/tests/analysis_tests.cpp src/tests/cache_tests.cpp src/tests/pipeline_cache_tests.cpp src/tests/optimizer_tests.cpp src/tests/device_tests.cpp src/tests/mesh_tests.cpp src/tests/live_capture_tests.cpp src/tests/replay_performance_tests.cpp src/tests/dump_feasibility.cpp
    src/tests/capture_save_tests.cpp src/shaders/syntax.cpp
    src/tests/export_tests.cpp src/tests/image_decode_tests.cpp src/tests/cli_tests.cpp src/tests/replay_cli_tests.cpp)
target_include_directories(gfxrecon_core_tests PRIVATE src thirdparty/ImGuiColorTextEdit thirdparty/json/include)
target_link_libraries(gfxrecon_core_tests PRIVATE gfxrecon_cli_lib gfxrecon_core imgui Threads::Threads spirv-cross-glsl)
if(MSVC)
    target_compile_options(gfxrecon_core_tests PRIVATE /utf-8)
    target_compile_definitions(gfxrecon_core_tests PRIVATE NOMINMAX)
    target_link_libraries(gfxrecon_core_tests PRIVATE shell32 bcrypt psapi)
endif()
add_dependencies(gfxrecon_core_tests gfxrecon_astcenc gfxrecon_samples)
add_test(NAME core COMMAND gfxrecon_core_tests)
add_test(NAME ui_auto COMMAND gfxrecon_gui_tests --test)
set_tests_properties(ui_auto PROPERTIES TIMEOUT 300 WORKING_DIRECTORY "$<TARGET_FILE_DIR:gfxrecon_gui_tests>")

# --- Install -------------------------------------------------------------------
install(TARGETS gfxrecon_gui RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(TARGETS gfxrecon_cli RUNTIME DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/licenses/" DESTINATION licenses)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/samples/" DESTINATION samples)
