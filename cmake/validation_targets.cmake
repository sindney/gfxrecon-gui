# Included after the pinned upstream project() by the backend build helper.
if(NOT PROJECT_NAME STREQUAL "GFXReconstruct" OR TARGET gfxrecon-gui-secondary)
    return()
endif()

get_filename_component(_gui_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${_gui_root}/cmake/gui_capture_control.cmake")
include("${_gui_root}/cmake/gui_replay_cache.cmake")
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND EXISTS "${_gui_root}/local-tools.json")
    file(READ "${_gui_root}/local-tools.json" _mesh_tools)
    string(JSON _mesh_compiler GET "${_mesh_tools}" compiler)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${_gui_root}/scripts/generate_mesh_workload.py"
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${_mesh_compiler}" COMMAND_ERROR_IS_FATAL ANY)
    add_executable(gfxrecon-gui-mesh "${_gui_root}/tests/workloads/secondary_main.cpp" "${CMAKE_BINARY_DIR}/gui-mesh/app.cpp")
    target_include_directories(gfxrecon-gui-mesh PRIVATE "${CMAKE_BINARY_DIR}/gui-mesh")
    target_link_libraries(gfxrecon-gui-mesh PRIVATE gfxrecon-testapp-base)
    target_compile_definitions(gfxrecon-gui-mesh PRIVATE GFXRECON_GUI_MESH)
    add_executable(gfxrecon-gui-indirect "${_gui_root}/tests/workloads/secondary_main.cpp" "${CMAKE_BINARY_DIR}/gui-indirect/app.cpp")
    target_include_directories(gfxrecon-gui-indirect PRIVATE "${CMAKE_BINARY_DIR}/gui-indirect")
    target_link_libraries(gfxrecon-gui-indirect PRIVATE gfxrecon-testapp-base)
    target_compile_definitions(gfxrecon-gui-indirect PRIVATE GFXRECON_GUI_MESH)
    set_target_properties(gfxrecon-gui-indirect PROPERTIES CXX_STANDARD 20 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
    if(MSVC)
        target_compile_options(gfxrecon-gui-indirect PRIVATE /utf-8)
        target_compile_definitions(gfxrecon-gui-indirect PRIVATE NOMINMAX)
    endif()
    file(READ "${CMAKE_BINARY_DIR}/gui-mesh/app.cpp" _live_source)
    string(REGEX REPLACE "const int NUM_FRAMES = [0-9]+;" "const int NUM_FRAMES = 1000000;" _live_source "${_live_source}")
    file(WRITE "${CMAKE_BINARY_DIR}/gui-mesh/live.cpp" "${_live_source}")
    add_executable(gfxrecon-gui-live "${_gui_root}/tests/workloads/secondary_main.cpp" "${CMAKE_BINARY_DIR}/gui-mesh/live.cpp")
    target_include_directories(gfxrecon-gui-live PRIVATE "${CMAKE_BINARY_DIR}/gui-mesh")
    target_link_libraries(gfxrecon-gui-live PRIVATE gfxrecon-testapp-base)
    target_compile_definitions(gfxrecon-gui-live PRIVATE GFXRECON_GUI_MESH)
    set_target_properties(gfxrecon-gui-live PROPERTIES CXX_STANDARD 20 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
    set_target_properties(gfxrecon-gui-mesh PROPERTIES CXX_STANDARD 20 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
    if(MSVC)
        target_compile_options(gfxrecon-gui-mesh PRIVATE /utf-8)
        target_compile_definitions(gfxrecon-gui-mesh PRIVATE NOMINMAX)
        target_compile_options(gfxrecon-gui-live PRIVATE /utf-8)
        target_compile_definitions(gfxrecon-gui-live PRIVATE NOMINMAX)
    endif()
endif()
file(READ "${PROJECT_SOURCE_DIR}/test/test_apps/triangle/app.cpp" _triangle)
set(_draw "        init.disp.cmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);\n\n        init.disp.cmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);\n\n        init.disp.cmdDraw(command_buffer, 3, 1, 0, 0);")
set(_secondary [=[
        // Record a genuine secondary and invoke it twice with additive blending.
        // The first/second dump must contain measurably different pixel values.
        auto secondary_allocate = allocate_info;
        secondary_allocate.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        VkCommandBuffer secondary;
        result = init.disp.allocateCommandBuffers(&secondary_allocate, &secondary);
        VERIFY_VK_RESULT("secondary allocation failed", result);
        VkCommandBufferInheritanceInfo inheritance{};
        inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inheritance.renderPass = render_pass_;
        inheritance.framebuffer = framebuffers_[image_index];
        VkCommandBufferBeginInfo secondary_begin{};
        secondary_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        secondary_begin.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        secondary_begin.pInheritanceInfo = &inheritance;
        result = init.disp.beginCommandBuffer(secondary, &secondary_begin);
        VERIFY_VK_RESULT("secondary begin failed", result);
        init.disp.cmdSetViewport(secondary, 0, 1, &viewport);
        init.disp.cmdSetScissor(secondary, 0, 1, &scissor);
        init.disp.cmdBindPipeline(secondary, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
        init.disp.cmdDraw(secondary, 3, 1, 0, 0);
        result = init.disp.endCommandBuffer(secondary);
        VERIFY_VK_RESULT("secondary end failed", result);
        init.disp.cmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
        init.disp.cmdExecuteCommands(command_buffer, 1, &secondary);
        init.disp.cmdExecuteCommands(command_buffer, 1, &secondary);
]=])
string(FIND "${_triangle}" "${_draw}" _position)
if(_position LESS 0)
    message(FATAL_ERROR "Pinned triangle source changed; revalidate the secondary fixture generator")
endif()
string(REPLACE "${_draw}" "${_secondary}" _triangle "${_triangle}")
string(REPLACE "colorBlendAttachment.blendEnable = VK_FALSE;" [=[colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;]=] _triangle "${_triangle}")
file(WRITE "${CMAKE_BINARY_DIR}/gui-secondary-triangle.cpp" "// Modified validation copy: secondary invocation and additive blending.\n${_triangle}")
add_executable(gfxrecon-gui-secondary "${_gui_root}/tests/workloads/secondary_main.cpp" "${CMAKE_BINARY_DIR}/gui-secondary-triangle.cpp")
target_include_directories(gfxrecon-gui-secondary PRIVATE "${PROJECT_SOURCE_DIR}/test/test_apps/triangle")
target_link_libraries(gfxrecon-gui-secondary PRIVATE gfxrecon-testapp-base)
set_target_properties(gfxrecon-gui-secondary PROPERTIES CXX_STANDARD 20 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/validation")
if(MSVC)
    target_compile_options(gfxrecon-gui-secondary PRIVATE /utf-8)
    target_compile_definitions(gfxrecon-gui-secondary PRIVATE NOMINMAX)
endif()
