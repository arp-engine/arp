function(arp_add_queen_native_hud_validation)
    if(EMSCRIPTEN OR NOT TARGET arp_build_a_queen_scene)
        return()
    endif()
    add_executable(arp_queen_native_hud_sources_tests tests/contract/queen_native_hud_sources_tests.cpp)
    target_include_directories(arp_queen_native_hud_sources_tests PRIVATE . src)
    if(APPLE)
        target_link_libraries(arp_queen_native_hud_sources_tests PRIVATE arp_build_a_queen_scene
            arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d
            arp_renderer_rhi_contracts arp_rhi arp_rhi_semantics)
    else()
        target_link_libraries(arp_queen_native_hud_sources_tests PRIVATE arp_build_a_queen_scene
            "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
            "$<LINK_GROUP:RESCAN,arp_renderer_rhi_contracts,arp_rhi,arp_rhi_semantics>")
    endif()
    target_compile_options(arp_queen_native_hud_sources_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_queen_native_hud_sources_tests)
    set(directory "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/native")
    add_test(NAME arp_queen_native_hud_sources_tests COMMAND arp_queen_native_hud_sources_tests "${directory}")
    add_test(NAME arp_queen_native_hud_sources_smoke COMMAND arp_queen_native_hud_sources_tests "${directory}" --smoke)
    set_tests_properties(arp_queen_native_hud_sources_tests arp_queen_native_hud_sources_smoke
        PROPERTIES TIMEOUT 120 LABELS "example;ui;io;source")
    add_test(NAME arp_queen_native_hud_assets_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/queen_native_hud_assets_tests.py")
    set_tests_properties(arp_queen_native_hud_assets_tests PROPERTIES TIMEOUT 30 LABELS "example;asset;source")
    add_test(NAME arp_queen_native_hud_host_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/queen_native_hud_host_http_smoke.py"
            "$<TARGET_FILE:arp_build_a_queen>" "${directory}")
    add_test(NAME arp_queen_native_hud_package_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/queen_native_hud_package_smoke.py"
            "$<TARGET_FILE:arp_build_a_queen>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets"
            "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set_tests_properties(arp_queen_native_hud_host_http_smoke arp_queen_native_hud_package_smoke
        PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE LABELS "example;ui;io;vulkan;smoke")
endfunction()
cmake_language(DEFER CALL arp_add_queen_native_hud_validation)
