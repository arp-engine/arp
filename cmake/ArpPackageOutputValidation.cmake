add_executable(arp_resource_package_output_tests tests/contract/asset_package_output_tests.cpp)
target_include_directories(arp_resource_package_output_tests PRIVATE . src)
target_compile_options(arp_resource_package_output_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_resource_package_output_tests PRIVATE arp_scene3d_assets
    ${arp_scene_2d_resource_link_libs})
arp_add_mingw_static_runtime(arp_resource_package_output_tests)
if(EMSCRIPTEN)
    target_link_options(arp_resource_package_output_tests PRIVATE
        -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0 -sABORTING_MALLOC=0)
endif()
add_test(NAME arp_resource_package_output_tests COMMAND arp_resource_package_output_tests)
add_test(NAME arp_resource_package_output_smoke COMMAND arp_resource_package_output_tests --smoke)
add_test(NAME arp_resource_package_source_output_tests COMMAND arp_resource_package_output_tests --sources)
add_test(NAME arp_resource_package_source_output_smoke COMMAND arp_resource_package_output_tests --source-smoke)
set_tests_properties(arp_resource_package_output_tests arp_resource_package_output_smoke
    arp_resource_package_source_output_tests arp_resource_package_source_output_smoke
    PROPERTIES TIMEOUT 90 LABELS "resource;io;package-output")
if(NOT EMSCRIPTEN)
    add_test(NAME arp_resource_package_terrain_output_tests COMMAND arp_resource_package_output_tests --terrain)
    add_test(NAME arp_resource_package_terrain_output_smoke COMMAND arp_resource_package_output_tests --terrain-smoke)
    set_tests_properties(arp_resource_package_terrain_output_tests arp_resource_package_terrain_output_smoke
        PROPERTIES TIMEOUT 90 LABELS "resource;io;package-output")
endif()
