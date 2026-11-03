function(arp_add_native_gui_shader_source_validation)
    if(EMSCRIPTEN OR NOT SLANGC_EXECUTABLE OR NOT SPIRV_REFLECT_EXECUTABLE)
        return()
    endif()
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/native-gui-source-fixture")
    set(publication "${directory}/gui_overlay.artifact-set.yaml")
    add_custom_command(OUTPUT "${publication}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py" ${ARP_SHADER_TOOL_INPUT_ARGS}
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --out-dir "${directory}" --module gui_overlay --graphics-module
        DEPENDS ${ARP_SHADER_TOOL_INPUT_DEPENDENCIES} scripts/compile_shaders.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            shaders/gui/overlay.slang "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
    add_custom_target(arp_native_gui_shader_source_fixture DEPENDS "${publication}")
    add_executable(arp_renderer_prepared_shader_tests tests/contract/renderer_prepared_shader_tests.cpp)
    target_include_directories(arp_renderer_prepared_shader_tests PRIVATE . src)
    if(APPLE)
        target_link_libraries(arp_renderer_prepared_shader_tests PRIVATE
            arp_renderer arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
    else()
        target_link_libraries(arp_renderer_prepared_shader_tests PRIVATE
            arp_renderer "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
            "$<LINK_GROUP:RESCAN,arp_renderer_rhi_contracts,arp_rhi,arp_rhi_semantics>")
    endif()
    target_compile_options(arp_renderer_prepared_shader_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_renderer_prepared_shader_tests)
    add_dependencies(arp_renderer_prepared_shader_tests arp_native_gui_shader_source_fixture)
    add_test(NAME arp_renderer_prepared_shader_tests COMMAND arp_renderer_prepared_shader_tests "${directory}")
    add_test(NAME arp_renderer_prepared_shader_null_smoke COMMAND arp_renderer_prepared_shader_tests "${directory}" --smoke)
    set_tests_properties(arp_renderer_prepared_shader_tests arp_renderer_prepared_shader_null_smoke
        PROPERTIES TIMEOUT 120 LABELS "renderer;shader;io;null;smoke")
    if(TARGET arp_build_a_queen_scene)
        add_executable(arp_queen_native_shader_sources_tests tests/contract/queen_native_shader_sources_tests.cpp)
        target_include_directories(arp_queen_native_shader_sources_tests PRIVATE . src)
        if(APPLE)
            target_link_libraries(arp_queen_native_shader_sources_tests PRIVATE arp_build_a_queen_scene
                arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d
                arp_renderer_rhi_contracts arp_rhi arp_rhi_semantics)
        else()
            target_link_libraries(arp_queen_native_shader_sources_tests PRIVATE arp_build_a_queen_scene
                "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
                "$<LINK_GROUP:RESCAN,arp_renderer_rhi_contracts,arp_rhi,arp_rhi_semantics>")
        endif()
        target_compile_options(arp_queen_native_shader_sources_tests PRIVATE -UNDEBUG)
        arp_add_mingw_static_runtime(arp_queen_native_shader_sources_tests)
        add_dependencies(arp_queen_native_shader_sources_tests arp_compile_shaders)
        add_test(NAME arp_queen_native_shader_host_http_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/queen_native_shader_host_http_smoke.py"
                "$<TARGET_FILE:arp_build_a_queen>" "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        set_tests_properties(arp_queen_native_shader_host_http_smoke PROPERTIES
            TIMEOUT 180 RUN_SERIAL TRUE LABELS "example;shader;io;vulkan;smoke")
        add_test(NAME arp_queen_native_shader_sources_tests COMMAND arp_queen_native_shader_sources_tests "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        add_test(NAME arp_queen_native_shader_sources_smoke COMMAND arp_queen_native_shader_sources_tests "${CMAKE_CURRENT_BINARY_DIR}/shaders" --smoke)
        set_tests_properties(arp_queen_native_shader_sources_tests arp_queen_native_shader_sources_smoke
            PROPERTIES TIMEOUT 120 LABELS "shader;resource;io;example;smoke")
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_native_gui_shader_source_validation)
