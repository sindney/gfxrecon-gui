function(gfx_gui_dump_readback)
    if(NOT TARGET gfxrecon_decode)
        return()
    endif()
    get_target_property(_sources gfxrecon_decode SOURCES)
    foreach(_file IN LISTS _sources)
        if(_file MATCHES "[/\\]vulkan_replay_dump_resources_common.cpp$")
            set(_original "${_file}")
        endif()
    endforeach()
    if(NOT _original)
        return()
    endif()
    file(READ "${_original}" _source)
    set(_find "        DumpedHostData raw_data;")
    set(_replace [=[        GFXRECON_LOG_INFO("GUI readback image id=%llu extent=%ux%u layers=%u mips=%u bytes=%llu",
            static_cast<unsigned long long>(image_info->capture_id), image_info->extent.width, image_info->extent.height,
            image_info->layer_count, image_info->level_count, static_cast<unsigned long long>(image_resource.resource_size));
        DumpedHostData raw_data;]=])
    string(FIND "${_source}" "${_find}" _at)
    if(_at LESS 0)
        message(FATAL_ERROR "Pinned image readback instrumentation no longer matches")
    endif()
    string(REPLACE "${_find}" "${_replace}" _source "${_source}")
    set(_find "    VkResult res = resource_util.ReadFromBufferResource(\n        dumped_buffer.buffer_info.handle")
    set(_replace [=[    GFXRECON_LOG_INFO("GUI readback buffer id=%llu offset=%llu bytes=%llu",
        static_cast<unsigned long long>(dumped_buffer.buffer_info.capture_id),
        static_cast<unsigned long long>(dumped_buffer.offset), static_cast<unsigned long long>(dumped_buffer.size));
    VkResult res = resource_util.ReadFromBufferResource(
        dumped_buffer.buffer_info.handle]=])
    string(FIND "${_source}" "${_find}" _at)
    if(_at LESS 0)
        message(FATAL_ERROR "Pinned buffer readback instrumentation no longer matches")
    endif()
    string(REPLACE "${_find}" "${_replace}" _source "${_source}")
    get_property(_root DIRECTORY PROPERTY GFX_GUI_REPLAY_ROOT)
    get_filename_component(_root "${_root}" ABSOLUTE)
    set(_source "#include \"${_root}/src/analysis/image_readback_layout.h\"\n${_source}")
    set(_find "    data.resize(total_dumped_subresources);")
    set(_replace [=[    const auto readback = ::gfx::PlanImageReadback(is_3d, image_info->level_count, image_info->layer_count,
        modified_subresource_range.base_mip_level, modified_subresource_range.level_count,
        modified_subresource_range.base_array_layer, modified_subresource_range.layer_count);
    if (!readback.levels || !readback.layers) return VK_ERROR_INITIALIZATION_FAILED;
    data.resize(total_dumped_subresources);]=])
    string(REPLACE "${_find}" "${_replace}" _source "${_source}")
    string(REPLACE "image_resource.level_count                                  = image_info->level_count;" "image_resource.level_count                                  = readback.levels;" _source "${_source}")
    string(REPLACE "image_resource.layer_count                                  = image_info->layer_count;" "image_resource.layer_count                                  = readback.layers;\n        image_resource.base_layer = readback.baseLayer;" _source "${_source}")
    string(REPLACE "                                                                                           image_info->level_count,\n                                                                                           image_info->layer_count," "                                                                                           readback.levels,\n                                                                                           readback.layers," _source "${_source}")
    string(REPLACE "(mip * image_info->layer_count + z)" "readback.Index(mip, z)" _source "${_source}")
    file(WRITE "${CMAKE_BINARY_DIR}/gui_vulkan_dump_resources_common.cpp" "${_source}")
    list(REMOVE_ITEM _sources "${_original}")
    set_property(TARGET gfxrecon_decode PROPERTY SOURCES "${_sources}")
    target_sources(gfxrecon_decode PRIVATE "${CMAKE_BINARY_DIR}/gui_vulkan_dump_resources_common.cpp")
    foreach(_file IN LISTS _sources)
        if(_file MATCHES "[/\\]vulkan_replay_dump_resources_delegate.cpp$")
            file(READ "${_file}" _delegate)
            set(_find "        if (output_image_format != KFormatRaw)")
            set(_replace [=[        // Preserve scalar depth before the display encoder quantizes it to RGB8.
        if (sub_res.aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
            util::bufferwriter::WriteBuffer(filename + ".depth.bin", image_dumped_data[i].data(), image_dumped_data[i].size());
        }
        if (output_image_format != KFormatRaw)]=])
            string(FIND "${_delegate}" "${_find}" _at)
            if(_at LESS 0)
                message(FATAL_ERROR "Pinned depth sidecar hook no longer matches")
            endif()
            string(REPLACE "${_find}" "${_replace}" _delegate "${_delegate}")
            file(WRITE "${CMAKE_BINARY_DIR}/gui_vulkan_dump_resources_delegate.cpp" "${_delegate}")
            get_target_property(_current gfxrecon_decode SOURCES)
            list(REMOVE_ITEM _current "${_file}")
            set_property(TARGET gfxrecon_decode PROPERTY SOURCES "${_current}")
            target_sources(gfxrecon_decode PRIVATE "${CMAKE_BINARY_DIR}/gui_vulkan_dump_resources_delegate.cpp")
        endif()
    endforeach()

endfunction()
cmake_language(DEFER CALL gfx_gui_dump_readback)
