include("${CMAKE_CURRENT_LIST_DIR}/ArpQueenContent.cmake")
arp_add_queen_content()
set(queen_root "${CMAKE_CURRENT_LIST_DIR}/..")
foreach(module IN ITEMS queen_portable_sky queen_portable_shadow queen_portable gui_bitmap)
    if(NOT EXISTS "${ARP_WEBGL_PROGRAM_DIRECTORY}/${module}.webgl-program.yaml")
        message(FATAL_ERROR "QUEEN-WEBGL-PROGRAM-MISSING: ${module}")
    endif()
endforeach()
add_executable(arp_build_a_queen_webgl
    "${queen_root}/examples/build_a_queen/webgl_main.cpp"
    "${queen_root}/examples/build_a_queen/portable_renderer.cpp"
    "${queen_root}/examples/build_a_queen/hud_content.cpp"
    "${queen_root}/src/ui/ui.cpp"
    "${queen_root}/src/ui/ui_control_events.cpp")
target_link_libraries(arp_build_a_queen_webgl PRIVATE arp_queen_content arp_platform arp_sprite_product)
target_link_options(arp_build_a_queen_webgl PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sSTACK_SIZE=2097152 -sSTACK_OVERFLOW_CHECK=2
    -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=0
    "-sEXPORTED_RUNTIME_METHODS=['ccall']"
    "SHELL:--preload-file \"${queen_root}/examples/build_a_queen/assets@/assets\""
    "SHELL:--preload-file \"${ARP_WEBGL_PROGRAM_DIRECTORY}@/artifacts\""
    "SHELL:--shell-file \"${queen_root}/examples/build_a_queen/web/index.html\"")
set_target_properties(arp_build_a_queen_webgl PROPERTIES SUFFIX ".html")
arp_webgl_track_preload_inputs(arp_build_a_queen_webgl
    "${queen_root}/examples/build_a_queen/assets" "${ARP_WEBGL_PROGRAM_DIRECTORY}"
    "${queen_root}/examples/build_a_queen/web/index.html")

add_executable(arp_webgl_platform_smoke "${queen_root}/tests/integration/render_system_webgl_smoke.cpp")
target_link_libraries(arp_webgl_platform_smoke PRIVATE arp_platform)
target_link_options(arp_webgl_platform_smoke PRIVATE -sASSERTIONS=1 -sEXIT_RUNTIME=0
    -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0
    "SHELL:--preload-file \"${ARP_WEBGL_PROGRAM_DIRECTORY}@/artifacts\"")
set_target_properties(arp_webgl_platform_smoke PROPERTIES SUFFIX ".js")
arp_webgl_track_preload_inputs(arp_webgl_platform_smoke "${ARP_WEBGL_PROGRAM_DIRECTORY}")
file(READ "${queen_root}/tests/integration/webgl1_runtime/index.html" platform_page)
string(REPLACE "arp_webgl_context_smoke.js" "arp_webgl_platform_smoke.js" platform_page "${platform_page}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/platform.html" "${platform_page}")
