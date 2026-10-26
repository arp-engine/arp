# Production implementation components shared by root runtime and integration validation.
function(arp_webgl_track_preload_inputs target)
    foreach(directory IN LISTS ARGN)
        if(IS_DIRECTORY "${directory}")
            file(GLOB_RECURSE preload_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${directory}/*")
        else()
            set(preload_inputs "${directory}")
        endif()
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${preload_inputs})
    endforeach()
endfunction()

function(arp_add_webgl_component_libraries)
    set(arp_component_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(ARP_THIRD_PARTY_DIR "${arp_component_root}/3rd")
    include("${arp_component_root}/cmake/ArpMimalloc.cmake")
    add_library(arp_core STATIC
        "${arp_component_root}/src/core/memory/cpu_allocator.cpp"
        "${arp_component_root}/src/core/io/file_io.cpp"
        "${arp_component_root}/src/core/hash/sha256.cpp"
        "${arp_component_root}/src/core/encoding/base64.cpp")
    target_include_directories(arp_core PUBLIC "${arp_component_root}/src")
    target_link_libraries(arp_core PUBLIC arp_mimalloc)
    target_compile_features(arp_core PUBLIC cxx_std_17)
    target_compile_definitions(arp_core PUBLIC ARP_STATIC_LINK=1)
    target_compile_options(arp_core PUBLIC -fexceptions)
    target_link_options(arp_core PUBLIC -fexceptions)
    add_library(arp_fkyaml_headers INTERFACE)
    target_include_directories(arp_fkyaml_headers INTERFACE "${arp_component_root}/3rd/fkYAML/include")
    include("${arp_component_root}/cmake/ArpShaderLibrary.cmake")
    arp_add_shader_library(arp_shader arp_core arp_fkyaml_headers)
    add_library(arp_rhi STATIC
        "${arp_component_root}/src/rhi/conformance/clear_workload.cpp"
        "${arp_component_root}/src/rhi/conformance/sampled_workload.cpp"
        "${arp_component_root}/src/rhi/conformance/storage_mrt_workload.cpp"
        "${arp_component_root}/src/rhi/frontend/resource_identity.cpp"
        "${arp_component_root}/src/rhi/frontend/pipeline_package_reflection.cpp"
        "${arp_component_root}/src/rhi/frontend/descriptor_descriptors.cpp"
        "${arp_component_root}/src/rhi/frontend/pipeline_cache.cpp"
        "${arp_component_root}/src/rhi/frontend/descriptor_cache.cpp"
        "${arp_component_root}/src/rhi/semantics/push_constant_validation.cpp")
    target_include_directories(arp_rhi PUBLIC "${arp_component_root}/src")
    target_link_libraries(arp_rhi PUBLIC arp_core arp_shader)
    include("${arp_component_root}/cmake/ArpWebGlRuntime.cmake")
    arp_add_webgl_runtime(arp_core arp_shader)
    target_link_libraries(arp_rhi_webgl PUBLIC arp_rhi)
    add_library(arp_ordered_graphics STATIC
        "${arp_component_root}/src/renderer/render_graph/original_frame_graph_execution.cpp"
        "${arp_component_root}/src/rhi/semantics/gpu_timing_lowering.cpp"
        "${arp_component_root}/src/renderer/render_graph/compiled_retirement_source.cpp"
        "${arp_component_root}/src/renderer/render_graph/frame_graph_ordered_execution.cpp"
        "${arp_component_root}/src/renderer/render_graph/conformance_sampled_recorder.cpp"
        "${arp_component_root}/src/renderer/render_graph/conformance_storage_mrt_recorder.cpp"
        "${arp_component_root}/src/renderer/render_graph/frame_graph_resource_pool.cpp"
        "${arp_component_root}/src/renderer/render_graph/frame_graph_uniform_ring_buffer.cpp"
        "${arp_component_root}/src/renderer/render_graph/render_graph.cpp"
        "${arp_component_root}/src/renderer/render_graph/transient_resource_allocator.cpp"
        "${arp_component_root}/src/platform/common/render_system_ordered_graphics.cpp")
    target_link_libraries(arp_ordered_graphics PUBLIC arp_rhi)
    target_link_libraries(arp_rhi_webgl PUBLIC arp_ordered_graphics)
    add_library(arp_platform STATIC
        "${arp_component_root}/src/platform/web/render_system_webgl.cpp")
    target_link_libraries(arp_platform PUBLIC arp_rhi_webgl arp_ordered_graphics)
    include("${arp_component_root}/cmake/ArpReflectionLibrary.cmake")
    arp_add_reflection_library(arp_reflection arp_core arp_fkyaml_headers)
    # Original product implementations, with no asset importer/editor/native
    # graphics dependency. The same classes are used by the native 2D target.
    add_library(arp_sprite_product STATIC
        "${arp_component_root}/src/renderer/pipeline/basic_forward_contract.cpp"
        "${arp_component_root}/src/renderer/pipeline/basic_forward_pipeline.cpp"
        "${arp_component_root}/src/renderer/pipeline/render_pipeline.cpp"
        "${arp_component_root}/src/ui/ui_bitmap_font_resource.cpp"
        "${arp_component_root}/src/renderer/gui/gui_ordered_renderer.cpp"
        "${arp_component_root}/src/assets/image_decoder.cpp"
        "${arp_component_root}/src/resource/resource_contract.cpp"
        "${arp_component_root}/src/resource/resource_manager.cpp"
        "${arp_component_root}/src/resource/resource_graph_plan.cpp"
        "${arp_component_root}/src/2d/sprite_resources.cpp"
        "${arp_component_root}/src/renderer/scene_2d_renderer.cpp"
        "${arp_component_root}/src/renderer/scene_2d_ordered.cpp")
    target_include_directories(arp_sprite_product PRIVATE "${arp_component_root}/3rd/stb")
    target_link_libraries(arp_sprite_product PUBLIC arp_ordered_graphics arp_reflection)
endfunction()
