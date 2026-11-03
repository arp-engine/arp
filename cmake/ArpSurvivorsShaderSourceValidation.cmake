function(arp_add_survivors_shader_source_validation)
    if(EMSCRIPTEN OR NOT SLANGC_EXECUTABLE OR NOT SPIRV_REFLECT_EXECUTABLE OR NOT TARGET arp_survivors_shader_sources)
        return()
    endif()
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/survivors-source-shader")
    set(publication "${directory}/scene_2d_sprites.artifact-set.yaml")
    add_custom_command(OUTPUT "${publication}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py" ${ARP_SHADER_TOOL_INPUT_ARGS}
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --out-dir "${directory}" --module scene_2d_sprites
        DEPENDS ${ARP_SHADER_TOOL_INPUT_DEPENDENCIES} scripts/compile_shaders.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            shaders/2d/sprites.slang "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
    add_custom_target(arp_survivors_source_shader_fixture DEPENDS "${publication}")
    add_executable(arp_survivors_shader_sources_tests tests/contract/survivors_shader_sources_tests.cpp)
    target_include_directories(arp_survivors_shader_sources_tests PRIVATE . src)
    target_compile_definitions(arp_survivors_shader_sources_tests PRIVATE ARP_SURVIVORS_SOURCE_SHADER_DIR="${directory}")
    if(APPLE)
        target_link_libraries(arp_survivors_shader_sources_tests PRIVATE arp_survivors_shader_sources arp_renderer
            arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_renderer_rhi_contracts arp_rhi arp_rhi_semantics)
    else()
        target_link_libraries(arp_survivors_shader_sources_tests PRIVATE arp_survivors_shader_sources arp_renderer
            "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
            "$<LINK_GROUP:RESCAN,arp_renderer_rhi_contracts,arp_rhi,arp_rhi_semantics>")
    endif()
    target_compile_options(arp_survivors_shader_sources_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_survivors_shader_sources_tests)
    add_dependencies(arp_survivors_shader_sources_tests arp_survivors_source_shader_fixture)
    foreach(mode IN ITEMS tests smoke)
        if(mode STREQUAL "smoke")
            set(args --smoke)
        else()
            set(args)
        endif()
        add_test(NAME arp_survivors_shader_sources_${mode}
            COMMAND arp_survivors_shader_sources_tests "${directory}" ${args})
        set_tests_properties(arp_survivors_shader_sources_${mode} PROPERTIES TIMEOUT 180 LABELS "example;shader;io;source")
    endforeach()
    add_test(NAME arp_survivors_shader_sources_native_smoke
        COMMAND arp_survivors_shader_sources_tests "${directory}" --native)
    add_test(NAME arp_survivors_shader_sources_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_shader_sources_http_smoke.py"
            "$<TARGET_FILE:arp_survivors_shader_sources_tests>" "${directory}")
    set_tests_properties(arp_survivors_shader_sources_native_smoke arp_survivors_shader_sources_http_smoke
        PROPERTIES TIMEOUT 180 LABELS "example;shader;io;source;smoke")
endfunction()
cmake_language(DEFER CALL arp_add_survivors_shader_source_validation)
