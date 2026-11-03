# Offline package receipts depend only on the existing cold IO host.
add_library(arp_survivors_package_inventory_io STATIC tools/survivors/package_file_inventory.cpp)
target_include_directories(arp_survivors_package_inventory_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(arp_survivors_package_inventory_io PUBLIC arp_tool_file_inventory_io)
if(NOT EMSCRIPTEN)
    add_executable(arp_survivors_package_inventory tools/survivors/package_inventory_main.cpp)
    target_link_libraries(arp_survivors_package_inventory PRIVATE arp_survivors_package_inventory_io)
    arp_add_mingw_static_runtime(arp_survivors_package_inventory)
    add_dependencies(arp_survivors arp_survivors_package_inventory)
    add_executable(arp_survivors_package_inventory_tests tests/contract/tool_file_inventory_tests.cpp)
    target_link_libraries(arp_survivors_package_inventory_tests PRIVATE arp_survivors_package_inventory_io)
    target_compile_options(arp_survivors_package_inventory_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_survivors_package_inventory_tests PRIVATE ARP_TEST_SURVIVORS_INVENTORY=1)
    arp_add_mingw_static_runtime(arp_survivors_package_inventory_tests)
    add_test(NAME arp_survivors_package_inventory_tests COMMAND arp_survivors_package_inventory_tests)
    add_test(NAME arp_survivors_package_inventory_smoke COMMAND arp_survivors_package_inventory_tests --smoke)
    set_tests_properties(arp_survivors_package_inventory_tests arp_survivors_package_inventory_smoke
        PROPERTIES TIMEOUT 120 LABELS "tools;io;cpu")
endif()

if(WIN32)
    add_test(NAME arp_survivors_package_inventory_cli_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_package_inventory_cli_tests.py"
            --executable "$<TARGET_FILE:arp_survivors_package_inventory>")
    add_test(NAME arp_survivors_package_inventory_comparison
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_package_inventory_comparison.py"
            --executable "$<TARGET_FILE:arp_survivors_package_inventory>"
            --source-build "${CMAKE_BINARY_DIR}" --pe "$<TARGET_FILE:arp_cold_tool_files_tests>"
            --output "${CMAKE_BINARY_DIR}/survivors-package-comparison")
    set_tests_properties(arp_survivors_package_inventory_cli_tests arp_survivors_package_inventory_comparison
        PROPERTIES TIMEOUT 300 LABELS "tools;io;cpu")
endif()

if(NOT EMSCRIPTEN)
    add_test(NAME arp_survivors_package_inventory_build_boundary_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/survivors_package_inventory_build_boundary_tests.py"
            --ninja "${CMAKE_MAKE_PROGRAM}" --build-dir "${CMAKE_BINARY_DIR}")
endif()

if(EMSCRIPTEN)
    add_executable(arp_survivors_package_inventory_wasm_tests tests/contract/tool_file_inventory_wasm_tests.cpp)
    target_link_libraries(arp_survivors_package_inventory_wasm_tests PRIVATE arp_survivors_package_inventory_io)
    target_compile_options(arp_survivors_package_inventory_wasm_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_survivors_package_inventory_wasm_tests PRIVATE ARP_TEST_SURVIVORS_INVENTORY=1)
    target_link_options(arp_survivors_package_inventory_wasm_tests PRIVATE
        -sASSERTIONS=1 -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=134217728
        -sALLOW_MEMORY_GROWTH=0 -sABORTING_MALLOC=0)
    if(ARP_COLD_TOOL_NODE)
        add_test(NAME arp_survivors_package_inventory_wasm_tests
            COMMAND "${ARP_COLD_TOOL_NODE}" "$<TARGET_FILE:arp_survivors_package_inventory_wasm_tests>")
        add_test(NAME arp_survivors_package_inventory_wasm_smoke
            COMMAND "${ARP_COLD_TOOL_NODE}" "$<TARGET_FILE:arp_survivors_package_inventory_wasm_tests>" --smoke)
        set_tests_properties(arp_survivors_package_inventory_wasm_tests arp_survivors_package_inventory_wasm_smoke
            PROPERTIES TIMEOUT 60 LABELS "tools;io;wasm")
    endif()
endif()

if(WIN32)
    add_test(NAME arp_survivors_authoring_input_comparison
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_authoring_input_comparison.py"
            --executable "$<TARGET_FILE:arp_survivors_package_inventory>"
            --output "${CMAKE_BINARY_DIR}/survivors-authoring-comparison")
    set_tests_properties(arp_survivors_authoring_input_comparison PROPERTIES TIMEOUT 180 LABELS "tools;io;cpu")
endif()

add_test(NAME arp_unified_io_build_prerequisite_tests
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/unified_io_build_prerequisite_tests.py")
set_tests_properties(arp_unified_io_build_prerequisite_tests PROPERTIES LABELS "tools;build;io")
