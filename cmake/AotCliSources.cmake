if(NOT EMSCRIPTEN AND TARGET arp_script_aotc)
    add_library(arp_aot_cli_sources STATIC tools/script_aotc/aot_cli_sources.cpp)
    target_include_directories(arp_aot_cli_sources PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(arp_aot_cli_sources PUBLIC arp_cold_tool_files arp_scriptvm_aot_tooling)
    target_link_libraries(arp_script_aotc PRIVATE arp_aot_cli_sources)
    if(MINGW)
        target_link_options(arp_script_aotc PRIVATE -municode)
    endif()
    add_executable(arp_aot_cli_source_tests tests/contract/aot_cli_source_tests.cpp)
    target_link_libraries(arp_aot_cli_source_tests PRIVATE arp_aot_cli_sources
        arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend)
    target_compile_definitions(arp_aot_cli_source_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(arp_aot_cli_source_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_aot_cli_source_tests)
    add_test(NAME arp_aot_cli_source_tests COMMAND arp_aot_cli_source_tests)
    add_test(NAME arp_aot_cli_source_smoke COMMAND arp_aot_cli_source_tests --smoke)
    set_tests_properties(arp_aot_cli_source_tests arp_aot_cli_source_smoke PROPERTIES
        TIMEOUT 120 LABELS "tools;scriptvm;io;smoke")
    if(WIN32)
        add_test(NAME arp_aot_cli_native_smoke COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/aot_cli_native_smoke.py"
            --executable "$<TARGET_FILE:arp_script_aotc>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/aot-cli-native")
        set_tests_properties(arp_aot_cli_native_smoke PROPERTIES
            TIMEOUT 180 LABELS "tools;scriptvm;io;smoke")
    endif()
endif()
