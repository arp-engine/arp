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
    "${queen_root}/examples/build_a_queen/character_source_task.cpp"
    "${queen_root}/examples/build_a_queen/web_shader_source_task.cpp"
    "${queen_root}/examples/common/startup_resource_task.cpp"
    "${queen_root}/examples/build_a_queen/web_font_source_task.cpp"
    "${queen_root}/examples/build_a_queen/portable_renderer.cpp"
    "${queen_root}/examples/build_a_queen/hud_content.cpp"
    "${queen_root}/src/ui/ui.cpp"
    "${queen_root}/src/ui/ui_control_events.cpp")
target_link_libraries(arp_build_a_queen_webgl PRIVATE arp_queen_content arp_platform arp_sprite_product)
target_link_options(arp_build_a_queen_webgl PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sSTACK_SIZE=2097152 -sSTACK_OVERFLOW_CHECK=2
    -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=0
    "-sEXPORTED_RUNTIME_METHODS=['ccall']"
    "SHELL:--shell-file \"${queen_root}/examples/build_a_queen/web/index.html\"")
set_target_properties(arp_build_a_queen_webgl PROPERTIES SUFFIX ".html")
add_custom_command(TARGET arp_build_a_queen_webgl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:arp_build_a_queen_webgl>/arp_build_a_queen_webgl.data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${queen_root}/examples/build_a_queen/assets/characters"
        "$<TARGET_FILE_DIR:arp_build_a_queen_webgl>/characters"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ARP_WEBGL_PROGRAM_DIRECTORY}"
        "$<TARGET_FILE_DIR:arp_build_a_queen_webgl>/artifacts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${queen_root}/examples/build_a_queen/assets/web"
        "$<TARGET_FILE_DIR:arp_build_a_queen_webgl>/fonts"
    VERBATIM)
arp_webgl_track_preload_inputs(arp_build_a_queen_webgl
    "${queen_root}/examples/build_a_queen/assets" "${ARP_WEBGL_PROGRAM_DIRECTORY}"
    "${queen_root}/examples/build_a_queen/web/index.html")

enable_testing()
add_executable(arp_queen_web_shader_sources_tests
    "${queen_root}/tests/contract/queen_web_shader_sources_tests.cpp"
    "${queen_root}/examples/build_a_queen/web_shader_source_task.cpp"
    "${queen_root}/examples/common/startup_resource_task.cpp")
target_include_directories(arp_queen_web_shader_sources_tests PRIVATE "${queen_root}")
target_link_libraries(arp_queen_web_shader_sources_tests PRIVATE arp_sprite_product)
target_compile_options(arp_queen_web_shader_sources_tests PRIVATE -UNDEBUG)
target_link_options(arp_queen_web_shader_sources_tests PRIVATE
    -sASSERTIONS=1 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0
    "SHELL:--preload-file \"${ARP_WEBGL_PROGRAM_DIRECTORY}@/shader-source-fixture\"")
add_test(NAME arp_queen_web_shader_sources_tests COMMAND arp_queen_web_shader_sources_tests /shader-source-fixture)
add_test(NAME arp_queen_web_shader_sources_smoke COMMAND arp_queen_web_shader_sources_tests /shader-source-fixture --smoke)
set_tests_properties(arp_queen_web_shader_sources_tests arp_queen_web_shader_sources_smoke
    PROPERTIES TIMEOUT 300 LABELS "shader;resource;io;example")
add_executable(arp_queen_web_font_sources_tests
    "${queen_root}/tests/contract/queen_web_font_sources_tests.cpp"
    "${queen_root}/examples/build_a_queen/web_font_source_task.cpp"
    "${queen_root}/examples/common/startup_resource_task.cpp")
target_include_directories(arp_queen_web_font_sources_tests PRIVATE "${queen_root}")
target_link_libraries(arp_queen_web_font_sources_tests PRIVATE arp_sprite_product)
target_compile_options(arp_queen_web_font_sources_tests PRIVATE -UNDEBUG)
target_link_options(arp_queen_web_font_sources_tests PRIVATE
    -sASSERTIONS=1 -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0
    "SHELL:--preload-file \"${queen_root}/examples/build_a_queen/assets/web@/font-source-fixture\"")
add_test(NAME arp_queen_web_font_sources_tests COMMAND arp_queen_web_font_sources_tests /font-source-fixture)
add_test(NAME arp_queen_web_font_sources_smoke COMMAND arp_queen_web_font_sources_tests /font-source-fixture --smoke)
set_tests_properties(arp_queen_web_font_sources_tests arp_queen_web_font_sources_smoke
    PROPERTIES TIMEOUT 90 LABELS "ui;resource;io;example")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(arp_queen_web_shader_source_lifecycle
    "${queen_root}/tests/smoke/queen_web_shader_source_lifecycle.cpp"
    "${queen_root}/examples/build_a_queen/web_shader_source_task.cpp"
    "${queen_root}/examples/common/startup_resource_task.cpp")
target_include_directories(arp_queen_web_shader_source_lifecycle PRIVATE "${queen_root}")
target_link_libraries(arp_queen_web_shader_source_lifecycle PRIVATE arp_sprite_product)
target_link_options(arp_queen_web_shader_source_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0)
configure_file("${queen_root}/tests/fixtures/queen_web_shader_source_lifecycle.html"
    "${CMAKE_CURRENT_BINARY_DIR}/queen_web_shader_source_lifecycle.html" COPYONLY)
add_executable(arp_queen_web_font_source_lifecycle
    "${queen_root}/tests/smoke/queen_web_font_source_lifecycle.cpp"
    "${queen_root}/examples/build_a_queen/web_font_source_task.cpp"
    "${queen_root}/examples/common/startup_resource_task.cpp")
target_include_directories(arp_queen_web_font_source_lifecycle PRIVATE "${queen_root}")
target_link_libraries(arp_queen_web_font_source_lifecycle PRIVATE arp_sprite_product)
target_link_options(arp_queen_web_font_source_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0)
configure_file("${queen_root}/tests/fixtures/queen_web_font_source_lifecycle.html"
    "${CMAKE_CURRENT_BINARY_DIR}/queen_web_font_source_lifecycle.html" COPYONLY)
find_program(ARP_WEBGL_RUNTIME_BROWSER NAMES chrome.exe msedge.exe
    HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
add_executable(arp_queen_character_source_lifecycle
    "${queen_root}/tests/smoke/queen_character_source_lifecycle.cpp"
    "${queen_root}/examples/build_a_queen/character_source_task.cpp"
    "${queen_root}/examples/build_a_queen/character_sources.cpp")
target_include_directories(arp_queen_character_source_lifecycle PRIVATE "${queen_root}")
target_link_libraries(arp_queen_character_source_lifecycle PRIVATE arp_core arp_fkyaml_headers)
target_link_options(arp_queen_character_source_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0)
configure_file("${queen_root}/tests/fixtures/queen_character_source_lifecycle.html"
    "${CMAKE_CURRENT_BINARY_DIR}/queen_character_source_lifecycle.html" COPYONLY)
add_test(NAME arp_queen_character_web_package_tests
    COMMAND "${Python3_EXECUTABLE}" "${queen_root}/tests/contract/queen_character_browser_smoke.py"
        --directory "${CMAKE_CURRENT_BINARY_DIR}" --mode package)
if(ARP_WEBGL_RUNTIME_BROWSER)
    add_test(NAME arp_queen_web_font_source_lifecycle_smoke
        COMMAND "${Python3_EXECUTABLE}" "${queen_root}/tests/contract/queen_character_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --mode font-lifecycle
            --browser "${ARP_WEBGL_RUNTIME_BROWSER}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/queen-font-lifecycle-ctest")
    set_tests_properties(arp_queen_web_font_source_lifecycle_smoke
        PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE LABELS "browser-native;ui;io;example;smoke")
    add_test(NAME arp_queen_web_shader_source_lifecycle_smoke
        COMMAND "${Python3_EXECUTABLE}" "${queen_root}/tests/contract/queen_character_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --mode shader-lifecycle
            --browser "${ARP_WEBGL_RUNTIME_BROWSER}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/queen-shader-lifecycle-ctest")
    set_tests_properties(arp_queen_web_shader_source_lifecycle_smoke
        PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE LABELS "browser-native;shader;io;example;smoke")
    add_test(NAME arp_queen_character_web_lifecycle_smoke
        COMMAND "${Python3_EXECUTABLE}" "${queen_root}/tests/contract/queen_character_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --mode lifecycle
            --browser "${ARP_WEBGL_RUNTIME_BROWSER}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/queen-source-lifecycle-ctest")
    set_tests_properties(arp_queen_character_web_lifecycle_smoke
        PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE LABELS "browser-native;io;example;smoke")
    foreach(mode IN ITEMS success missing manifest cancel shader-missing shader-hash shader-cancel font-missing font-hash font-cancel)
        add_test(NAME arp_queen_character_web_${mode}_smoke
            COMMAND "${Python3_EXECUTABLE}" "${queen_root}/tests/contract/queen_character_browser_smoke.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}" --mode "${mode}"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/queen-source-${mode}-ctest")
        set_tests_properties(arp_queen_character_web_${mode}_smoke
            PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "browser-native;webgl1;io;example;smoke;prototype")
    endforeach()
endif()

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
include("${queen_root}/cmake/ArpSurvivorsGameplayWebValidation.cmake")
include("${queen_root}/cmake/ArpAudioMemoryImportWebValidation.cmake")
