include_guard(GLOBAL)
# Shared, authenticated Slang fixture for the three original storage preparations.
set(ARP_BINDLESS_STORAGE_ROOT_PREBUILT_DIR "" CACHE PATH "Explicit verified StorageVector package directory (cross-host fixtures)")
if(EMSCRIPTEN)
    set(storage_root_mode Emulated)
    set(storage_root_mode_name emulated)
    set(storage_root_dir "${ARP_BINDLESS_WEBGPU_RHI_FIXTURE_DIR}/original-root")
    set(storage_root_runtime "/bindless-rhi-fixture/original-root/StorageVector.emulated.bindless.yaml")
else()
    set(storage_root_mode Native)
    set(storage_root_mode_name native)
    set(storage_root_dir "${CMAKE_BINARY_DIR}/bindless-storage-root-fixture")
endif()
if(ARP_BINDLESS_STORAGE_ROOT_PREBUILT_DIR)
    if(EMSCRIPTEN)
        message(FATAL_ERROR "StorageVector Web build requires its generated VFS fixture")
    endif()
    set(storage_root_dir "${ARP_BINDLESS_STORAGE_ROOT_PREBUILT_DIR}")
    if(NOT EXISTS "${storage_root_dir}/StorageVector.native.bindless.yaml")
        message(FATAL_ERROR "Explicit StorageVector prebuilt publication is missing")
    endif()
else()
    if(NOT Python3_EXECUTABLE OR NOT SLANGC_EXECUTABLE)
        message(STATUS "StorageVector device smoke unavailable: Python/Slang or an explicit prebuilt directory required")
        return()
    endif()
    set(storage_root_publication "${storage_root_dir}/StorageVector.${storage_root_mode_name}.bindless.yaml")
    add_custom_command(OUTPUT "${storage_root_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_storage_vector.yaml"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_storage_vector.slang"
            --bindless-entry storeMain --bindless-entry readMain --bindless-mode "${storage_root_mode}"
            --out-dir "${storage_root_dir}"
        DEPENDS tests/fixtures/bindless_storage_vector.yaml tests/fixtures/bindless_storage_vector.slang
            scripts/bindless_codegen.py scripts/compile_shaders.py scripts/slang_resource_switch.py
            scripts/buffer_vector_record.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
    add_custom_target(arp_bindless_storage_root_fixture DEPENDS "${storage_root_publication}")
endif()
if(NOT EMSCRIPTEN)
    set(storage_root_runtime "${storage_root_dir}/StorageVector.native.bindless.yaml")
endif()
foreach(storage_root_target arp_rhi_metal_bindless_graphics_tests arp_rhi_vulkan_bindless_compute_tests arp_rhi_metal_original_texture_upload_tests arp_rhi_webgpu_original_bindless_root_browser_smoke arp_original_depth_table_expansion_artifact_tests)
    if(TARGET ${storage_root_target})
        target_compile_definitions(${storage_root_target} PRIVATE ARP_BINDLESS_STORAGE_ROOT_PACKAGE="${storage_root_runtime}")
        if(TARGET arp_bindless_storage_root_fixture)
            add_dependencies(${storage_root_target} arp_bindless_storage_root_fixture)
            if(EMSCRIPTEN)
                set_property(TARGET ${storage_root_target} APPEND PROPERTY LINK_DEPENDS "${storage_root_publication}")
            endif()
        endif()
    endif()
endforeach()
if(APPLE)
    foreach(storage_readback_target arp_rhi_metal_bindless_graphics_tests arp_rhi_metal_original_texture_upload_tests)
        if(TARGET ${storage_readback_target})
            target_sources(${storage_readback_target} PRIVATE tests/fixtures/metal_storage_readback_fixture.mm)
        endif()
    endforeach()
endif()
if(EMSCRIPTEN AND TARGET arp_original_depth_table_expansion_artifact_tests)
    target_link_options(arp_original_depth_table_expansion_artifact_tests PRIVATE
        "--preload-file=${storage_root_dir}@/bindless-rhi-fixture/original-root")
    if(ARP_WEBGPU_BROWSER_EXECUTABLE)
        add_test(NAME arp_rhi_webgpu_mixed_content16_plan_browser_tests
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/original_depth_table_expansion_preflight_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}" --runner "${CMAKE_BINARY_DIR}"
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-mixed-content16-plan"
                --completed-depth-only --mixed-content-plan --timeout-seconds 240)
        set_tests_properties(arp_rhi_webgpu_mixed_content16_plan_browser_tests PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        foreach(mixed_terminal complete caller-drop retired-lifetime recorded-cancel retire-before-submit
            recording-error reversed-receipts submission-error throw-before throw-after pending-cancel)
        foreach(mixed_case clean fault)
            set(mixed_case_args)
            set(mixed_test_case "${mixed_case}")
            if(NOT mixed_terminal STREQUAL "complete")
                list(APPEND mixed_case_args --mixed-terminal "${mixed_terminal}")
                set(mixed_test_case "${mixed_terminal}-${mixed_case}")
            endif()
            if(mixed_case STREQUAL "fault")
                list(APPEND mixed_case_args --mixed-fault)
            endif()
            add_test(NAME arp_rhi_webgpu_public_mixed12_${mixed_test_case}_browser_tests
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/original_depth_table_expansion_preflight_smoke.py"
                    --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}" --runner "${CMAKE_BINARY_DIR}"
                    --output "${CMAKE_BINARY_DIR}/artifacts/bindless-mixed12-${mixed_test_case}"
                    --completed-depth-only --mixed-execution ${mixed_case_args} --timeout-seconds 240)
            set_tests_properties(arp_rhi_webgpu_public_mixed12_${mixed_test_case}_browser_tests PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endforeach()
        endforeach()
    endif()
endif()

if(APPLE AND TARGET arp_rhi_metal_original_texture_upload_tests)
    add_test(NAME arp_rhi_metal_public_storage9_tests COMMAND arp_rhi_metal_original_texture_upload_tests --storage-execution)
    set_tests_properties(arp_rhi_metal_public_storage9_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()
if(TARGET arp_rhi_vulkan_bindless_compute_tests)
    add_test(NAME arp_rhi_vulkan_public_storage9_tests COMMAND arp_rhi_vulkan_bindless_compute_tests --storage-execution)
    set_tests_properties(arp_rhi_vulkan_public_storage9_tests PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()

if(TARGET arp_rhi_vulkan_bindless_compute_tests AND TARGET arp_bindless_sampled_depth_fixture
    AND TARGET arp_bindless_completed_graphics_fixture)
    add_test(NAME arp_rhi_vulkan_mixed_content15_plan_tests COMMAND arp_rhi_vulkan_bindless_compute_tests --mixed-content-plan)
    set_tests_properties(arp_rhi_vulkan_mixed_content15_plan_tests PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
    add_test(NAME arp_rhi_vulkan_public_mixed11_tests COMMAND arp_rhi_vulkan_bindless_compute_tests --mixed-execution)
    set_tests_properties(arp_rhi_vulkan_public_mixed11_tests PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
    add_test(NAME arp_rhi_vulkan_public_mixed11_lifecycle_tests COMMAND arp_rhi_vulkan_bindless_compute_tests --mixed-lifecycle)
    # Eight terminal scenarios reuse the bounded clean/fault execution workload.
    set_tests_properties(arp_rhi_vulkan_public_mixed11_lifecycle_tests PROPERTIES TIMEOUT 1440 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()
