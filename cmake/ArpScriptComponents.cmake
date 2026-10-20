# Scene/ArpScript component integration; independent of game logic.
add_library(arp_script_components STATIC src/scene/script_component_module.cpp src/scene/script_component_codec.cpp)
target_include_directories(arp_script_components PUBLIC src)
target_link_libraries(arp_script_components PUBLIC arp_scene_assets arp_scriptvm_interpreter arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_runtime arp_scriptvm_aot_tooling)
add_library(arp_scene_script_reflection STATIC $<TARGET_OBJECTS:arp_script_plugin_scene_static>)
target_link_libraries(arp_scene_script_reflection PUBLIC arp_2d arp_scriptvm_runtime)
add_executable(arp_script_component_codegen tests/tooling/script_component_codegen.cpp)
target_include_directories(arp_script_component_codegen PRIVATE .)
target_link_libraries(arp_script_component_codegen PRIVATE arp_script_components arp_scene_script_reflection)
arp_add_mingw_static_runtime(arp_script_component_codegen)
set(component_aot_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/component-aot")
add_custom_command(OUTPUT "${component_aot_dir}/components.gen.cpp" "${component_aot_dir}/script_aot_v5.gen.h"
    COMMAND $<TARGET_FILE:arp_script_component_codegen> "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/script_components.arp" "${component_aot_dir}"
    DEPENDS arp_script_component_codegen tests/fixtures/script_components.arp
    VERBATIM)
add_library(arp_test_script_components STATIC "${component_aot_dir}/components.gen.cpp")
target_include_directories(arp_test_script_components PRIVATE src "${component_aot_dir}")
target_link_libraries(arp_test_script_components PUBLIC arp_scriptvm_aot_tooling)
add_executable(arp_script_component_tests tests/contract/script_component_tests.cpp)
target_include_directories(arp_script_component_tests PRIVATE .)
target_compile_definitions(arp_script_component_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_script_component_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_script_component_tests PRIVATE arp_script_components arp_scene_script_reflection arp_test_script_components)
arp_add_mingw_static_runtime(arp_script_component_tests)
add_test(NAME arp_script_component_tests COMMAND arp_script_component_tests)
add_test(NAME arp_script_component_aot_tests COMMAND arp_script_component_tests --aot)
add_test(NAME arp_script_component_null_smoke COMMAND arp_script_component_tests --smoke)

arp_add_script_plugin(
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/modules/arp.scene2d_module.arpplugin"
    TARGET arp_script_plugin_scene2d
    LINK_LIBRARIES arp_2d_resources arp_2d arp_scene_assets arp_audio arp_scriptvm_runtime)
add_library(arp_scene2d_script_reflection STATIC $<TARGET_OBJECTS:arp_script_plugin_scene2d_static>)
target_link_libraries(arp_scene2d_script_reflection PUBLIC arp_2d_resources arp_2d arp_scene_assets arp_audio arp_scriptvm_runtime)
target_link_libraries(arp_physics2d_tests PRIVATE arp_scene2d_script_reflection)

target_link_libraries(arp_script_component_codegen PRIVATE arp_scene2d_script_reflection)
target_link_libraries(arp_script_component_tests PRIVATE arp_scene2d_script_reflection)
set(component_domain_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/component-domains-aot")
add_custom_command(OUTPUT "${component_domain_dir}/components.gen.cpp" "${component_domain_dir}/script_aot_v5.gen.h"
    COMMAND $<TARGET_FILE:arp_script_component_codegen> "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/script_component_domains.arp" "${component_domain_dir}" --domains
    DEPENDS arp_script_component_codegen tests/fixtures/script_component_domains.arp VERBATIM)
add_library(arp_test_component_domains STATIC "${component_domain_dir}/components.gen.cpp")
target_include_directories(arp_test_component_domains PRIVATE src "${component_domain_dir}")
target_link_libraries(arp_test_component_domains PUBLIC arp_scriptvm_aot_tooling)
add_executable(arp_script_component_domain_tests tests/contract/script_component_domain_tests.cpp)
target_include_directories(arp_script_component_domain_tests PRIVATE .)
target_compile_definitions(arp_script_component_domain_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_script_component_domain_tests PRIVATE -UNDEBUG)
target_link_libraries(arp_script_component_domain_tests PRIVATE arp_script_components arp_scene_script_reflection arp_scene2d_script_reflection arp_test_component_domains)
arp_add_mingw_static_runtime(arp_script_component_domain_tests)
add_test(NAME arp_script_component_domain_tests COMMAND arp_script_component_domain_tests)
add_test(NAME arp_script_component_domain_aot_tests COMMAND arp_script_component_domain_tests --aot)
