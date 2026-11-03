# Actual ordinary compiler publications; no hand-authored substitute shader.
function(arp_add_queen_shader_source_validation)
    if(EMSCRIPTEN OR NOT SLANGC_EXECUTABLE OR NOT SPIRV_REFLECT_EXECUTABLE OR NOT ARP_SPIRV_CROSS_LIBRARY)
        message(STATUS "QUEEN-SHADER-SOURCE-NATIVE-FIXTURE-UNAVAILABLE")
        return()
    endif()
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/queen-source-programs")
    set(outputs)
    foreach(module IN ITEMS queen_portable_sky queen_portable_shadow queen_portable gui_bitmap)
        set(publication "${directory}/${module}.webgl-program.yaml")
        add_custom_command(OUTPUT "${publication}"
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py" ${ARP_SHADER_TOOL_INPUT_ARGS}
                --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
                --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${directory}"
                --module "${module}" --graphics-module --glsl-es-100
            DEPENDS ${ARP_SHADER_TOOL_INPUT_DEPENDENCIES} scripts/compile_shaders.py scripts/webgl_shader_artifact.py
                scripts/spirv_cross_api.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
                "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}" "${ARP_SPIRV_CROSS_LIBRARY}"
                shaders/examples/build_a_queen_portable_sky.slang
                shaders/examples/build_a_queen_portable_shadow.slang
                shaders/examples/build_a_queen_portable.slang shaders/gui/bitmap.slang
            VERBATIM)
        list(APPEND outputs "${publication}")
    endforeach()
    add_custom_target(arp_queen_shader_source_fixture DEPENDS ${outputs})
    add_executable(arp_queen_web_shader_sources_tests tests/contract/queen_web_shader_sources_tests.cpp
        examples/build_a_queen/web_shader_source_task.cpp
        examples/common/startup_resource_task.cpp)
    target_include_directories(arp_queen_web_shader_sources_tests PRIVATE . src)
    if(APPLE)
        target_link_libraries(arp_queen_web_shader_sources_tests PRIVATE
            arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
    else()
        target_link_libraries(arp_queen_web_shader_sources_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>")
    endif()
    target_compile_options(arp_queen_web_shader_sources_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_queen_web_shader_sources_tests)
    add_dependencies(arp_queen_web_shader_sources_tests arp_queen_shader_source_fixture)
    add_test(NAME arp_queen_web_shader_sources_tests COMMAND arp_queen_web_shader_sources_tests "${directory}")
    add_test(NAME arp_queen_web_shader_sources_smoke COMMAND arp_queen_web_shader_sources_tests "${directory}" --smoke)
    set_tests_properties(arp_queen_web_shader_sources_tests arp_queen_web_shader_sources_smoke
        PROPERTIES TIMEOUT 300 LABELS "shader;resource;io;example")
endfunction()
cmake_language(DEFER CALL arp_add_queen_shader_source_validation)
