include_guard(GLOBAL)
if(NOT TARGET arp_bindless_graphics_hdr_gi_fixture)
    return()
endif()

set(graphics_wocc_ibl_inputs_manifest "${CMAKE_SOURCE_DIR}/tests/fixtures/wocc_ibl_inputs/manifest.yaml")
add_executable(arp_bindless_wocc_ibl_inputs_tests tests/contract/bindless_wocc_ibl_inputs_tests.cpp)
target_include_directories(arp_bindless_wocc_ibl_inputs_tests PRIVATE . src 3rd/fkYAML/include)
target_link_libraries(arp_bindless_wocc_ibl_inputs_tests PRIVATE arp_rhi)
if(MSVC)
    target_compile_options(arp_bindless_wocc_ibl_inputs_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_bindless_wocc_ibl_inputs_tests PRIVATE -UNDEBUG)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(arp_bindless_wocc_ibl_inputs_tests PRIVATE
        "--embed-file=${CMAKE_SOURCE_DIR}/tests/fixtures/wocc_ibl_inputs@/wocc-ibl-inputs")
    set(graphics_wocc_ibl_inputs_test_manifest "/wocc-ibl-inputs/manifest.yaml")
else()
    set(graphics_wocc_ibl_inputs_test_manifest "${graphics_wocc_ibl_inputs_manifest}")
endif()
add_test(NAME arp_bindless_wocc_ibl_inputs_tests
    COMMAND arp_bindless_wocc_ibl_inputs_tests "${graphics_wocc_ibl_inputs_test_manifest}")

set(graphics_wocc_ibl_reference_dir "${CMAKE_BINARY_DIR}/bindless-graphics-wocc-ibl-reference")
set(graphics_wocc_ibl_reference_outputs)
foreach(gi_mode Native Emulated)
    string(TOLOWER "${gi_mode}" gi_mode_name)
    set(gi_publication "${graphics_wocc_ibl_reference_dir}/GraphicsWoccIblReference.${gi_mode_name}.bindless.yaml")
    list(APPEND graphics_wocc_ibl_reference_outputs "${gi_publication}")
    add_custom_command(OUTPUT "${gi_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_wocc_ibl_reference.yaml"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/ordinary_wocc_ibl_reference.slang"
            --bindless-entry vertexMain --bindless-entry cleanFragment --bindless-entry faultFragment
            --bindless-mode "${gi_mode}" --out-dir "${graphics_wocc_ibl_reference_dir}"
        DEPENDS tests/fixtures/bindless_graphics_wocc_ibl_reference.yaml tests/fixtures/ordinary_wocc_ibl_reference.slang
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_graphics_wocc_ibl_reference_fixture DEPENDS ${graphics_wocc_ibl_reference_outputs})
set(graphics_wocc_ibl_dir "${CMAKE_BINARY_DIR}/bindless-graphics-wocc-ibl")
set(graphics_wocc_ibl_outputs)
foreach(gi_mode Native Emulated)
    string(TOLOWER "${gi_mode}" gi_mode_name)
    set(gi_publication "${graphics_wocc_ibl_dir}/GraphicsWoccIbl.${gi_mode_name}.bindless.yaml")
    list(APPEND graphics_wocc_ibl_outputs "${gi_publication}")
    add_custom_command(OUTPUT "${gi_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_wocc_ibl.yaml"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_wocc_ibl.slang"
            --bindless-entry vertexMain --bindless-entry cleanFragment --bindless-entry faultFragment
            --bindless-mode "${gi_mode}" --out-dir "${graphics_wocc_ibl_dir}"
        DEPENDS tests/fixtures/bindless_graphics_wocc_ibl.yaml tests/fixtures/bindless_wocc_ibl.slang
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_graphics_wocc_ibl_fixture DEPENDS ${graphics_wocc_ibl_outputs})
if(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke)
    add_dependencies(arp_rhi_webgpu_original_bindless_root_browser_smoke
        arp_bindless_graphics_wocc_ibl_fixture arp_bindless_graphics_wocc_ibl_reference_fixture)
    target_include_directories(arp_rhi_webgpu_original_bindless_root_browser_smoke PRIVATE 3rd/fkYAML/include)
    target_link_options(arp_rhi_webgpu_original_bindless_root_browser_smoke PRIVATE
        "--preload-file=${graphics_wocc_ibl_dir}@/bindless-graphics-wocc-ibl"
        "--preload-file=${graphics_wocc_ibl_reference_dir}@/bindless-graphics-wocc-ibl-reference"
        "--preload-file=${CMAKE_SOURCE_DIR}/tests/fixtures/wocc_ibl_inputs@/wocc-ibl-inputs")
    set_property(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke APPEND PROPERTY LINK_DEPENDS
        ${graphics_wocc_ibl_outputs} ${graphics_wocc_ibl_reference_outputs}
        "${graphics_wocc_ibl_inputs_manifest}"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/wocc_ibl_inputs/horizon.rgba16f"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/wocc_ibl_inputs/zenith.rgba16f"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/wocc_ibl_inputs/brdf.rgba16f")
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND Python3_Interpreter_FOUND)
        add_test(NAME arp_rhi_webgpu_original_bindless_wocc_ibl_browser_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_rhi_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
                --runner "$<TARGET_FILE_DIR:arp_rhi_webgpu_original_bindless_root_browser_smoke>"
                --timeout-seconds 480 --original-bindless-root-preparation
                --original-root-wocc-ibl --wasm-baseline-only
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-original-root-wocc-ibl")
        set_tests_properties(arp_rhi_webgpu_original_bindless_wocc_ibl_browser_smoke
            PROPERTIES TIMEOUT 540 RUN_SERIAL TRUE)
    endif()
endif()
add_test(NAME arp_bindless_graphics_wocc_ibl_codegen_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
        --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
        ${bindless_spirv_val_args}
        BindlessCodegenTests.test_graphics_wocc_ibl_stage_and_target_contract)
add_test(NAME arp_bindless_graphics_wocc_ibl_reference_codegen_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
        --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
        ${bindless_spirv_val_args}
        BindlessCodegenTests.test_graphics_wocc_ibl_direct_reference_contract)
add_test(NAME arp_bindless_wocc_ibl_oracle_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_wocc_ibl_oracle_test.py")
add_test(NAME arp_bindless_wocc_ibl_browser_result_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_wocc_ibl_browser_result_test.py")

if(APPLE AND TARGET arp_rhi_metal)
    add_executable(arp_rhi_metal_bindless_graphics_wocc_ibl_tests
        tests/integration/rhi_metal_bindless_graphics_wocc_ibl_tests.cpp
        tests/fixtures/metal_sampled_material_readback_fixture.mm)
    target_include_directories(arp_rhi_metal_bindless_graphics_wocc_ibl_tests PRIVATE . src)
    target_link_libraries(arp_rhi_metal_bindless_graphics_wocc_ibl_tests PRIVATE arp_rhi_metal)
    target_compile_options(arp_rhi_metal_bindless_graphics_wocc_ibl_tests PRIVATE -UNDEBUG)
    add_dependencies(arp_rhi_metal_bindless_graphics_wocc_ibl_tests
        arp_bindless_graphics_wocc_ibl_fixture arp_bindless_graphics_wocc_ibl_reference_fixture)
    add_test(NAME arp_rhi_metal_bindless_graphics_wocc_ibl_tests
        COMMAND arp_rhi_metal_bindless_graphics_wocc_ibl_tests
            "${graphics_wocc_ibl_dir}/GraphicsWoccIbl.native.bindless.yaml"
            "${graphics_wocc_ibl_reference_dir}/GraphicsWoccIblReference.native.bindless.yaml"
            "${graphics_wocc_ibl_inputs_manifest}"
            "${CMAKE_BINARY_DIR}/bindless-wocc-ibl-inputs")
    set_tests_properties(arp_rhi_metal_bindless_graphics_wocc_ibl_tests PROPERTIES
        TIMEOUT 90 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()
