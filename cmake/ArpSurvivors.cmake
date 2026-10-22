# 2D runtime acceptance; standalone game does not depend on editor/WOCC.
add_subdirectory(3rd/box2d-3.1.1 ${CMAKE_BINARY_DIR}/_deps/box2d)
add_library(arp_physics2d STATIC src/physics/2d/physics_world_2d.cpp)
target_include_directories(arp_physics2d PUBLIC src)
target_link_libraries(arp_physics2d PUBLIC arp_core arp_reflection PRIVATE box2d)
add_test(NAME arp_physics_dependency_contract_tests
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/physics_dependency_contract_tests.py" "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME arp_scene_migration_tests
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/scene_migration_tests.py")
add_test(NAME arp_scene_serialization_architecture_tests
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/scene_serialization_architecture_tests.py")
add_test(NAME arp_serialization_codegen_tests
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/serialization_codegen_tests.py" "$<TARGET_FILE:arp_reflect_codegen>")
add_executable(arp_physics2d_tests tests/contract/physics2d_tests.cpp)
target_include_directories(arp_physics2d_tests PRIVATE src)
target_compile_options(arp_physics2d_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_physics2d_tests PRIVATE arp_physics2d)
add_test(NAME arp_physics2d_tests COMMAND arp_physics2d_tests)
add_library(arp_2d_resources STATIC src/2d/sprite_resources.cpp src/2d/tile_resources.cpp src/2d/tile_map_runtime.cpp)
target_sources(arp_2d_resources PRIVATE src/ui/ui_bitmap_font_resource.cpp)
target_include_directories(arp_2d_resources PUBLIC src)
target_link_libraries(arp_2d_resources PUBLIC arp_resource arp_assets arp_physics2d)
add_library(arp_2d_renderer STATIC src/renderer/scene_2d_renderer.cpp src/renderer/scene_2d_ordered.cpp)
add_library(arp_scene_assets STATIC src/scene/scene_asset_resource.cpp src/scene/scene_reflection_persistence.cpp)
arp_reflection_generate(
    TARGET arp_scene_assets MODULE scene_authoring
    OUT_JSON "${CMAKE_CURRENT_BINARY_DIR}/generated/reflection/scene_authoring/schema.json"
    OUT_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/reflection/scene_authoring/schema.cpp"
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/math/math.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/transform.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/scene_asset_resource.h")
target_sources(arp_2d_resources PRIVATE src/2d/scene_component_persistence.cpp)
arp_reflection_generate(
    TARGET arp_2d_resources MODULE scene2d_authoring
    OUT_JSON "${CMAKE_CURRENT_BINARY_DIR}/generated/reflection/scene2d_authoring/schema.json"
    OUT_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/reflection/scene2d_authoring/schema.cpp"
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/physics/2d/physics_world_2d.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/2d/bounds.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/2d/components.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/2d/physics_components.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/2d/sprite_animation_component.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/2d/tile_map_resource_component.h")
target_include_directories(arp_scene_assets PUBLIC src)
target_link_libraries(arp_scene_assets PUBLIC arp_scene arp_resource)
target_include_directories(arp_2d_renderer PUBLIC src)
target_link_libraries(arp_2d_renderer PUBLIC arp_2d_resources arp_renderer)
add_executable(arp_scene_2d_resource_tests tests/contract/scene_2d_resource_tests.cpp)
target_include_directories(arp_scene_2d_resource_tests PRIVATE . src)
target_compile_definitions(arp_scene_2d_resource_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_scene_2d_resource_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_scene_2d_resource_tests PRIVATE arp_2d_resources arp_2d)
add_test(NAME arp_scene_2d_resource_tests COMMAND arp_scene_2d_resource_tests)
add_executable(arp_gui_ordered_plan_tests tests/contract/gui_ordered_plan_tests.cpp
    src/renderer/gui/gui_ordered_renderer.cpp)
target_include_directories(arp_gui_ordered_plan_tests PRIVATE src)
target_link_libraries(arp_gui_ordered_plan_tests PRIVATE arp_render_graph arp_rhi arp_rhi_semantics arp_2d_resources)
target_compile_options(arp_gui_ordered_plan_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_gui_ordered_plan_tests)
add_test(NAME arp_gui_ordered_plan_tests COMMAND arp_gui_ordered_plan_tests)
add_executable(arp_ui_bitmap_font_resource_tests tests/contract/ui_bitmap_font_resource_tests.cpp)
target_link_libraries(arp_ui_bitmap_font_resource_tests PRIVATE arp_2d_resources)
target_compile_options(arp_ui_bitmap_font_resource_tests PRIVATE -UNDEBUG)
target_compile_definitions(arp_ui_bitmap_font_resource_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
arp_add_mingw_static_runtime(arp_ui_bitmap_font_resource_tests)
add_test(NAME arp_ui_bitmap_font_resource_tests COMMAND arp_ui_bitmap_font_resource_tests)
add_test(NAME arp_ui_bitmap_font_cook_tests
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/ui_bitmap_font_cook_test.py")
add_executable(arp_basic_forward_contract_tests tests/contract/basic_forward_contract_tests.cpp
    src/renderer/pipeline/basic_forward_contract.cpp
    src/renderer/pipeline/basic_forward_pipeline.cpp src/renderer/pipeline/render_pipeline.cpp)
target_link_libraries(arp_basic_forward_contract_tests PRIVATE arp_resource arp_render_graph arp_rhi arp_rhi_semantics)
target_compile_options(arp_basic_forward_contract_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_basic_forward_contract_tests)
add_test(NAME arp_basic_forward_contract_tests COMMAND arp_basic_forward_contract_tests)

add_executable(arp_cpu_work_measurement_tests tests/perf/performance_telemetry_tests.cpp)
target_link_libraries(arp_cpu_work_measurement_tests PRIVATE arp_core)
target_compile_options(arp_cpu_work_measurement_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_cpu_work_measurement_tests)
add_test(NAME arp_cpu_work_measurement_tests COMMAND arp_cpu_work_measurement_tests)
add_executable(arp_scene_2d_services_tests tests/contract/scene_2d_services_tests.cpp)
target_include_directories(arp_scene_2d_services_tests PRIVATE . src)
target_compile_definitions(arp_scene_2d_services_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_scene_2d_services_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_scene_2d_services_tests PRIVATE arp_2d_resources
    "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime>")
arp_add_mingw_static_runtime(arp_scene_2d_services_tests)
add_test(NAME arp_scene_2d_services_tests COMMAND arp_scene_2d_services_tests)
add_executable(arp_scene_2d_codec_tests tests/contract/scene_2d_codec_tests.cpp)
target_include_directories(arp_scene_2d_codec_tests PRIVATE . src)
target_compile_definitions(arp_scene_2d_codec_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_scene_2d_codec_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_scene_2d_codec_tests PRIVATE arp_2d_resources arp_2d arp_scene_assets
    "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime>")
arp_add_mingw_static_runtime(arp_scene_2d_codec_tests)
add_test(NAME arp_scene_2d_codec_tests COMMAND arp_scene_2d_codec_tests)
add_executable(arp_scene_2d_world_tests tests/contract/scene_2d_world_tests.cpp)
target_include_directories(arp_scene_2d_world_tests PRIVATE . src)
target_compile_definitions(arp_scene_2d_world_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_scene_2d_world_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_scene_2d_world_tests PRIVATE arp_2d_resources arp_2d arp_scene_assets
    "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime>")
add_test(NAME arp_scene_2d_world_tests COMMAND arp_scene_2d_world_tests)
add_executable(arp_scene_2d_runtime_contract_tests tests/contract/scene_2d_runtime_contract_tests.cpp)
target_include_directories(arp_scene_2d_runtime_contract_tests PRIVATE src)
target_link_libraries(arp_scene_2d_runtime_contract_tests PRIVATE arp_core)
target_compile_options(arp_scene_2d_runtime_contract_tests PRIVATE -UNDEBUG)
add_test(NAME arp_scene_2d_runtime_contract_tests COMMAND arp_scene_2d_runtime_contract_tests)

include(cmake/ArpScriptComponents.cmake)
add_executable(arp_script_record_pool_tests tests/contract/script_record_pool_tests.cpp)
target_include_directories(arp_script_record_pool_tests PRIVATE src)
target_link_libraries(arp_script_record_pool_tests PRIVATE arp_core)
target_compile_options(arp_script_record_pool_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_script_record_pool_tests)
add_test(NAME arp_script_record_pool_tests COMMAND arp_script_record_pool_tests)
add_executable(arp_survivors_script_codegen examples/survivors/script_codegen.cpp examples/survivors/content.cpp)
target_include_directories(arp_survivors_script_codegen PRIVATE examples/survivors src)
target_link_libraries(arp_survivors_script_codegen PRIVATE arp_script_components arp_scene_script_reflection arp_scene2d_script_reflection arp_scriptvm_aot_tooling)
arp_add_mingw_static_runtime(arp_survivors_script_codegen)
add_test(NAME arp_survivors_script_cpu_smoke COMMAND arp_survivors_script_codegen "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets" "${CMAKE_BINARY_DIR}/generated/survivors-aot" --check)
set_tests_properties(arp_survivors_script_cpu_smoke PROPERTIES TIMEOUT 30)
set(survivors_aot_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/survivors-aot")
file(GLOB survivors_script_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets/scripts/*.arp")
add_custom_command(OUTPUT "${survivors_aot_dir}/survivors.gen.cpp" "${survivors_aot_dir}/script_aot_v5.gen.h" "${survivors_aot_dir}/survivors.manifest.yaml"
    COMMAND $<TARGET_FILE:arp_survivors_script_codegen> "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets" "${survivors_aot_dir}/staging"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${survivors_aot_dir}/staging/survivors.gen.cpp" "${survivors_aot_dir}/survivors.gen.cpp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${survivors_aot_dir}/staging/script_aot_v5.gen.h" "${survivors_aot_dir}/script_aot_v5.gen.h"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${survivors_aot_dir}/staging/survivors.manifest.yaml" "${survivors_aot_dir}/survivors.manifest.yaml"
    DEPENDS arp_survivors_script_codegen
        examples/survivors/assets/script-package.yaml
        examples/survivors/assets/game.yaml
        ${survivors_script_sources}
    VERBATIM)
add_library(arp_survivors_script STATIC "${survivors_aot_dir}/survivors.gen.cpp")
target_include_directories(arp_survivors_script PRIVATE src "${survivors_aot_dir}")
target_link_libraries(arp_survivors_script PUBLIC arp_scriptvm_aot_tooling)
add_library(arp_survivors_game STATIC examples/survivors/content.cpp examples/survivors/game.cpp)
target_include_directories(arp_survivors_game PUBLIC examples/survivors src)
target_link_libraries(arp_survivors_game PUBLIC arp_script_components arp_scene_script_reflection arp_scene2d_script_reflection arp_survivors_script)
add_executable(arp_survivors examples/survivors/main.cpp)
add_executable(arp_survivors_game_tests tests/contract/survivors_game_tests.cpp)
target_compile_definitions(arp_survivors_game_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_survivors_game_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_survivors_game_tests PRIVATE arp_survivors_game)
arp_add_mingw_static_runtime(arp_survivors_game_tests)
foreach(script_test IN ITEMS survivors_script)
    add_executable(arp_${script_test}_tests tests/contract/${script_test}_tests.cpp)
    target_compile_definitions(arp_${script_test}_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(arp_${script_test}_tests PRIVATE -UNDEBUG)
    target_link_libraries(arp_${script_test}_tests PRIVATE arp_survivors_game)
    arp_add_mingw_static_runtime(arp_${script_test}_tests)
    add_test(NAME arp_${script_test}_tests COMMAND arp_${script_test}_tests)
endforeach()
add_test(NAME arp_survivors_game_tests COMMAND arp_survivors_game_tests)
add_test(NAME arp_survivors_memory_tests COMMAND arp_survivors_game_tests --memory-check)
set_tests_properties(arp_survivors_memory_tests PROPERTIES TIMEOUT 180)
add_test(NAME arp_survivors_full_memory_tests COMMAND arp_survivors_game_tests --memory-full-check)
set_tests_properties(arp_survivors_full_memory_tests PROPERTIES TIMEOUT 1200)
target_include_directories(arp_survivors PRIVATE src)
target_link_libraries(arp_survivors PRIVATE arp_survivors_game arp_rhi arp_rhi_semantics arp_core)
arp_add_mingw_static_runtime(arp_survivors)
add_test(NAME arp_survivors_null_smoke COMMAND arp_survivors --null-smoke --seed 42)
set_tests_properties(arp_survivors_null_smoke PROPERTIES TIMEOUT 30)

if(WIN32)
    target_sources(arp_survivors PRIVATE src/platform/common/platform_lifecycle_state.cpp src/platform/common/renderdoc_capture_scope.cpp)
    target_include_directories(arp_survivors PRIVATE "C:/Program Files/RenderDoc")
    target_compile_definitions(arp_survivors PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    target_link_libraries(arp_survivors PRIVATE arp_2d_renderer arp_platform
        "$<LINK_GROUP:RESCAN,arp_gui,arp_ui_runtime,arp_resource>" arp_scriptvm_aot_tooling)
    add_dependencies(arp_survivors arp_compile_shaders)
    add_test(NAME arp_survivors_vulkan_hidden_smoke COMMAND arp_survivors --script-aot --hidden --autoplay --fixed-step --frames 120 --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-smoke-save")
    add_test(NAME arp_survivors_interpreter_hidden_smoke COMMAND arp_survivors --script-interpreter --hidden --autoplay --fixed-step --frames 120 --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-interpreter-smoke-save")
    add_test(NAME arp_survivors_interpreter_menu_smoke COMMAND arp_survivors --script-interpreter --hidden --fixed-step --frames 2 --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-interpreter-menu-save")
    add_test(NAME arp_survivors_memory_vulkan_smoke COMMAND arp_survivors --normal-restart-check 3 --hidden --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-memory-save")
    set_tests_properties(arp_survivors_memory_vulkan_smoke PROPERTIES TIMEOUT 120)
    set_tests_properties(arp_survivors_interpreter_hidden_smoke arp_survivors_interpreter_menu_smoke PROPERTIES TIMEOUT 60)
    add_test(NAME arp_survivors_resize_smoke COMMAND arp_survivors --hidden --autoplay --fixed-step --resize-check --frames 90 --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-resize-save")
    add_test(NAME arp_survivors_pause_smoke COMMAND arp_survivors --hidden --autoplay --scenario pause --seconds 4 --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-pause-save")
    add_test(NAME arp_survivors_death_smoke COMMAND arp_survivors --hidden --autoplay --fixed-step --scenario death --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-death-save")
    add_test(NAME arp_survivors_evolution_smoke COMMAND arp_survivors --hidden --autoplay --fixed-step --scenario evolution --frames 180 --mute --save-dir "${CMAKE_BINARY_DIR}/survivors-evolution-save")
    set_tests_properties(arp_survivors_evolution_smoke PROPERTIES TIMEOUT 45)
    # DirectNative's retained-value graph checks are expensive in the component
    # prototype. This is a correctness smoke; performance has its own strict gate.
    set_tests_properties(arp_survivors_vulkan_hidden_smoke PROPERTIES TIMEOUT 240)
    set_tests_properties(arp_survivors_death_smoke PROPERTIES TIMEOUT 45)
    add_custom_command(TARGET arp_survivors POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets" "$<TARGET_FILE_DIR:arp_survivors>/assets")
    add_executable(arp_scene_2d_vulkan_hidden_smoke tests/smoke/scene_2d_vulkan_hidden_smoke.cpp src/platform/common/platform_lifecycle_state.cpp)
    target_include_directories(arp_scene_2d_vulkan_hidden_smoke PRIVATE . src "C:/Program Files/RenderDoc")
    target_compile_definitions(arp_scene_2d_vulkan_hidden_smoke PRIVATE
        ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" ARP_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders" ARP_RENDERDOC_APP_AVAILABLE=1)
    target_compile_options(arp_scene_2d_vulkan_hidden_smoke PRIVATE -UNDEBUG)
    target_link_libraries(arp_scene_2d_vulkan_hidden_smoke PRIVATE arp_scene_assets arp_2d_renderer arp_physics2d arp_platform arp_rhi arp_rhi_semantics
        "$<LINK_GROUP:RESCAN,arp_gui,arp_ui_runtime,arp_resource>" arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
    arp_add_mingw_static_runtime(arp_scene_2d_vulkan_hidden_smoke)
    add_dependencies(arp_scene_2d_vulkan_hidden_smoke arp_compile_shaders)
    add_test(NAME arp_scene_2d_vulkan_hidden_smoke COMMAND arp_scene_2d_vulkan_hidden_smoke)
    set_tests_properties(arp_scene_2d_vulkan_hidden_smoke PROPERTIES TIMEOUT 45)
    add_test(NAME arp_scene_2d_animation_vulkan_smoke COMMAND arp_scene_2d_vulkan_hidden_smoke --animation)
    add_test(NAME arp_scene_2d_physics_vulkan_smoke COMMAND arp_scene_2d_vulkan_hidden_smoke --physics)
    add_test(NAME arp_scene_2d_tiles_vulkan_smoke COMMAND arp_scene_2d_vulkan_hidden_smoke --tiles)
    set_tests_properties(arp_scene_2d_animation_vulkan_smoke arp_scene_2d_physics_vulkan_smoke PROPERTIES TIMEOUT 45)
endif()

add_test(NAME arp_survivors_script_extension_test COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_script_extension_test.py"
    --build "${CMAKE_BINARY_DIR}" --output "${CMAKE_BINARY_DIR}/script-extension-proof")
set_tests_properties(arp_survivors_script_extension_test PROPERTIES TIMEOUT 150)
