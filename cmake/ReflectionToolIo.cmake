add_library(arp_reflection_codegen_io STATIC tools/reflect_codegen/reflection_tool_io.cpp)
target_include_directories(arp_reflection_codegen_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arp_reflection_codegen_io PUBLIC arp_cold_tool_files)
target_link_libraries(arp_reflect_codegen PRIVATE arp_reflection_codegen_io)
if(MINGW)
    target_link_options(arp_reflect_codegen PRIVATE -municode)
endif()
if(NOT EMSCRIPTEN)
    foreach(tool_test IN ITEMS reflection_tool_io native_tool_outputs)
        add_executable(arp_${tool_test}_tests tests/contract/${tool_test}_tests.cpp)
        target_link_libraries(arp_${tool_test}_tests PRIVATE arp_reflection_codegen_io)
        target_compile_options(arp_${tool_test}_tests PRIVATE -UNDEBUG)
        arp_add_mingw_static_runtime(arp_${tool_test}_tests)
        add_test(NAME arp_${tool_test}_tests COMMAND arp_${tool_test}_tests)
        add_test(NAME arp_${tool_test}_smoke COMMAND arp_${tool_test}_tests --smoke)
        set_tests_properties(arp_${tool_test}_tests arp_${tool_test}_smoke PROPERTIES
            TIMEOUT 60 LABELS "tools;io;cpu")
    endforeach()
    add_test(NAME arp_reflection_codegen_io_cli_smoke
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/reflection_codegen_io_cli_smoke.py"
            --executable "$<TARGET_FILE:arp_reflect_codegen>"
            --output "${CMAKE_BINARY_DIR}/reflection-codegen-cli")
    set_tests_properties(arp_reflection_codegen_io_cli_smoke PROPERTIES TIMEOUT 180 LABELS "tools;io;cpu;http;smoke")
    add_test(NAME arp_reflection_codegen_io_build_boundary_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/reflection_codegen_io_build_boundary_tests.py"
            --ninja "${CMAKE_MAKE_PROGRAM}" --build-dir "${CMAKE_BINARY_DIR}")
endif()
