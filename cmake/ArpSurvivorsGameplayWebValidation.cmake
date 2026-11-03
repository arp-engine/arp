# CPU preparation only. Shares the standalone Web runtime without creating
# a Survivors Web game, renderer or MEMFS-backed production source path.
set(survivors_text_root "${queen_root}/examples/survivors/assets")
set(survivors_text_fixture "${CMAKE_CURRENT_BINARY_DIR}/survivors-gameplay-fixture")
file(GLOB survivors_text_fixture_scripts CONFIGURE_DEPENDS "${survivors_text_root}/scripts/*.arp")
add_custom_command(OUTPUT "${survivors_text_fixture}/manifest.yaml"
    COMMAND "${Python3_EXECUTABLE}" "${queen_root}/tests/fixtures/survivors_gameplay_files.py"
        --source "${survivors_text_root}" --output "${survivors_text_fixture}/payload"
        --stamp "${survivors_text_fixture}/manifest.yaml"
    DEPENDS "${queen_root}/tests/fixtures/survivors_gameplay_files.py"
        "${survivors_text_root}/game.yaml" "${survivors_text_root}/script-package.yaml"
        ${survivors_text_fixture_scripts}
    VERBATIM)
add_custom_target(arp_survivors_gameplay_text_fixture DEPENDS "${survivors_text_fixture}/manifest.yaml")
add_library(arp_survivors_gameplay_sources_web STATIC
    "${queen_root}/examples/survivors/gameplay_text.cpp"
    "${queen_root}/examples/survivors/gameplay_source_task.cpp"
    "${queen_root}/examples/common/startup_resource_task.cpp")
target_link_libraries(arp_survivors_gameplay_sources_web PUBLIC arp_sprite_product)
add_executable(arp_survivors_gameplay_sources_tests
    "${queen_root}/tests/contract/survivors_gameplay_sources_tests.cpp")
target_include_directories(arp_survivors_gameplay_sources_tests PRIVATE "${queen_root}")
target_link_libraries(arp_survivors_gameplay_sources_tests PRIVATE arp_survivors_gameplay_sources_web)
target_compile_definitions(arp_survivors_gameplay_sources_tests PRIVATE ARP_SURVIVORS_ASSET_DIR="/gameplay-source-fixture")
target_compile_options(arp_survivors_gameplay_sources_tests PRIVATE -UNDEBUG)
target_link_options(arp_survivors_gameplay_sources_tests PRIVATE
    -sASSERTIONS=1 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0
    "SHELL:--preload-file \"${survivors_text_fixture}/payload@/gameplay-source-fixture\"")
set_property(TARGET arp_survivors_gameplay_sources_tests APPEND PROPERTY LINK_DEPENDS "${survivors_text_fixture}/manifest.yaml")
add_dependencies(arp_survivors_gameplay_sources_tests arp_survivors_gameplay_text_fixture)
add_test(NAME arp_survivors_gameplay_sources_tests COMMAND arp_survivors_gameplay_sources_tests /gameplay-source-fixture)
add_test(NAME arp_survivors_gameplay_sources_smoke COMMAND arp_survivors_gameplay_sources_tests /gameplay-source-fixture --smoke)
set_tests_properties(arp_survivors_gameplay_sources_tests arp_survivors_gameplay_sources_smoke
    PROPERTIES TIMEOUT 180 LABELS "script;resource;io;example;node")
add_executable(arp_survivors_gameplay_source_lifecycle
    "${queen_root}/tests/smoke/survivors_gameplay_source_lifecycle.cpp")
target_include_directories(arp_survivors_gameplay_source_lifecycle PRIVATE "${queen_root}")
target_link_libraries(arp_survivors_gameplay_source_lifecycle PRIVATE arp_survivors_gameplay_sources_web)
target_link_options(arp_survivors_gameplay_source_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0)
configure_file("${queen_root}/tests/fixtures/survivors_gameplay_source_lifecycle.html"
    "${CMAKE_CURRENT_BINARY_DIR}/survivors_gameplay_source_lifecycle.html" COPYONLY)
if(ARP_WEBGL_RUNTIME_BROWSER)
    add_test(NAME arp_survivors_gameplay_source_fetch_smoke
        COMMAND "${Python3_EXECUTABLE}" "${queen_root}/tests/contract/survivors_gameplay_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_WEBGL_RUNTIME_BROWSER}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/survivors-gameplay-fetch-ctest")
    set_tests_properties(arp_survivors_gameplay_source_fetch_smoke PROPERTIES
        TIMEOUT 150 RUN_SERIAL TRUE LABELS "browser-native;script;io;example;smoke")
endif()
