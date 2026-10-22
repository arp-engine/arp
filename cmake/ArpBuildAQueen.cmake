option(ARP_BUILD_A_QUEEN "Build the Build a Queen example and its tests" ON)
if(NOT ARP_BUILD_A_QUEEN)
    return()
endif()

add_library(arp_build_a_queen_game STATIC examples/build_a_queen/game.cpp)
target_include_directories(arp_build_a_queen_game PUBLIC examples/build_a_queen src)
target_link_libraries(arp_build_a_queen_game PUBLIC arp_core)

add_library(arp_build_a_queen_scene STATIC examples/build_a_queen/scene_renderer.cpp examples/build_a_queen/scene_content.cpp)
target_include_directories(arp_build_a_queen_scene PUBLIC examples/build_a_queen src)
target_link_libraries(arp_build_a_queen_scene PUBLIC arp_build_a_queen_game arp_renderer arp_rhi arp_rhi_semantics arp_assets arp_animation)
add_dependencies(arp_build_a_queen_scene arp_compile_shaders)

add_executable(arp_build_a_queen
    examples/build_a_queen/main.cpp
    examples/build_a_queen/hud_renderer.cpp
    examples/build_a_queen/hud_content.cpp
    src/platform/common/platform_lifecycle_state.cpp
    src/platform/common/renderdoc_capture_scope.cpp)
target_include_directories(arp_build_a_queen PRIVATE . src)
target_link_libraries(arp_build_a_queen PRIVATE
    arp_build_a_queen_scene arp_platform arp_2d_renderer arp_2d_resources
    "$<LINK_GROUP:RESCAN,arp_gui,arp_ui_runtime,arp_resource>"
    arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter
    arp_scriptvm_aot_tooling)
arp_add_mingw_static_runtime(arp_build_a_queen)
add_dependencies(arp_build_a_queen arp_compile_shaders)
add_custom_command(TARGET arp_build_a_queen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets"
        "$<TARGET_FILE_DIR:arp_build_a_queen>/assets"
    VERBATIM)
set_property(TARGET arp_build_a_queen APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/queen_atlas.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/queen_atlas.yaml"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/characters/Casual.glb"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/characters/Formal.glb"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/characters/character_manifest.yaml")
add_test(NAME arp_build_a_queen_null_smoke COMMAND arp_build_a_queen --null-smoke)
set_tests_properties(arp_build_a_queen_null_smoke PROPERTIES TIMEOUT 30)

if(WIN32)
    if(EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_build_a_queen PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_build_a_queen PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_build_a_queen_vulkan_hidden_smoke
        COMMAND arp_build_a_queen --hidden --autoplay --frames 120)
    set_tests_properties(arp_build_a_queen_vulkan_hidden_smoke PROPERTIES TIMEOUT 60)
endif()

add_executable(arp_build_a_queen_game_tests tests/contract/build_a_queen_game_tests.cpp)
target_compile_options(arp_build_a_queen_game_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_build_a_queen_game_tests PRIVATE arp_build_a_queen_game)
arp_add_mingw_static_runtime(arp_build_a_queen_game_tests)
add_test(NAME arp_build_a_queen_game_tests COMMAND arp_build_a_queen_game_tests)

add_executable(arp_queen_hud_tests examples/build_a_queen/tests/hud_tests.cpp
    examples/build_a_queen/hud_content.cpp)
target_link_libraries(arp_queen_hud_tests PRIVATE arp_build_a_queen_game arp_2d_resources
    "$<LINK_GROUP:RESCAN,arp_gui,arp_ui_runtime,arp_resource>"
    arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter
    arp_scriptvm_aot_tooling)
target_compile_options(arp_queen_hud_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_queen_hud_tests)
add_test(NAME arp_queen_hud_tests COMMAND arp_queen_hud_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/web")

add_executable(arp_build_a_queen_asset_tests tests/contract/build_a_queen_asset_tests.cpp)
target_include_directories(arp_build_a_queen_asset_tests PRIVATE src)
target_link_libraries(arp_build_a_queen_asset_tests PRIVATE arp_assets arp_animation arp_reflection)
arp_add_mingw_static_runtime(arp_build_a_queen_asset_tests)
add_test(NAME arp_build_a_queen_asset_tests
    COMMAND arp_build_a_queen_asset_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/characters")

add_executable(arp_build_a_queen_animation_shader_tests
    tests/contract/build_a_queen_animation_shader_tests.cpp)
add_dependencies(arp_build_a_queen_animation_shader_tests arp_compile_shaders)
add_test(NAME arp_build_a_queen_animation_shader_tests
    COMMAND arp_build_a_queen_animation_shader_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/examples/build_a_queen_skinned.slang"
        "${CMAKE_BINARY_DIR}/shaders")

add_executable(arp_build_a_queen_lighting_shader_tests
    tests/contract/build_a_queen_lighting_shader_tests.cpp)
add_dependencies(arp_build_a_queen_lighting_shader_tests arp_compile_shaders)
add_test(NAME arp_build_a_queen_lighting_shader_tests
    COMMAND arp_build_a_queen_lighting_shader_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/examples"
        "${CMAKE_BINARY_DIR}/shaders")
