include_guard(GLOBAL)
if(NOT TARGET arp_bindless_pbr_material_fixture)
    return()
endif()
set(bindless_terrain27_dir "${CMAKE_BINARY_DIR}/bindless-terrain27")
set(terrain_reference_dir "${CMAKE_BINARY_DIR}/bindless-implicit-reference/ordinary_terrain27")
set(terrain_reference_publication "${terrain_reference_dir}/ordinary_terrain27.graphics.module.yaml")
add_custom_command(OUTPUT "${terrain_reference_publication}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
        --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
        --fixture ordinary_terrain27 --graphics-module --preserve-implicit-sampling
        --out-dir "${terrain_reference_dir}"
    DEPENDS tests/fixtures/ordinary_terrain27.slang scripts/compile_shaders.py
        scripts/shader_bindings_codegen.py scripts/spirv_layout.py
        "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
    VERBATIM)
add_custom_command(OUTPUT "${terrain_reference_dir}/inputs.json"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/terrain27_implicit_sampling_reference.py"
        --write-inputs "${terrain_reference_dir}/inputs.json"
    DEPENDS tests/contract/terrain27_implicit_sampling_reference.py tests/tools/bindless_graphics_readback_oracles.py
    VERBATIM)
add_custom_target(arp_bindless_terrain27_implicit_reference_fixture
    DEPENDS "${terrain_reference_publication}" "${terrain_reference_dir}/inputs.json")
if(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke)
    add_dependencies(arp_rhi_webgpu_original_bindless_root_browser_smoke arp_bindless_terrain27_implicit_reference_fixture)
endif()
add_test(NAME arp_bindless_terrain27_implicit_reference_contract_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/terrain27_implicit_sampling_reference_test.py"
        --runner "${CMAKE_BINARY_DIR}")
set_tests_properties(arp_bindless_terrain27_implicit_reference_contract_tests PROPERTIES TIMEOUT 30)
set(bindless_terrain27_outputs)
foreach(terrain_mode Native Emulated)
    string(TOLOWER "${terrain_mode}" terrain_mode_name)
    set(terrain_publication "${bindless_terrain27_dir}/GraphicsTerrain27.${terrain_mode_name}.bindless.yaml")
    list(APPEND bindless_terrain27_outputs "${terrain_publication}")
    add_custom_command(OUTPUT "${terrain_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_terrain27.yaml"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_terrain27.slang"
            --bindless-entry vertexMain --bindless-entry cleanFragment --bindless-entry faultFragment
            --bindless-mode "${terrain_mode}" --out-dir "${bindless_terrain27_dir}"
        DEPENDS tests/fixtures/bindless_graphics_terrain27.yaml tests/fixtures/bindless_graphics_terrain27.slang
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_terrain27_fixture DEPENDS ${bindless_terrain27_outputs})
if(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke)
    add_dependencies(arp_rhi_webgpu_original_bindless_root_browser_smoke arp_bindless_terrain27_fixture)
    target_link_options(arp_rhi_webgpu_original_bindless_root_browser_smoke PRIVATE
        "--preload-file=${bindless_terrain27_dir}@/bindless-terrain27")
    set_property(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke APPEND PROPERTY LINK_DEPENDS
        ${bindless_terrain27_outputs})
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND Python3_Interpreter_FOUND)
        add_test(NAME arp_rhi_webgpu_original_bindless_terrain27_browser_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_rhi_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
                --runner "$<TARGET_FILE_DIR:arp_rhi_webgpu_original_bindless_root_browser_smoke>"
                --timeout-seconds 480 --original-bindless-root-preparation
                --original-root-matrix-execution-only --original-root-terrain27 --wasm-baseline-only
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-original-root-terrain27")
        set_tests_properties(arp_rhi_webgpu_original_bindless_terrain27_browser_smoke
            PROPERTIES TIMEOUT 540 RUN_SERIAL TRUE)
    endif()
endif()
if(TARGET arp_bindless_pipeline_demand_tests)
    add_dependencies(arp_bindless_pipeline_demand_tests arp_bindless_terrain27_fixture)
    set(terrain_device_plan_args --package "${bindless_terrain27_dir}/GraphicsTerrain27.emulated.bindless.yaml"
        --adapter 2 31 27 1 1 1 4)
    if(EMSCRIPTEN)
        target_link_options(arp_bindless_pipeline_demand_tests PRIVATE
            "--embed-file=${bindless_terrain27_dir}@/bindless-terrain27")
        set(terrain_device_plan_args --package /bindless-terrain27/GraphicsTerrain27.emulated.bindless.yaml
            --adapter 2 31 27 1 1 1 4)
        if(ARP_NODE_EXECUTABLE)
            add_test(NAME arp_bindless_terrain27_device_plan_tests COMMAND "${ARP_NODE_EXECUTABLE}"
                "$<TARGET_FILE:arp_bindless_pipeline_demand_tests>" ${terrain_device_plan_args})
        endif()
    else()
        add_test(NAME arp_bindless_terrain27_device_plan_tests COMMAND arp_bindless_pipeline_demand_tests
            ${terrain_device_plan_args})
    endif()
endif()
add_test(NAME arp_bindless_terrain27_oracle_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_terrain27_oracle_test.py")
add_test(NAME arp_bindless_terrain27_codegen_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
    BindlessCodegenTests.test_terrain27_complete_binding_and_gradient_refusals)
add_executable(arp_bindless_terrain27_data_tests tests/contract/bindless_terrain27_data_tests.cpp)
target_link_libraries(arp_bindless_terrain27_data_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_terrain27_data_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_terrain27_data_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_terrain27_data_tests>")
    endif()
else()
    add_test(NAME arp_bindless_terrain27_data_tests COMMAND arp_bindless_terrain27_data_tests)
endif()
if(TARGET arp_rhi_vulkan AND NOT APPLE AND NOT EMSCRIPTEN)
    add_executable(arp_rhi_vulkan_bindless_terrain27_tests tests/integration/rhi_vulkan_bindless_graphics_matrix_tests.cpp)
    target_include_directories(arp_rhi_vulkan_bindless_terrain27_tests PRIVATE . src)
    target_link_libraries(arp_rhi_vulkan_bindless_terrain27_tests PRIVATE arp_rhi_vulkan arp_renderer)
    target_compile_options(arp_rhi_vulkan_bindless_terrain27_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_vulkan_bindless_terrain27_tests PRIVATE ARP_BINDLESS_TERRAIN27_FIXTURE=1
        ARP_BINDLESS_DIRECT_REFERENCE_PREFIX="${terrain_reference_dir}/ordinary_terrain27")
    add_dependencies(arp_rhi_vulkan_bindless_terrain27_tests arp_bindless_terrain27_fixture arp_bindless_terrain27_implicit_reference_fixture)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_rhi_vulkan_bindless_terrain27_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_rhi_vulkan_bindless_terrain27_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_rhi_vulkan_bindless_terrain27_tests COMMAND arp_rhi_vulkan_bindless_terrain27_tests
        "${bindless_terrain27_dir}/GraphicsTerrain27.native.bindless.yaml")
    set_tests_properties(arp_rhi_vulkan_bindless_terrain27_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 RUN_SERIAL TRUE)
endif()

if(APPLE AND TARGET arp_rhi_metal)
    add_executable(arp_rhi_metal_bindless_terrain27_tests tests/integration/rhi_metal_bindless_graphics_matrix_tests.cpp)
    target_sources(arp_rhi_metal_bindless_terrain27_tests PRIVATE tests/fixtures/metal_sampled_material_readback_fixture.mm)
    target_include_directories(arp_rhi_metal_bindless_terrain27_tests PRIVATE . src)
    target_link_libraries(arp_rhi_metal_bindless_terrain27_tests PRIVATE arp_rhi_metal arp_renderer)
    target_compile_options(arp_rhi_metal_bindless_terrain27_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_metal_bindless_terrain27_tests PRIVATE ARP_BINDLESS_TERRAIN27_FIXTURE=1
        ARP_BINDLESS_DIRECT_REFERENCE_PREFIX="${terrain_reference_dir}/ordinary_terrain27")
    add_dependencies(arp_rhi_metal_bindless_terrain27_tests arp_bindless_terrain27_fixture arp_bindless_terrain27_implicit_reference_fixture)
    add_test(NAME arp_rhi_metal_bindless_terrain27_tests COMMAND arp_rhi_metal_bindless_terrain27_tests
        "${bindless_terrain27_dir}/GraphicsTerrain27.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_bindless_terrain27_tests PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()

add_executable(arp_bindless_terrain27_memory_tests tests/contract/bindless_instance64_memory_tests.cpp)
target_compile_definitions(arp_bindless_terrain27_memory_tests PRIVATE ARP_BINDLESS_TERRAIN27_FIXTURE=1)
target_link_libraries(arp_bindless_terrain27_memory_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_terrain27_memory_tests PRIVATE -UNDEBUG)
add_dependencies(arp_bindless_terrain27_memory_tests arp_bindless_terrain27_fixture)
if(EMSCRIPTEN)
    target_link_options(arp_bindless_terrain27_memory_tests PRIVATE
        "--embed-file=${bindless_terrain27_dir}@/bindless-terrain27")
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_terrain27_memory_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_terrain27_memory_tests>"
            /bindless-terrain27/GraphicsTerrain27.emulated.bindless.yaml
            /bindless-terrain27/GraphicsTerrain27.native.bindless.yaml)
    endif()
else()
    add_test(NAME arp_bindless_terrain27_memory_tests COMMAND arp_bindless_terrain27_memory_tests
        "${bindless_terrain27_dir}/GraphicsTerrain27.emulated.bindless.yaml"
        "${bindless_terrain27_dir}/GraphicsTerrain27.native.bindless.yaml")
endif()
