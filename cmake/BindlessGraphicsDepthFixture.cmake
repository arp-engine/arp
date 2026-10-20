include_guard(GLOBAL)
if(NOT SLANGC_EXECUTABLE OR NOT SPIRV_REFLECT_EXECUTABLE OR NOT Python3_Interpreter_FOUND)
    return()
endif()

set(bindless_graphics_depth_dir "${CMAKE_BINARY_DIR}/bindless-graphics-depth-color")
set(bindless_graphics_depth_outputs)
foreach(depth_mode Native Emulated)
    string(TOLOWER "${depth_mode}" depth_mode_name)
    set(depth_publication "${bindless_graphics_depth_dir}/GraphicsDepthColor.${depth_mode_name}.bindless.yaml")
    list(APPEND bindless_graphics_depth_outputs "${depth_publication}")
    add_custom_command(OUTPUT "${depth_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_depth_color.yaml"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_depth_color.slang"
            --bindless-entry vertexMain --bindless-entry cleanFragment --bindless-entry faultFragment
            --bindless-mode "${depth_mode}" --out-dir "${bindless_graphics_depth_dir}"
        DEPENDS tests/fixtures/bindless_graphics_depth_color.yaml tests/fixtures/bindless_graphics_depth_color.slang
            scripts/compile_shaders.py scripts/bindless_codegen.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_graphics_depth_color_fixture DEPENDS ${bindless_graphics_depth_outputs})
if(EMSCRIPTEN AND TARGET arp_rhi_webgpu)
    add_executable(arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests
        tests/integration/rhi_webgpu_bindless_graphics_depth_pipeline_tests.cpp)
    target_include_directories(arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests PRIVATE . src)
    target_link_libraries(arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests PRIVATE arp_rhi_webgpu arp_rhi)
    target_compile_options(arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests PRIVATE
        -O1 -UNDEBUG --use-port=emdawnwebgpu)
    target_link_options(arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests PRIVATE
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_storage_format_probe.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_device_bootstrap_pre.js"
        "--preload-file=${bindless_graphics_depth_dir}@/bindless-depth"
        -sASYNCIFY -sASYNCIFY_STACK_SIZE=16384 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2
        -sSTACK_SIZE=65536 -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=67108864)
    add_dependencies(arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests arp_bindless_graphics_depth_color_fixture)
    set_property(TARGET arp_rhi_webgpu_bindless_graphics_depth_pipeline_tests APPEND PROPERTY LINK_DEPENDS
        ${bindless_graphics_depth_outputs}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_storage_format_probe.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_device_bootstrap_pre.js")
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND Python3_Interpreter_FOUND)
        add_test(NAME arp_rhi_webgpu_bindless_graphics_depth_pipeline_browser_tests
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_depth_pipeline_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}" --runner "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-webgpu-depth-pipeline")
        set_tests_properties(arp_rhi_webgpu_bindless_graphics_depth_pipeline_browser_tests PROPERTIES
            TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME arp_rhi_webgpu_bindless_graphics_depth_closure_browser_tests
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_depth_pipeline_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}" --runner "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-webgpu-depth-closure19" --closure)
        set_tests_properties(arp_rhi_webgpu_bindless_graphics_depth_closure_browser_tests PROPERTIES
            TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME arp_rhi_webgpu_bindless_graphics_depth_execution_browser_tests
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_depth_pipeline_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}" --runner "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-webgpu-depth-execution14" --execution)
        set_tests_properties(arp_rhi_webgpu_bindless_graphics_depth_execution_browser_tests PROPERTIES
            TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME arp_rhi_webgpu_bindless_depth_write_return_browser_tests
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_depth_pipeline_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}" --runner "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-webgpu-depth-write-return1" --rewrite)
        set_tests_properties(arp_rhi_webgpu_bindless_depth_write_return_browser_tests PROPERTIES
            TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
endif()
if(APPLE AND TARGET arp_rhi_metal)
    add_executable(arp_rhi_metal_bindless_graphics_depth_pipeline_tests
        tests/integration/rhi_metal_bindless_graphics_depth_pipeline_tests.cpp)
    target_include_directories(arp_rhi_metal_bindless_graphics_depth_pipeline_tests PRIVATE . src)
    target_link_libraries(arp_rhi_metal_bindless_graphics_depth_pipeline_tests PRIVATE arp_rhi_metal)
    target_compile_options(arp_rhi_metal_bindless_graphics_depth_pipeline_tests PRIVATE -UNDEBUG)
    add_dependencies(arp_rhi_metal_bindless_graphics_depth_pipeline_tests arp_bindless_graphics_depth_color_fixture)
    add_test(NAME arp_rhi_metal_bindless_graphics_depth_pipeline_tests
        COMMAND arp_rhi_metal_bindless_graphics_depth_pipeline_tests
            "${bindless_graphics_depth_dir}/GraphicsDepthColor.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_bindless_graphics_depth_pipeline_tests PROPERTIES
        TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()
if(APPLE AND TARGET arp_rhi_metal_bindless_graphics_tests
    AND TARGET arp_bindless_sampled_depth_fixture AND TARGET arp_bindless_completed_graphics_fixture)
    add_dependencies(arp_rhi_metal_bindless_graphics_tests arp_bindless_graphics_depth_color_fixture)
    add_test(NAME arp_rhi_metal_ordinary_depth_closure16_tests
        COMMAND arp_rhi_metal_bindless_graphics_tests
            --publication "${ARP_COMPLETED_GRAPHICS_FIXTURE_DIR}/RootVertexBuffer.native.bindless.yaml"
            --ordinary-depth-closure "${ARP_SAMPLED_DEPTH_FIXTURE_DIR}/SampledDepth.native.bindless.yaml"
            "${bindless_graphics_depth_dir}/GraphicsDepthColor.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_ordinary_depth_closure16_tests PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
    add_test(NAME arp_rhi_metal_ordinary_depth_execution2_tests
        COMMAND arp_rhi_metal_bindless_graphics_tests
            --publication "${ARP_COMPLETED_GRAPHICS_FIXTURE_DIR}/RootVertexBuffer.native.bindless.yaml"
            --ordinary-depth-execution "${ARP_SAMPLED_DEPTH_FIXTURE_DIR}/SampledDepth.native.bindless.yaml"
            "${bindless_graphics_depth_dir}/GraphicsDepthColor.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_ordinary_depth_execution2_tests PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()
if(TARGET arp_rhi_vulkan_bindless_compute_tests)
    add_dependencies(arp_rhi_vulkan_bindless_compute_tests arp_bindless_graphics_depth_color_fixture)
    target_compile_definitions(arp_rhi_vulkan_bindless_compute_tests PRIVATE
        ARP_VULKAN_ORDINARY_DEPTH_GRAPHICS_PACKAGE="${bindless_graphics_depth_dir}/GraphicsDepthColor.native.bindless.yaml")
endif()
set(bindless_graphics_depth_verifier_args)
if(TARGET arp_rhi_bindless_package_tests AND NOT EMSCRIPTEN)
    list(APPEND bindless_graphics_depth_verifier_args
        --runtime-verifier "$<TARGET_FILE:arp_rhi_bindless_package_tests>")
endif()
add_test(NAME arp_bindless_graphics_depth_color_codegen_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
        --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
        ${bindless_spirv_val_args}
        ${bindless_graphics_depth_verifier_args}
        BindlessCodegenTests.test_graphics_depth_color_stage_and_target_contract)
