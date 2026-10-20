add_executable(arp_bindless_workload_resources_tests tests/contract/bindless_workload_resources_tests.cpp)
target_link_libraries(arp_bindless_workload_resources_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_workload_resources_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_workload_resources_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_workload_resources_tests>")
    endif()
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND Python3_Interpreter_FOUND)
        add_test(NAME arp_bindless_workload_resources_browser_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_workload_resources_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
                --runner "$<TARGET_FILE_DIR:arp_bindless_workload_resources_tests>"
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-workload-resources")
        set_tests_properties(arp_bindless_workload_resources_browser_smoke PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE)
    endif()
else()
    add_test(NAME arp_bindless_workload_resources_tests COMMAND arp_bindless_workload_resources_tests)
endif()

add_executable(arp_bindless_workload_capacity_tests tests/contract/bindless_workload_capacity_tests.cpp)
target_link_libraries(arp_bindless_workload_capacity_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_workload_capacity_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_workload_capacity_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_workload_capacity_tests>")
    endif()
else()
    add_test(NAME arp_bindless_workload_capacity_tests COMMAND arp_bindless_workload_capacity_tests)
endif()

add_executable(arp_bindless_gpu_visibility_layout_tests tests/contract/bindless_gpu_visibility_layout_tests.cpp)

add_executable(arp_original_bindless_indirect_access_tests tests/contract/original_bindless_indirect_access_tests.cpp)
target_link_libraries(arp_original_bindless_indirect_access_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_original_bindless_indirect_access_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_original_bindless_indirect_access_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_original_bindless_indirect_access_tests>")
    endif()
else()
    add_test(NAME arp_original_bindless_indirect_access_tests COMMAND arp_original_bindless_indirect_access_tests)
endif()
target_link_libraries(arp_bindless_gpu_visibility_layout_tests PRIVATE arp_rhi arp_rhi_semantics)
target_include_directories(arp_bindless_gpu_visibility_layout_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
target_compile_options(arp_bindless_gpu_visibility_layout_tests PRIVATE -UNDEBUG)
set(bindless_visibility_test_args)
if(Python3_Interpreter_FOUND AND SLANGC_EXECUTABLE AND SPIRV_REFLECT_EXECUTABLE)
    set(bindless_visibility_shader_dir "${CMAKE_BINARY_DIR}/bindless-gpu-visibility")
    add_custom_command(OUTPUT "${bindless_visibility_shader_dir}/gpu_visibility_layout.computeMain.entry.yaml"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --fixture gpu_visibility_layout --single-entry computeMain
            --out-dir "${bindless_visibility_shader_dir}"
        DEPENDS shaders/core/gpu_visibility.slang shaders/core/instance_transform.slang
            tests/fixtures/gpu_visibility_layout.slang
            scripts/compile_shaders.py scripts/shader_bindings_codegen.py
        VERBATIM)
    add_custom_target(arp_bindless_gpu_visibility_shader_fixture
        DEPENDS "${bindless_visibility_shader_dir}/gpu_visibility_layout.computeMain.entry.yaml")
    add_dependencies(arp_bindless_gpu_visibility_layout_tests arp_bindless_gpu_visibility_shader_fixture)
    if(EMSCRIPTEN)
        target_link_options(arp_bindless_gpu_visibility_layout_tests PRIVATE
            "--embed-file=${bindless_visibility_shader_dir}@/bindless-gpu-visibility")
        set(bindless_visibility_test_args /bindless-gpu-visibility/gpu_visibility_layout.spv
            /bindless-gpu-visibility/gpu_visibility_layout.wgsl)
    else()
        set(bindless_visibility_test_args "${bindless_visibility_shader_dir}/gpu_visibility_layout.spv"
            "${bindless_visibility_shader_dir}/gpu_visibility_layout.wgsl")
    endif()
    set(bindless_visibility_kernel_outputs)
    foreach(visibility_phase Reset Cull Emit Finalize)
        set(visibility_kernel_dir "${bindless_visibility_shader_dir}/${visibility_phase}")
        set(visibility_kernel_entry "visibility${visibility_phase}ComputeMain")
        set(visibility_kernel_publication "${visibility_kernel_dir}/gpu_visibility.${visibility_kernel_entry}.entry.yaml")
        add_custom_command(OUTPUT "${visibility_kernel_publication}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
                --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
                --module gpu_visibility --single-entry "${visibility_kernel_entry}"
                --out-dir "${visibility_kernel_dir}"
            DEPENDS shaders/renderer/gpu_visibility.slang shaders/core/gpu_visibility.slang
                shaders/core/gpu_visibility_kernel.slang
                shaders/core/instance_transform.slang scripts/compile_shaders.py scripts/shader_bindings_codegen.py
            VERBATIM)
        list(APPEND bindless_visibility_kernel_outputs "${visibility_kernel_publication}")
        if(EMSCRIPTEN)
            list(APPEND bindless_visibility_test_args "/bindless-gpu-visibility/${visibility_phase}/gpu_visibility.spv"
                "/bindless-gpu-visibility/${visibility_phase}/gpu_visibility.wgsl")
        else()
            list(APPEND bindless_visibility_test_args "${visibility_kernel_dir}/gpu_visibility.spv"
                "${visibility_kernel_dir}/gpu_visibility.wgsl")
        endif()
    endforeach()
    add_custom_target(arp_bindless_gpu_visibility_kernel_shaders DEPENDS ${bindless_visibility_kernel_outputs})
    add_dependencies(arp_bindless_gpu_visibility_layout_tests arp_bindless_gpu_visibility_kernel_shaders)
    set(bindless_visibility_publications)
    foreach(visibility_mode Native Emulated)
        string(TOLOWER "${visibility_mode}" visibility_mode_name)
        set(visibility_publication "${bindless_visibility_shader_dir}/Public/ArpGpuVisibility.${visibility_mode_name}.bindless.yaml")
        list(APPEND bindless_visibility_publications "${visibility_publication}")
        add_custom_command(OUTPUT "${visibility_publication}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
                --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
                --bindless-variant "${CMAKE_SOURCE_DIR}/shaders/renderer/gpu_visibility.bindless.yaml"
                --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/instance_transform.slang"
                --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/gpu_visibility.slang"
                --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/gpu_visibility_kernel.slang"
                --bindless-source "${CMAKE_SOURCE_DIR}/shaders/renderer/gpu_visibility_bindless.slang"
                --bindless-entry visibilityResetComputeMain --bindless-entry visibilityCullComputeMain
                --bindless-entry visibilityEmitComputeMain --bindless-entry visibilityFinalizeComputeMain
                --bindless-mode "${visibility_mode}" --out-dir "${bindless_visibility_shader_dir}/Public"
            DEPENDS shaders/renderer/gpu_visibility.bindless.yaml shaders/renderer/gpu_visibility_bindless.slang
                shaders/core/instance_transform.slang shaders/core/gpu_visibility.slang shaders/core/gpu_visibility_kernel.slang
                scripts/bindless_codegen.py scripts/gpu_visibility_codegen.py scripts/compile_shaders.py
                scripts/buffer_vector_record.py scripts/slang_resource_switch.py scripts/spirv_layout.py
                scripts/shader_bindings_codegen.py "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
            VERBATIM)
        if(NOT EMSCRIPTEN)
            add_test(NAME arp_bindless_gpu_visibility_${visibility_mode_name}_package_tests
                COMMAND arp_rhi_bindless_package_tests "${visibility_publication}" valid gpu-visibility)
        endif()
    endforeach()
    foreach(visibility_mode Native Emulated)
        string(TOLOWER "${visibility_mode}" visibility_mode_name)
        set(visibility_publication "${bindless_visibility_shader_dir}/Public/ArpGpuVisibilityConsumer.${visibility_mode_name}.bindless.yaml")
        list(APPEND bindless_visibility_publications "${visibility_publication}")
        add_custom_command(OUTPUT "${visibility_publication}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
                --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
                --bindless-variant "${CMAKE_SOURCE_DIR}/shaders/renderer/gpu_visibility_consumer.bindless.yaml"
                --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/instance_transform.slang"
                --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/gpu_visibility.slang"
                --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/gpu_visibility_kernel.slang"
                --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/renderer/gpu_visibility_bindless.slang"
                --bindless-source "${CMAKE_SOURCE_DIR}/shaders/renderer/gpu_visibility_consumer.slang"
                --bindless-entry visibilityResetComputeMain --bindless-entry visibilityCullComputeMain
                --bindless-entry visibilityEmitComputeMain --bindless-entry visibilityFinalizeComputeMain
                --bindless-entry visibilityVertexMain --bindless-entry visibilityFragmentMain
                --bindless-mode "${visibility_mode}" --out-dir "${bindless_visibility_shader_dir}/Public"
            DEPENDS shaders/renderer/gpu_visibility_consumer.bindless.yaml shaders/renderer/gpu_visibility_consumer.slang
                shaders/renderer/gpu_visibility.bindless.yaml shaders/renderer/gpu_visibility_bindless.slang
                shaders/core/instance_transform.slang shaders/core/gpu_visibility.slang shaders/core/gpu_visibility_kernel.slang
                scripts/bindless_codegen.py scripts/gpu_visibility_codegen.py scripts/compile_shaders.py
                scripts/buffer_vector_record.py scripts/slang_resource_switch.py scripts/spirv_layout.py
                scripts/shader_bindings_codegen.py "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
            VERBATIM)
        if(NOT EMSCRIPTEN)
            add_test(NAME arp_bindless_gpu_visibility_consumer_${visibility_mode_name}_package_tests
                COMMAND arp_rhi_bindless_package_tests "${visibility_publication}" valid gpu-visibility-consumer)
            add_test(NAME arp_bindless_gpu_visibility_${visibility_mode_name}_root_composition_tests
                COMMAND arp_rhi_bindless_package_tests "${visibility_publication}" valid gpu-visibility-root-composition "${visibility_mode_name}")
        endif()
    endforeach()
    add_custom_target(arp_bindless_gpu_visibility_public_shaders DEPENDS ${bindless_visibility_publications})
    add_dependencies(arp_rhi_bindless_package_tests arp_bindless_gpu_visibility_public_shaders)
endif()
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_gpu_visibility_layout_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_gpu_visibility_layout_tests>" ${bindless_visibility_test_args})
    endif()
else()
    add_test(NAME arp_bindless_gpu_visibility_layout_tests COMMAND arp_bindless_gpu_visibility_layout_tests
        ${bindless_visibility_test_args})
endif()
