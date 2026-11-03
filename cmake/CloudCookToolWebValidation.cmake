# Independent CPU fixture; does not build or launch the Web Editor product.
add_custom_target(arp_cloud_cook_tool_identity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_cook_tool_identity.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/cloud_cook_tool_identity.h"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/cloud_cook_tool_identity.h" VERBATIM)
add_executable(arp_cloud_cook_tool_fetch_lifecycle tests/smoke/cloud_cook_tool_fetch_lifecycle.cpp)
add_dependencies(arp_cloud_cook_tool_fetch_lifecycle arp_cloud_cook_tool_identity)
target_include_directories(arp_cloud_cook_tool_fetch_lifecycle PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(arp_cloud_cook_tool_fetch_lifecycle PRIVATE arp_cloud_cook_tooling)
target_link_options(arp_cloud_cook_tool_fetch_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=268435456 -sALLOW_MEMORY_GROWTH=0
    -sABORTING_MALLOC=0 -sSTACK_SIZE=1048576)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/scripts/browser_progress_transport.js"
    "${CMAKE_CURRENT_BINARY_DIR}/browser_progress_transport.js" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cloud_cook_tool_lifecycle.html"
    "${CMAKE_CURRENT_BINARY_DIR}/cloud_cook_tool_lifecycle.html" COPYONLY)
find_program(ARP_CLOUD_COOK_BROWSER NAMES chrome.exe msedge.exe
    HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
if(ARP_CLOUD_COOK_BROWSER)
    add_test(NAME arp_cloud_cook_tool_fetch_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_cook_tool_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_CLOUD_COOK_BROWSER}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/cloud-cook-tool-fetch")
    set_tests_properties(arp_cloud_cook_tool_fetch_smoke PROPERTIES
        TIMEOUT 600 RUN_SERIAL TRUE LABELS "tools;cloud;io;browser-native;smoke")
endif()
find_program(ARP_CLOUD_COOK_NODE NAMES node)
if(ARP_CLOUD_COOK_NODE)
    add_test(NAME arp_cloud_cook_tool_io_tests_wasm
        COMMAND "${ARP_CLOUD_COOK_NODE}" "$<TARGET_FILE:arp_cloud_cook_tool_io_tests>")
    add_test(NAME arp_cloud_cook_tool_io_smoke_wasm
        COMMAND "${ARP_CLOUD_COOK_NODE}" "$<TARGET_FILE:arp_cloud_cook_tool_io_tests>" --smoke)
    set_tests_properties(arp_cloud_cook_tool_io_tests_wasm arp_cloud_cook_tool_io_smoke_wasm PROPERTIES
        TIMEOUT 60 LABELS "tools;cloud;io;smoke")
endif()
