# Native observational fixture; no runtime limit or execution gate promotion.
add_executable(arp_compute_dispatch_validation_tests tests/contract/compute_dispatch_validation_tests.cpp)
target_link_libraries(arp_compute_dispatch_validation_tests PRIVATE arp_rhi)
target_include_directories(arp_compute_dispatch_validation_tests PRIVATE src)
target_compile_options(arp_compute_dispatch_validation_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_compute_dispatch_validation_tests)
add_test(NAME arp_compute_dispatch_validation_contract COMMAND arp_compute_dispatch_validation_tests)
if(Python3_EXECUTABLE AND SLANGC_EXECUTABLE)
    set(dispatch_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/compute-dispatch-probe")
    set(dispatch_probe_entry "${dispatch_probe_dir}/compute_dispatch_grid_probe.computeMain.entry.yaml")
    set(dispatch_probe_payloads)
    set(dispatch_probe_tools "${SLANGC_EXECUTABLE}")
    set(dispatch_probe_args)
    if(ARP_SPIRV_CROSS_LIBRARY)
        list(APPEND dispatch_probe_tools "${ARP_SPIRV_CROSS_LIBRARY}")
    endif()
    if(SPIRV_REFLECT_EXECUTABLE)
        list(APPEND dispatch_probe_tools "${SPIRV_REFLECT_EXECUTABLE}")
        list(APPEND dispatch_probe_args --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}")
    endif()
    foreach(extension slang spv metal wgsl reflection.yaml artifact-set.yaml)
        list(APPEND dispatch_probe_payloads "${dispatch_probe_dir}/compute_dispatch_grid_probe.${extension}")
    endforeach()
    add_custom_command(OUTPUT "${dispatch_probe_entry}"
        BYPRODUCTS ${dispatch_probe_payloads}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --fixture compute_dispatch_grid_probe --single-entry computeMain
            --slangc "${SLANGC_EXECUTABLE}" --out-dir "${dispatch_probe_dir}" ${dispatch_probe_args}
        DEPENDS tests/fixtures/compute_dispatch_grid_probe.slang scripts/compile_shaders.py
            scripts/shader_bindings_codegen.py ${dispatch_probe_tools}
        VERBATIM)
    add_custom_target(arp_compute_dispatch_grid_probe_shader DEPENDS "${dispatch_probe_entry}")
    set(dispatch_probe_pin "${CMAKE_CURRENT_BINARY_DIR}/generated/metal-dispatch/metal_dispatch_probe_pin.h")
    add_custom_command(OUTPUT "${dispatch_probe_pin}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/pin_compute_dispatch_probe.py"
            --directory "${dispatch_probe_dir}" --output "${dispatch_probe_pin}"
        DEPENDS "${dispatch_probe_entry}" ${dispatch_probe_payloads}
            scripts/pin_compute_dispatch_probe.py tests/contract/compute_dispatch_grid_probe_shader_tests.py
            scripts/spirv_layout.py tests/fixtures/compute_dispatch_grid_probe.slang
        VERBATIM)
    add_custom_target(arp_compute_dispatch_probe_pin DEPENDS "${dispatch_probe_pin}")
    add_test(NAME arp_compute_dispatch_grid_probe_shader_contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/compute_dispatch_grid_probe_shader_tests.py"
            --directory "${dispatch_probe_dir}")
    if(APPLE AND TARGET arp_rhi_metal)
        target_sources(arp_rhi_metal PRIVATE src/rhi/backends/metal/metal_dispatch_validation_session.mm)
        target_include_directories(arp_rhi_metal PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/metal-dispatch")
        target_compile_definitions(arp_rhi_metal PRIVATE ARP_METAL_DISPATCH_VALIDATION_PROBE_ENTRY="${dispatch_probe_entry}")
        add_dependencies(arp_rhi_metal arp_compute_dispatch_probe_pin)
        add_executable(arp_rhi_metal_dispatch_validation_session_tests
            tests/integration/rhi_metal_dispatch_validation_session_tests.mm)
        target_link_libraries(arp_rhi_metal_dispatch_validation_session_tests PRIVATE arp_rhi_metal arp_renderer)
        target_compile_options(arp_rhi_metal_dispatch_validation_session_tests PRIVATE -UNDEBUG)
        add_test(NAME arp_rhi_metal_dispatch_validation_session
            COMMAND arp_rhi_metal_dispatch_validation_session_tests)
        set_tests_properties(arp_rhi_metal_dispatch_validation_session PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_dispatch_l2_preparation
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --l2-preparation)
        set_tests_properties(arp_rhi_metal_dispatch_l2_preparation PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_cloud_dispatch_admission
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --cloud-admission)
        set_tests_properties(arp_rhi_metal_cloud_dispatch_admission PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_original_compute_creation
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --original-compute-creation "${dispatch_probe_entry}")
        set_tests_properties(arp_rhi_metal_original_compute_creation PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_original_compute_acceptance
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --original-compute-acceptance "${dispatch_probe_entry}")
        set_tests_properties(arp_rhi_metal_original_compute_acceptance PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_dependencies(arp_rhi_metal_dispatch_validation_session_tests arp_rhi_original_cloud_graphics_module)
        add_dependencies(arp_rhi_metal_dispatch_validation_session_tests arp_cloud_foreground_module)
        add_dependencies(arp_rhi_metal_dispatch_validation_session_tests
            arp_rhi_original_cloud_compute_entries arp_rhi_original_cloud_spatial_graphics_module)
        add_test(NAME arp_rhi_metal_ordinary_native_memory
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-native-memory "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(arp_rhi_metal_ordinary_native_memory PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_ordinary_buffer_bindings
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-buffer-bindings "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME arp_rhi_metal_ordinary_owner_cloud
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-owner-cloud "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(arp_rhi_metal_ordinary_owner_cloud PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_ordinary_queue_acceptance
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-queue-acceptance
                "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(arp_rhi_metal_ordinary_queue_acceptance PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_ordinary_owner_completion
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-owner-completion
                "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(arp_rhi_metal_ordinary_owner_completion PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        foreach(cloud_mode IN ITEMS 1 4 6 12 24 25 26 27)
            add_test(NAME arp_rhi_metal_ordinary_owner_cloud_${cloud_mode}
                COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-owner-cloud
                    "${CMAKE_CURRENT_BINARY_DIR}" "${cloud_mode}")
            set_tests_properties(arp_rhi_metal_ordinary_owner_cloud_${cloud_mode} PROPERTIES
                TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        endforeach()
        foreach(history_mode IN ITEMS 0 1 4 12)
            add_test(NAME arp_rhi_metal_ordinary_cloud_history_${history_mode}
                COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-cloud-history
                    "${CMAKE_CURRENT_BINARY_DIR}" "${history_mode}")
            set_tests_properties(arp_rhi_metal_ordinary_cloud_history_${history_mode} PROPERTIES
                TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        endforeach()
        foreach(slot_count IN ITEMS 2 3)
            add_test(NAME arp_rhi_metal_ordinary_cloud_slots_${slot_count}
                COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-cloud-slots
                    "${CMAKE_CURRENT_BINARY_DIR}" "${slot_count}")
            set_tests_properties(arp_rhi_metal_ordinary_cloud_slots_${slot_count} PROPERTIES
                TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
            add_test(NAME arp_rhi_metal_ordinary_cloud_resize_${slot_count}
                COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-cloud-resize
                    "${CMAKE_CURRENT_BINARY_DIR}" "${slot_count}")
            set_tests_properties(arp_rhi_metal_ordinary_cloud_resize_${slot_count} PROPERTIES
                TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        endforeach()
        add_test(NAME arp_rhi_metal_ordinary_owner_sampled
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-owner-sampled "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(arp_rhi_metal_ordinary_owner_sampled PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_ordinary_owner_compute
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-owner-compute "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(arp_rhi_metal_ordinary_owner_compute PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_ordinary_owner
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --ordinary-owner "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(arp_rhi_metal_ordinary_owner PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        set_tests_properties(arp_rhi_metal_ordinary_buffer_bindings PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_original_graphics_acceptance
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --original-graphics-acceptance
                "${original_cloud_module}" "${cloud_foreground_module}")
        set_tests_properties(arp_rhi_metal_original_graphics_acceptance PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_original_graphics_creation
            COMMAND arp_rhi_metal_dispatch_validation_session_tests --original-graphics-creation "${original_cloud_module}")
        set_tests_properties(arp_rhi_metal_original_graphics_creation PROPERTIES
            TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_test(NAME arp_rhi_metal_dispatch_session_probe_custody
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/metal_dispatch_session_probe_custody_tests.py"
                --executable $<TARGET_FILE:arp_rhi_metal_dispatch_validation_session_tests>
                --publication "${dispatch_probe_entry}")
        # Temporarily mutates and restores this generated publication. Keep all
        # other tests away while proving that a valid replacement cannot mint it.
        set_tests_properties(arp_rhi_metal_dispatch_session_probe_custody PROPERTIES
            TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
        add_executable(arp_rhi_metal_dispatch_grid_probe tests/integration/rhi_metal_dispatch_grid_probe.mm)
        target_link_libraries(arp_rhi_metal_dispatch_grid_probe PRIVATE arp_rhi_metal)
        target_compile_options(arp_rhi_metal_dispatch_grid_probe PRIVATE -UNDEBUG)
        add_dependencies(arp_rhi_metal_dispatch_grid_probe arp_compute_dispatch_grid_probe_shader)
        add_test(NAME arp_rhi_metal_dispatch_grid_request_contract
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/metal_dispatch_grid_probe_request_tests.py"
                --executable $<TARGET_FILE:arp_rhi_metal_dispatch_grid_probe>)
        add_test(NAME arp_rhi_metal_dispatch_grid_smoke
            COMMAND arp_rhi_metal_dispatch_grid_probe "${dispatch_probe_entry}" 8 6 1 1536)
        add_test(NAME arp_rhi_metal_dispatch_grid_3d_smoke
            COMMAND arp_rhi_metal_dispatch_grid_probe "${dispatch_probe_entry}" 3 2 3 1536)
        add_test(NAME arp_rhi_metal_dispatch_grid_720p
            COMMAND arp_rhi_metal_dispatch_grid_probe "${dispatch_probe_entry}" 160 90 1 16777216)
        set_tests_properties(arp_rhi_metal_dispatch_grid_smoke PROPERTIES TIMEOUT 60 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        set_tests_properties(arp_rhi_metal_dispatch_grid_3d_smoke PROPERTIES TIMEOUT 60 ENVIRONMENT "MTL_DEBUG_LAYER=1")
        set_tests_properties(arp_rhi_metal_dispatch_grid_request_contract PROPERTIES TIMEOUT 60)
        set_tests_properties(arp_rhi_metal_dispatch_grid_720p PROPERTIES TIMEOUT 300 ENVIRONMENT "MTL_DEBUG_LAYER=1")
    endif()
endif()
if(TARGET arp_rhi_metal AND NOT (APPLE AND Python3_EXECUTABLE AND SLANGC_EXECUTABLE))
    target_sources(arp_rhi_metal PRIVATE src/rhi/backends/metal/metal_dispatch_validation_session_stub.cpp)
endif()
