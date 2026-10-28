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
        shaders/renderer/cloud_composite_impl.slang shaders/renderer/cloud_sample_storage.slang shaders/renderer/cloud_spatial_resolve_impl.slang
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
        shaders/renderer/cloud_composite_impl.slang shaders/renderer/cloud_sample_storage.slang shaders/renderer/cloud_spatial_resolve_impl.slang
        shaders/renderer/volume/medium_integration.slang scripts/compile_shaders.py
        scripts/shader_bindings_codegen.py ${original_cloud_publication_tools} VERBATIM)
add_custom_target(arp_rhi_original_cloud_spatial_graphics_module DEPENDS "${original_cloud_spatial_module}")
set(original_cloud_compact_module "${original_cloud_module_dir}/cloud_compact_composite.graphics.module.yaml")
set(original_cloud_compact_payloads)
foreach(module_directory "" "Vertex/" "Fragment/")
    foreach(module_extension slang spv metal wgsl reflection.yaml)
        list(APPEND original_cloud_compact_payloads
            "${original_cloud_module_dir}/${module_directory}cloud_compact_composite.${module_extension}")
    endforeach()
endforeach()
list(APPEND original_cloud_compact_payloads
    "${original_cloud_module_dir}/Vertex/cloud_compact_composite.vertexMain.entry.yaml"
    "${original_cloud_module_dir}/Fragment/cloud_compact_composite.fragmentMain.entry.yaml")
add_custom_command(OUTPUT "${original_cloud_compact_module}"
    BYPRODUCTS ${original_cloud_compact_payloads}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
        --module cloud_compact_composite --graphics-module --out-dir "${original_cloud_module_dir}"
        ${original_graphics_compiler_args}
    DEPENDS shaders/renderer/cloud_compact_composite.slang shaders/renderer/cloud_spatial_composite.slang shaders/renderer/cloud_common.slang
        shaders/renderer/cloud_composite_impl.slang shaders/renderer/cloud_sample_storage.slang shaders/renderer/cloud_spatial_resolve_impl.slang
        shaders/renderer/volume/medium_integration.slang scripts/compile_shaders.py
        scripts/shader_bindings_codegen.py ${original_cloud_publication_tools} VERBATIM)
add_custom_target(arp_rhi_original_cloud_compact_graphics_module DEPENDS "${original_cloud_compact_module}")
set(original_cloud_compute_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/original-cloud-compute")
set(original_cloud_compute_entries)
foreach(cloud_module cloud_raymarch cloud_temporal cloud_spatial_resolve cloud_compact_resolve cloud_asset_slice)
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
            shaders/renderer/cloud_sample_storage.slang shaders/renderer/cloud_spatial_resolve_impl.slang shaders/renderer/cloud_spatial_resolve.slang
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

# Scene producer uses the same original graphics-module publication workflow.
set(scene_opaque_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/scene-opaque")
set(scene_opaque_module "${scene_opaque_dir}/scene_opaque.graphics.module.yaml")
set(scene_opaque_payloads)
foreach(stage_directory "" "Vertex/" "Fragment/")
    foreach(extension slang spv metal wgsl reflection.yaml)
        list(APPEND scene_opaque_payloads "${scene_opaque_dir}/${stage_directory}scene_opaque.${extension}")
    endforeach()
endforeach()
list(APPEND scene_opaque_payloads
    "${scene_opaque_dir}/Vertex/scene_opaque.vertexMain.entry.yaml"
    "${scene_opaque_dir}/Fragment/scene_opaque.fragmentMain.entry.yaml")
add_custom_command(OUTPUT "${scene_opaque_module}"
    BYPRODUCTS ${scene_opaque_payloads}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
        --module scene_opaque --graphics-module --out-dir "${scene_opaque_dir}"
        ${original_graphics_compiler_args}
    DEPENDS shaders/renderer/scene_opaque.slang scripts/compile_shaders.py
        scripts/shader_bindings_codegen.py VERBATIM)
add_custom_target(arp_scene_opaque_module DEPENDS "${scene_opaque_module}")
add_test(NAME arp_scene_opaque_shader_contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/scene_opaque_shader_tests.py"
        --directory "${scene_opaque_dir}")
if(TARGET arp_shader_tests)
    add_dependencies(arp_shader_tests arp_scene_opaque_module)
    target_compile_definitions(arp_shader_tests PRIVATE ARP_SCENE_OPAQUE_MODULE_DIR="${scene_opaque_dir}")
endif()

# PBR keeps the same publication/selected-entry workflow with its own ABI.
set(scene_pbr_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/scene-pbr")
set(scene_pbr_module "${scene_pbr_dir}/scene_pbr.graphics.module.yaml")
set(scene_pbr_payloads)
foreach(stage_directory "" "Vertex/" "Fragment/")
    foreach(extension slang spv metal wgsl reflection.yaml)
        list(APPEND scene_pbr_payloads "${scene_pbr_dir}/${stage_directory}scene_pbr.${extension}")
    endforeach()
endforeach()
list(APPEND scene_pbr_payloads
    "${scene_pbr_dir}/Vertex/scene_pbr.vertexMain.entry.yaml"
    "${scene_pbr_dir}/Fragment/scene_pbr.fragmentMain.entry.yaml")
add_custom_command(OUTPUT "${scene_pbr_module}"
    BYPRODUCTS ${scene_pbr_payloads}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
        --module scene_pbr --graphics-module --out-dir "${scene_pbr_dir}"
        ${original_graphics_compiler_args}
    DEPENDS shaders/renderer/scene_pbr.slang shaders/renderer/pbr_brdf.slang
        shaders/core/pbr_material.slang shaders/core/leaf_surface.slang
        scripts/compile_shaders.py scripts/shader_bindings_codegen.py
        ${original_cloud_publication_tools} VERBATIM)
add_custom_target(arp_scene_pbr_module DEPENDS "${scene_pbr_module}")
add_test(NAME arp_scene_pbr_shader_contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/scene_pbr_shader_tests.py"
        --directory "${scene_pbr_dir}")
if(TARGET arp_shader_tests)
    add_dependencies(arp_shader_tests arp_scene_pbr_module)
    target_compile_definitions(arp_shader_tests PRIVATE ARP_SCENE_PBR_MODULE_DIR="${scene_pbr_dir}")
endif()
