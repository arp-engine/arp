# Offline authoring tool: no engine DLL, provider credentials or GPU work.
add_library(arp_prompt_manifest_tooling STATIC tools/ai_asset_generator/prompt_manifest.cpp
    tools/ai_asset_generator/prompt_manifest_application.cpp)
target_include_directories(arp_prompt_manifest_tooling PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arp_prompt_manifest_tooling PUBLIC arp_cold_tool_files arp_fkyaml_headers)
if(NOT EMSCRIPTEN)
    add_executable(arp_ai_asset_generator tools/ai_asset_generator/main.cpp)
    target_link_libraries(arp_ai_asset_generator PRIVATE arp_prompt_manifest_tooling)
    arp_add_mingw_static_runtime(arp_ai_asset_generator)
    add_executable(arp_prompt_manifest_tests tests/contract/prompt_manifest_tests.cpp)
    target_link_libraries(arp_prompt_manifest_tests PRIVATE arp_prompt_manifest_tooling)
    target_compile_options(arp_prompt_manifest_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_prompt_manifest_tests)
    add_test(NAME arp_prompt_manifest_tests COMMAND arp_prompt_manifest_tests)
    add_test(NAME arp_prompt_manifest_smoke COMMAND arp_prompt_manifest_tests --smoke)
    set_tests_properties(arp_prompt_manifest_tests arp_prompt_manifest_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools;io;smoke")
    if(WIN32)
        add_test(NAME arp_prompt_manifest_cli_smoke COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/prompt_manifest_cli_smoke.py"
            --executable "$<TARGET_FILE:arp_ai_asset_generator>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/prompt-manifest-cli")
        set_tests_properties(arp_prompt_manifest_cli_smoke PROPERTIES
            TIMEOUT 120 LABELS "tools;io;smoke")
    endif()
endif()
