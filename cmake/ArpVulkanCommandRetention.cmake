# RHI-owned tests, independent of cloud/example target registration.
include("${CMAKE_CURRENT_LIST_DIR}/ArpAllocatorBacking.cmake")
if(TARGET arp_rhi_vulkan AND NOT APPLE AND NOT EMSCRIPTEN)
    target_sources(arp_rhi_vulkan PRIVATE
        src/rhi/backends/vulkan/vulkan_original_graphics_pipeline.cpp
        src/rhi/backends/vulkan/vulkan_original_compute_pipeline.cpp
        src/rhi/backends/vulkan/vulkan_original_graphics_binding.cpp)
    add_executable(arp_rhi_vulkan_original_graphics_tests tests/integration/rhi_vulkan_original_graphics_tests.cpp)
    target_link_libraries(arp_rhi_vulkan_original_graphics_tests PRIVATE arp_rhi_vulkan)
    target_include_directories(arp_rhi_vulkan_original_graphics_tests PRIVATE .)
    target_compile_options(arp_rhi_vulkan_original_graphics_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_vulkan_original_graphics_tests PRIVATE
        ARP_ORIGINAL_CLOUD_MODULE="${original_cloud_module}"
        ARP_ORIGINAL_CLOUD_SPATIAL_MODULE="${original_cloud_spatial_module}"
        ARP_ORIGINAL_CLOUD_COMPACT_MODULE="${original_cloud_compact_module}")
    target_compile_definitions(arp_rhi_vulkan_original_graphics_tests PRIVATE
        ARP_ORIGINAL_CLOUD_RAYMARCH="${original_cloud_compute_dir}/cloud_raymarch.computeMain.entry.yaml"
        ARP_ORIGINAL_CLOUD_SPATIAL_RESOLVE="${original_cloud_compute_dir}/cloud_spatial_resolve.computeMain.entry.yaml"
        ARP_ORIGINAL_CLOUD_COMPACT_RESOLVE="${original_cloud_compute_dir}/cloud_compact_resolve.computeMain.entry.yaml"
        ARP_ORIGINAL_CLOUD_ASSET_SLICE="${original_cloud_compute_dir}/cloud_asset_slice.computeMain.entry.yaml"
        ARP_ORIGINAL_CLOUD_TEMPORAL="${original_cloud_compute_dir}/cloud_temporal.computeMain.entry.yaml")
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_rhi_vulkan_original_graphics_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_rhi_vulkan_original_graphics_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_dependencies(arp_rhi_vulkan_original_graphics_tests arp_rhi_original_cloud_graphics_module)
    add_dependencies(arp_rhi_vulkan_original_graphics_tests arp_rhi_original_cloud_spatial_graphics_module arp_rhi_original_cloud_compact_graphics_module)
    add_dependencies(arp_rhi_vulkan_original_graphics_tests arp_rhi_original_cloud_compute_entries)
    arp_add_mingw_static_runtime(arp_rhi_vulkan_original_graphics_tests)
    add_test(NAME arp_rhi_original_graphics_creation_contract COMMAND arp_rhi_vulkan_original_graphics_tests --contract)
    add_test(NAME arp_rhi_original_graphics_creation_null_smoke COMMAND arp_rhi_vulkan_original_graphics_tests --null-hidden-frame)
    add_test(NAME arp_rhi_vulkan_original_graphics_creation_native COMMAND arp_rhi_vulkan_original_graphics_tests)
    set_tests_properties(arp_rhi_vulkan_original_graphics_creation_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_original_compute_creation_contract COMMAND arp_rhi_vulkan_original_graphics_tests --compute-contract)
    add_test(NAME arp_rhi_original_compute_creation_null_smoke COMMAND arp_rhi_vulkan_original_graphics_tests --compute-null-hidden-frame)
    add_test(NAME arp_rhi_vulkan_original_compute_creation_native COMMAND arp_rhi_vulkan_original_graphics_tests --compute)
    set_tests_properties(arp_rhi_vulkan_original_compute_creation_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_ordinary_mixed_contract COMMAND arp_rhi_vulkan_original_graphics_tests --contract)
    add_test(NAME arp_rhi_ordinary_mixed_null_smoke COMMAND arp_rhi_vulkan_original_graphics_tests --null-hidden-frame)
    add_test(NAME arp_rhi_vulkan_ordinary_mixed_native COMMAND arp_rhi_vulkan_original_graphics_tests --mixed)
    set_tests_properties(arp_rhi_vulkan_ordinary_mixed_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    add_test(NAME arp_rhi_vulkan_original_prepared_device_native COMMAND arp_rhi_vulkan_original_graphics_tests --mixed-device)
    set_tests_properties(arp_rhi_vulkan_original_prepared_device_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    if(TARGET arp_renderer)
        add_executable(arp_cloud_original_frame_graph_tests tests/integration/rhi_vulkan_original_graphics_tests.cpp)
        target_link_libraries(arp_cloud_original_frame_graph_tests PRIVATE arp_renderer arp_rhi_vulkan arp_scriptvm_runtime)
        target_include_directories(arp_cloud_original_frame_graph_tests PRIVATE .)
        target_compile_options(arp_cloud_original_frame_graph_tests PRIVATE -UNDEBUG)
        get_target_property(original_test_definitions arp_rhi_vulkan_original_graphics_tests COMPILE_DEFINITIONS)
        target_compile_definitions(arp_cloud_original_frame_graph_tests PRIVATE ${original_test_definitions} ARP_CLOUD_FRAME_GRAPH_TEST=1)
        add_test(NAME arp_cloud_cooked_asset_slices_native COMMAND arp_cloud_original_frame_graph_tests
            --cooked-asset-slices "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml")
        set_tests_properties(arp_cloud_cooked_asset_slices_native PROPERTIES
            FIXTURES_REQUIRED cloud_reference_cook SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "cloud;resource;native;slice")
        target_compile_definitions(arp_cloud_original_frame_graph_tests PRIVATE
            ARP_CLOUD_FOREGROUND_MODULE="${cloud_foreground_module}"
            ARP_SCENE_OPAQUE_MODULE="${scene_opaque_module}"
            ARP_SCENE_PBR_MODULE="${scene_pbr_module}")
        if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
            target_include_directories(arp_cloud_original_frame_graph_tests PRIVATE "C:/Program Files/RenderDoc")
        endif()
        add_dependencies(arp_cloud_original_frame_graph_tests arp_rhi_original_cloud_graphics_module arp_rhi_original_cloud_compute_entries)
        add_dependencies(arp_cloud_original_frame_graph_tests arp_rhi_original_cloud_spatial_graphics_module arp_rhi_original_cloud_compact_graphics_module)
        add_dependencies(arp_cloud_original_frame_graph_tests arp_cloud_foreground_module)
        add_dependencies(arp_cloud_original_frame_graph_tests arp_scene_opaque_module)
        add_dependencies(arp_cloud_original_frame_graph_tests arp_scene_pbr_module)
        add_test(NAME arp_scene_pbr_sampling_native COMMAND arp_cloud_original_frame_graph_tests --pbr-sampling)
        set_tests_properties(arp_scene_pbr_sampling_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "renderer;pbr;upload;native")
        foreach(camera perspective orthographic)
            if(camera STREQUAL "orthographic")
                set(scene_opaque_mode --feature-owned-history-ortho)
            else()
                set(scene_opaque_mode --feature-owned-history)
            endif()
            add_test(NAME arp_cloud_scene_opaque_${camera}_native
                COMMAND arp_cloud_original_frame_graph_tests ${scene_opaque_mode} compact)
            set_tests_properties(arp_cloud_scene_opaque_${camera}_native PROPERTIES
                ENVIRONMENT "ARP_CLOUD_SCENE_OPAQUE=1;ARP_CLOUD_SCENE_RESOURCE_MODE=alias"
                SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "renderer;cloud;scene-opaque;native")
        endforeach()
        arp_add_mingw_static_runtime(arp_cloud_original_frame_graph_tests)
        add_test(NAME arp_cloud_original_frame_graph_contract COMMAND arp_cloud_original_frame_graph_tests --contract)
        add_test(NAME arp_cloud_original_frame_graph_null_smoke COMMAND arp_cloud_original_frame_graph_tests --null-hidden-frame)
        foreach(reference_case clear sparse thick below inside above horizon backlit occlusion orthographic)
            add_test(NAME arp_cloud_p03_reference_${reference_case}
                COMMAND arp_cloud_original_frame_graph_tests --reference-case ${reference_case})
            set_tests_properties(arp_cloud_p03_reference_${reference_case} PROPERTIES
                ENVIRONMENT "ARP_CLOUD_SCENE_RESOURCE_MODE=id"
                SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "cloud;p03;reference;native")
        endforeach()
        add_test(NAME arp_cloud_mixed_pass_timing_program COMMAND arp_cloud_original_frame_graph_tests --mixed-timing-program)
        add_test(NAME arp_cloud_mixed_pass_timing_native COMMAND arp_cloud_original_frame_graph_tests --mixed-timing-native)
        add_test(NAME arp_cloud_frame_graph_pass_timing_native COMMAND arp_cloud_original_frame_graph_tests --frame-graph-pass-timing)
        add_test(NAME arp_cloud_frame_graph_depth_timing_compact
            COMMAND arp_cloud_original_frame_graph_tests --frame-graph-depth-timing-host-device compact)
        set_tests_properties(arp_cloud_frame_graph_depth_timing_compact PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200 LABELS "cloud;native;history;compact")
        foreach(resolve temporal reconstruction)
            add_test(NAME arp_cloud_frame_graph_depth_timing_${resolve}
                COMMAND arp_cloud_original_frame_graph_tests --frame-graph-depth-timing ${resolve})
            set_tests_properties(arp_cloud_frame_graph_depth_timing_${resolve}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200 LABELS "renderer;timing;native;history;opaque-depth")
            add_test(NAME arp_cloud_frame_graph_depth_timing_host_device_${resolve}
                COMMAND arp_cloud_original_frame_graph_tests --frame-graph-depth-timing-host-device ${resolve})
            set_tests_properties(arp_cloud_frame_graph_depth_timing_host_device_${resolve}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200 LABELS "renderer;timing;native;history;placement")
        endforeach()
        set_tests_properties(arp_cloud_frame_graph_pass_timing_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "renderer;timing;native")
        set_tests_properties(arp_cloud_mixed_pass_timing_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "rhi;timing;native")
        set_tests_properties(arp_cloud_mixed_pass_timing_program PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "rhi;timing;program-projection")
        add_test(NAME arp_cloud_original_frame_graph_native COMMAND arp_cloud_original_frame_graph_tests --feature-graph)
        foreach(scene_mode id alias)
            add_test(NAME arp_cloud_scene_feature_${scene_mode}_native
                COMMAND arp_cloud_original_frame_graph_tests --feature-owned-history compact)
            set_tests_properties(arp_cloud_scene_feature_${scene_mode}_native PROPERTIES
                TIMEOUT 1200 ENVIRONMENT "ARP_CLOUD_SCENE_RESOURCE_MODE=${scene_mode}"
                LABELS "cloud;scene;native;history")
        endforeach()
        foreach(resolve temporal reconstruction compact)
            add_test(NAME arp_cloud_renderer_cold_pipelines_${resolve}
                COMMAND arp_cloud_original_frame_graph_tests --feature-cold-pipelines ${resolve})
            set_tests_properties(arp_cloud_renderer_cold_pipelines_${resolve}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
            foreach(camera perspective orthographic)
                if(camera STREQUAL "orthographic")
                    set(owned_history_argument --feature-owned-history-ortho)
                else()
                    set(owned_history_argument --feature-owned-history)
                endif()
                add_test(NAME arp_cloud_renderer_history_${resolve}_${camera}
                    COMMAND arp_cloud_original_frame_graph_tests ${owned_history_argument} ${resolve})
                set_tests_properties(arp_cloud_renderer_history_${resolve}_${camera}
                    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200)
            endforeach()
        endforeach()
        add_test(NAME arp_cloud_original_spatial_pipeline_native COMMAND arp_cloud_original_frame_graph_tests --compute)
        set_tests_properties(arp_cloud_original_spatial_pipeline_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        add_test(NAME arp_cloud_original_history_sequence COMMAND arp_cloud_original_frame_graph_tests --feature-history)
        set_tests_properties(arp_cloud_original_history_sequence PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        foreach(quality reference high medium medium-full-probe medium-spatial)
            add_test(NAME arp_cloud_original_quality_${quality}
                COMMAND arp_cloud_original_frame_graph_tests --feature-quality ${quality})
            set_tests_properties(arp_cloud_original_quality_${quality}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200)
        endforeach()
        foreach(camera perspective orthographic)
            add_test(NAME arp_cloud_original_spatial_motion_${camera}
                COMMAND arp_cloud_original_frame_graph_tests --feature-spatial-motion ${camera})
            set_tests_properties(arp_cloud_original_spatial_motion_${camera}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200)
            add_test(NAME arp_cloud_original_moving_occlusion_${camera}
                COMMAND arp_cloud_original_frame_graph_tests --feature-moving-occlusion ${camera})
            set_tests_properties(arp_cloud_original_moving_occlusion_${camera}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        endforeach()
        foreach(scene below inside above)
            add_test(NAME arp_cloud_original_spatial_quality_${scene}
                COMMAND arp_cloud_original_frame_graph_tests --feature-spatial-quality ${scene})
            set_tests_properties(arp_cloud_original_spatial_quality_${scene}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200)
        endforeach()
        foreach(probe ray-short resolve-short resolve-offset)
            add_test(NAME arp_cloud_original_density_range_${probe}
                COMMAND arp_cloud_original_frame_graph_tests --feature-density-range ${probe})
            set_tests_properties(arp_cloud_original_density_range_${probe}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        endforeach()
        set_tests_properties(arp_cloud_original_frame_graph_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        foreach(cloud_case RANGE 1 6)
            add_test(NAME arp_cloud_original_reference_${cloud_case} COMMAND arp_cloud_original_frame_graph_tests --feature-cloud ${cloud_case})
            set_tests_properties(arp_cloud_original_reference_${cloud_case} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        endforeach()
        foreach(cloud_case RANGE 4 6)
            add_test(NAME arp_cloud_original_foreground_${cloud_case}
                COMMAND arp_cloud_original_frame_graph_tests --feature-foreground ${cloud_case})
            set_tests_properties(arp_cloud_original_foreground_${cloud_case}
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
        endforeach()
    endif()
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_render_pass_storage.cpp)
    add_executable(arp_rhi_vulkan_render_pass_storage_tests tests/integration/rhi_vulkan_render_pass_storage_tests.cpp)
    target_link_libraries(arp_rhi_vulkan_render_pass_storage_tests PRIVATE arp_rhi_vulkan)
    target_include_directories(arp_rhi_vulkan_render_pass_storage_tests PRIVATE .)
    target_compile_options(arp_rhi_vulkan_render_pass_storage_tests PRIVATE -UNDEBUG)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_rhi_vulkan_render_pass_storage_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_rhi_vulkan_render_pass_storage_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    arp_add_mingw_static_runtime(arp_rhi_vulkan_render_pass_storage_tests)
    add_test(NAME arp_rhi_vulkan_render_pass_storage_native COMMAND arp_rhi_vulkan_render_pass_storage_tests)
    set_tests_properties(arp_rhi_vulkan_render_pass_storage_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_compute.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_state.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_clear.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_bindings.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_graphics.cpp)
    add_executable(arp_rhi_vulkan_command_retention_tests
        tests/integration/rhi_vulkan_command_retention_tests.cpp)
    target_link_libraries(arp_rhi_vulkan_command_retention_tests PRIVATE arp_rhi_vulkan)
    target_include_directories(arp_rhi_vulkan_command_retention_tests PRIVATE .)
    target_compile_options(arp_rhi_vulkan_command_retention_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_rhi_vulkan_command_retention_tests)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_rhi_vulkan_command_retention_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_rhi_vulkan_command_retention_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_rhi_vulkan_command_retention_contract
        COMMAND arp_rhi_vulkan_command_retention_tests --contract)
    add_test(NAME arp_rhi_vulkan_command_retention_native
        COMMAND arp_rhi_vulkan_command_retention_tests)
    add_test(NAME arp_rhi_vulkan_original_program_custody_native
        COMMAND arp_rhi_vulkan_command_retention_tests --original-program-custody)
    set_tests_properties(arp_rhi_vulkan_original_program_custody_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_ordinary_texture_upload_native
        COMMAND arp_rhi_vulkan_command_retention_tests --ordinary-texture-upload)
    set_tests_properties(arp_rhi_vulkan_ordinary_texture_upload_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_public_texture_upload_native
        COMMAND arp_rhi_vulkan_command_retention_tests --public-texture-upload)
    set_tests_properties(arp_rhi_vulkan_public_texture_upload_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS "rhi;upload;native")
    add_test(NAME arp_rhi_vulkan_bounded_debug_labels_native
        COMMAND arp_rhi_vulkan_command_retention_tests --ordinary-debug-labels)
    add_test(NAME arp_rhi_vulkan_cold_staging_journal_native
        COMMAND arp_rhi_vulkan_command_retention_tests --staging-journal)
    add_test(NAME arp_rhi_vulkan_cold_staging_pool_native
        COMMAND arp_rhi_vulkan_command_retention_tests --staging-pool)
    set_tests_properties(arp_rhi_vulkan_cold_staging_pool_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_texture_pool_initialization_native
        COMMAND arp_rhi_vulkan_command_retention_tests --staging-pool-initialization)
    set_tests_properties(arp_rhi_vulkan_texture_pool_initialization_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_texture_pool_registered_native
        COMMAND arp_rhi_vulkan_command_retention_tests --staging-pool-registered)
    set_tests_properties(arp_rhi_vulkan_texture_pool_registered_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_cold_staging_journal_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_bounded_debug_labels_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_timestamp_host_reset_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-reset)
    add_test(NAME arp_rhi_vulkan_timestamp_completion_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-completion)
    add_test(NAME arp_rhi_vulkan_original_query_creation_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-original)
    add_test(NAME arp_rhi_vulkan_original_query_source_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-source)
    add_test(NAME arp_rhi_vulkan_original_query_storage_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-storage)
    add_test(NAME arp_rhi_vulkan_original_query_journal_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-journal)
    set_tests_properties(arp_rhi_vulkan_original_query_journal_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_original_query_mixed_journal_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-mixed-journal)
    set_tests_properties(arp_rhi_vulkan_original_query_mixed_journal_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_original_query_submit_rollback_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-submit-rollback)
    set_tests_properties(arp_rhi_vulkan_original_query_submit_rollback_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_original_query_profiling_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-profiling)
    set_tests_properties(arp_rhi_vulkan_original_query_profiling_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME arp_rhi_vulkan_query_slot_lifecycle_native
        COMMAND arp_rhi_vulkan_command_retention_tests --query-slots)
    set_tests_properties(arp_rhi_vulkan_query_slot_lifecycle_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_command_retention_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_timestamp_host_reset_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_timestamp_completion_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_original_query_creation_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_original_query_source_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set_tests_properties(arp_rhi_vulkan_original_query_storage_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()
