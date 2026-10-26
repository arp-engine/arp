# Project manifests describe authoring. Runtime targets consume exported assets.
add_library(arp_editor_project STATIC project_manifest.cpp project_metadata.cpp project_scene_authoring.cpp project_identity.cpp project_input_snapshot.cpp project_package_snapshot.cpp project_resource_locations.cpp)
target_include_directories(arp_editor_project PUBLIC
    "${ARP_EDITOR_ENGINE_ROOT}" "${ARP_EDITOR_ENGINE_ROOT}/src")
target_link_libraries(arp_editor_project PUBLIC arp_core arp_resource arp_fkyaml_headers arp_scene3d_assets arp_2d_resources)

add_executable(arp_editor_project_manifest_tests ../tests/project_manifest_tests.cpp)
target_link_libraries(arp_editor_project_manifest_tests PRIVATE arp_editor_project)
target_compile_definitions(arp_editor_project_manifest_tests PRIVATE
    ARP_STATIC_LINK=1 ARP_SOURCE_DIR="${ARP_EDITOR_ENGINE_ROOT}")
if(MSVC)
    target_compile_options(arp_editor_project_manifest_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_editor_project_manifest_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_editor_project_manifest_tests COMMAND arp_editor_project_manifest_tests)
add_test(NAME arp_editor_project_manifest_fixture_smoke
    COMMAND arp_editor_project_manifest_tests --fixture)

if(CMAKE_GENERATOR STREQUAL "Ninja")
    add_test(NAME arp_editor_project_build_boundary_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${ARP_EDITOR_ENGINE_ROOT}/editor/tests/project_build_boundary_tests.py"
            --ninja "${CMAKE_MAKE_PROGRAM}" --build-dir "${CMAKE_BINARY_DIR}"
            --engine-target "$<TARGET_FILE_NAME:arp_engine>" --editor enabled)
endif()

if(NOT EMSCRIPTEN)
    add_executable(arp_editor_project_migrate ../tools/project_migrate.cpp)
    target_link_libraries(arp_editor_project_migrate PRIVATE arp_editor_project)
    target_compile_definitions(arp_editor_project_migrate PRIVATE ARP_STATIC_LINK=1)
    if(MINGW)
        target_link_options(arp_editor_project_migrate PRIVATE -municode)
    endif()
    add_test(NAME arp_editor_project_migration_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8
            "${ARP_EDITOR_ENGINE_ROOT}/editor/tests/project_migration_tests.py"
            "$<TARGET_FILE:arp_editor_project_migrate>")
endif()

add_executable(arp_editor_project_input_snapshot_tests ../tests/project_input_snapshot_tests.cpp)
target_link_libraries(arp_editor_project_input_snapshot_tests PRIVATE arp_editor_project)
if(MSVC)
    target_compile_options(arp_editor_project_input_snapshot_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_editor_project_input_snapshot_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_editor_project_input_snapshot_tests COMMAND arp_editor_project_input_snapshot_tests)
add_test(NAME arp_editor_project_input_snapshot_smoke COMMAND arp_editor_project_input_snapshot_tests --smoke)
set_tests_properties(arp_editor_project_input_snapshot_tests arp_editor_project_input_snapshot_smoke PROPERTIES TIMEOUT 60)

add_executable(arp_editor_resource_locations_tests ../tests/resource_locations_tests.cpp)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_editor_resource_locations_tests PRIVATE arp_editor_project arp_scriptvm_host_plugin_runtime)
else()
    target_link_libraries(arp_editor_resource_locations_tests PRIVATE "$<LINK_GROUP:RESCAN,arp_editor_project,arp_resource,arp_scriptvm_host_plugin_runtime,arp_scene3d_assets,arp_scene_assets,arp_3d,arp_2d_resources>")
endif()
target_compile_options(arp_editor_resource_locations_tests PRIVATE -UNDEBUG)
add_test(NAME arp_editor_resource_locations_tests COMMAND arp_editor_resource_locations_tests)
add_test(NAME arp_editor_resource_locations_smoke COMMAND arp_editor_resource_locations_tests --smoke)
set_tests_properties(arp_editor_resource_locations_tests arp_editor_resource_locations_smoke PROPERTIES TIMEOUT 60)
