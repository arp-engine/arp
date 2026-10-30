set(ARP_EDITOR_WEB_PROGRAMS "" CACHE PATH "Explicit host-generated Editor WebGL shader publication")
set(ARP_EDITOR_WEB_MONO_FONT "" CACHE FILEPATH "Explicit Web Editor monospace font")
set(ARP_EDITOR_WEB_CJK_FONT "" CACHE FILEPATH "Explicit Web Editor CJK font")
foreach(required IN ITEMS ARP_EDITOR_WEB_MONO_FONT ARP_EDITOR_WEB_CJK_FONT)
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "ARP-EDITOR-WEBGL-FONT-MISSING: ${required}")
    endif()
endforeach()
foreach(program IN ITEMS gui_unified environment/editor_viewport_environment
    pbr/editor_viewport_pbr sprite/editor_viewport_sprite)
    if(NOT EXISTS "${ARP_EDITOR_WEB_PROGRAMS}/${program}.webgl-program.yaml")
        message(FATAL_ERROR "ARP-EDITOR-WEBGL-SHADER-MISSING: ${program}")
    endif()
endforeach()
target_compile_definitions(arp_editor PRIVATE ARP_SOURCE_DIR="/app")
target_sources(arp_editor PRIVATE web/browser_workspace.cpp
    viewport/editor_scene_viewport_environment_contract.cpp
    viewport/editor_scene_viewport_ordered_renderer.cpp
    viewport/editor_scene_viewport_mesh_cache.cpp
    viewport/editor_scene_viewport_texture_cache.cpp
    viewport/editor_scene_viewport_material_payload.cpp
    viewport/editor_scene_viewport_shader_binding_contract.cpp
    viewport/editor_scene_viewport_pipeline_contract.cpp
    viewport/editor_scene_viewport_sprite_contract.cpp
    viewport/editor_scene_viewport_sprite_extraction.cpp
    viewport/editor_scene_viewport_sprite_renderer.cpp)
target_link_libraries(arp_editor PRIVATE arp_editor_shell arp_assets arp_platform arp_renderer
    arp_render_graph arp_rhi arp_rhi_semantics arp_gui arp_ui_runtime arp_resource arp_2d_resources
    arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter
    arp_scriptvm_aot_tooling)
# Explicit worker and Wasm budgets. The browser host yields between polls; it
# never waits synchronously for file workers on the UI thread.
target_link_options(arp_editor PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sSTACK_SIZE=4194304
    -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=0
    -g0
    -sPTHREAD_POOL_SIZE=16 -sPTHREAD_POOL_SIZE_STRICT=2
    "-sEXPORTED_RUNTIME_METHODS=['ccall','FS']"
    "--shell-file=${ARP_EDITOR_ENGINE_ROOT}/editor/web/index.html"
    "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/editor/resources@/app/editor/resources"
    "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/editor/web/project@/workspace/default"
    "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/scene_2d@/app/tests/fixtures/scene_2d"
    "--preload-file=${ARP_EDITOR_WEB_PROGRAMS}@/artifacts"
    "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/3rd/JoltPhysics-5.6.0/Assets/Fonts/Roboto-Regular.ttf@/fonts/editor.ttf"
    "--preload-file=${ARP_EDITOR_WEB_MONO_FONT}@/fonts/code.ttf"
    "--preload-file=${ARP_EDITOR_WEB_CJK_FONT}@/fonts/cjk.ttf")
set_target_properties(arp_editor PROPERTIES SUFFIX ".html")
configure_file("${ARP_EDITOR_ENGINE_ROOT}/editor/web/cli_bridge.js"
    "${CMAKE_BINARY_DIR}/editor_cli_bridge.js" COPYONLY)
configure_file("${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/editor_webgl_cli_smoke.js"
    "${CMAKE_BINARY_DIR}/editor_webgl_cli_smoke.js" COPYONLY)
add_test(NAME arp_editor_webgl_bridge_contract
    COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_web_bridge_tests.py")
configure_file("${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/editor_webgl_smoke.js"
    "${CMAKE_BINARY_DIR}/editor_webgl_smoke.js" COPYONLY)
configure_file("${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/editor_webgl_capture.js"
    "${CMAKE_BINARY_DIR}/editor_webgl_capture.js" COPYONLY)
configure_file("${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/editor_webgl_negative.js"
    "${CMAKE_BINARY_DIR}/editor_webgl_negative.js" COPYONLY)
set(ARP_EDITOR_WEB_BROWSER "C:/Program Files/Google/Chrome/Application/chrome.exe" CACHE FILEPATH "Browser for Editor WebGL smoke")
add_test(NAME arp_editor_webgl_shader_contract
    COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_webgl_shader.py"
    --programs "${ARP_EDITOR_WEB_PROGRAMS}")
if(EXISTS "${ARP_EDITOR_WEB_BROWSER}")
    set(ARP_EDITOR_WEB_NATIVE_CLI "${ARP_EDITOR_ENGINE_ROOT}/build/win-vulkan-debug/arp_editor_cli.exe" CACHE FILEPATH "Native Interpreter CLI for Web Editor smoke")
    if(EXISTS "${ARP_EDITOR_WEB_NATIVE_CLI}")
        add_test(NAME arp_editor_webgl_cli_smoke
            COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_web_cli_smoke.py"
            --directory "${CMAKE_BINARY_DIR}" --browser "${ARP_EDITOR_WEB_BROWSER}"
            --cli "${ARP_EDITOR_WEB_NATIVE_CLI}" --output "${CMAKE_BINARY_DIR}/editor-web-evidence/cli")
        set_tests_properties(arp_editor_webgl_cli_smoke PROPERTIES TIMEOUT 360)
    endif()
    add_test(NAME arp_editor_webgl_browser_smoke
        COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_webgl_browser.py"
        --browser "${ARP_EDITOR_WEB_BROWSER}" --directory "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_BINARY_DIR}/editor-web-evidence/smoke")
    set_tests_properties(arp_editor_webgl_browser_smoke PROPERTIES TIMEOUT 240)
    add_test(NAME arp_editor_webgl_viewport_smoke
        COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_webgl_browser.py"
        --browser "${ARP_EDITOR_WEB_BROWSER}" --directory "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_BINARY_DIR}/editor-web-evidence/viewport" --viewport)
    set_tests_properties(arp_editor_webgl_viewport_smoke PROPERTIES TIMEOUT 540)
    add_test(NAME arp_editor_webgl_catalog_smoke
        COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_webgl_browser.py"
        --browser "${ARP_EDITOR_WEB_BROWSER}" --directory "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_BINARY_DIR}/editor-web-evidence/catalog" --catalog)
    set_tests_properties(arp_editor_webgl_catalog_smoke PROPERTIES TIMEOUT 240)
    add_test(NAME arp_editor_webgl_workspace_lifecycle_smoke
        COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_webgl_browser.py"
        --browser "${ARP_EDITOR_WEB_BROWSER}" --directory "${CMAKE_BINARY_DIR}"
        --output "${CMAKE_BINARY_DIR}/editor-web-evidence/lifecycle" --cycles 20)
    set_tests_properties(arp_editor_webgl_workspace_lifecycle_smoke PROPERTIES TIMEOUT 540)
    foreach(scenario IN ITEMS derivatives fragment-depth srgb shader visibility loss)
        add_test(NAME arp_editor_webgl_${scenario}_smoke
            COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_webgl_browser.py"
            --browser "${ARP_EDITOR_WEB_BROWSER}" --directory "${CMAKE_BINARY_DIR}"
            --output "${CMAKE_BINARY_DIR}/editor-web-evidence/${scenario}" --negative "${scenario}")
        set_tests_properties(arp_editor_webgl_${scenario}_smoke PROPERTIES TIMEOUT 100)
    endforeach()
endif()
set_property(TARGET arp_editor APPEND PROPERTY LINK_DEPENDS
    "${ARP_EDITOR_ENGINE_ROOT}/editor/web/index.html")
include("${ARP_EDITOR_ENGINE_ROOT}/cmake/ArpWebGlComponentLibraries.cmake")
arp_webgl_track_preload_inputs(arp_editor "${ARP_EDITOR_WEB_PROGRAMS}"
    "${ARP_EDITOR_ENGINE_ROOT}/editor/web/project" "${ARP_EDITOR_ENGINE_ROOT}/editor/resources")
