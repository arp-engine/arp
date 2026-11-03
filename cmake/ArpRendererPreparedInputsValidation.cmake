function(arp_add_renderer_prepared_inputs_validation)
    if(EMSCRIPTEN OR NOT SLANGC_EXECUTABLE OR NOT SPIRV_REFLECT_EXECUTABLE OR NOT TARGET arp_2d_renderer)
        return()
    endif()
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/renderer-prepared-input-fixture")
    set(publications)
    foreach(module IN ITEMS fxaa wocc_taa ssao display_transfer scene_2d_sprites)
        set(publication "${directory}/${module}.artifact-set.yaml")
        add_custom_command(OUTPUT "${publication}"
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py" ${ARP_SHADER_TOOL_INPUT_ARGS}
                --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
                --out-dir "${directory}" --module "${module}"
            DEPENDS ${ARP_SHADER_TOOL_INPUT_DEPENDENCIES} scripts/compile_shaders.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
                shaders/wocc/fxaa.slang shaders/wocc/taa.slang shaders/wocc/ssao.slang
                shaders/renderer/display_transfer.slang shaders/2d/sprites.slang
                "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
            VERBATIM)
        list(APPEND publications "${publication}")
    endforeach()
    # An original FS-only graphics-module is a negative push-stage ABI fixture.
    set(stageProof "${directory}/stage-proof/fxaa.artifact-set.yaml")
    add_custom_command(OUTPUT "${stageProof}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py" ${ARP_SHADER_TOOL_INPUT_ARGS}
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --out-dir "${directory}/stage-proof" --module fxaa --graphics-module
        DEPENDS ${ARP_SHADER_TOOL_INPUT_DEPENDENCIES} scripts/compile_shaders.py scripts/spirv_layout.py scripts/shader_bindings_codegen.py
            shaders/wocc/fxaa.slang "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
    add_custom_target(arp_renderer_prepared_input_fixture DEPENDS ${publications} "${stageProof}")
    add_executable(arp_renderer_prepared_inputs_tests tests/contract/renderer_prepared_inputs_tests.cpp)
    target_include_directories(arp_renderer_prepared_inputs_tests PRIVATE . src)
    target_compile_definitions(arp_renderer_prepared_inputs_tests PRIVATE ARP_PREPARED_FIXTURE_DIR="${directory}")
    if(APPLE)
        target_link_libraries(arp_renderer_prepared_inputs_tests PRIVATE arp_renderer arp_2d_renderer
            arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d
            arp_renderer_rhi_contracts arp_rhi arp_rhi_semantics)
    else()
        target_link_libraries(arp_renderer_prepared_inputs_tests PRIVATE arp_renderer arp_2d_renderer
            "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
            "$<LINK_GROUP:RESCAN,arp_renderer_rhi_contracts,arp_rhi,arp_rhi_semantics>")
    endif()
    target_compile_options(arp_renderer_prepared_inputs_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_renderer_prepared_inputs_tests)
    add_dependencies(arp_renderer_prepared_inputs_tests arp_renderer_prepared_input_fixture)
    add_test(NAME arp_renderer_prepared_inputs_tests COMMAND arp_renderer_prepared_inputs_tests "${directory}")
    add_test(NAME arp_renderer_prepared_inputs_null_smoke COMMAND arp_renderer_prepared_inputs_tests "${directory}" --smoke)
    set_tests_properties(arp_renderer_prepared_inputs_tests arp_renderer_prepared_inputs_null_smoke
        PROPERTIES TIMEOUT 180 LABELS "renderer;shader;io;null;smoke")
    add_test(NAME arp_renderer_prepared_inputs_http_smoke COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/renderer_prepared_inputs_http_smoke.py"
        "$<TARGET_FILE:arp_renderer_prepared_inputs_tests>" "${directory}")
    set_tests_properties(arp_renderer_prepared_inputs_http_smoke PROPERTIES TIMEOUT 180 LABELS "renderer;shader;http;smoke")
    if(WIN32 AND TARGET arp_platform)
        add_executable(arp_renderer_prepared_inputs_vulkan_smoke
            tests/smoke/renderer_prepared_inputs_vulkan_smoke.cpp tests/fixtures/postprocess_fixture.cpp
            src/platform/common/platform_lifecycle_state.cpp)
        target_include_directories(arp_renderer_prepared_inputs_vulkan_smoke PRIVATE . src "${CMAKE_BINARY_DIR}/shaders")
        target_compile_definitions(arp_renderer_prepared_inputs_vulkan_smoke PRIVATE
            ARP_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders" ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            ARP_PREPARED_FIXTURE_DIR="${directory}")
        if(EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
            target_include_directories(arp_renderer_prepared_inputs_vulkan_smoke PRIVATE "C:/Program Files/RenderDoc")
            target_compile_definitions(arp_renderer_prepared_inputs_vulkan_smoke PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
        endif()
        target_compile_options(arp_renderer_prepared_inputs_vulkan_smoke PRIVATE -UNDEBUG)
        target_link_libraries(arp_renderer_prepared_inputs_vulkan_smoke PRIVATE arp_scene_assets arp_renderer
            arp_2d_renderer arp_physics2d arp_platform arp_rhi arp_rhi_semantics
            "$<LINK_GROUP:RESCAN,arp_gui,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
            arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
        arp_add_mingw_static_runtime(arp_renderer_prepared_inputs_vulkan_smoke)
        add_dependencies(arp_renderer_prepared_inputs_vulkan_smoke arp_renderer_prepared_input_fixture arp_compile_shaders)
        add_test(NAME arp_renderer_prepared_inputs_vulkan_smoke COMMAND arp_renderer_prepared_inputs_vulkan_smoke)
        add_test(NAME arp_renderer_cold_inputs_vulkan_smoke COMMAND arp_renderer_prepared_inputs_vulkan_smoke --cold)
        add_test(NAME arp_renderer_prepared_inputs_vulkan_lifecycle COMMAND arp_renderer_prepared_inputs_vulkan_smoke --lifecycle)
        set_tests_properties(arp_renderer_prepared_inputs_vulkan_smoke arp_renderer_cold_inputs_vulkan_smoke
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE LABELS "renderer;shader;vulkan;smoke")
        set_tests_properties(arp_renderer_prepared_inputs_vulkan_lifecycle PROPERTIES
            TIMEOUT 180 RUN_SERIAL TRUE LABELS "renderer;shader;vulkan;lifecycle")
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_renderer_prepared_inputs_validation)
