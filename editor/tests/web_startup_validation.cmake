# Explicit original publication oracle, independent of the live Web product.
set(ARP_EDITOR_WEB_STARTUP_PROGRAMS "" CACHE PATH "Original Web Editor ES100 publications for startup tests")
set(ARP_EDITOR_WEB_STARTUP_MONO "C:/Windows/Fonts/consola.ttf" CACHE FILEPATH "Original startup-test monospace font")
set(ARP_EDITOR_WEB_STARTUP_CJK "C:/Windows/Fonts/Deng.ttf" CACHE FILEPATH "Original startup-test CJK font")
if(NOT ARP_EDITOR_WEB_STARTUP_PROGRAMS)
    return()
endif()
set(web_startup_identity "${CMAKE_CURRENT_BINARY_DIR}/editor_web_startup_identity.h")
add_custom_target(arp_editor_web_startup_identity
    COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/editor/tests/web_startup_identity.py"
        --root "${ARP_EDITOR_ENGINE_ROOT}" --shaders "${ARP_EDITOR_WEB_STARTUP_PROGRAMS}"
        --mono "${ARP_EDITOR_WEB_STARTUP_MONO}" --cjk "${ARP_EDITOR_WEB_STARTUP_CJK}"
        --output "${web_startup_identity}"
    BYPRODUCTS "${web_startup_identity}" VERBATIM)
add_executable(arp_editor_web_startup_tests tests/web_startup_sources_tests.cpp
    viewport/editor_scene_viewport_pipeline_contract.cpp
    viewport/editor_scene_viewport_shader_binding_contract.cpp
    viewport/editor_scene_viewport_sprite_contract.cpp)
add_dependencies(arp_editor_web_startup_tests arp_editor_web_startup_identity)
target_include_directories(arp_editor_web_startup_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(arp_editor_web_startup_tests PRIVATE arp_editor_web_startup_sources arp_scriptvm_runtime)
target_compile_options(arp_editor_web_startup_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    target_link_options(arp_editor_web_startup_tests PRIVATE
        -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=0
        -sABORTING_MALLOC=0 -sSTACK_SIZE=1048576)
    configure_file("${ARP_EDITOR_ENGINE_ROOT}/scripts/browser_progress_transport.js"
        "${CMAKE_CURRENT_BINARY_DIR}/browser_progress_transport.js" COPYONLY)
    configure_file(tests/fixtures/web_startup_lifecycle.html
        "${CMAKE_CURRENT_BINARY_DIR}/web_startup_lifecycle.html" COPYONLY)
    find_program(ARP_EDITOR_WEB_STARTUP_BROWSER NAMES chrome.exe msedge.exe
        HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
    if(ARP_EDITOR_WEB_STARTUP_BROWSER)
        add_test(NAME arp_editor_web_startup_fetch_smoke
            COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/editor/tests/web_startup_browser_smoke.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_EDITOR_WEB_STARTUP_BROWSER}"
                --shaders "${ARP_EDITOR_WEB_STARTUP_PROGRAMS}"
                --mono "${ARP_EDITOR_WEB_STARTUP_MONO}" --cjk "${ARP_EDITOR_WEB_STARTUP_CJK}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/web-startup-fetch-ctest")
        set_tests_properties(arp_editor_web_startup_fetch_smoke PROPERTIES
            TIMEOUT 1200 RUN_SERIAL TRUE LABELS "editor;io;browser-native;smoke")
    endif()
else()
    target_link_libraries(arp_editor_web_startup_tests PRIVATE "$<LINK_GROUP:RESCAN,arp_rhi,arp_rhi_semantics>")
    arp_add_mingw_static_runtime(arp_editor_web_startup_tests)
    add_test(NAME arp_editor_web_startup_tests COMMAND arp_editor_web_startup_tests)
    add_test(NAME arp_editor_web_startup_smoke COMMAND arp_editor_web_startup_tests --smoke)
    set_tests_properties(arp_editor_web_startup_tests arp_editor_web_startup_smoke PROPERTIES
        TIMEOUT 300 LABELS "editor;io;smoke")
endif()
