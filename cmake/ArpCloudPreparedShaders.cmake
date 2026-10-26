# Shared Slang publications consumed by the cloud feature and RHI tests.
set(original_cloud_module_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/original-cloud-graphics")
set(original_cloud_module "${original_cloud_module_dir}/cloud_composite.graphics.module.yaml")
set(original_graphics_compiler_args)
set(original_cloud_publication_tools)
if(SLANGC_EXECUTABLE)
    list(APPEND original_graphics_compiler_args --slangc "${SLANGC_EXECUTABLE}")
    list(APPEND original_cloud_publication_tools "${SLANGC_EXECUTABLE}")
endif()
if(ARP_SPIRV_CROSS_LIBRARY)
    list(APPEND original_cloud_publication_tools "${ARP_SPIRV_CROSS_LIBRARY}")
endif()
set(original_cloud_module_payloads)
foreach(module_directory "" "Vertex/" "Fragment/")
    foreach(module_extension slang spv metal wgsl reflection.yaml)
        list(APPEND original_cloud_module_payloads
            "${original_cloud_module_dir}/${module_directory}cloud_composite.${module_extension}")
    endforeach()
endforeach()
list(APPEND original_cloud_module_payloads
    "${original_cloud_module_dir}/Vertex/cloud_composite.vertexMain.entry.yaml"
    "${original_cloud_module_dir}/Fragment/cloud_composite.fragmentMain.entry.yaml")
add_custom_command(OUTPUT "${original_cloud_module}"
    BYPRODUCTS ${original_cloud_module_payloads}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
        --module cloud_composite --graphics-module --out-dir "${original_cloud_module_dir}"
        ${original_graphics_compiler_args}
    DEPENDS shaders/renderer/cloud_composite.slang shaders/renderer/cloud_common.slang
        shaders/renderer/cloud_composite_impl.slang
        shaders/renderer/volume/medium_integration.slang scripts/compile_shaders.py
        scripts/shader_bindings_codegen.py ${original_cloud_publication_tools} VERBATIM)
add_custom_target(arp_rhi_original_cloud_graphics_module DEPENDS "${original_cloud_module}")
set(original_cloud_spatial_module "${original_cloud_module_dir}/cloud_spatial_composite.graphics.module.yaml")
set(original_cloud_spatial_payloads)
foreach(module_directory "" "Vertex/" "Fragment/")
    foreach(module_extension slang spv metal wgsl reflection.yaml)
        list(APPEND original_cloud_spatial_payloads
            "${original_cloud_module_dir}/${module_directory}cloud_spatial_composite.${module_extension}")
    endforeach()
endforeach()
list(APPEND original_cloud_spatial_payloads
    "${original_cloud_module_dir}/Vertex/cloud_spatial_composite.vertexMain.entry.yaml"
    "${original_cloud_module_dir}/Fragment/cloud_spatial_composite.fragmentMain.entry.yaml")
add_custom_command(OUTPUT "${original_cloud_spatial_module}"
    BYPRODUCTS ${original_cloud_spatial_payloads}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
        --module cloud_spatial_composite --graphics-module --out-dir "${original_cloud_module_dir}"
        ${original_graphics_compiler_args}
    DEPENDS shaders/renderer/cloud_spatial_composite.slang shaders/renderer/cloud_common.slang
        shaders/renderer/cloud_composite_impl.slang
        shaders/renderer/volume/medium_integration.slang scripts/compile_shaders.py
        scripts/shader_bindings_codegen.py ${original_cloud_publication_tools} VERBATIM)
add_custom_target(arp_rhi_original_cloud_spatial_graphics_module DEPENDS "${original_cloud_spatial_module}")
set(original_cloud_compute_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/original-cloud-compute")
set(original_cloud_compute_entries)
foreach(cloud_module cloud_raymarch cloud_temporal cloud_spatial_resolve)
    set(cloud_entry "${original_cloud_compute_dir}/${cloud_module}.computeMain.entry.yaml")
    set(cloud_payloads)
    foreach(extension slang spv metal wgsl reflection.yaml)
        list(APPEND cloud_payloads "${original_cloud_compute_dir}/${cloud_module}.${extension}")
    endforeach()
    add_custom_command(OUTPUT "${cloud_entry}"
        BYPRODUCTS ${cloud_payloads}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --module ${cloud_module} --single-entry computeMain --out-dir "${original_cloud_compute_dir}"
            ${original_graphics_compiler_args}
        DEPENDS "shaders/renderer/${cloud_module}.slang" shaders/renderer/cloud_common.slang
            shaders/renderer/cloud_ray_integration.slang shaders/renderer/cloud_history.slang
            shaders/renderer/volume/medium_integration.slang scripts/compile_shaders.py
            scripts/shader_bindings_codegen.py ${original_cloud_publication_tools} VERBATIM)
    list(APPEND original_cloud_compute_entries "${cloud_entry}")
endforeach()
add_custom_target(arp_rhi_original_cloud_compute_entries DEPENDS ${original_cloud_compute_entries})

set(cloud_foreground_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/cloud-foreground")
set(cloud_foreground_module "${cloud_foreground_dir}/cloud_foreground.graphics.module.yaml")
set(cloud_foreground_payloads)
foreach(stage_directory "" "Vertex/" "Fragment/")
    foreach(extension slang spv metal wgsl reflection.yaml)
        list(APPEND cloud_foreground_payloads
            "${cloud_foreground_dir}/${stage_directory}cloud_foreground.${extension}")
    endforeach()
endforeach()
list(APPEND cloud_foreground_payloads
    "${cloud_foreground_dir}/Vertex/cloud_foreground.vertexMain.entry.yaml"
    "${cloud_foreground_dir}/Fragment/cloud_foreground.fragmentMain.entry.yaml")
add_custom_command(OUTPUT "${cloud_foreground_module}"
    BYPRODUCTS ${cloud_foreground_payloads}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
        --fixture cloud_foreground --graphics-module --out-dir "${cloud_foreground_dir}"
        ${original_graphics_compiler_args}
    DEPENDS tests/fixtures/cloud_foreground.slang scripts/compile_shaders.py
        scripts/shader_bindings_codegen.py VERBATIM)
add_custom_target(arp_cloud_foreground_module DEPENDS "${cloud_foreground_module}")
add_test(NAME arp_cloud_foreground_shader_contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_foreground_shader_tests.py"
        --directory "${cloud_foreground_dir}")
