include_guard(GLOBAL)
if(NOT TARGET arp_bindless_terrain27_fixture)
    return()
endif()
set(bindless_terrain_record_dir "${CMAKE_BINARY_DIR}/bindless-terrain-record")
set(bindless_terrain_record_outputs)
foreach(record_mode Native Emulated)
    string(TOLOWER "${record_mode}" record_mode_name)
    set(record_publication "${bindless_terrain_record_dir}/TerrainMaterialRecord.${record_mode_name}.bindless.yaml")
    list(APPEND bindless_terrain_record_outputs "${record_publication}")
    add_custom_command(OUTPUT "${record_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_terrain_material_record.yaml"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/terrain_surface.slang"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/renderer/bindless_terrain_material.slang"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_terrain_material_record.slang"
            --bindless-entry vertexMain --bindless-entry colorMain --bindless-entry depthMain --bindless-entry shadowMain
            --bindless-mode "${record_mode}" --out-dir "${bindless_terrain_record_dir}"
        DEPENDS tests/fixtures/bindless_terrain_material_record.yaml tests/fixtures/bindless_terrain_material_record.slang
            shaders/core/terrain_surface.slang shaders/renderer/bindless_terrain_material.slang
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_terrain_record_fixture DEPENDS ${bindless_terrain_record_outputs})
add_executable(arp_bindless_terrain_material_record_tests tests/contract/bindless_terrain_material_record_tests.cpp)
target_include_directories(arp_bindless_terrain_material_record_tests PRIVATE . src)
target_link_libraries(arp_bindless_terrain_material_record_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_terrain_material_record_tests PRIVATE -UNDEBUG)
add_dependencies(arp_bindless_terrain_material_record_tests arp_bindless_terrain_record_fixture)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_terrain_material_record_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_terrain_material_record_tests>")
    endif()
else()
    add_test(NAME arp_bindless_terrain_material_record_tests COMMAND arp_bindless_terrain_material_record_tests)
endif()
add_test(NAME arp_bindless_terrain_material_record_codegen_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
    BindlessCodegenTests.test_terrain_production_record_layout_and_resource_demand)
