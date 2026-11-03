# Exercise existing foundation/IO tests that were not wired into the hand-maintained build.
foreach(source IN ITEMS
    tests/unit/core_curve_tests.cpp
    tests/unit/core_text_decoder_tests.cpp
    tests/unit/core_encoding_hash_tests.cpp
    tests/unit/core_completion_queue_tests.cpp
    tests/unit/core_work_service_tests.cpp
    tests/unit/core_file_io_tests.cpp
    tests/unit/core_blocking_file_reader_tests.cpp
    tests/unit/core_file_service_thread_pool_backend_tests.cpp
    tests/contract/core_file_read_contract_tests.cpp
    tests/contract/core_file_save_tests.cpp
    tests/contract/core_memory_file_save_tests.cpp
    tests/contract/core_file_save_receipt_tests.cpp
    tests/contract/core_workspace_file_backend_contract_tests.cpp
    tests/contract/core_file_text_service_tests.cpp
    tests/contract/core_http_file_contract_tests.cpp
    tests/contract/network_http_stream_control_tests.cpp
    tests/contract/network_resolver_tests.cpp
    tests/contract/core_work_completion_capacity_tests.cpp
    tests/contract/core_mounted_file_backend_tests.cpp
    tests/contract/core_file_query_tests.cpp
    tests/contract/core_native_directory_file_backend_tests.cpp
    tests/contract/core_file_service_contract_tests.cpp
    tests/smoke/core_file_service_smoke.cpp
    tests/smoke/core_file_text_smoke.cpp
    tests/smoke/core_mounted_file_backend_smoke.cpp
    tests/smoke/core_native_directory_file_backend_smoke.cpp
    tests/integration/package_manifest_tests.cpp)
    get_filename_component(name "${source}" NAME_WE)
    set(target "arp_${name}")
    if(NOT TARGET ${target})
        add_executable(${target} "${source}")
        target_link_libraries(${target} PRIVATE arp_core)
        target_include_directories(${target} PRIVATE . src)
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        arp_add_mingw_static_runtime(${target})
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES TIMEOUT 60 LABELS "core;package-regression")
    endif()
endforeach()

add_test(NAME arp_core_file_save_smoke COMMAND arp_core_file_save_tests --smoke)
set_tests_properties(arp_core_file_save_smoke PROPERTIES TIMEOUT 60 LABELS "core;io;smoke")
add_test(NAME arp_core_file_save_receipt_smoke COMMAND arp_core_file_save_receipt_tests --smoke)
set_tests_properties(arp_core_file_save_receipt_smoke PROPERTIES TIMEOUT 60 LABELS "core;io;smoke")
add_test(NAME arp_core_memory_file_save_smoke COMMAND arp_core_memory_file_save_tests --smoke)
set_tests_properties(arp_core_memory_file_save_smoke PROPERTIES TIMEOUT 60 LABELS "core;io;smoke")

add_executable(arp_core_workspace_file_wire_tests tests/contract/core_workspace_file_wire_tests.cpp)
target_include_directories(arp_core_workspace_file_wire_tests PRIVATE . src
    3rd/assimp-6.0.5/contrib/rapidjson/include)
target_link_libraries(arp_core_workspace_file_wire_tests PRIVATE arp_core)
target_compile_options(arp_core_workspace_file_wire_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_core_workspace_file_wire_tests)
add_test(NAME arp_core_workspace_file_wire_tests COMMAND arp_core_workspace_file_wire_tests)
add_test(NAME arp_core_workspace_file_wire_smoke COMMAND arp_core_workspace_file_wire_tests --smoke)
set_tests_properties(arp_core_workspace_file_wire_tests arp_core_workspace_file_wire_smoke
    PROPERTIES TIMEOUT 60 LABELS "core;io;workspace;smoke")

add_executable(arp_core_http_file_backend_tests tests/contract/core_http_file_backend_tests.cpp)
target_link_libraries(arp_core_http_file_backend_tests PRIVATE arp_core)
target_include_directories(arp_core_http_file_backend_tests PRIVATE src)
if(NOT MSVC)
    target_compile_options(arp_core_http_file_backend_tests PRIVATE -UNDEBUG)
endif()
arp_add_mingw_static_runtime(arp_core_http_file_backend_tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_test(NAME arp_core_http_file_backend_tests COMMAND arp_core_http_file_backend_tests)
else()
    add_test(NAME arp_core_http_file_backend_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/core_http_file_loopback.py"
            "$<TARGET_FILE:arp_core_http_file_backend_tests>")
endif()
set_tests_properties(arp_core_http_file_backend_tests PROPERTIES TIMEOUT 60 LABELS "core;io;smoke")

add_executable(arp_network_http_authenticated_transfer_tests
    tests/contract/network_http_authenticated_transfer_tests.cpp)
target_include_directories(arp_network_http_authenticated_transfer_tests PRIVATE . src)
target_link_libraries(arp_network_http_authenticated_transfer_tests PRIVATE arp_network_http_transport)
target_compile_options(arp_network_http_authenticated_transfer_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_network_http_authenticated_transfer_tests)
if(EMSCRIPTEN)
    add_test(NAME arp_network_http_authenticated_transfer_tests COMMAND arp_network_http_authenticated_transfer_tests)
else()
    add_test(NAME arp_network_http_authenticated_transfer_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/network_http_authenticated_loopback.py"
            "$<TARGET_FILE:arp_network_http_authenticated_transfer_tests>")
endif()
set_tests_properties(arp_network_http_authenticated_transfer_tests
    PROPERTIES TIMEOUT 60 LABELS "network;http;io;smoke")

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(arp_core_http_file_fetch_smoke tests/smoke/core_http_file_fetch_smoke.cpp)
    target_link_libraries(arp_core_http_file_fetch_smoke PRIVATE arp_core)
    target_compile_options(arp_core_http_file_fetch_smoke PRIVATE -UNDEBUG)
    target_link_options(arp_core_http_file_fetch_smoke PRIVATE -sASYNCIFY=1 -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1)
    configure_file(tests/fixtures/core_http_file_fetch_smoke.html
        "${CMAKE_CURRENT_BINARY_DIR}/arp_core_http_file_fetch_smoke.html" COPYONLY)
    find_program(ARP_IO_BROWSER_EXECUTABLE NAMES chrome chrome.exe chromium chromium-browser
        PATHS "C:/Program Files/Google/Chrome/Application")
    if(ARP_IO_BROWSER_EXECUTABLE)
        add_test(NAME arp_core_http_file_fetch_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/core_http_file_browser.py"
                --browser "${ARP_IO_BROWSER_EXECUTABLE}"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/io-fetch-evidence")
        set_tests_properties(arp_core_http_file_fetch_smoke PROPERTIES TIMEOUT 65 LABELS "core;io;browser;smoke")
    endif()
    add_executable(arp_network_http_authenticated_fetch_smoke tests/smoke/network_http_authenticated_fetch_smoke.cpp)
    target_include_directories(arp_network_http_authenticated_fetch_smoke PRIVATE . src)
    target_link_libraries(arp_network_http_authenticated_fetch_smoke PRIVATE arp_network_http_transport)
    target_compile_options(arp_network_http_authenticated_fetch_smoke PRIVATE -UNDEBUG)
    target_link_options(arp_network_http_authenticated_fetch_smoke PRIVATE
        -sASYNCIFY=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=33554432 -sALLOW_MEMORY_GROWTH=0)
    configure_file(tests/fixtures/network_http_authenticated_fetch_smoke.html
        "${CMAKE_CURRENT_BINARY_DIR}/arp_network_http_authenticated_fetch_smoke.html" COPYONLY)
    if(ARP_IO_BROWSER_EXECUTABLE)
        add_test(NAME arp_network_http_authenticated_fetch_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/network_http_authenticated_browser.py"
                --browser "${ARP_IO_BROWSER_EXECUTABLE}"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/io-authenticated-fetch-evidence")
        set_tests_properties(arp_network_http_authenticated_fetch_smoke
            PROPERTIES TIMEOUT 65 LABELS "network;http;io;browser;smoke")
    endif()
    add_executable(arp_core_workspace_file_fetch_smoke tests/smoke/core_workspace_file_fetch_smoke.cpp)
    target_include_directories(arp_core_workspace_file_fetch_smoke PRIVATE . src)
    target_link_libraries(arp_core_workspace_file_fetch_smoke PRIVATE arp_core)
    target_compile_options(arp_core_workspace_file_fetch_smoke PRIVATE -UNDEBUG)
    target_link_options(arp_core_workspace_file_fetch_smoke PRIVATE -sASYNCIFY=1 -sEXIT_RUNTIME=0
        -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0)
    configure_file(tests/fixtures/core_workspace_file_fetch_smoke.html
        "${CMAKE_CURRENT_BINARY_DIR}/arp_core_workspace_file_fetch_smoke.html" COPYONLY)
    set(ARP_WORKSPACE_NATIVE_HOST "${CMAKE_CURRENT_SOURCE_DIR}/build/win-vulkan-debug/editor/arp_editor_workspace_service.exe"
        CACHE FILEPATH "Explicit native workspace fixture host for actual browser IO validation")
    if(ARP_IO_BROWSER_EXECUTABLE AND EXISTS "${ARP_WORKSPACE_NATIVE_HOST}")
        add_test(NAME arp_core_workspace_file_fetch_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/core_workspace_file_browser.py"
                --browser "${ARP_IO_BROWSER_EXECUTABLE}" --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --host "${ARP_WORKSPACE_NATIVE_HOST}" --output "${CMAKE_CURRENT_BINARY_DIR}/io-workspace-fetch-evidence")
        set_tests_properties(arp_core_workspace_file_fetch_smoke PROPERTIES TIMEOUT 150 LABELS "core;io;workspace;browser;smoke")
    endif()
endif()

add_test(NAME arp_unified_io_inventory_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/unified_io_inventory_tests.py")
add_test(NAME arp_unified_io_inventory_gate
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/unified_io_inventory.py"
        --check "${CMAKE_CURRENT_SOURCE_DIR}/docs/unified-io-callsite-inventory.yaml")
set_tests_properties(arp_unified_io_inventory_tests arp_unified_io_inventory_gate
    PROPERTIES TIMEOUT 60 LABELS "architecture;core;io")

include("${CMAKE_CURRENT_LIST_DIR}/CoreCpuBackingPolicyValidation.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/VersionedYamlValidation.cmake")
