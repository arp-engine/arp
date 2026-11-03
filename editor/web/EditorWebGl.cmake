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
    app/editor_project_source_host.cpp
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
target_link_libraries(arp_editor PRIVATE arp_editor_web_startup_sources arp_editor_shell arp_assets arp_platform arp_renderer
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
    "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/editor/web/project@/workspace/default"
    "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/scene_2d@/app/tests/fixtures/scene_2d")
# Original inputs are explicit same-origin static sources, not hidden Wasm/MEMFS
# fallbacks. The default workspace and viewport fixture remain separate contracts.
set(editor_web_sources "${CMAKE_BINARY_DIR}/editor-sources")
add_custom_target(arp_editor_web_startup_delivery
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${editor_web_sources}/ui" "${editor_web_sources}/fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ARP_EDITOR_ENGINE_ROOT}/editor/resources/web-ui.sources.yaml"
        "${ARP_EDITOR_ENGINE_ROOT}/editor/resources/editor.arpui"
        "${ARP_EDITOR_ENGINE_ROOT}/editor/resources/editor_atlas.png" "${editor_web_sources}/ui"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ARP_EDITOR_ENGINE_ROOT}/3rd/JoltPhysics-5.6.0/Assets/Fonts/Roboto-Regular.ttf" "${editor_web_sources}/fonts/editor.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ARP_EDITOR_WEB_MONO_FONT}" "${editor_web_sources}/fonts/code.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ARP_EDITOR_WEB_CJK_FONT}" "${editor_web_sources}/fonts/cjk.ttf"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${ARP_EDITOR_WEB_PROGRAMS}" "${editor_web_sources}/shaders"
    VERBATIM)
add_dependencies(arp_editor arp_editor_web_startup_delivery)
add_test(NAME arp_editor_web_startup_delivery_contract
    COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_web_startup_delivery.py"
        --directory "${CMAKE_BINARY_DIR}" --shaders "${ARP_EDITOR_WEB_PROGRAMS}"
        --mono "${ARP_EDITOR_WEB_MONO_FONT}" --cjk "${ARP_EDITOR_WEB_CJK_FONT}")
set_tests_properties(arp_editor_web_startup_delivery_contract PROPERTIES
    TIMEOUT 60 LABELS "editor;io;contract")
set_target_properties(arp_editor PROPERTIES SUFFIX ".html")
configure_file("${ARP_EDITOR_ENGINE_ROOT}/editor/web/cli_bridge.js"
    "${CMAKE_BINARY_DIR}/editor_cli_bridge.js" COPYONLY)
configure_file("${ARP_EDITOR_ENGINE_ROOT}/editor/web/workspace_connection.js"
    "${CMAKE_BINARY_DIR}/editor_workspace_connection.js" COPYONLY)
configure_file("${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/editor_webgl_cli_smoke.js"
    "${CMAKE_BINARY_DIR}/editor_webgl_cli_smoke.js" COPYONLY)
configure_file("${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/editor_webgl_workspace_smoke.js"
    "${CMAKE_BINARY_DIR}/editor_webgl_workspace_smoke.js" COPYONLY)
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
        set(ARP_EDITOR_WEB_WORKSPACE_HOST "${ARP_EDITOR_ENGINE_ROOT}/build/win-vulkan-debug/editor/arp_editor_workspace_service.exe"
            CACHE FILEPATH "Native workspace host for Web product connection smoke")
        set(ARP_EDITOR_WEB_NATIVE_SOURCE_READER "${ARP_EDITOR_ENGINE_ROOT}/build/win-vulkan-debug/editor/arp_editor_workspace_source_host_tests.exe"
            CACHE FILEPATH "Original native Editor loader fixture for Web-written projects")
        if(EXISTS "${ARP_EDITOR_WEB_WORKSPACE_HOST}" AND EXISTS "${ARP_EDITOR_WEB_NATIVE_SOURCE_READER}")
            add_test(NAME arp_editor_webgl_workspace_source_smoke
                COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_web_workspace_smoke.py"
                --directory "${CMAKE_BINARY_DIR}" --browser "${ARP_EDITOR_WEB_BROWSER}"
                --cli "${ARP_EDITOR_WEB_NATIVE_CLI}" --host "${ARP_EDITOR_WEB_WORKSPACE_HOST}"
                --native-reader "${ARP_EDITOR_WEB_NATIVE_SOURCE_READER}"
                --output "${CMAKE_BINARY_DIR}/editor-web-evidence/workspace-source")
            set_tests_properties(arp_editor_webgl_workspace_source_smoke
                PROPERTIES TIMEOUT 540 RUN_SERIAL TRUE LABELS "editor;io;workspace;browser;smoke")
            add_test(NAME arp_editor_webgl_script_reload_source_smoke
                COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_web_workspace_smoke.py"
                --directory "${CMAKE_BINARY_DIR}" --browser "${ARP_EDITOR_WEB_BROWSER}"
                --cli "${ARP_EDITOR_WEB_NATIVE_CLI}" --host "${ARP_EDITOR_WEB_WORKSPACE_HOST}"
                --native-reader "${ARP_EDITOR_WEB_NATIVE_SOURCE_READER}" --reload-modules
                --output "${CMAKE_BINARY_DIR}/editor-web-evidence/script-module-reload")
            set_tests_properties(arp_editor_webgl_script_reload_source_smoke
                PROPERTIES TIMEOUT 720 RUN_SERIAL TRUE LABELS "editor;io;script;reload;browser;smoke")
            foreach(receipt_cycles IN ITEMS 1 20)
                add_test(NAME arp_editor_webgl_receipt_fault_${receipt_cycles}_smoke
                    COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_web_workspace_smoke.py"
                    --directory "${CMAKE_BINARY_DIR}" --browser "${ARP_EDITOR_WEB_BROWSER}"
                    --cli "${ARP_EDITOR_WEB_NATIVE_CLI}" --host "${ARP_EDITOR_WEB_WORKSPACE_HOST}"
                    --native-reader "${ARP_EDITOR_WEB_NATIVE_SOURCE_READER}"
                    --receipt-faults --receipt-cycles "${receipt_cycles}"
                    --output "${CMAKE_BINARY_DIR}/editor-web-evidence/receipt-fault-${receipt_cycles}")
                set_tests_properties(arp_editor_webgl_receipt_fault_${receipt_cycles}_smoke
                    PROPERTIES TIMEOUT 4200 RUN_SERIAL TRUE LABELS "editor;io;receipt;browser;smoke")
            endforeach()
            set(ARP_EDITOR_WOCC_WORKSPACE_ROOT "" CACHE PATH "Explicit staged WOCC verification root with adoption.yaml")
            set(ARP_EDITOR_WEB_NATIVE_EDITOR "${ARP_EDITOR_ENGINE_ROOT}/build/win-vulkan-debug/arp_editor.exe"
                CACHE FILEPATH "Native Editor for actual WOCC Web-written project reopen")
            if(ARP_EDITOR_WOCC_WORKSPACE_ROOT)
                if(NOT IS_DIRECTORY "${ARP_EDITOR_WOCC_WORKSPACE_ROOT}" OR NOT EXISTS "${ARP_EDITOR_WEB_NATIVE_EDITOR}")
                    message(FATAL_ERROR "EDITOR-WOCC-EXPLICIT-WORKSPACE-OR-NATIVE-EDITOR-MISSING")
                endif()
                add_test(NAME arp_editor_webgl_wocc_workspace_smoke
                    COMMAND "${Python3_EXECUTABLE}" "${ARP_EDITOR_ENGINE_ROOT}/tests/contract/editor_web_workspace_smoke.py"
                        --directory "${CMAKE_BINARY_DIR}" --browser "${ARP_EDITOR_WEB_BROWSER}"
                        --cli "${ARP_EDITOR_WEB_NATIVE_CLI}" --host "${ARP_EDITOR_WEB_WORKSPACE_HOST}"
                        --native-reader "${ARP_EDITOR_WEB_NATIVE_SOURCE_READER}"
                        --native-editor "${ARP_EDITOR_WEB_NATIVE_EDITOR}"
                        --wocc-root "${ARP_EDITOR_WOCC_WORKSPACE_ROOT}"
                        --output "${CMAKE_BINARY_DIR}/editor-web-evidence/wocc-workspace")
                set_tests_properties(arp_editor_webgl_wocc_workspace_smoke
                    PROPERTIES TIMEOUT 1200 RUN_SERIAL TRUE LABELS "editor;io;wocc;browser;smoke")
            endif()
        endif()
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
