set(ARP_EDITOR_UI_SOURCE_HEAP_BYTES "536870912" CACHE STRING "Fixed CPU UI probe heap: 256 or 512 MiB")
if(NOT ARP_EDITOR_UI_SOURCE_HEAP_BYTES MATCHES "^(268435456|536870912)$")
    message(FATAL_ERROR "EDITOR-UI-FETCH-HEAP-PROFILE")
endif()
# Explicit CPU-only test, original UI sources through real Fetch. No Editor
# product launch, GPU, preload or system-font redistribution is involved.
set(ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS "" CACHE PATH "Native compiler publication root for the UI Fetch oracle")
set(ARP_EDITOR_UI_SOURCE_FONT_ROOT "C:/Windows/Fonts" CACHE PATH "Original system fonts used only by the UI Fetch probe")
if(NOT EXISTS "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}/gui_overlay.artifact-set.yaml")
    message(FATAL_ERROR "EDITOR-UI-FETCH-ORIGINAL-SHADERS")
endif()
set(ui_probe_identity "${CMAKE_CURRENT_BINARY_DIR}/editor_ui_source_identity.h")
set(ui_probe_root "${ARP_EDITOR_ENGINE_ROOT}")
add_custom_command(OUTPUT "${ui_probe_identity}"
    COMMAND "${Python3_EXECUTABLE}" "${ui_probe_root}/editor/tests/ui_source_identity.py"
        --ui-root "${ui_probe_root}/editor/resources"
        --font-root "${ARP_EDITOR_UI_SOURCE_FONT_ROOT}"
        --shader-root "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}"
        --output "${ui_probe_identity}"
    DEPENDS "${ui_probe_root}/editor/tests/ui_source_identity.py"
        "${ui_probe_root}/editor/tests/fixtures/ui_source_files.py"
        "${ui_probe_root}/editor/resources/windows-ui.sources.yaml"
        "${ui_probe_root}/editor/resources/editor.arpui" "${ui_probe_root}/editor/resources/editor_atlas.png"
        "${ARP_EDITOR_UI_SOURCE_FONT_ROOT}/segoeui.ttf" "${ARP_EDITOR_UI_SOURCE_FONT_ROOT}/seguisb.ttf"
        "${ARP_EDITOR_UI_SOURCE_FONT_ROOT}/consola.ttf" "${ARP_EDITOR_UI_SOURCE_FONT_ROOT}/Deng.ttf"
        "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}/gui_overlay.artifact-set.yaml"
        "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}/gui_overlay.slang"
        "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}/gui_overlay.spv"
        "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}/gui_overlay.metal"
        "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}/gui_overlay.wgsl"
        "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}/gui_overlay.reflection.yaml"
    VERBATIM)
add_executable(arp_editor_ui_source_lifecycle tests/ui_source_lifecycle.cpp "${ui_probe_identity}")
target_include_directories(arp_editor_ui_source_lifecycle PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(arp_editor_ui_source_lifecycle PRIVATE arp_editor_ui_sources arp_scriptvm_runtime)
target_compile_options(arp_editor_ui_source_lifecycle PRIVATE -UNDEBUG)
target_link_options(arp_editor_ui_source_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=${ARP_EDITOR_UI_SOURCE_HEAP_BYTES} -sABORTING_MALLOC=0 -sALLOW_MEMORY_GROWTH=0
    -sSTACK_SIZE=1048576)
configure_file("${ui_probe_root}/scripts/browser_progress_transport.js"
    "${CMAKE_CURRENT_BINARY_DIR}/browser_progress_transport.js" COPYONLY)
configure_file(tests/fixtures/ui_source_lifecycle.html
    "${CMAKE_CURRENT_BINARY_DIR}/ui_source_lifecycle.html" COPYONLY)
find_program(ARP_EDITOR_UI_SOURCE_BROWSER NAMES chrome.exe msedge.exe
    HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
if(ARP_EDITOR_UI_SOURCE_BROWSER)
    add_test(NAME arp_editor_ui_sources_fetch_smoke
        COMMAND "${Python3_EXECUTABLE}" "${ui_probe_root}/editor/tests/ui_sources_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_EDITOR_UI_SOURCE_BROWSER}"
            --font-root "${ARP_EDITOR_UI_SOURCE_FONT_ROOT}"
            --shader-root "${ARP_EDITOR_UI_SOURCE_ORIGINAL_SHADERS}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/ui-source-fetch-ctest")
    # 25 cycles *10s + three browser retirements *10s +20s fixture overhead.
    set_tests_properties(arp_editor_ui_sources_fetch_smoke PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE
        LABELS "editor;ui;io;browser-native;smoke")
endif()
