# The identical production compiler/artifact library is shared with isolated
# Wasm contracts. No replacement parser or synthetic artifact owner is linked.
function(arp_add_shader_library target core_target yaml_target)
    set(shader_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    add_library(${target}
        "${shader_root}/shader/shader_compiler.cpp"
        "${shader_root}/shader/shader_pipeline_reflection_validator.cpp"
        "${shader_root}/shader/shader_reflection_cache.cpp"
        "${shader_root}/shader/generated_shader_artifact_set.cpp"
        "${shader_root}/shader/single_entry_shader_artifact.cpp"
        "${shader_root}/shader/webgl_shader_program_artifact.cpp"
        "${shader_root}/shader/original_depth_table_expansion_artifact.cpp"
        "${shader_root}/shader/spirv_buffer_declaration_layout.cpp"
        "${shader_root}/shader/wgsl_buffer_declaration_layout.cpp"
        "${shader_root}/shader/target_buffer_memory_declarations.cpp"
        "${shader_root}/shader/runtime_array_length_operations.cpp"
        "${shader_root}/shader/graphics_shader_module_artifact.cpp")
    target_include_directories(${target} PUBLIC "${shader_root}")
    target_link_libraries(${target} PUBLIC ${core_target} ${yaml_target})
endfunction()
