function(gfx_gui_draw_visualization)
    if(NOT TARGET gfxrecon_decode)
        return()
    endif()
    get_property(_root DIRECTORY PROPERTY GFX_GUI_REPLAY_ROOT)
    get_filename_component(_root "${_root}" ABSOLUTE)
    get_target_property(_sources gfxrecon_decode SOURCES)
    foreach(_file IN LISTS _sources)
        if(_file MATCHES "gui_vulkan_replay_consumer_base.cpp$")
            file(READ "${_file}" _source)
            set(_source "#include \"${_root}/cmake/gui_draw_visualization.h\"\n${_source}")
            string(REPLACE "    object_info_table_ = CommonObjectInfoTable::GetSingleton();" "    ::gfx_gui_visual::Load(options_.dump_resources_output_dir, options_.dump_resources_block_indices);\n    object_info_table_ = CommonObjectInfoTable::GetSingleton();" _source "${_source}")
            set(_find "    // Forward device creation to next layer/driver")
            set(_replace [=[    VkPhysicalDeviceFeatures gui_features{};
    // The SDK injects depth/stencil resolve for readback on Vulkan 1.1, but
    // that extension also requires create_renderpass2 below core Vulkan 1.2.
    if (options_.dumping_resources && physical_device_info->parent_info.api_version >= VK_MAKE_VERSION(1,1,0) && physical_device_info->parent_info.api_version < VK_MAKE_VERSION(1,2,0)) {
        auto& extensions=create_state.modified_extensions;
        auto has=[&](const char* name){return std::any_of(extensions.begin(),extensions.end(),[&](const char* value){return std::string(value)==name;});};
        if(has(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME)&&!has(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)){
            uint32_t count=0;auto* table=GetInstanceTable(physical_device);
            table->EnumerateDeviceExtensionProperties(physical_device,nullptr,&count,nullptr);
            std::vector<VkExtensionProperties> available(count);
            table->EnumerateDeviceExtensionProperties(physical_device,nullptr,&count,available.data());
            for(const auto& extension:available)if(std::string(extension.extensionName)==VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME){extensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);break;}
            create_state.modified_create_info.enabledExtensionCount=static_cast<uint32_t>(extensions.size());
            create_state.modified_create_info.ppEnabledExtensionNames=extensions.data();
        }
    }
    if (::gfx_gui_visual::enabled) {
        VkPhysicalDeviceFeatures supported{};
        GetInstanceTable(physical_device)->GetPhysicalDeviceFeatures(physical_device, &supported);
        ::gfx_gui_visual::wire_supported = supported.fillModeNonSolid;
        if (::gfx_gui_visual::mode == "wireframe" && supported.fillModeNonSolid) {
            auto* next = reinterpret_cast<const VkBaseInStructure*>(create_state.modified_create_info.pNext);
            VkPhysicalDeviceFeatures2* features2 = nullptr;
            for (; next; next = next->pNext) if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) features2 = reinterpret_cast<VkPhysicalDeviceFeatures2*>(const_cast<VkBaseInStructure*>(next));
            if (features2) features2->features.fillModeNonSolid = VK_TRUE;
            else {
                if (create_state.modified_create_info.pEnabledFeatures) gui_features = *create_state.modified_create_info.pEnabledFeatures;
                gui_features.fillModeNonSolid = VK_TRUE;
                create_state.modified_create_info.pEnabledFeatures = &gui_features;
            }
        }
    }
    // Forward device creation to next layer/driver]=])
            string(REPLACE "${_find}" "${_replace}" _source "${_source}")
            set(_find "    // If a pipeline cache was created, track it to know when to destroy it/save it to file")
            set(_replace [=[    if (replay_result == VK_SUCCESS && ::gfx_gui_visual::enabled) {
        for (uint32_t i=0; i<create_info_count; ++i)
            ::gfx_gui_visual::Prepare(GetDeviceTable(in_device), func, in_device, pPipelines->GetPointer()[i], out_pipelines[i], maybe_replaced_create_infos[i]);
    }
    // If a pipeline cache was created, track it to know when to destroy it/save it to file]=])
            string(FIND "${_source}" "VkResult VulkanReplayConsumerBase::OverrideCreateGraphicsPipelines(" _start)
            string(FIND "${_source}" "VkResult VulkanReplayConsumerBase::OverrideCreateComputePipelines(" _end)
            math(EXPR _length "${_end}-${_start}")
            string(SUBSTRING "${_source}" 0 ${_start} _prefix)
            string(SUBSTRING "${_source}" ${_start} ${_length} _graphics)
            string(SUBSTRING "${_source}" ${_end} -1 _suffix)
            string(REPLACE "${_find}" "${_replace}" _graphics "${_graphics}")
            set(_source "${_prefix}${_graphics}${_suffix}")
            string(REPLACE "        const auto device_table = GetInjectedDeviceCalls(device);" "        const auto device_table = GetInjectedDeviceCalls(device);\n        ::gfx_gui_visual::Release(GetDeviceTable(device), device);" _source "${_source}")
            string(REPLACE "        VkDevice device = info->handle;\n    });" "        VkDevice device = info->handle;\n        ::gfx_gui_visual::Release(GetDeviceTable(device), device);\n    });" _source "${_source}")
            file(WRITE "${_file}" "${_source}")
        endif()
        if(_file MATCHES "[/\\]vulkan_replay_dump_resources_draw_calls.cpp$")
            file(READ "${_file}" _source)
            set(_source "#include \"${_root}/cmake/gui_draw_visualization.h\"\n${_source}")
            foreach(_call "func(GetWorkCommandBuffer(), vertex_count, instance_count, first_vertex, first_instance);" "func(GetWorkCommandBuffer(), index_count, instance_count, first_index, vertex_offset, first_instance);" "func(GetWorkCommandBuffer(), buffer_info->handle, offset, draw_count, stride);")
                set(_replace "if (must_dump && ::gfx_gui_visual::enabled) { auto table=device_table_.Open(); ::gfx_gui_visual::Execute(table, bound_gr_pipeline_ ? bound_gr_pipeline_->handle : VK_NULL_HANDLE, GetWorkCommandBuffer(), [&]{ ${_call} }); } else ${_call}")
                string(REPLACE "${_call}" "${_replace}" _source "${_source}")
            endforeach()
            string(REPLACE "    // Copy vertex attribute info" "    // Copy vertex attribute info" _source "${_source}")
            string(REPLACE "        dc_params = InsertNewDrawIndirectParameters(dc_index, buffer_info, offset, draw_count, stride);" "        if (::gfx_gui_visual::enabled && ::gfx_gui_visual::subdraw > 0 && uint32_t(::gfx_gui_visual::subdraw) < draw_count) func(GetWorkCommandBuffer(), buffer_info->handle, offset, uint32_t(::gfx_gui_visual::subdraw), stride);\n        ::gfx_gui_visual::SelectIndirect(true, offset, draw_count, stride);\n        dc_params = InsertNewDrawIndirectParameters(dc_index, buffer_info, offset, draw_count, stride);" _source "${_source}")
            string(REPLACE "        dc_params = InsertNewDrawIndexedIndirectParameters(dc_index, buffer_info, offset, draw_count, stride);" "        if (::gfx_gui_visual::enabled && ::gfx_gui_visual::subdraw > 0 && uint32_t(::gfx_gui_visual::subdraw) < draw_count) func(GetWorkCommandBuffer(), buffer_info->handle, offset, uint32_t(::gfx_gui_visual::subdraw), stride);\n        ::gfx_gui_visual::SelectIndirect(true, offset, draw_count, stride);\n        dc_params = InsertNewDrawIndexedIndirectParameters(dc_index, buffer_info, offset, draw_count, stride);" _source "${_source}")
            set(_count_call "func(GetWorkCommandBuffer(),\n         buffer_info->handle,\n         offset,\n         count_buffer_info->handle,\n         count_buffer_offset,\n         max_draw_count,\n         stride);")
            string(REPLACE "        dc_params = InsertNewIndirectCountParameters(dc_index," "        if (::gfx_gui_visual::enabled && ::gfx_gui_visual::subdraw >= 0) { ::gfx_gui_visual::error=\"Individual GPU-counted indirect draws are not supported; select All draws\"; ::gfx_gui_visual::Report(); }\n        dc_params = InsertNewIndirectCountParameters(dc_index," _source "${_source}")
            string(REPLACE "${_count_call}" "if (must_dump && ::gfx_gui_visual::enabled) { auto table=device_table_.Open(); ::gfx_gui_visual::Execute(table, bound_gr_pipeline_ ? bound_gr_pipeline_->handle : VK_NULL_HANDLE, GetWorkCommandBuffer(), [&]{ ${_count_call} }); } else ${_count_call}" _source "${_source}")
            file(WRITE "${CMAKE_BINARY_DIR}/gui_vulkan_draw_calls.cpp" "${_source}")
            get_target_property(_current gfxrecon_decode SOURCES)
            list(REMOVE_ITEM _current "${_file}")
            set_property(TARGET gfxrecon_decode PROPERTY SOURCES "${_current}")
            target_sources(gfxrecon_decode PRIVATE "${CMAKE_BINARY_DIR}/gui_vulkan_draw_calls.cpp")
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL gfx_gui_draw_visualization)
