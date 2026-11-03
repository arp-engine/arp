# Shared offline file acquisition; independent of shader/Editor/game bootstraps.
add_library(arp_tool_file_inventory_io STATIC tools/io/file_inventory.cpp tools/io/file_inventory_cli.cpp)
target_include_directories(arp_tool_file_inventory_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arp_tool_file_inventory_io PUBLIC arp_cold_tool_files arp_fkyaml_headers)
if(NOT EMSCRIPTEN)
    add_executable(arp_tool_file_inventory tools/io/file_inventory_main.cpp)
    target_link_libraries(arp_tool_file_inventory PRIVATE arp_tool_file_inventory_io)
    arp_add_mingw_static_runtime(arp_tool_file_inventory)
    add_executable(arp_tool_file_inventory_tests tests/contract/tool_file_inventory_tests.cpp)
    target_link_libraries(arp_tool_file_inventory_tests PRIVATE arp_tool_file_inventory_io)
    target_compile_options(arp_tool_file_inventory_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_tool_file_inventory_tests)
    add_test(NAME arp_tool_file_inventory_tests COMMAND arp_tool_file_inventory_tests)
    add_test(NAME arp_tool_file_inventory_smoke COMMAND arp_tool_file_inventory_tests --smoke)
    set_tests_properties(arp_tool_file_inventory_tests arp_tool_file_inventory_smoke
        PROPERTIES TIMEOUT 120 LABELS "tools;io;cpu")
else()
    add_executable(arp_tool_file_inventory_wasm_tests tests/contract/tool_file_inventory_wasm_tests.cpp)
    target_link_libraries(arp_tool_file_inventory_wasm_tests PRIVATE arp_tool_file_inventory_io)
    target_compile_options(arp_tool_file_inventory_wasm_tests PRIVATE -UNDEBUG)
    target_link_options(arp_tool_file_inventory_wasm_tests PRIVATE
        -sASSERTIONS=1 -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=134217728
        -sALLOW_MEMORY_GROWTH=0 -sABORTING_MALLOC=0)
    if(ARP_COLD_TOOL_NODE)
        add_test(NAME arp_tool_file_inventory_wasm_tests
            COMMAND "${ARP_COLD_TOOL_NODE}" "$<TARGET_FILE:arp_tool_file_inventory_wasm_tests>")
        add_test(NAME arp_tool_file_inventory_wasm_smoke
            COMMAND "${ARP_COLD_TOOL_NODE}" "$<TARGET_FILE:arp_tool_file_inventory_wasm_tests>" --smoke)
        set_tests_properties(arp_tool_file_inventory_wasm_tests arp_tool_file_inventory_wasm_smoke
            PROPERTIES TIMEOUT 60 LABELS "tools;io;wasm")
    endif()
endif()
if(WIN32)
    add_test(NAME arp_tool_file_inventory_cli_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/tool_file_inventory_cli_tests.py"
            --executable "$<TARGET_FILE:arp_tool_file_inventory>"
            --legacy-executable "$<TARGET_FILE:arp_survivors_package_inventory>")
    set_tests_properties(arp_tool_file_inventory_cli_tests PROPERTIES TIMEOUT 120 LABELS "tools;io;cpu")
endif()

if(WIN32 AND SLANGC_EXECUTABLE AND SPIRV_REFLECT_EXECUTABLE)
    add_test(NAME arp_shader_file_input_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/shader_file_input_tests.py"
            --executable "$<TARGET_FILE:arp_tool_file_inventory>")
    foreach(mode IN ITEMS comparison smoke)
        set(shader_input_mode_args)
        if(mode STREQUAL "smoke")
            set(shader_input_mode_args --smoke)
        endif()
        add_test(NAME arp_shader_file_input_${mode}
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/shader_file_input_comparison.py"
                --executable "$<TARGET_FILE:arp_tool_file_inventory>"
                --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
                --output "${CMAKE_BINARY_DIR}/shader-tool-input-${mode}" ${shader_input_mode_args})
        set_tests_properties(arp_shader_file_input_${mode} PROPERTIES TIMEOUT 1200 LABELS "tools;io;shader;cpu")
    endforeach()
    set_tests_properties(arp_shader_file_input_tests PROPERTIES TIMEOUT 120 LABELS "tools;io;shader;cpu")
endif()

# The WebGL shader tool discovery is deferred; register after that discovery so
# a clean configure has the same gates as a populated cache.
function(arp_add_shader_dependent_input_validation)
    if(NOT WIN32 OR NOT SLANGC_EXECUTABLE OR NOT SPIRV_REFLECT_EXECUTABLE OR NOT ARP_SPIRV_CROSS_LIBRARY)
        message(STATUS "SHADER-DEPENDENT-INPUT-TOOLS-UNAVAILABLE")
        return()
    endif()
    set(tool_args --executable "$<TARGET_FILE:arp_tool_file_inventory>"
        --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
        --library "${ARP_SPIRV_CROSS_LIBRARY}")
    add_test(NAME arp_shader_dependent_input_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/shader_dependent_input_tests.py"
            ${tool_args})
    set_tests_properties(arp_shader_dependent_input_tests PROPERTIES TIMEOUT 180 LABELS "tools;io;shader;cpu")
    foreach(mode IN ITEMS comparison smoke)
        set(mode_args)
        if(mode STREQUAL "smoke")
            set(mode_args --smoke)
        endif()
        add_test(NAME arp_shader_dependent_input_${mode}
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/shader_dependent_input_comparison.py"
                ${tool_args} --output "${CMAKE_BINARY_DIR}/shader-dependent-input-${mode}" ${mode_args})
        set_tests_properties(arp_shader_dependent_input_${mode} PROPERTIES TIMEOUT 1800 LABELS "tools;io;shader;cpu")
    endforeach()
    add_test(NAME arp_shader_dependent_input_build_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/shader_dependent_input_build_tests.py"
            --ninja "${CMAKE_MAKE_PROGRAM}" --build-dir "${CMAKE_BINARY_DIR}")
    set_tests_properties(arp_shader_dependent_input_build_tests PROPERTIES TIMEOUT 120 LABELS "tools;io;build")
endfunction()
cmake_language(DEFER CALL arp_add_shader_dependent_input_validation)
