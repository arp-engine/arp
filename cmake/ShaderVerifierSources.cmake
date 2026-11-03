add_library(arp_spirv_verifier_sources STATIC tools/shader/spirv_verifier_source.cpp)
target_include_directories(arp_spirv_verifier_sources PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arp_spirv_verifier_sources PUBLIC arp_cold_tool_files)
foreach(verifier IN ITEMS arp_fragment_atomic_write_verifier arp_spirv_buffer_declaration_layout_verifier)
    target_link_libraries(${verifier} PRIVATE arp_spirv_verifier_sources)
    arp_add_mingw_static_runtime(${verifier})
endforeach()
if(NOT EMSCRIPTEN)
    add_executable(arp_shader_verifier_source_tests tests/contract/shader_verifier_source_tests.cpp)
    target_link_libraries(arp_shader_verifier_source_tests PRIVATE arp_spirv_verifier_sources)
    target_compile_options(arp_shader_verifier_source_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_shader_verifier_source_tests)
    add_test(NAME arp_shader_verifier_source_tests COMMAND arp_shader_verifier_source_tests)
    add_test(NAME arp_shader_verifier_source_smoke COMMAND arp_shader_verifier_source_tests --smoke)
    set_tests_properties(arp_shader_verifier_source_tests arp_shader_verifier_source_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools;shader;io;smoke")
    if(WIN32 AND TARGET arp_single_entry_shader_fixture)
        foreach(mode IN ITEMS native http)
            add_test(NAME arp_shader_verifier_${mode}_smoke COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/shader_verifier_cli_smoke.py"
                --proof-tool "$<TARGET_FILE:arp_fragment_atomic_write_verifier>"
                --layout-tool "$<TARGET_FILE:arp_spirv_buffer_declaration_layout_verifier>"
                --source "${CMAKE_CURRENT_BINARY_DIR}/single-entry-fixture/writeFragment/ordinary_graphics_storage_write.spv"
                --output "${CMAKE_CURRENT_BINARY_DIR}/shader-verifier-${mode}" --mode ${mode})
            set_tests_properties(arp_shader_verifier_${mode}_smoke PROPERTIES
                TIMEOUT 180 LABELS "tools;shader;io;smoke")
        endforeach()
    endif()
endif()
