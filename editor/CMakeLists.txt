get_filename_component(ARP_EDITOR_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_subdirectory(project)

add_library(arp_editor_script_authoring STATIC workspace/editor_script_modules.cpp)
target_include_directories(arp_editor_script_authoring PUBLIC "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
target_link_libraries(arp_editor_script_authoring PUBLIC arp_editor_project arp_script_components)

add_executable(arp_editor_script_modules_tests tests/script_modules_tests.cpp)
target_compile_definitions(arp_editor_script_modules_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_script_modules_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_editor_script_modules_tests PRIVATE arp_editor_script_authoring arp_scene_script_reflection)
add_test(NAME arp_editor_script_modules_tests COMMAND arp_editor_script_modules_tests)
add_test(NAME arp_editor_script_modules_smoke COMMAND arp_editor_script_modules_tests --smoke)
set_tests_properties(arp_editor_script_modules_tests arp_editor_script_modules_smoke PROPERTIES TIMEOUT 60)

add_library(arp_editor_shell STATIC
    command/editor_command.cpp
    remote/editor_remote_protocol.cpp
    remote/editor_remote_server.cpp
    ingress/platform_input_adapter.cpp
    workspace/editor_presentation_state.cpp
    workspace/game_preview_model.cpp
    workspace/editor_shell.cpp
    workspace/editor_shell_commands.cpp
    workspace/editor_scene_presence.cpp
    workspace/editor_scene_presence_commands.cpp
    workspace/editor_scene_history.cpp
    workspace/editor_scene_transform_transaction.cpp
    workspace/editor_scene_transaction.cpp
    workspace/editor_shell_project.cpp
    workspace/editor_shell_scene.cpp
    workspace/editor_shell_scene_navigation.cpp
    workspace/editor_shell_reference_drag.cpp
    workspace/editor_shell_scene_transform.cpp
    workspace/editor_shell_scene_export.cpp
    workspace/editor_scene_export.cpp
    workspace/editor_scene_publication_preparation.cpp
    workspace/editor_project_panel.cpp
    workspace/editor_modal_session.cpp
    workspace/editor_scene_panel.cpp
    workspace/editor_scene_panel_presentation.cpp
    workspace/editor_scene_transform_panel.cpp
    workspace/editor_script_file_panel.cpp
    workspace/editor_script_input.cpp
    workspace/editor_scene_keyboard_input.cpp
    workspace/editor_script_pointer_input.cpp
    workspace/editor_exit_panel.cpp
    workspace/project_browser_model.cpp
    workspace/project_browser.cpp
    workspace/editor_project_resources.cpp
    workspace/editor_terrain_preview.cpp
    workspace/editor_terrain_browser.cpp
    workspace/editor_shell_terrain.cpp
    workspace/editor_terrain_build.cpp
    workspace/editor_scene_component_registry.cpp
    workspace/editor_scene_document.cpp
    workspace/editor_scene_inspector.cpp
    workspace/editor_scene_inspector_selection.cpp
    workspace/editor_scene_reference_focus.cpp
    workspace/editor_scene_reference_assignment.cpp
    workspace/editor_scene_view_state.cpp
    workspace/editor_shell_view_state.cpp
    workspace/editor_shell_resource_navigation.cpp
    workspace/editor_shell_inspector_selection.cpp
    workspace/editor_scene_authoring_codec.cpp
    workspace/editor_scene_persistence.cpp
    workspace/editor_recovery_codec.cpp
    workspace/editor_recovery_region.cpp
    workspace/editor_recovery_session.cpp
    workspace/editor_recovery_preparation.cpp
    workspace/editor_recovery_admission.cpp
    viewport/editor_scene_viewport_contract.cpp
    viewport/editor_scene_viewport_frame_builder.cpp
    viewport/editor_scene_viewport_physics_overlay.cpp
    viewport/editor_scene_component_gizmos.cpp
    viewport/editor_scene_viewport_camera.cpp
    viewport/editor_scene_selection_focus.cpp
    viewport/editor_scene_transform_frame.cpp
    viewport/editor_scene_transform_batch.cpp
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
    arp_editor_project arp_editor_script_authoring arp_ui_runtime arp_resource arp_network arp_3d arp_2d arp_physics2d arp_scriptvm_runtime)
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
    target_link_libraries(arp_editor_tests PRIVATE "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
endif()
add_test(NAME arp_editor_tests COMMAND arp_editor_tests)
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_play_launch_tests tests/play_launch_tests.cpp app/game_preview.cpp)
    target_compile_definitions(arp_editor_play_launch_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_play_launch_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_play_launch_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_play_launch_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_play_launch_tests COMMAND arp_editor_play_launch_tests)
    add_test(NAME arp_editor_play_launch_smoke COMMAND arp_editor_play_launch_tests --smoke)
    set_tests_properties(arp_editor_play_launch_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_scene_replacement_preparation_tests tests/scene_replacement_preparation_tests.cpp)
    target_compile_options(arp_editor_scene_replacement_preparation_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_scene_replacement_preparation_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_scene_replacement_preparation_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_scene_replacement_preparation_tests COMMAND arp_editor_scene_replacement_preparation_tests)
    add_test(NAME arp_editor_scene_replacement_preparation_smoke COMMAND arp_editor_scene_replacement_preparation_tests --smoke)
    set_tests_properties(arp_editor_scene_replacement_preparation_tests arp_editor_scene_replacement_preparation_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_transform_batch_tests tests/transform_batch_tests.cpp)
    target_compile_options(arp_editor_transform_batch_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_transform_batch_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_transform_batch_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_transform_batch_tests COMMAND arp_editor_transform_batch_tests)
    add_test(NAME arp_editor_transform_batch_smoke COMMAND arp_editor_transform_batch_tests --smoke)
    set_tests_properties(arp_editor_transform_batch_tests arp_editor_transform_batch_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_transform_transaction_tests tests/transform_transaction_tests.cpp)
    target_compile_definitions(arp_editor_transform_transaction_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_transform_transaction_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_transform_transaction_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_transform_transaction_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_transform_transaction_tests COMMAND arp_editor_transform_transaction_tests)
    add_test(NAME arp_editor_transform_transaction_smoke COMMAND arp_editor_transform_transaction_tests --smoke)
    set_tests_properties(arp_editor_transform_transaction_tests arp_editor_transform_transaction_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_transform_selection_gesture_tests tests/transform_selection_gesture_tests.cpp)
    target_compile_definitions(arp_editor_transform_selection_gesture_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_transform_selection_gesture_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_transform_selection_gesture_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_transform_selection_gesture_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_transform_selection_gesture_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_transform_selection_gesture_tests COMMAND arp_editor_transform_selection_gesture_tests)
    add_test(NAME arp_editor_transform_selection_gesture_smoke COMMAND arp_editor_transform_selection_gesture_tests --smoke)
    set_tests_properties(arp_editor_transform_selection_gesture_tests arp_editor_transform_selection_gesture_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_transform_settings_tests tests/transform_settings_tests.cpp)
    target_include_directories(arp_editor_transform_settings_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}/3rd/assimp-6.0.5/contrib/rapidjson/include")
    target_compile_definitions(arp_editor_transform_settings_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_transform_settings_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_transform_settings_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_transform_settings_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_transform_settings_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_transform_settings_tests COMMAND arp_editor_transform_settings_tests)
    add_test(NAME arp_editor_transform_settings_smoke COMMAND arp_editor_transform_settings_tests --smoke)
    set_tests_properties(arp_editor_transform_settings_tests arp_editor_transform_settings_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_transform_settings_panel_tests tests/transform_settings_panel_tests.cpp)
    target_include_directories(arp_editor_transform_settings_panel_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}/3rd/assimp-6.0.5/contrib/rapidjson/include")
    target_compile_definitions(arp_editor_transform_settings_panel_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_transform_settings_panel_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_transform_settings_panel_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_transform_settings_panel_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_transform_settings_panel_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_transform_settings_panel_tests COMMAND arp_editor_transform_settings_panel_tests)
    add_test(NAME arp_editor_transform_settings_panel_smoke COMMAND arp_editor_transform_settings_panel_tests --smoke)
    set_tests_properties(arp_editor_transform_settings_panel_tests arp_editor_transform_settings_panel_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_inspector_selection_ui_tests
        tests/inspector_selection_ui_tests.cpp validation/editor_inspector_selection_smoke.cpp)
    target_include_directories(arp_editor_inspector_selection_ui_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}/3rd/assimp-6.0.5/contrib/rapidjson/include")
    target_compile_definitions(arp_editor_inspector_selection_ui_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_inspector_selection_ui_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_inspector_selection_ui_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_inspector_selection_ui_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_inspector_selection_ui_tests COMMAND arp_editor_inspector_selection_ui_tests)
    add_test(NAME arp_editor_inspector_selection_ui_smoke COMMAND arp_editor_inspector_selection_ui_tests --smoke)
    set_tests_properties(arp_editor_inspector_selection_ui_tests arp_editor_inspector_selection_ui_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_reference_focus_tests tests/reference_focus_tests.cpp
        validation/editor_reference_focus_smoke.cpp)
    target_compile_definitions(arp_editor_reference_focus_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_reference_focus_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_reference_focus_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_reference_focus_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_reference_focus_tests COMMAND arp_editor_reference_focus_tests)
    add_test(NAME arp_editor_reference_focus_smoke COMMAND arp_editor_reference_focus_tests --smoke)
    set_tests_properties(arp_editor_reference_focus_tests arp_editor_reference_focus_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_reference_assignment_tests tests/reference_assignment_tests.cpp)
    target_compile_definitions(arp_editor_reference_assignment_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_reference_assignment_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_reference_assignment_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_reference_assignment_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_reference_assignment_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_reference_assignment_tests COMMAND arp_editor_reference_assignment_tests)
    add_test(NAME arp_editor_reference_assignment_smoke COMMAND arp_editor_reference_assignment_tests --smoke)
    add_test(NAME arp_editor_material_reload_tests COMMAND arp_editor_reference_assignment_tests --probe-material-reload)
    set_tests_properties(arp_editor_material_reload_tests PROPERTIES TIMEOUT 60)
    set_tests_properties(arp_editor_reference_assignment_tests arp_editor_reference_assignment_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_reference_assignment_command_tests tests/reference_assignment_command_tests.cpp)
    target_compile_definitions(arp_editor_reference_assignment_command_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_reference_assignment_command_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_reference_assignment_command_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_reference_assignment_command_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_reference_assignment_command_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_reference_assignment_command_tests COMMAND arp_editor_reference_assignment_command_tests)
    add_test(NAME arp_editor_reference_assignment_command_smoke COMMAND arp_editor_reference_assignment_command_tests --smoke)
    set_tests_properties(arp_editor_reference_assignment_command_tests arp_editor_reference_assignment_command_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_resource_navigation_tests tests/resource_navigation_tests.cpp validation/editor_resource_navigation_smoke.cpp)
    target_compile_definitions(arp_editor_resource_navigation_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_resource_navigation_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_resource_navigation_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_resource_navigation_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_resource_navigation_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_resource_navigation_tests COMMAND arp_editor_resource_navigation_tests)
    add_test(NAME arp_editor_resource_navigation_smoke COMMAND arp_editor_resource_navigation_tests --smoke)
    set_tests_properties(arp_editor_resource_navigation_tests arp_editor_resource_navigation_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_inspector_selection_tests tests/inspector_selection_tests.cpp)
    target_compile_definitions(arp_editor_inspector_selection_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_inspector_selection_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_inspector_selection_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_inspector_selection_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_inspector_selection_tests COMMAND arp_editor_inspector_selection_tests)
    add_test(NAME arp_editor_inspector_selection_smoke COMMAND arp_editor_inspector_selection_tests --smoke)
    set_tests_properties(arp_editor_inspector_selection_tests arp_editor_inspector_selection_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_inspector_picker_tests
        tests/inspector_picker_tests.cpp validation/editor_inspector_picker_smoke.cpp
        validation/editor_inspector_resource_reference_smoke.cpp validation/editor_inspector_reference_smoke.cpp)
    target_compile_definitions(arp_editor_inspector_picker_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_inspector_picker_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_inspector_picker_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_inspector_picker_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_inspector_picker_tests COMMAND arp_editor_inspector_picker_tests)
    add_test(NAME arp_editor_inspector_picker_smoke COMMAND arp_editor_inspector_picker_tests --smoke)
    set_tests_properties(arp_editor_inspector_picker_tests arp_editor_inspector_picker_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_inspector_resource_reference_tests
        tests/inspector_resource_reference_tests.cpp validation/editor_inspector_resource_reference_smoke.cpp)
    target_compile_definitions(arp_editor_inspector_resource_reference_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_inspector_resource_reference_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_inspector_resource_reference_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_inspector_resource_reference_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_inspector_resource_reference_tests COMMAND arp_editor_inspector_resource_reference_tests)
    add_test(NAME arp_editor_inspector_resource_reference_smoke COMMAND arp_editor_inspector_resource_reference_tests --smoke)
    set_tests_properties(arp_editor_inspector_resource_reference_tests arp_editor_inspector_resource_reference_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_inspector_reference_tests
        tests/inspector_reference_tests.cpp validation/editor_inspector_reference_smoke.cpp)
    target_compile_definitions(arp_editor_inspector_reference_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_inspector_reference_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_inspector_reference_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_inspector_reference_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_inspector_reference_tests COMMAND arp_editor_inspector_reference_tests)
    add_test(NAME arp_editor_inspector_reference_smoke COMMAND arp_editor_inspector_reference_tests --smoke)
    set_tests_properties(arp_editor_inspector_reference_tests arp_editor_inspector_reference_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_inspector_safety_tests tests/inspector_safety_tests.cpp)
    target_compile_definitions(arp_editor_inspector_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_inspector_safety_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_inspector_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_inspector_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_inspector_safety_tests COMMAND arp_editor_inspector_safety_tests)
    add_test(NAME arp_editor_inspector_safety_smoke COMMAND arp_editor_inspector_safety_tests --smoke)
    set_tests_properties(arp_editor_inspector_safety_tests arp_editor_inspector_safety_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_transform_gesture_tests tests/transform_gesture_tests.cpp)
    target_compile_definitions(arp_editor_transform_gesture_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_transform_gesture_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_transform_gesture_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_transform_gesture_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_transform_gesture_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_transform_gesture_tests COMMAND arp_editor_transform_gesture_tests)
    add_test(NAME arp_editor_transform_gesture_smoke COMMAND arp_editor_transform_gesture_tests --smoke)
    add_test(NAME arp_editor_single_gesture_safety_tests COMMAND arp_editor_transform_gesture_tests --safety)
    add_test(NAME arp_editor_single_gesture_safety_smoke COMMAND arp_editor_transform_gesture_tests --safety-smoke)
    set_tests_properties(arp_editor_single_gesture_safety_tests arp_editor_single_gesture_safety_smoke PROPERTIES TIMEOUT 120)
    set_tests_properties(arp_editor_transform_gesture_tests arp_editor_transform_gesture_smoke PROPERTIES TIMEOUT 120)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_selection_focus_tests tests/selection_focus_tests.cpp)
    target_compile_definitions(arp_editor_selection_focus_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_selection_focus_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_selection_focus_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_selection_focus_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_selection_focus_tests COMMAND arp_editor_selection_focus_tests)
    add_test(NAME arp_editor_selection_focus_smoke COMMAND arp_editor_selection_focus_tests --smoke)
    set_tests_properties(arp_editor_selection_focus_tests arp_editor_selection_focus_smoke PROPERTIES TIMEOUT 120)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_live_service_session_tests tests/live_service_session_tests.cpp)
    target_compile_definitions(arp_editor_live_service_session_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_live_service_session_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_live_service_session_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_live_service_session_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_live_service_session_tests COMMAND arp_editor_live_service_session_tests)
    add_test(NAME arp_editor_live_service_session_smoke COMMAND arp_editor_live_service_session_tests --smoke)
    set_tests_properties(arp_editor_live_service_session_tests arp_editor_live_service_session_smoke PROPERTIES TIMEOUT 120)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_scene_service_binding_tests tests/scene_service_binding_tests.cpp)
    target_compile_definitions(arp_editor_scene_service_binding_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_scene_service_binding_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_scene_service_binding_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_scene_service_binding_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_scene_service_binding_tests COMMAND arp_editor_scene_service_binding_tests)
    add_test(NAME arp_editor_scene_service_binding_smoke COMMAND arp_editor_scene_service_binding_tests --smoke)
    set_tests_properties(arp_editor_scene_service_binding_tests arp_editor_scene_service_binding_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_scene_interaction_handoff_tests tests/scene_interaction_handoff_tests.cpp)
    target_compile_definitions(arp_editor_scene_interaction_handoff_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_scene_interaction_handoff_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_scene_interaction_handoff_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_scene_interaction_handoff_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_scene_interaction_handoff_tests COMMAND arp_editor_scene_interaction_handoff_tests)
    add_test(NAME arp_editor_scene_interaction_handoff_smoke COMMAND arp_editor_scene_interaction_handoff_tests --smoke)
    set_tests_properties(arp_editor_scene_interaction_handoff_tests arp_editor_scene_interaction_handoff_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_document_transaction_tests tests/document_transaction_tests.cpp)
    target_compile_definitions(arp_editor_document_transaction_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_document_transaction_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_document_transaction_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_document_transaction_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_document_transaction_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_document_transaction_tests COMMAND arp_editor_document_transaction_tests)
    add_test(NAME arp_editor_document_transaction_smoke COMMAND arp_editor_document_transaction_tests --smoke)
    add_test(NAME arp_editor_object_command_safety_tests COMMAND arp_editor_document_transaction_tests --safety)
    add_test(NAME arp_editor_object_command_safety_smoke COMMAND arp_editor_document_transaction_tests --safety-smoke)
    add_test(NAME arp_editor_scene_identity_history_tests COMMAND arp_editor_document_transaction_tests --identity-history)
    add_test(NAME arp_editor_scene_identity_history_smoke COMMAND arp_editor_document_transaction_tests --identity-smoke)
    add_test(NAME arp_editor_physics_authoring_history_tests COMMAND arp_editor_document_transaction_tests --physics-authoring)
    add_test(NAME arp_editor_physics_authoring_history_smoke COMMAND arp_editor_document_transaction_tests --physics-authoring-smoke)
    add_test(NAME arp_editor_scene_history_budget_tests COMMAND arp_editor_document_transaction_tests --history-budget)
    add_test(NAME arp_editor_scene_history_budget_smoke COMMAND arp_editor_document_transaction_tests --history-budget-smoke)
    set_tests_properties(arp_editor_scene_history_budget_tests arp_editor_scene_history_budget_smoke PROPERTIES TIMEOUT 120)
    set_tests_properties(arp_editor_physics_authoring_history_tests arp_editor_physics_authoring_history_smoke PROPERTIES TIMEOUT 120)
    add_test(NAME arp_editor_reparent_safety_tests COMMAND arp_editor_document_transaction_tests --reparent-safety)
    add_test(NAME arp_editor_reparent_safety_smoke COMMAND arp_editor_document_transaction_tests --reparent-smoke)
    set_tests_properties(arp_editor_object_command_safety_tests arp_editor_object_command_safety_smoke PROPERTIES TIMEOUT 120)
    set_tests_properties(arp_editor_document_transaction_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_script_file_safety_tests tests/script_file_safety_tests.cpp)
    target_compile_definitions(arp_editor_script_file_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_script_file_safety_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_script_file_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_script_file_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_script_file_safety_tests COMMAND arp_editor_script_file_safety_tests)
    add_test(NAME arp_editor_script_file_safety_smoke COMMAND arp_editor_script_file_safety_tests --smoke)
    set_tests_properties(arp_editor_script_file_safety_smoke PROPERTIES TIMEOUT 60)
    add_executable(arp_editor_script_mutation_safety_tests tests/script_mutation_safety_tests.cpp)
    target_compile_definitions(arp_editor_script_mutation_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_script_mutation_safety_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_script_mutation_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_script_mutation_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_script_mutation_safety_tests COMMAND arp_editor_script_mutation_safety_tests)
    add_test(NAME arp_editor_script_mutation_safety_smoke COMMAND arp_editor_script_mutation_safety_tests --smoke)
    set_tests_properties(arp_editor_script_mutation_safety_tests arp_editor_script_mutation_safety_smoke PROPERTIES TIMEOUT 120)
    add_executable(arp_editor_script_command_safety_tests tests/script_command_safety_tests.cpp
        validation/editor_script_command_smoke.cpp)
    target_compile_definitions(arp_editor_script_command_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_script_command_safety_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_script_command_safety_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_script_command_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_script_command_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_script_command_safety_tests COMMAND arp_editor_script_command_safety_tests)
    add_test(NAME arp_editor_script_command_safety_smoke COMMAND arp_editor_script_command_safety_tests --smoke)
    set_tests_properties(arp_editor_script_command_safety_tests arp_editor_script_command_safety_smoke PROPERTIES TIMEOUT 180)
    add_executable(arp_editor_script_input_safety_tests tests/script_input_safety_tests.cpp
        validation/editor_script_input_smoke.cpp)
    target_compile_definitions(arp_editor_script_input_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_script_input_safety_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_script_input_safety_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_script_input_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_script_input_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_script_input_safety_tests COMMAND arp_editor_script_input_safety_tests)
    add_test(NAME arp_editor_script_input_safety_smoke COMMAND arp_editor_script_input_safety_tests --smoke)
    set_tests_properties(arp_editor_script_input_safety_tests arp_editor_script_input_safety_smoke PROPERTIES TIMEOUT 180)
    add_executable(arp_editor_script_modal_safety_tests tests/script_modal_safety_tests.cpp
        validation/editor_script_modal_smoke.cpp)
    target_compile_definitions(arp_editor_script_modal_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_script_modal_safety_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_script_modal_safety_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_script_modal_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_script_modal_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_script_modal_safety_tests COMMAND arp_editor_script_modal_safety_tests)
    add_test(NAME arp_editor_script_modal_safety_smoke COMMAND arp_editor_script_modal_safety_tests --smoke)
    set_tests_properties(arp_editor_script_modal_safety_tests arp_editor_script_modal_safety_smoke PROPERTIES TIMEOUT 180)
    add_executable(arp_editor_modifier_state_tests tests/modifier_state_tests.cpp
        validation/editor_modifier_smoke.cpp)
    target_compile_definitions(arp_editor_modifier_state_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_modifier_state_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_modifier_state_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_modifier_state_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_modifier_state_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_modifier_state_tests COMMAND arp_editor_modifier_state_tests)
    add_test(NAME arp_editor_modifier_state_smoke COMMAND arp_editor_modifier_state_tests --smoke)
    set_tests_properties(arp_editor_modifier_state_tests arp_editor_modifier_state_smoke PROPERTIES TIMEOUT 180)
    add_executable(arp_editor_scene_keyboard_receipt_tests tests/scene_keyboard_receipt_tests.cpp
        validation/editor_scene_keyboard_smoke.cpp)
    target_compile_definitions(arp_editor_scene_keyboard_receipt_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_scene_keyboard_receipt_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_scene_keyboard_receipt_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_scene_keyboard_receipt_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_scene_keyboard_receipt_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_scene_keyboard_receipt_tests COMMAND arp_editor_scene_keyboard_receipt_tests)
    add_test(NAME arp_editor_scene_keyboard_receipt_smoke COMMAND arp_editor_scene_keyboard_receipt_tests --smoke)
    set_tests_properties(arp_editor_scene_keyboard_receipt_tests arp_editor_scene_keyboard_receipt_smoke PROPERTIES TIMEOUT 180)
    add_executable(arp_editor_scene_modal_safety_tests tests/scene_modal_safety_tests.cpp
        validation/editor_scene_modal_smoke.cpp)
    target_compile_definitions(arp_editor_scene_modal_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_scene_modal_safety_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_scene_modal_safety_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_scene_modal_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_scene_modal_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_scene_modal_safety_tests COMMAND arp_editor_scene_modal_safety_tests)
    add_test(NAME arp_editor_scene_modal_safety_smoke COMMAND arp_editor_scene_modal_safety_tests --smoke)
    set_tests_properties(arp_editor_scene_modal_safety_tests arp_editor_scene_modal_safety_smoke PROPERTIES TIMEOUT 180)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_exit_safety_tests tests/exit_safety_tests.cpp)
    target_compile_definitions(arp_editor_exit_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_exit_safety_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_exit_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_exit_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_exit_safety_tests COMMAND arp_editor_exit_safety_tests)
    add_test(NAME arp_editor_exit_safety_smoke COMMAND arp_editor_exit_safety_tests --smoke)
    set_tests_properties(arp_editor_exit_safety_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_script_close_tests tests/script_close_tests.cpp)
    target_compile_definitions(arp_editor_script_close_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_script_close_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_script_close_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_script_close_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_script_close_tests COMMAND arp_editor_script_close_tests)
    add_test(NAME arp_editor_script_close_smoke COMMAND arp_editor_script_close_tests --smoke)
    set_tests_properties(arp_editor_script_close_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_scene_file_safety_tests tests/scene_file_safety_tests.cpp)
    target_compile_definitions(arp_editor_scene_file_safety_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_scene_file_safety_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_scene_file_safety_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_scene_file_safety_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_scene_file_safety_tests COMMAND arp_editor_scene_file_safety_tests)
    add_test(NAME arp_editor_scene_file_safety_smoke COMMAND arp_editor_scene_file_safety_tests --smoke)
    add_test(NAME arp_editor_reflection_metadata_smoke COMMAND arp_editor_scene_file_safety_tests --metadata-smoke)
    set_tests_properties(arp_editor_reflection_metadata_smoke PROPERTIES TIMEOUT 120)
    set_tests_properties(arp_editor_scene_file_safety_smoke PROPERTIES TIMEOUT 60)
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_editor_recovery_codec_tests tests/recovery_codec_tests.cpp)
    target_compile_definitions(arp_editor_recovery_codec_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_recovery_codec_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_recovery_codec_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_recovery_codec_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_recovery_codec_tests COMMAND arp_editor_recovery_codec_tests)
    add_test(NAME arp_editor_recovery_codec_smoke COMMAND arp_editor_recovery_codec_tests --smoke)
    set_tests_properties(arp_editor_recovery_codec_smoke PROPERTIES TIMEOUT 60)
    add_executable(arp_editor_recovery_session_tests tests/recovery_session_tests.cpp)
    target_compile_definitions(arp_editor_recovery_session_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_recovery_session_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_recovery_session_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_recovery_session_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_recovery_session_tests COMMAND arp_editor_recovery_session_tests)
    add_test(NAME arp_editor_recovery_session_smoke COMMAND arp_editor_recovery_session_tests --smoke)
    set_tests_properties(arp_editor_recovery_session_tests arp_editor_recovery_session_smoke PROPERTIES TIMEOUT 60)
    add_executable(arp_editor_recovery_preparation_tests tests/recovery_preparation_tests.cpp)
    target_compile_definitions(arp_editor_recovery_preparation_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_recovery_preparation_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_recovery_preparation_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_recovery_preparation_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_recovery_preparation_tests COMMAND arp_editor_recovery_preparation_tests)
    add_test(NAME arp_editor_recovery_preparation_smoke COMMAND arp_editor_recovery_preparation_tests --smoke)
    set_tests_properties(arp_editor_recovery_preparation_tests arp_editor_recovery_preparation_smoke PROPERTIES TIMEOUT 60)
    add_executable(arp_editor_script_recovery_identity_tests tests/script_recovery_identity_tests.cpp)
    target_compile_definitions(arp_editor_script_recovery_identity_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_script_recovery_identity_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_script_recovery_identity_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_script_recovery_identity_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_script_recovery_identity_tests COMMAND arp_editor_script_recovery_identity_tests)
    add_test(NAME arp_editor_script_recovery_identity_smoke COMMAND arp_editor_script_recovery_identity_tests --smoke)
    set_tests_properties(arp_editor_script_recovery_identity_tests arp_editor_script_recovery_identity_smoke PROPERTIES TIMEOUT 60)
    add_executable(arp_editor_recovery_admission_tests tests/recovery_admission_tests.cpp)
    target_compile_definitions(arp_editor_recovery_admission_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_recovery_admission_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_recovery_admission_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_recovery_admission_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_recovery_admission_tests COMMAND arp_editor_recovery_admission_tests)
    add_test(NAME arp_editor_recovery_admission_smoke COMMAND arp_editor_recovery_admission_tests --smoke)
    set_tests_properties(arp_editor_recovery_admission_tests arp_editor_recovery_admission_smoke PROPERTIES TIMEOUT 60)
endif()
add_executable(arp_editor_scene_command_tests tests/scene_command_tests.cpp)
target_compile_definitions(arp_editor_scene_command_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_scene_command_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_scene_command_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d arp_2d_resources)
else()
    target_link_libraries(arp_editor_scene_command_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
endif()
add_test(NAME arp_editor_scene_command_tests COMMAND arp_editor_scene_command_tests)
add_executable(arp_editor_project_workflow_tests tests/project_workflow_tests.cpp)
target_compile_definitions(arp_editor_project_workflow_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_project_workflow_tests PRIVATE -UNDEBUG)
if(MINGW)
    target_compile_options(arp_editor_project_workflow_tests PRIVATE -Wa,-mbig-obj)
endif()
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_project_workflow_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d arp_2d_resources)
else()
    target_link_libraries(arp_editor_project_workflow_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
endif()
add_test(NAME arp_editor_project_workflow_tests COMMAND arp_editor_project_workflow_tests)
add_executable(arp_editor_scene_export_tests tests/scene_export_tests.cpp)
target_compile_definitions(arp_editor_scene_export_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_scene_export_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_scene_export_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d arp_2d_resources)
else()
    target_link_libraries(arp_editor_scene_export_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
endif()
add_test(NAME arp_editor_scene_export_tests COMMAND arp_editor_scene_export_tests)
add_test(NAME arp_editor_scene_export_smoke COMMAND arp_editor_scene_export_tests --smoke)
set_tests_properties(arp_editor_scene_export_smoke PROPERTIES TIMEOUT 60)
add_executable(arp_editor_scene_publication_preparation_tests tests/scene_publication_preparation_tests.cpp)
target_compile_options(arp_editor_scene_publication_preparation_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_scene_publication_preparation_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d arp_2d_resources)
else()
    target_link_libraries(arp_editor_scene_publication_preparation_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
endif()
add_test(NAME arp_editor_scene_publication_preparation_tests COMMAND arp_editor_scene_publication_preparation_tests)
add_test(NAME arp_editor_scene_publication_preparation_smoke COMMAND arp_editor_scene_publication_preparation_tests --smoke)
set_tests_properties(arp_editor_scene_publication_preparation_smoke PROPERTIES TIMEOUT 60)
add_test(NAME arp_editor_scene_document_smoke
    COMMAND arp_editor_project_workflow_tests --scene-document-smoke)
set_tests_properties(arp_editor_scene_document_smoke PROPERTIES TIMEOUT 60)
add_test(NAME arp_editor_scene_ui_smoke
    COMMAND arp_editor_project_workflow_tests --scene-ui-smoke)
set_tests_properties(arp_editor_scene_ui_smoke PROPERTIES TIMEOUT 60)
add_test(NAME arp_editor_scene_export_ui_contract_tests
    COMMAND arp_editor_project_workflow_tests --export-ui-contract)
add_test(NAME arp_editor_scene_export_ui_smoke
    COMMAND arp_editor_project_workflow_tests --export-ui-smoke)
set_tests_properties(arp_editor_scene_export_ui_contract_tests arp_editor_scene_export_ui_smoke PROPERTIES TIMEOUT 60)
add_test(NAME arp_editor_project_settings_smoke
    COMMAND arp_editor_project_workflow_tests --settings-smoke)
set_tests_properties(arp_editor_project_settings_smoke PROPERTIES TIMEOUT 60)
add_test(NAME arp_editor_scene_identity_contract_tests
    COMMAND arp_editor_project_workflow_tests --identity-contract)
add_test(NAME arp_editor_scene_identity_smoke
    COMMAND arp_editor_project_workflow_tests --identity-smoke)
set_tests_properties(arp_editor_scene_identity_smoke PROPERTIES TIMEOUT 60)

if(NOT EMSCRIPTEN)
    add_executable(arp_editor_scene_migrate tools/scene_migrate.cpp)
    if(APPLE)
        target_link_libraries(arp_editor_scene_migrate PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_scene_migrate PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    if(MINGW)
        target_link_options(arp_editor_scene_migrate PRIVATE -municode)
    endif()
    add_test(NAME arp_editor_scene_migration_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_migration_tests.py"
            "$<TARGET_FILE:arp_editor_scene_migrate>")
    add_test(NAME arp_editor_scene_migration_smoke
        COMMAND "${Python3_EXECUTABLE}" -X utf8
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_migration_tests.py"
            "$<TARGET_FILE:arp_editor_scene_migrate>"
            SceneMigrationTests.test_explicit_identity_and_preserve_source)
endif()
add_executable(arp_editor_scene_viewport_contract_tests tests/scene_viewport_contract_tests.cpp)
target_compile_definitions(arp_editor_scene_viewport_contract_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_include_directories(arp_editor_scene_viewport_contract_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_scene_viewport_contract_tests PRIVATE
        arp_editor_viewport_renderer arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_renderer arp_rhi_semantics arp_rhi arp_2d_resources)
else()
    target_link_libraries(arp_editor_scene_viewport_contract_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_viewport_renderer,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_renderer,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>"
        arp_rhi_semantics arp_rhi)
endif()
target_compile_options(arp_editor_scene_viewport_contract_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_scene_viewport_contract_tests COMMAND arp_editor_scene_viewport_contract_tests)
add_executable(arp_editor_terrain_preview_tests tests/terrain_preview_tests.cpp)
target_compile_definitions(arp_editor_terrain_preview_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_terrain_preview_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_terrain_preview_tests PRIVATE arp_editor_viewport_renderer arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_renderer arp_rhi_semantics arp_rhi arp_2d_resources)
else()
    target_link_libraries(arp_editor_terrain_preview_tests PRIVATE "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_viewport_renderer,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_renderer,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>" arp_rhi_semantics arp_rhi)
endif()
add_test(NAME arp_editor_terrain_preview_tests COMMAND arp_editor_terrain_preview_tests)
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
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
endif()
target_compile_options(arp_editor_command_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_command_tests COMMAND arp_editor_command_tests)
add_executable(arp_editor_command_receipt_tests tests/command_receipt_tests.cpp command/editor_command.cpp)
target_include_directories(arp_editor_command_receipt_tests PRIVATE "${ARP_EDITOR_ENGINE_ROOT}")
target_link_libraries(arp_editor_command_receipt_tests PRIVATE arp_core arp_fkyaml_headers)
target_compile_options(arp_editor_command_receipt_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_command_receipt_tests COMMAND arp_editor_command_receipt_tests)
add_test(NAME arp_editor_command_receipt_smoke COMMAND arp_editor_command_receipt_tests --smoke)
set_tests_properties(arp_editor_command_receipt_tests arp_editor_command_receipt_smoke PROPERTIES TIMEOUT 60)
add_executable(arp_editor_remote_server_tests tests/remote_server_tests.cpp)
target_compile_definitions(arp_editor_remote_server_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
target_compile_options(arp_editor_remote_server_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_remote_server_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
else()
    target_link_libraries(arp_editor_remote_server_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
endif()
add_test(NAME arp_editor_remote_server_tests COMMAND arp_editor_remote_server_tests)
set_tests_properties(arp_editor_remote_server_tests PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
add_executable(arp_editor_support_tests tests/support_tests.cpp)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_support_tests PRIVATE
        arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
else()
    target_link_libraries(arp_editor_support_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
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
        "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
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
    ingress/editor_platform_input_router.cpp
    validation/editor_reference_drag_smoke.cpp
    validation/editor_inspector_resource_reference_smoke.cpp
    validation/editor_inspector_picker_smoke.cpp
    validation/editor_inspector_reference_smoke.cpp
    validation/editor_reference_focus_smoke.cpp
    validation/editor_resource_navigation_smoke.cpp
    validation/editor_transform_gesture_smoke.cpp
    validation/editor_transform_input_smoke.cpp
    validation/editor_transform_settings_smoke.cpp
    validation/editor_inspector_safety_smoke.cpp
    validation/editor_inspector_label_smoke.cpp
    validation/editor_script_command_smoke.cpp
    validation/editor_script_input_smoke.cpp
    validation/editor_script_modal_smoke.cpp
    validation/editor_modifier_smoke.cpp
    validation/editor_scene_keyboard_smoke.cpp
    validation/editor_scene_modal_smoke.cpp
    validation/editor_object_view_smoke.cpp
    validation/editor_inspector_selection_smoke.cpp
    app/editor_application.cpp
    app/game_preview.cpp
    ui/editor_ui_assets.cpp
    "${ARP_EDITOR_ENGINE_ROOT}/src/platform/common/platform_lifecycle_state.cpp")
if(NOT EMSCRIPTEN)
    add_test(NAME arp_editor_play_launch_cli_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/play_launch_cli_tests.py" "$<TARGET_FILE:arp_editor>")
    set_tests_properties(arp_editor_play_launch_cli_tests PROPERTIES TIMEOUT 60)
    if(TARGET arp_editor_cli)
        add_test(NAME arp_editor_transform_pivot_cli_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/transform_pivot_cli_tests.py"
                "$<TARGET_FILE:arp_editor_cli>")
        set_tests_properties(arp_editor_transform_pivot_cli_tests PROPERTIES TIMEOUT 60)
        add_test(NAME arp_editor_transform_settings_cli_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/transform_settings_cli_tests.py"
                "$<TARGET_FILE:arp_editor_cli>")
        set_tests_properties(arp_editor_transform_settings_cli_tests PROPERTIES TIMEOUT 120)
        add_test(NAME arp_editor_selection_focus_cli_smoke
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/selection_focus_cli_tests.py"
                "$<TARGET_FILE:arp_editor>" "$<TARGET_FILE:arp_editor_cli>")
        set_tests_properties(arp_editor_selection_focus_cli_smoke PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    endif()
endif()
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
    foreach(_scenario IN ITEMS before move scale rotate undo redo world_axes local_axes local_move
            local_scale world_scale_rejected local_rotate world_rotate local_undo local_redo
            snap_before snap_move snap_negative snap_scale snap_rotate snap_undo snap_redo batch_move batch_undo batch_redo
            group_move group_rotate group_scale group_individual group_undo group_redo
            settings_open settings_draft settings_invalid settings_failed settings_applied settings_cancel
            input_tool input_text input_modal input_recovery)
        add_test(NAME arp_editor_transform_gesture_${_scenario}_hidden_smoke
            COMMAND arp_editor --smoke --transform-gesture-smoke ${_scenario})
        set_tests_properties(arp_editor_transform_gesture_${_scenario}_hidden_smoke
            PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    endforeach()
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
if(TARGET arp_editor_scene_migrate)
    add_dependencies(arp_editor arp_editor_scene_migrate)
endif()
set_target_properties(arp_editor arp_editor_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")

if(NOT EMSCRIPTEN)
    foreach(scenario view_visible view_hidden view_shown view_locked view_unlocked view_ui_hidden view_ui_shown drag_hover drag_assigned drag_rejected drag_cancelled nav_valid nav_current nav_missing nav_unmapped nav_failed nav_retry focus_valid focus_current focus_missing focus_failed focus_retry normal failed retry ref_unassigned ref_missing ref_type ref_repaired res_unassigned res_missing res_type res_not_ready res_capacity res_repaired res_stale picker_first picker_last picker_repaired picker_invalid picker_objects picker_object_repaired selection_same selection_mixed selection_incompatible selection_ambiguous selection_failed selection_retry selection_single)
        add_test(NAME arp_editor_inspector_${scenario}_hidden_smoke COMMAND arp_editor --smoke --inspector-safety-smoke ${scenario})
        set_tests_properties(arp_editor_inspector_${scenario}_hidden_smoke PROPERTIES TIMEOUT 60)
    endforeach()
endif()

if(NOT EMSCRIPTEN)
    add_executable(arp_editor_reference_drag_tests tests/reference_drag_tests.cpp
        ingress/editor_platform_input_router.cpp validation/editor_reference_drag_smoke.cpp)
    target_compile_definitions(arp_editor_reference_drag_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_reference_drag_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_reference_drag_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_reference_drag_tests PRIVATE
            arp_editor_shell arp_platform arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_reference_drag_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>" arp_platform)
    endif()
    add_test(NAME arp_editor_reference_drag_tests COMMAND arp_editor_reference_drag_tests)
    add_test(NAME arp_editor_reference_drag_smoke COMMAND arp_editor_reference_drag_tests --smoke)
    set_tests_properties(arp_editor_reference_drag_tests arp_editor_reference_drag_smoke PROPERTIES TIMEOUT 120)
endif()

if(NOT EMSCRIPTEN)
    add_executable(arp_editor_object_view_state_tests tests/object_view_state_tests.cpp)
    target_compile_options(arp_editor_object_view_state_tests PRIVATE -UNDEBUG)
    if(APPLE)
        target_link_libraries(arp_editor_object_view_state_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_object_view_state_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_object_view_state_tests COMMAND arp_editor_object_view_state_tests)
    add_test(NAME arp_editor_object_view_state_smoke COMMAND arp_editor_object_view_state_tests --smoke)
    set_tests_properties(arp_editor_object_view_state_tests arp_editor_object_view_state_smoke PROPERTIES TIMEOUT 120)
    add_executable(arp_editor_object_view_command_tests tests/object_view_command_tests.cpp)
    target_compile_definitions(arp_editor_object_view_command_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_object_view_command_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_object_view_command_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_object_view_command_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_object_view_command_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_object_view_command_tests COMMAND arp_editor_object_view_command_tests)
    add_test(NAME arp_editor_object_view_command_smoke COMMAND arp_editor_object_view_command_tests --smoke)
    set_tests_properties(arp_editor_object_view_command_tests arp_editor_object_view_command_smoke PROPERTIES TIMEOUT 120)
    add_executable(arp_editor_object_view_ui_tests tests/object_view_ui_tests.cpp ingress/editor_platform_input_router.cpp)
    target_compile_definitions(arp_editor_object_view_ui_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_object_view_ui_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_object_view_ui_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_object_view_ui_tests PRIVATE
            arp_editor_shell arp_platform arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_object_view_ui_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>" arp_platform)
    endif()
    add_test(NAME arp_editor_object_view_ui_tests COMMAND arp_editor_object_view_ui_tests)
    add_test(NAME arp_editor_object_view_ui_smoke COMMAND arp_editor_object_view_ui_tests --smoke)
    set_tests_properties(arp_editor_object_view_ui_tests arp_editor_object_view_ui_smoke PROPERTIES TIMEOUT 120)
    add_executable(arp_editor_inspector_label_layout_tests
        tests/inspector_label_layout_tests.cpp validation/editor_inspector_label_smoke.cpp)
    target_compile_definitions(arp_editor_inspector_label_layout_tests PRIVATE ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
    target_compile_options(arp_editor_inspector_label_layout_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_compile_options(arp_editor_inspector_label_layout_tests PRIVATE -Wa,-mbig-obj)
    endif()
    if(APPLE)
        target_link_libraries(arp_editor_inspector_label_layout_tests PRIVATE
            arp_editor_shell arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d arp_2d_resources)
    else()
        target_link_libraries(arp_editor_inspector_label_layout_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_editor_project,arp_editor_shell,arp_editor_script_authoring,arp_script_components,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d,arp_2d_resources,arp_scene3d_assets,arp_scene_assets>")
    endif()
    add_test(NAME arp_editor_inspector_label_layout_tests COMMAND arp_editor_inspector_label_layout_tests)
    add_test(NAME arp_editor_inspector_label_layout_smoke COMMAND arp_editor_inspector_label_layout_tests --smoke)
    set_tests_properties(arp_editor_inspector_label_layout_tests arp_editor_inspector_label_layout_smoke PROPERTIES TIMEOUT 120)
    foreach(scenario label_nested label_locked script_failed script_retry script_closed
        script_input_edited script_input_selected script_input_saved
        script_modal_open script_modal_failed script_modal_retry script_modal_closed
        modifier_control modifier_shift modifier_released
        scene_keys_tool scene_keys_text scene_keys_save
        scene_modal_open scene_modal_failed scene_modal_retry scene_modal_saved scene_modal_confirm)
        add_test(NAME arp_editor_inspector_${scenario}_hidden_smoke COMMAND arp_editor --smoke --inspector-safety-smoke ${scenario})
        set_tests_properties(arp_editor_inspector_${scenario}_hidden_smoke PROPERTIES TIMEOUT 60)
    endforeach()
endif()
