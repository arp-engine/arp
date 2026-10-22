get_filename_component(ARP_EDITOR_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_library(arp_editor_shell STATIC
    command/editor_command.cpp
    remote/editor_remote_protocol.cpp
    remote/editor_remote_server.cpp
    ingress/platform_input_adapter.cpp
    workspace/editor_presentation_state.cpp
    workspace/game_preview_model.cpp
    workspace/editor_shell.cpp
    workspace/editor_shell_commands.cpp
    workspace/project_browser_model.cpp
    workspace/project_browser.cpp
    workspace/editor_project_resources.cpp
    workspace/editor_terrain_build.cpp
    workspace/editor_scene_component_registry.cpp
    workspace/editor_scene_document.cpp
    workspace/editor_scene_inspector.cpp
    workspace/editor_scene_authoring_codec.cpp
    workspace/editor_scene_persistence.cpp
    viewport/editor_scene_viewport_contract.cpp
    viewport/editor_scene_viewport_frame_builder.cpp
    viewport/editor_scene_viewport_physics_overlay.cpp
    viewport/editor_scene_component_gizmos.cpp
    viewport/editor_scene_viewport_camera.cpp
    viewport/editor_gltf_material_mapping.cpp
    viewport/editor_scene_viewport_texture_source.cpp
    viewport/editor_scene_viewport_texture_admission.cpp
    viewport/editor_scene_viewport_resource_resolver.cpp
    workspace/script_document_session.cpp
    workspace/script_document.cpp
    workspace/editor_input.cpp
    workspace/editor_presentation.cpp)
target_sources(arp_editor_shell PRIVATE
    "${CMAKE_BINARY_DIR}/generated/reflection/arp_scene/arp_scene.reflection.gen.cpp"
    "${CMAKE_BINARY_DIR}/generated/reflection/arp_scene2d/arp_scene2d.reflection.gen.cpp"
    "${CMAKE_BINARY_DIR}/generated/reflection/arp_scene3d/arp_scene3d.reflection.gen.cpp"
    "${CMAKE_BINARY_DIR}/generated/reflection/arp_resources/arp_resources.reflection.gen.cpp")
add_dependencies(arp_editor_shell
    arp_script_plugin_scene_arp_scene_reflection_generated
    arp_script_plugin_scene2d_arp_scene2d_reflection_generated
    arp_script_plugin_scene3d_arp_scene3d_reflection_generated
    arp_resource_arp_resources_reflection_generated)
arp_reflection_generate(
    TARGET arp_editor_shell
    MODULE arp_editor_component_properties
    OUT_JSON "${CMAKE_BINARY_DIR}/generated/reflection/arp_editor_component_properties/arp_editor_component_properties.reflection.json"
    OUT_CPP "${CMAKE_BINARY_DIR}/generated/reflection/arp_editor_component_properties/arp_editor_component_properties.reflection.gen.cpp"
    HEADERS
        "${ARP_EDITOR_ENGINE_ROOT}/src/audio/audio_components.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/scene/runtime_components.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/ui/document_component_editor_properties.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/2d/screen_canvas_component.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/2d/sprite_animation_component.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/2d/tile_map_resource_component.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/2d/physics_components.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/3d/animator_component.h"
        "${ARP_EDITOR_ENGINE_ROOT}/src/scene/scene_asset_resource.h")
target_include_directories(arp_editor_shell PUBLIC "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
target_include_directories(arp_editor_shell PRIVATE "${ARP_EDITOR_ENGINE_ROOT}/3rd/assimp-6.0.5/contrib/rapidjson/include")
target_link_libraries(arp_editor_shell PUBLIC
    arp_ui_runtime arp_resource arp_network arp_3d arp_2d arp_physics2d arp_scriptvm_runtime)
add_library(arp_editor_viewport_renderer STATIC
    viewport/editor_scene_viewport_mesh_cache.cpp
    viewport/editor_scene_viewport_draw_executor.cpp
    viewport/editor_scene_viewport_texture_cache.cpp
    viewport/editor_scene_viewport_material_payload.cpp
    viewport/editor_scene_viewport_shader_binding_contract.cpp
    viewport/editor_scene_viewport_pipeline_contract.cpp
    viewport/editor_scene_viewport_composite.cpp
    viewport/editor_scene_viewport_sprite_renderer.cpp
    viewport/editor_scene_viewport_environment.cpp
    viewport/editor_scene_viewport_session.cpp
    viewport/editor_scene_viewport_shader_package.cpp)
target_include_directories(arp_editor_viewport_renderer PUBLIC
    "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
target_link_libraries(arp_editor_viewport_renderer PUBLIC arp_editor_shell arp_renderer)
if(MINGW)
    target_compile_options(arp_editor_shell PRIVATE "-Wa,-mbig-obj")
    target_compile_options(arp_editor_viewport_renderer PRIVATE "-Wa,-mbig-obj")
endif()
add_executable(arp_editor_tests tests/interaction_tests.cpp app/game_preview.cpp)
target_compile_definitions(arp_editor_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
else()
    target_link_libraries(arp_editor_tests PRIVATE "$<LINK_GROUP:RESCAN,arp_editor_shell,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_2d_resources>")
endif()
add_test(NAME arp_editor_tests COMMAND arp_editor_tests)
add_executable(arp_editor_scene_command_tests tests/scene_command_tests.cpp)
target_compile_definitions(arp_editor_scene_command_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_scene_command_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_scene_command_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d arp_2d_resources)
else()
    target_link_libraries(arp_editor_scene_command_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_shell,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d,arp_2d_resources>")
endif()
add_test(NAME arp_editor_scene_command_tests COMMAND arp_editor_scene_command_tests)
add_executable(arp_editor_scene_viewport_contract_tests tests/scene_viewport_contract_tests.cpp)
target_compile_definitions(arp_editor_scene_viewport_contract_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_include_directories(arp_editor_scene_viewport_contract_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_scene_viewport_contract_tests PRIVATE
        arp_editor_viewport_renderer arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_renderer arp_rhi_semantics arp_rhi arp_2d_resources)
else()
    target_link_libraries(arp_editor_scene_viewport_contract_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_viewport_renderer,arp_editor_shell,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_renderer,arp_2d_resources>"
        arp_rhi_semantics arp_rhi)
endif()
target_compile_options(arp_editor_scene_viewport_contract_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_scene_viewport_contract_tests COMMAND arp_editor_scene_viewport_contract_tests)
add_executable(arp_editor_scene_viewport_sprite_pass_contract_tests
    tests/scene_viewport_sprite_pass_contract_tests.cpp)
target_include_directories(arp_editor_scene_viewport_sprite_pass_contract_tests PRIVATE
    "${ARP_EDITOR_ENGINE_ROOT}"
    "${ARP_EDITOR_ENGINE_ROOT}/src")
target_link_libraries(arp_editor_scene_viewport_sprite_pass_contract_tests PRIVATE arp_core)
target_compile_options(arp_editor_scene_viewport_sprite_pass_contract_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_scene_viewport_sprite_pass_contract_tests
    COMMAND arp_editor_scene_viewport_sprite_pass_contract_tests)
add_executable(arp_editor_scene_viewport_shader_binding_contract_tests
    tests/scene_viewport_shader_binding_contract_tests.cpp
    viewport/editor_scene_viewport_shader_binding_contract.cpp)
target_include_directories(arp_editor_scene_viewport_shader_binding_contract_tests PRIVATE
    "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
target_link_libraries(arp_editor_scene_viewport_shader_binding_contract_tests PRIVATE arp_shader arp_rhi)
target_compile_options(arp_editor_scene_viewport_shader_binding_contract_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_scene_viewport_shader_binding_contract_tests
    COMMAND arp_editor_scene_viewport_shader_binding_contract_tests)
add_executable(arp_editor_scene_viewport_pipeline_contract_tests
    tests/scene_viewport_pipeline_contract_tests.cpp)
target_compile_definitions(arp_editor_scene_viewport_pipeline_contract_tests PRIVATE
    ARP_EDITOR_VIEWPORT_PBR_REFLECTION="${CMAKE_BINARY_DIR}/shaders/editor_viewport_pbr.reflection.yaml"
    ARP_EDITOR_VIEWPORT_PBR_SOURCE="${CMAKE_BINARY_DIR}/shaders/editor_viewport_pbr.slang"
    ARP_EDITOR_VIEWPORT_PBR_SPIRV="${CMAKE_BINARY_DIR}/shaders/editor_viewport_pbr.spv"
    ARP_EDITOR_VIEWPORT_PBR_MSL="${CMAKE_BINARY_DIR}/shaders/editor_viewport_pbr.metal"
    ARP_EDITOR_VIEWPORT_PBR_WGSL="${CMAKE_BINARY_DIR}/shaders/editor_viewport_pbr.wgsl"
    ARP_EDITOR_VIEWPORT_SPRITE_REFLECTION="${CMAKE_BINARY_DIR}/shaders/editor_viewport_sprite.reflection.yaml"
    ARP_EDITOR_VIEWPORT_SPRITE_SOURCE="${CMAKE_BINARY_DIR}/shaders/editor_viewport_sprite.slang"
    ARP_EDITOR_VIEWPORT_SPRITE_SPIRV="${CMAKE_BINARY_DIR}/shaders/editor_viewport_sprite.spv"
    ARP_EDITOR_VIEWPORT_SPRITE_MSL="${CMAKE_BINARY_DIR}/shaders/editor_viewport_sprite.metal"
    ARP_EDITOR_VIEWPORT_SPRITE_WGSL="${CMAKE_BINARY_DIR}/shaders/editor_viewport_sprite.wgsl")
target_include_directories(arp_editor_scene_viewport_pipeline_contract_tests PRIVATE
    "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
target_link_libraries(arp_editor_scene_viewport_pipeline_contract_tests PRIVATE
    arp_editor_viewport_renderer arp_shader arp_rhi)
target_compile_options(arp_editor_scene_viewport_pipeline_contract_tests PRIVATE -UNDEBUG)
if(TARGET arp_compile_shaders)
    add_dependencies(arp_editor_scene_viewport_pipeline_contract_tests arp_compile_shaders)
endif()
add_test(NAME arp_editor_scene_viewport_pipeline_contract_tests
    COMMAND arp_editor_scene_viewport_pipeline_contract_tests)
add_executable(arp_editor_scene_viewport_environment_tests tests/scene_viewport_environment_tests.cpp)
target_compile_definitions(arp_editor_scene_viewport_environment_tests PRIVATE
    ARP_EDITOR_ENVIRONMENT_SHADER_ROOT="${CMAKE_BINARY_DIR}/shaders")
target_link_libraries(arp_editor_scene_viewport_environment_tests PRIVATE arp_editor_viewport_renderer arp_shader arp_rhi)
target_compile_options(arp_editor_scene_viewport_environment_tests PRIVATE -UNDEBUG)
add_dependencies(arp_editor_scene_viewport_environment_tests arp_compile_shaders)
add_test(NAME arp_editor_scene_viewport_environment_tests COMMAND arp_editor_scene_viewport_environment_tests)
add_executable(arp_editor_command_tests tests/command_tests.cpp)
target_compile_definitions(arp_editor_command_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_command_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_2d_resources)
else()
    target_link_libraries(arp_editor_command_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_shell,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources>")
endif()
target_compile_options(arp_editor_command_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_command_tests COMMAND arp_editor_command_tests)
add_executable(arp_editor_remote_server_tests tests/remote_server_tests.cpp)
target_compile_definitions(arp_editor_remote_server_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_remote_server_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_remote_server_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
else()
    target_link_libraries(arp_editor_remote_server_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_shell,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources>")
endif()
add_test(NAME arp_editor_remote_server_tests COMMAND arp_editor_remote_server_tests)
set_tests_properties(arp_editor_remote_server_tests PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_executable(arp_editor_support_tests tests/support_tests.cpp)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_support_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
else()
    target_link_libraries(arp_editor_support_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_shell,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources>")
endif()
target_include_directories(arp_editor_support_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
target_compile_options(arp_editor_support_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_support_tests COMMAND arp_editor_support_tests)
add_executable(arp_editor_project_resource_tests tests/project_resource_tests.cpp)
target_include_directories(arp_editor_project_resource_tests PRIVATE
    "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_project_resource_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
else()
    target_link_libraries(arp_editor_project_resource_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_shell,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources>")
endif()
target_compile_options(arp_editor_project_resource_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_project_resource_tests COMMAND arp_editor_project_resource_tests)
add_test(NAME arp_editor_build_contract_tests
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${ARP_EDITOR_ENGINE_ROOT}/tests/tooling/editor_build_contract_tests.py")
if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(arp_editor_cli cli/main.cpp)
    target_include_directories(arp_editor_cli PRIVATE "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
    target_compile_definitions(arp_editor_cli PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_link_libraries(arp_editor_cli PRIVATE
        arp_scriptvm_editor_runtime
        arp_scriptvm_host_plugin_runtime
        arp_script_plugin_network_static
        arp_network)
    if(WIN32 AND MINGW)
        target_link_options(arp_editor_cli PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    set_target_properties(arp_editor_cli PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
    target_compile_definitions(arp_editor_remote_server_tests PRIVATE
        ARP_EDITOR_CLI_EXECUTABLE="$<TARGET_FILE:arp_editor_cli>")
    add_dependencies(arp_editor_remote_server_tests arp_editor_cli)
    add_test(NAME arp_editor_cli_help_smoke COMMAND arp_editor_cli --help)
    set_tests_properties(arp_editor_cli_help_smoke PROPERTIES TIMEOUT 30)
endif()
add_executable(arp_editor main.cpp
    app/editor_application.cpp
    app/game_preview.cpp
    ui/editor_ui_assets.cpp
    "${ARP_EDITOR_ENGINE_ROOT}/src/platform/common/platform_lifecycle_state.cpp")
target_include_directories(arp_editor PRIVATE "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
if(EMSCRIPTEN)
    set(ARP_EDITOR_WEB_MONO_FONT "" CACHE FILEPATH "Monospace font file preloaded by the ARP Editor Web build")
    set(ARP_EDITOR_WEB_CJK_FONT "" CACHE FILEPATH "CJK font file preloaded by the ARP Editor Web build")
    if(NOT ARP_EDITOR_WEB_MONO_FONT)
        foreach(_candidate IN ITEMS "C:/Windows/Fonts/consola.ttf" "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf" "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf")
            if(EXISTS "${_candidate}")
                set(ARP_EDITOR_WEB_MONO_FONT "${_candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT ARP_EDITOR_WEB_CJK_FONT)
        foreach(_candidate IN ITEMS "C:/Windows/Fonts/Deng.ttf" "C:/Windows/Fonts/msyh.ttc" "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc" "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc")
            if(EXISTS "${_candidate}")
                set(ARP_EDITOR_WEB_CJK_FONT "${_candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT ARP_EDITOR_WEB_MONO_FONT OR NOT ARP_EDITOR_WEB_CJK_FONT)
        message(FATAL_ERROR "ARP Editor Web build needs ARP_EDITOR_WEB_MONO_FONT and ARP_EDITOR_WEB_CJK_FONT font files")
    endif()
    target_compile_definitions(arp_editor PRIVATE ARP_SOURCE_DIR="/app" ARP_SHADER_DIR="/shaders")
    target_link_libraries(arp_editor PRIVATE arp_editor_viewport_renderer arp_editor_shell arp_assets arp_platform arp_renderer arp_render_graph arp_rhi arp_rhi_semantics arp_gui arp_ui_runtime arp_resource arp_2d_resources arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
    target_link_options(arp_editor PRIVATE
        "--pre-js=${ARP_EDITOR_ENGINE_ROOT}/editor/tools/web_diagnostics_pre.js"
        "--pre-js=${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/webgpu_device_bootstrap_pre.js"
        -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sALLOW_MEMORY_GROWTH=1
        -sGROWABLE_ARRAYBUFFERS=0
        "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/editor/resources@/app/editor/resources"
        "--preload-file=${CMAKE_BINARY_DIR}/shaders@/shaders"
        "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/3rd/JoltPhysics-5.6.0/Assets/Fonts/Roboto-Regular.ttf@/fonts/editor.ttf"
        "--preload-file=${ARP_EDITOR_WEB_MONO_FONT}@/fonts/code.ttf"
        "--preload-file=${ARP_EDITOR_WEB_CJK_FONT}@/fonts/cjk.ttf")
    set_property(TARGET arp_editor APPEND PROPERTY LINK_DEPENDS
        "${ARP_EDITOR_ENGINE_ROOT}/editor/tools/web_diagnostics_pre.js"
        "${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/webgpu_device_bootstrap_pre.js")
    set_target_properties(arp_editor PROPERTIES SUFFIX ".html")
    add_dependencies(arp_editor arp_compile_shaders)
else()
    target_compile_definitions(arp_editor PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}" ARP_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_editor PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_editor PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor PRIVATE arp_editor_viewport_renderer arp_editor_shell arp_assets arp_platform arp_renderer arp_render_graph arp_rhi arp_rhi_semantics arp_gui arp_ui_runtime arp_resource arp_2d_resources arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
    else()
        target_link_libraries(arp_editor PRIVATE arp_editor_viewport_renderer arp_editor_shell arp_assets arp_platform arp_renderer arp_render_graph arp_rhi arp_rhi_semantics "$<LINK_GROUP:RESCAN,arp_gui,arp_ui_runtime,arp_resource,arp_2d_resources>" arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
    endif()
    if(WIN32)
        enable_language(RC)
        set(ARP_EDITOR_ICON_FILE
            "${ARP_EDITOR_ENGINE_ROOT}/editor/resources/arp_editor.ico")
        set(ARP_EDITOR_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/arp_editor_icon.rc")
        set(ARP_EDITOR_ICON_CONTRACT_HEADER
            "${ARP_EDITOR_ENGINE_ROOT}/src/platform/windows/windows_application_icon.h")
        configure_file("${ARP_EDITOR_ENGINE_ROOT}/editor/resources/arp_editor.rc.in"
            "${ARP_EDITOR_ICON_RC}" @ONLY)
        set_source_files_properties("${ARP_EDITOR_ICON_RC}" PROPERTIES
            OBJECT_DEPENDS "${ARP_EDITOR_ICON_FILE};${ARP_EDITOR_ICON_CONTRACT_HEADER}")
        add_library(arp_editor_icon OBJECT "${ARP_EDITOR_ICON_RC}")
        target_sources(arp_editor PRIVATE $<TARGET_OBJECTS:arp_editor_icon>)
        add_executable(arp_platform_window_icon_tests
            "${ARP_EDITOR_ENGINE_ROOT}/tests/integration/platform_window_icon_tests.cpp"
            $<TARGET_OBJECTS:arp_editor_icon>)
        target_include_directories(arp_platform_window_icon_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}/src")
        target_link_libraries(arp_platform_window_icon_tests PRIVATE arp_platform)
        target_compile_options(arp_platform_window_icon_tests PRIVATE -UNDEBUG)
        if(MINGW)
            target_link_options(arp_platform_window_icon_tests PRIVATE -static-libstdc++ -static-libgcc)
        endif()
        add_test(NAME arp_platform_window_icon_tests COMMAND arp_platform_window_icon_tests)
        set_tests_properties(arp_platform_window_icon_tests PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        if(MINGW)
            target_link_options(arp_editor PRIVATE -static-libstdc++ -static-libgcc)
        endif()
    endif()
    add_dependencies(arp_editor arp_compile_shaders)
    add_test(NAME arp_editor_hidden_smoke COMMAND arp_editor --smoke)
    add_test(NAME arp_editor_after_hidden_smoke COMMAND arp_editor --smoke --after)
    add_test(NAME arp_editor_script_hidden_smoke
        COMMAND arp_editor --smoke --project-root ${ARP_EDITOR_ENGINE_ROOT}
                --open-script editor/resources/editor_sample.arp)
    if(WIN32 AND NOT EMSCRIPTEN)
        add_test(NAME arp_editor_viewport_pipeline_hidden_smoke
            COMMAND arp_editor --smoke --viewport-pipeline-smoke)
        set_tests_properties(arp_editor_viewport_pipeline_hidden_smoke PROPERTIES
            TIMEOUT 60 RUN_SERIAL TRUE)
        add_test(NAME arp_editor_terrain_resource_hidden_smoke
            COMMAND arp_editor --smoke --terrain-resource-smoke)
        set_tests_properties(arp_editor_terrain_resource_hidden_smoke PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
        add_test(NAME arp_editor_viewport_runtime_hidden_smoke
            COMMAND arp_editor --smoke --viewport-runtime-smoke
                    --project-root ${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/editor_tilemap_project)
        set_tests_properties(arp_editor_viewport_runtime_hidden_smoke PROPERTIES
            TIMEOUT 60 RUN_SERIAL TRUE)
    endif()
    set_tests_properties(arp_editor_hidden_smoke arp_editor_after_hidden_smoke
        arp_editor_script_hidden_smoke PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endif()


# Keep the standard editor beside the engine artifacts in this build tree.
set_target_properties(arp_editor arp_editor_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
