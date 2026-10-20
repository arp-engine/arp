include_guard(GLOBAL)
if(NOT TARGET arp_bindless_instance64_fixture)
    return()
endif()
set(bindless_pbr_material_dir "${CMAKE_BINARY_DIR}/bindless-pbr-material")
set(bindless_pbr_record_dir "${CMAKE_BINARY_DIR}/bindless-pbr-record")
set(bindless_pbr_record_outputs)
foreach(record_mode Native Emulated)
    string(TOLOWER "${record_mode}" record_mode_name)
    set(record_publication "${bindless_pbr_record_dir}/PbrMaterialRecord.${record_mode_name}.bindless.yaml")
    list(APPEND bindless_pbr_record_outputs "${record_publication}")
    add_custom_command(OUTPUT "${record_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_pbr_material_record.yaml"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/leaf_surface.slang"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/pbr_material.slang"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/renderer/bindless_pbr_material.slang"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_pbr_material_record.slang"
            --bindless-entry vertexMain --bindless-entry colorMain --bindless-entry depthMain --bindless-entry shadowMain
            --bindless-mode "${record_mode}" --out-dir "${bindless_pbr_record_dir}"
        DEPENDS tests/fixtures/bindless_pbr_material_record.yaml tests/fixtures/bindless_pbr_material_record.slang
            shaders/core/leaf_surface.slang shaders/core/pbr_material.slang shaders/renderer/bindless_pbr_material.slang
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_pbr_record_fixture DEPENDS ${bindless_pbr_record_outputs})
set(pbr_reference_dir "${CMAKE_BINARY_DIR}/bindless-implicit-reference/ordinary_pbr_material")
set(pbr_reference_publication "${pbr_reference_dir}/ordinary_pbr_material.graphics.module.yaml")
add_custom_command(OUTPUT "${pbr_reference_publication}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
        --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
        --fixture ordinary_pbr_material --graphics-module --preserve-implicit-sampling
        --out-dir "${pbr_reference_dir}"
    DEPENDS tests/fixtures/ordinary_pbr_material.slang shaders/core/leaf_surface.slang
        shaders/core/pbr_material.slang scripts/compile_shaders.py scripts/shader_bindings_codegen.py
        scripts/spirv_layout.py "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
    VERBATIM)
add_custom_command(OUTPUT "${pbr_reference_dir}/inputs.json"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/pbr_implicit_sampling_reference.py"
        --write-inputs "${pbr_reference_dir}/inputs.json"
    DEPENDS tests/contract/pbr_implicit_sampling_reference.py tests/tools/bindless_graphics_readback_oracles.py
    VERBATIM)
add_custom_target(arp_bindless_pbr_implicit_reference_fixture
    DEPENDS "${pbr_reference_publication}" "${pbr_reference_dir}/inputs.json")
add_test(NAME arp_bindless_pbr_implicit_reference_contract_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/pbr_implicit_sampling_reference_test.py"
        --runner "${CMAKE_BINARY_DIR}")
set_tests_properties(arp_bindless_pbr_implicit_reference_contract_tests PROPERTIES TIMEOUT 30)
set(bindless_pbr_material_outputs)
foreach(material_mode Native Emulated)
    string(TOLOWER "${material_mode}" material_mode_name)
    set(material_publication "${bindless_pbr_material_dir}/GraphicsPbrMaterial.${material_mode_name}.bindless.yaml")
    list(APPEND bindless_pbr_material_outputs "${material_publication}")
    add_custom_command(OUTPUT "${material_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_pbr_material.yaml"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/leaf_surface.slang"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/pbr_material.slang"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_pbr_material.slang"
            --bindless-entry vertexMain --bindless-entry cleanFragment --bindless-entry faultFragment
            --bindless-mode "${material_mode}" --out-dir "${bindless_pbr_material_dir}"
        DEPENDS tests/fixtures/bindless_graphics_pbr_material.yaml tests/fixtures/bindless_graphics_pbr_material.slang
            shaders/core/leaf_surface.slang shaders/core/pbr_material.slang
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_pbr_material_fixture DEPENDS ${bindless_pbr_material_outputs})
add_dependencies(arp_bindless_pbr_material_fixture arp_bindless_pbr_implicit_reference_fixture)
if(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke)
    add_dependencies(arp_rhi_webgpu_original_bindless_root_browser_smoke arp_bindless_pbr_material_fixture)
    target_link_options(arp_rhi_webgpu_original_bindless_root_browser_smoke PRIVATE
        "--preload-file=${bindless_pbr_material_dir}@/bindless-pbr-material")
    set_property(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke APPEND PROPERTY LINK_DEPENDS
        ${bindless_pbr_material_outputs})
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND Python3_Interpreter_FOUND)
        add_test(NAME arp_rhi_webgpu_original_bindless_pbr_material_browser_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_rhi_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
                --runner "$<TARGET_FILE_DIR:arp_rhi_webgpu_original_bindless_root_browser_smoke>"
                --timeout-seconds 480 --original-bindless-root-preparation
                --original-root-matrix-execution-only --original-root-pbr-material --wasm-baseline-only
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-original-root-pbr-material")
        set_tests_properties(arp_rhi_webgpu_original_bindless_pbr_material_browser_smoke
            PROPERTIES TIMEOUT 540 RUN_SERIAL TRUE)
    endif()
endif()
if(TARGET arp_rhi_vulkan AND NOT APPLE AND NOT EMSCRIPTEN)
    add_executable(arp_rhi_vulkan_bindless_pbr_material_tests tests/integration/rhi_vulkan_bindless_graphics_matrix_tests.cpp)
    target_include_directories(arp_rhi_vulkan_bindless_pbr_material_tests PRIVATE . src)
    target_link_libraries(arp_rhi_vulkan_bindless_pbr_material_tests PRIVATE arp_rhi_vulkan arp_renderer)
    target_compile_options(arp_rhi_vulkan_bindless_pbr_material_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_vulkan_bindless_pbr_material_tests PRIVATE ARP_BINDLESS_PBR_MATERIAL_FIXTURE=1
        ARP_BINDLESS_DIRECT_REFERENCE_PREFIX="${pbr_reference_dir}/ordinary_pbr_material")
    add_dependencies(arp_rhi_vulkan_bindless_pbr_material_tests arp_bindless_pbr_material_fixture)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_rhi_vulkan_bindless_pbr_material_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_rhi_vulkan_bindless_pbr_material_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_rhi_vulkan_bindless_pbr_material_tests COMMAND arp_rhi_vulkan_bindless_pbr_material_tests
        "${bindless_pbr_material_dir}/GraphicsPbrMaterial.native.bindless.yaml")
    set_tests_properties(arp_rhi_vulkan_bindless_pbr_material_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 RUN_SERIAL TRUE)
endif()
add_executable(arp_bindless_pbr_material_data_tests tests/contract/bindless_pbr_material_data_tests.cpp)
add_dependencies(arp_bindless_pbr_material_data_tests arp_bindless_pbr_record_fixture)
target_include_directories(arp_bindless_pbr_material_data_tests PRIVATE . src)
target_link_libraries(arp_bindless_pbr_material_data_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_pbr_material_data_tests PRIVATE -UNDEBUG)
add_executable(arp_bindless_pbr_material_memory_tests tests/contract/bindless_instance64_memory_tests.cpp)
target_compile_definitions(arp_bindless_pbr_material_memory_tests PRIVATE ARP_BINDLESS_PBR_MATERIAL_FIXTURE=1)
target_link_libraries(arp_bindless_pbr_material_memory_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_pbr_material_memory_tests PRIVATE -UNDEBUG)
add_dependencies(arp_bindless_pbr_material_memory_tests arp_bindless_pbr_material_fixture)
if(EMSCRIPTEN)
    target_link_options(arp_bindless_pbr_material_memory_tests PRIVATE
        "--embed-file=${bindless_pbr_material_dir}@/bindless-pbr-material")
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_pbr_material_data_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_pbr_material_data_tests>")
        add_test(NAME arp_bindless_pbr_material_memory_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_pbr_material_memory_tests>"
            /bindless-pbr-material/GraphicsPbrMaterial.emulated.bindless.yaml
            /bindless-pbr-material/GraphicsPbrMaterial.native.bindless.yaml)
    endif()
else()
    add_test(NAME arp_bindless_pbr_material_data_tests COMMAND arp_bindless_pbr_material_data_tests)
    add_test(NAME arp_bindless_pbr_material_memory_tests COMMAND arp_bindless_pbr_material_memory_tests
        "${bindless_pbr_material_dir}/GraphicsPbrMaterial.emulated.bindless.yaml"
        "${bindless_pbr_material_dir}/GraphicsPbrMaterial.native.bindless.yaml")
endif()
add_test(NAME arp_bindless_pbr_material_codegen_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
    BindlessCodegenTests.test_pbr_material_demand_and_sampling_refusals)
add_test(NAME arp_bindless_pbr_record_codegen_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
    BindlessCodegenTests.test_pbr_production_record_layout_and_depth_entry_stages)
if(APPLE AND TARGET arp_rhi_metal)
    add_executable(arp_rhi_metal_bindless_pbr_material_tests tests/integration/rhi_metal_bindless_graphics_matrix_tests.cpp)
    target_sources(arp_rhi_metal_bindless_pbr_material_tests PRIVATE tests/fixtures/metal_sampled_material_readback_fixture.mm)
    target_include_directories(arp_rhi_metal_bindless_pbr_material_tests PRIVATE . src)
    target_link_libraries(arp_rhi_metal_bindless_pbr_material_tests PRIVATE arp_rhi_metal arp_renderer)
    target_compile_options(arp_rhi_metal_bindless_pbr_material_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_metal_bindless_pbr_material_tests PRIVATE ARP_BINDLESS_PBR_MATERIAL_FIXTURE=1
        ARP_BINDLESS_DIRECT_REFERENCE_PREFIX="${pbr_reference_dir}/ordinary_pbr_material")
    add_dependencies(arp_rhi_metal_bindless_pbr_material_tests arp_bindless_pbr_material_fixture)
    add_test(NAME arp_rhi_metal_bindless_pbr_material_tests COMMAND arp_rhi_metal_bindless_pbr_material_tests
        "${bindless_pbr_material_dir}/GraphicsPbrMaterial.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_bindless_pbr_material_tests PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()
