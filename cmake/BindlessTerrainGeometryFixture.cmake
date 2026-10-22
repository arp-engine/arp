include_guard(GLOBAL)
if(NOT TARGET arp_bindless_terrain_record_fixture)
    return()
endif()
set(bindless_terrain_geometry_dir "${CMAKE_BINARY_DIR}/bindless-terrain-geometry")
set(bindless_terrain_geometry_libraries
    shaders/core/checked_arithmetic.slang
    shaders/core/instance_transform.slang shaders/core/foliage_geometry.slang
    shaders/core/skinning_geometry.slang shaders/core/pbr_geometry.slang
    shaders/renderer/bindless_pbr_geometry.slang shaders/core/terrain_surface.slang
    shaders/renderer/bindless_terrain_material.slang shaders/renderer/bindless_terrain_geometry.slang)
set(bindless_terrain_geometry_arguments)
foreach(library IN LISTS bindless_terrain_geometry_libraries)
    list(APPEND bindless_terrain_geometry_arguments --bindless-source-library "${CMAKE_SOURCE_DIR}/${library}")
endforeach()
foreach(entry terrainColorVertex terrainDepthVertex terrainShadowVertex terrainColorMain terrainDepthMain terrainShadowMain)
    list(APPEND bindless_terrain_geometry_arguments --bindless-entry "${entry}")
endforeach()
set(bindless_terrain_geometry_outputs)
foreach(mode Native Emulated)
    string(TOLOWER "${mode}" mode_name)
    set(publication "${bindless_terrain_geometry_dir}/TerrainGeometryFamily.${mode_name}.bindless.yaml")
    list(APPEND bindless_terrain_geometry_outputs "${publication}")
    add_custom_command(OUTPUT "${publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_terrain_geometry_family.yaml"
            ${bindless_terrain_geometry_arguments}
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_terrain_geometry_family.slang"
            --bindless-mode "${mode}" --out-dir "${bindless_terrain_geometry_dir}"
        DEPENDS tests/fixtures/bindless_terrain_geometry_family.yaml tests/fixtures/bindless_terrain_geometry_family.slang
            ${bindless_terrain_geometry_libraries}
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_terrain_geometry_fixture DEPENDS ${bindless_terrain_geometry_outputs})
add_dependencies(arp_rhi_bindless_package_tests arp_bindless_terrain_geometry_fixture)
foreach(mode native emulated)
    add_test(NAME arp_bindless_terrain_geometry_${mode}_package_tests COMMAND arp_rhi_bindless_package_tests
        "${bindless_terrain_geometry_dir}/TerrainGeometryFamily.${mode}.bindless.yaml" valid terrain-geometry-family "${mode}")
    add_test(NAME arp_bindless_terrain_geometry_${mode}_scene_roots_tests COMMAND arp_rhi_bindless_package_tests
        "${bindless_terrain_geometry_dir}/TerrainGeometryFamily.${mode}.bindless.yaml" valid terrain-geometry-family "${mode}" scene-roots)
endforeach()
add_executable(arp_bindless_terrain_geometry_data_tests tests/contract/bindless_terrain_geometry_data_tests.cpp)
target_include_directories(arp_bindless_terrain_geometry_data_tests PRIVATE src)
target_link_libraries(arp_bindless_terrain_geometry_data_tests PRIVATE arp_core)
target_compile_options(arp_bindless_terrain_geometry_data_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_terrain_geometry_data_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_terrain_geometry_data_tests>")
    endif()
else()
    add_test(NAME arp_bindless_terrain_geometry_data_tests COMMAND arp_bindless_terrain_geometry_data_tests)
endif()
add_test(NAME arp_bindless_terrain_geometry_codegen_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
    BindlessCodegenTests.test_terrain_geometry_family_stages_and_frame_layout)
