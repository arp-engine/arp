if(EMSCRIPTEN)
    include("${CMAKE_CURRENT_LIST_DIR}/ArpSurvivorsContentWebValidation.cmake")
    return()
endif()

add_executable(arp_survivors_content_sources_tests tests/contract/survivors_content_sources_tests.cpp)
target_include_directories(arp_survivors_content_sources_tests PRIVATE . src)
target_link_libraries(arp_survivors_content_sources_tests PRIVATE arp_survivors_content_sources
    arp_scene_script_reflection arp_scene2d_script_reflection ${arp_scene_2d_resource_link_libs})
target_compile_definitions(arp_survivors_content_sources_tests PRIVATE
    ARP_SURVIVORS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets")
target_compile_options(arp_survivors_content_sources_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_survivors_content_sources_tests)
foreach(mode IN ITEMS tests smoke native_smoke)
    if(mode STREQUAL "tests")
        set(args)
    elseif(mode STREQUAL "smoke")
        set(args --smoke)
    else()
        set(args --native)
    endif()
    add_test(NAME arp_survivors_content_sources_${mode}
        COMMAND arp_survivors_content_sources_tests "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets" ${args})
    set_tests_properties(arp_survivors_content_sources_${mode} PROPERTIES TIMEOUT 180 LABELS "example;io;resource")
endforeach()
add_test(NAME arp_survivors_content_sources_http_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_content_sources_http_smoke.py"
        "$<TARGET_FILE:arp_survivors_content_sources_tests>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets")
set_tests_properties(arp_survivors_content_sources_http_smoke PROPERTIES TIMEOUT 90 LABELS "example;io;resource;http;smoke")

