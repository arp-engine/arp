include_guard(GLOBAL)
# One shared ABI for the production copy/reduction operations. Compilation and
# authentic per-entry reflection are prerequisites, never an execution grant.
set(ARP_BINDLESS_DEPTH_PYRAMID_PREBUILT_DIR "" CACHE PATH "Explicit verified DepthPyramid cross-host package directory")
if(NOT ARP_BINDLESS_DEPTH_PYRAMID_PREBUILT_DIR AND (NOT Python3_EXECUTABLE OR NOT SLANGC_EXECUTABLE OR NOT SPIRV_REFLECT_EXECUTABLE))
    return()
endif()
set(ARP_BINDLESS_DEPTH_PYRAMID_DIR "${CMAKE_BINARY_DIR}/bindless-depth-pyramid-fixture")
if(ARP_BINDLESS_DEPTH_PYRAMID_PREBUILT_DIR)
    if(EMSCRIPTEN)
        message(FATAL_ERROR "DepthPyramid Web build requires its generated VFS fixture")
    endif()
    set(ARP_BINDLESS_DEPTH_PYRAMID_DIR "${ARP_BINDLESS_DEPTH_PYRAMID_PREBUILT_DIR}")
endif()
set(depth_pyramid_publications)
foreach(pyramid_mode Native Emulated)
    string(TOLOWER "${pyramid_mode}" pyramid_mode_name)
    set(pyramid_publication "${ARP_BINDLESS_DEPTH_PYRAMID_DIR}/DepthPyramid.${pyramid_mode_name}.bindless.yaml")
    list(APPEND depth_pyramid_publications "${pyramid_publication}")
    if(ARP_BINDLESS_DEPTH_PYRAMID_PREBUILT_DIR)
        if(NOT EXISTS "${pyramid_publication}")
            message(FATAL_ERROR "Explicit DepthPyramid prebuilt publication is missing: ${pyramid_publication}")
        endif()
    else()
    add_custom_command(OUTPUT "${pyramid_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_depth_pyramid.yaml"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_depth_pyramid.slang"
            --bindless-entry depthPyramidCopyMain --bindless-entry depthPyramidReduceMain
            --bindless-entry depthPyramidReadMain --bindless-mode "${pyramid_mode}"
            --out-dir "${ARP_BINDLESS_DEPTH_PYRAMID_DIR}"
        DEPENDS tests/fixtures/bindless_depth_pyramid.yaml tests/fixtures/bindless_depth_pyramid.slang
            scripts/bindless_codegen.py scripts/compile_shaders.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
    endif()
    if(TARGET arp_rhi_bindless_package_tests AND NOT EMSCRIPTEN)
        add_test(NAME arp_rhi_bindless_depth_pyramid_${pyramid_mode_name}_package_tests
            COMMAND arp_rhi_bindless_package_tests "${pyramid_publication}" valid depth-pyramid)
    endif()
endforeach()
add_custom_target(arp_bindless_depth_pyramid_fixture DEPENDS ${depth_pyramid_publications})
foreach(pyramid_target arp_rhi_vulkan_bindless_compute_tests arp_rhi_metal_original_texture_upload_tests arp_rhi_metal_bindless_graphics_tests arp_original_depth_table_expansion_artifact_tests)
    if(TARGET ${pyramid_target})
        if(EMSCRIPTEN)
            set(pyramid_runtime "/bindless-depth-pyramid-fixture/DepthPyramid.emulated.bindless.yaml")
            target_link_options(${pyramid_target} PRIVATE "--preload-file=${ARP_BINDLESS_DEPTH_PYRAMID_DIR}@/bindless-depth-pyramid-fixture")
            set_property(TARGET ${pyramid_target} APPEND PROPERTY LINK_DEPENDS ${depth_pyramid_publications})
        else()
            set(pyramid_runtime "${ARP_BINDLESS_DEPTH_PYRAMID_DIR}/DepthPyramid.native.bindless.yaml")
        endif()
        target_compile_definitions(${pyramid_target} PRIVATE ARP_BINDLESS_DEPTH_PYRAMID_PACKAGE="${pyramid_runtime}")
        add_dependencies(${pyramid_target} arp_bindless_depth_pyramid_fixture)
    endif()
endforeach()
if(TARGET arp_rhi_bindless_package_tests)
    add_dependencies(arp_rhi_bindless_package_tests arp_bindless_depth_pyramid_fixture)
endif()
add_test(NAME arp_bindless_depth_pyramid_schema_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
        BindlessCodegenTests.test_depth_pyramid_shared_image_schema_and_exact_budgets
        BindlessCodegenTests.test_native_single_element_tables_use_the_reflected_scalar_abi)
if(TARGET arp_rhi_bindless_package_tests AND NOT EMSCRIPTEN)
    add_test(NAME arp_bindless_depth_pyramid_cross_target_tests
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            ${bindless_spirv_val_args} --runtime-verifier "$<TARGET_FILE:arp_rhi_bindless_package_tests>"
            BindlessCodegenTests.test_depth_pyramid_shared_image_cross_target_package)
endif()
if(ARP_WEBGPU_BROWSER_EXECUTABLE)
    add_test(NAME arp_bindless_depth_pyramid_browser_preflight
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_package_browser_preflight.py"
            --publication "${ARP_BINDLESS_DEPTH_PYRAMID_DIR}/DepthPyramid.emulated.bindless.yaml"
            --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
            --output "${CMAKE_BINARY_DIR}/artifacts/bindless-depth-pyramid-preflight")
    set_tests_properties(arp_bindless_depth_pyramid_browser_preflight PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
endif()
