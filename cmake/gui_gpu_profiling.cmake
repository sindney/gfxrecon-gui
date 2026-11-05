# The bundled backend gets GPU timing without modifying the pinned submodule.
function(gfx_gui_gpu_profiling)
    if(NOT TARGET gfxrecon_decode OR NOT TARGET gfxrecon-replay)
        return()
    endif()
    get_property(_root DIRECTORY PROPERTY GFX_GUI_REPLAY_ROOT)
    get_filename_component(_root "${_root}" ABSOLUTE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_root}/cmake/gui_gpu_profiling.h" "${_root}/cmake/gui_gpu_profiling_sha256.h" "${_root}/cmake/gui_frame_counters.h")

    macro(_gfx_gpu_replace _variable _before _after)
        string(FIND "${${_variable}}" "${_before}" _gpu_match)
        if(_gpu_match LESS 0)
            message(FATAL_ERROR "Pinned GPU profiling patch no longer matches: ${_before}")
        endif()
        string(REPLACE "${_before}" "${_after}" ${_variable} "${${_variable}}")
    endmacro()

    get_target_property(_sources gfxrecon_decode SOURCES)
    set(_consumer "")
    foreach(_file IN LISTS _sources)
        if(_file MATCHES "gui_vulkan_replay_consumer_base.cpp$")
            set(_consumer "${_file}")
        endif()
    endforeach()
    if(NOT _consumer)
        message(FATAL_ERROR "GPU profiling requires the bundled generated replay consumer")
    endif()
    file(READ "${_consumer}" _source)
    set(_source "#include \"${_root}/cmake/gui_gpu_profiling.h\"\n${_source}")

    foreach(_swapchain Captured Offscreen Virtual)
        string(TOLOWER "${_swapchain}" _mode)
        _gfx_gpu_replace(_source
            "            swapchain_ = std::make_unique<Vulkan${_swapchain}Swapchain>();"
            "            swapchain_ = std::make_unique<Vulkan${_swapchain}Swapchain>();\n            ::gfx_gui_gpu::SwapchainSelected(\"${_mode}\");")
    endforeach()

    _gfx_gpu_replace(_source "    // Forward device creation to next layer/driver"
        "    ::gfx_gui_gpu::ConfigureDevice(GetInstanceTable(physical_device), physical_device_info->parent_info, physical_device, create_state.modified_create_info, create_state.modified_extensions);\n    // Forward device creation to next layer/driver")
    _gfx_gpu_replace(_source
        "    result = PostCreateDeviceUpdateState(physical_device_info, *replay_device, create_state, device_info);"
        "    result = PostCreateDeviceUpdateState(physical_device_info, *replay_device, create_state, device_info);\n    ::gfx_gui_gpu::CreatedDevice(GetInstanceTable(physical_device), physical_device_info->parent_info, physical_device, *replay_device, result);")
    _gfx_gpu_replace(_source "VulkanReplayConsumerBase::~VulkanReplayConsumerBase()\n{"
        "VulkanReplayConsumerBase::~VulkanReplayConsumerBase()\n{\n    ::gfx_gui_gpu::Finish();")
    _gfx_gpu_replace(_source "    VulkanReplayConsumerBase::WaitDevicesIdle();"
        "    VulkanReplayConsumerBase::WaitDevicesIdle();\n    ::gfx_gui_gpu::ReleaseAfterIdle([this](VkDevice device) { return GetDeviceTable(device); });")
    _gfx_gpu_replace(_source "void VulkanReplayConsumerBase::ReleaseDeviceResources(const VulkanDeviceInfo* device_info)\n{"
        "void VulkanReplayConsumerBase::ReleaseDeviceResources(const VulkanDeviceInfo* device_info)\n{\n    if (device_info) ::gfx_gui_gpu::BeforeDeviceRelease(device_info->handle);")

    set(_submit_before [=[        result = func(
            queue_info->handle, static_cast<uint32_t>(current_submits_span.size()), current_submits_span.data(), fence);]=])
    set(_submit_after [=[        result = ::gfx_gui_gpu::SubmitFrame(func,
            GetDeviceTable(device_info->handle), GetInstanceTable(device_info->parent),
            GetObjectInfoTable(), [this](VkDevice device) { return GetDeviceTable(device); },
            device_info->parent, device_info->handle, queue_info->handle,
            queue_info->capture_id, queue_info->family_index, index,
            static_cast<uint32_t>(current_submits_span.size()), current_submits_span.data(), fence);]=])
    # This exact branch appears once for vkQueueSubmit and once for Submit2/KHR.
    string(LENGTH "${_source}" _source_length)
    string(LENGTH "${_submit_before}" _submit_length)
    string(REPLACE "${_submit_before}" "" _without_submits "${_source}")
    string(LENGTH "${_without_submits}" _without_length)
    math(EXPR _match_count "(${_source_length} - ${_without_length}) / ${_submit_length}")
    if(NOT _match_count EQUAL 2)
        message(FATAL_ERROR "Pinned GPU profiling submit patch expected exactly two driver calls")
    endif()
    _gfx_gpu_replace(_source "${_submit_before}" "${_submit_after}")

    # Effective bytes are checked only after a successful replacement creation.
    set(_shader_before [=[    VkResult vk_res = func(
        device_info->handle, &override_info, GetAllocationCallbacks(pAllocator), pShaderModule->GetHandlePointer());]=])
    set(_shader_after [=[    VkResult vk_res = func(
        device_info->handle, &override_info, GetAllocationCallbacks(pAllocator), pShaderModule->GetHandlePointer());
    ::gfx_gui_gpu::ShaderCreated(handle_id, override_info.pCode, override_info.codeSize, vk_res, file_code != nullptr);]=])
    _gfx_gpu_replace(_source "${_shader_before}" "${_shader_after}")
    file(WRITE "${_consumer}" "${_source}")

    get_target_property(_replay_sources gfxrecon-replay SOURCES)
    set(_feature "")
    foreach(_file IN LISTS _replay_sources)
        if(_file MATCHES "[/\\]replay_vulkan_feature.cpp$")
            set(_feature "${_file}")
        endif()
    endforeach()
    if(NOT _feature)
        message(FATAL_ERROR "Pinned GPU profiling replay Vulkan feature source was not found")
    endif()
    file(READ "${_feature}" _feature_source)
    set(_feature_source "#include \"${_root}/cmake/gui_gpu_profiling.h\"\n${_feature_source}")
    _gfx_gpu_replace(_feature_source "    return { { \"\",\n               { \"Capture the replaying GFXR file. Capture uses the same log options as\","
        "    return { { \"<json>\", { \"Measure isolated frame GPU timestamps using a versioned GUI request\" }, true, \"--gpu-measurement-request\" },\n             { \"\",\n               { \"Capture the replaying GFXR file. Capture uses the same log options as\",")
    _gfx_gpu_replace(_feature_source "    capture_filename_    = capture_filename;"
        "    ::gfx_gui_gpu::Load(arg_parser.GetArgumentValue(\"--gpu-measurement-request\"));\n    capture_filename_    = capture_filename;")
    set(_generated_feature "${CMAKE_BINARY_DIR}/gui_replay_vulkan_feature.cpp")
    file(WRITE "${_generated_feature}" "${_feature_source}")
    list(REMOVE_ITEM _replay_sources "${_feature}")
    set_property(TARGET gfxrecon-replay PROPERTY SOURCES "${_replay_sources}")
    target_sources(gfxrecon-replay PRIVATE "${_generated_feature}")
    get_filename_component(_feature_dir "${_feature}" DIRECTORY)
    target_include_directories(gfxrecon-replay PRIVATE "${_feature_dir}")
    if(MSVC)
        set_property(SOURCE "${_consumer}" TARGET_DIRECTORY gfxrecon_decode APPEND PROPERTY COMPILE_OPTIONS /EHsc)
        set_property(SOURCE "${_generated_feature}" TARGET_DIRECTORY gfxrecon-replay APPEND PROPERTY COMPILE_OPTIONS /EHsc)
    endif()
endfunction()
cmake_language(DEFER CALL gfx_gui_gpu_profiling)
