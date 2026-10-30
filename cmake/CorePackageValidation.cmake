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
endif()

add_test(NAME arp_unified_io_inventory_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/unified_io_inventory_tests.py")
add_test(NAME arp_unified_io_inventory_gate
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/unified_io_inventory.py"
        --check "${CMAKE_CURRENT_SOURCE_DIR}/docs/unified-io-callsite-inventory.yaml")
set_tests_properties(arp_unified_io_inventory_tests arp_unified_io_inventory_gate
    PROPERTIES TIMEOUT 60 LABELS "architecture;core;io")
