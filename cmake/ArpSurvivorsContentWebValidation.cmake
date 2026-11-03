# CPU-only source/typed-resource probe. Never builds or launches the Editor.
add_executable(arp_survivors_content_source_lifecycle tests/smoke/survivors_content_source_lifecycle.cpp)
target_include_directories(arp_survivors_content_source_lifecycle PRIVATE . src)
target_link_libraries(arp_survivors_content_source_lifecycle PRIVATE
    arp_survivors_gameplay_sources arp_survivors_content_sources
    arp_scene_script_reflection arp_scene2d_script_reflection ${arp_scene_2d_resource_link_libs})
target_compile_options(arp_survivors_content_source_lifecycle PRIVATE -UNDEBUG)
target_link_options(arp_survivors_content_source_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0
    -sSTACK_SIZE=1048576)
configure_file(tests/fixtures/survivors_content_source_lifecycle.html
    "${CMAKE_CURRENT_BINARY_DIR}/survivors_content_source_lifecycle.html" COPYONLY)
set(ARP_SURVIVORS_CONTENT_BROWSER "" CACHE FILEPATH "Browser for CPU-only Survivors Content Fetch validation")
if(ARP_SURVIVORS_CONTENT_BROWSER)
    add_test(NAME arp_survivors_content_fetch_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_content_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_SURVIVORS_CONTENT_BROWSER}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/survivors-content-fetch-ctest")
    set_tests_properties(arp_survivors_content_fetch_smoke PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
        LABELS "example;io;resource;browser-native;smoke")
endif()
