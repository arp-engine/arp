# Shared implementation source, used by the host contracts and isolated Wasm
# runtime/resource/program components. This target does not open the production
# WebGL device factory or claim a complete product dependency closure.
function(arp_add_webgl_runtime core_target shader_target)
    add_library(arp_rhi_webgl STATIC
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_runtime.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_resources.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_program.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_pipeline.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_pipeline_preparation.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_device.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_ordered_graphics.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_descriptors.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_framebuffer.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_canvas.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/rhi/backends/webgl/webgl_execution.cpp")
    target_include_directories(arp_rhi_webgl PUBLIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    target_link_libraries(arp_rhi_webgl PUBLIC ${core_target} ${shader_target})
    target_compile_features(arp_rhi_webgl PUBLIC cxx_std_17)
    target_compile_definitions(arp_rhi_webgl PUBLIC ARP_STATIC_LINK=1)
    if(EMSCRIPTEN)
        target_link_options(arp_rhi_webgl PUBLIC -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=1)
    endif()
endfunction()
