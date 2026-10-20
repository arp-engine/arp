include_guard(GLOBAL)
if(NOT TARGET arp_bindless_skinning256_fixture)
    return()
endif()
if(TARGET arp_rhi_vulkan AND NOT APPLE AND NOT EMSCRIPTEN)
    add_executable(arp_rhi_vulkan_bindless_instance64_tests tests/integration/rhi_vulkan_bindless_graphics_matrix_tests.cpp)
    target_include_directories(arp_rhi_vulkan_bindless_instance64_tests PRIVATE . src)
    target_link_libraries(arp_rhi_vulkan_bindless_instance64_tests PRIVATE arp_rhi_vulkan)
    target_compile_options(arp_rhi_vulkan_bindless_instance64_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_vulkan_bindless_instance64_tests PRIVATE ARP_BINDLESS_INSTANCE64_FIXTURE=1)
    add_dependencies(arp_rhi_vulkan_bindless_instance64_tests arp_bindless_instance64_fixture)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_rhi_vulkan_bindless_instance64_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_rhi_vulkan_bindless_instance64_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_rhi_vulkan_bindless_instance64_tests COMMAND arp_rhi_vulkan_bindless_instance64_tests
        "${CMAKE_BINARY_DIR}/bindless-instance64/GraphicsInstance64.native.bindless.yaml")
    set_tests_properties(arp_rhi_vulkan_bindless_instance64_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 RUN_SERIAL TRUE)
endif()
set(bindless_instance64_dir "${CMAKE_BINARY_DIR}/bindless-instance64")
set(bindless_instance64_outputs)
foreach(instance_mode Native Emulated)
    string(TOLOWER "${instance_mode}" instance_mode_name)
    set(instance_publication "${bindless_instance64_dir}/GraphicsInstance64.${instance_mode_name}.bindless.yaml")
    list(APPEND bindless_instance64_outputs "${instance_publication}")
    add_custom_command(OUTPUT "${instance_publication}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
            --bindless-variant "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_instance64.yaml"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/instance_transform.slang"
            --bindless-source-library "${CMAKE_SOURCE_DIR}/shaders/core/foliage_geometry.slang"
            --bindless-source "${CMAKE_SOURCE_DIR}/tests/fixtures/bindless_graphics_instance64.slang"
            --bindless-entry vertexMain --bindless-entry cleanFragment --bindless-entry faultFragment
            --bindless-mode "${instance_mode}" --out-dir "${bindless_instance64_dir}"
        DEPENDS tests/fixtures/bindless_graphics_instance64.yaml tests/fixtures/bindless_graphics_instance64.slang
            shaders/core/instance_transform.slang shaders/core/foliage_geometry.slang scripts/compile_shaders.py scripts/bindless_codegen.py
            scripts/slang_resource_switch.py scripts/buffer_vector_record.py scripts/spirv_layout.py
            scripts/shader_bindings_codegen.py "${SLANGC_EXECUTABLE}" "${SPIRV_REFLECT_EXECUTABLE}"
        VERBATIM)
endforeach()
add_custom_target(arp_bindless_instance64_fixture DEPENDS ${bindless_instance64_outputs})
if(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke)
    add_dependencies(arp_rhi_webgpu_original_bindless_root_browser_smoke arp_bindless_instance64_fixture)
    target_link_options(arp_rhi_webgpu_original_bindless_root_browser_smoke PRIVATE
        "--preload-file=${bindless_instance64_dir}@/bindless-instance64")
    set_property(TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke APPEND PROPERTY LINK_DEPENDS
        ${bindless_instance64_outputs})
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND Python3_Interpreter_FOUND)
        add_test(NAME arp_rhi_webgpu_original_bindless_instance64_browser_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_rhi_smoke.py"
                --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
                --runner "$<TARGET_FILE_DIR:arp_rhi_webgpu_original_bindless_root_browser_smoke>"
                --timeout-seconds 480 --original-bindless-root-preparation
                --original-root-matrix-execution-only --original-root-instance64 --wasm-baseline-only
                --output "${CMAKE_BINARY_DIR}/artifacts/bindless-original-root-instance64")
        set_tests_properties(arp_rhi_webgpu_original_bindless_instance64_browser_smoke
            PROPERTIES TIMEOUT 540 RUN_SERIAL TRUE)
    endif()
endif()
if(APPLE AND TARGET arp_rhi_metal)
    add_executable(arp_rhi_metal_bindless_instance64_tests tests/integration/rhi_metal_bindless_graphics_matrix_tests.cpp)
    target_include_directories(arp_rhi_metal_bindless_instance64_tests PRIVATE . src)
    target_link_libraries(arp_rhi_metal_bindless_instance64_tests PRIVATE arp_rhi_metal)
    target_compile_options(arp_rhi_metal_bindless_instance64_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_metal_bindless_instance64_tests PRIVATE ARP_BINDLESS_INSTANCE64_FIXTURE=1)
    add_dependencies(arp_rhi_metal_bindless_instance64_tests arp_bindless_instance64_fixture)
    add_test(NAME arp_rhi_metal_bindless_instance64_tests COMMAND arp_rhi_metal_bindless_instance64_tests
        "${bindless_instance64_dir}/GraphicsInstance64.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_bindless_instance64_tests PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()
add_executable(arp_bindless_instance64_data_tests tests/contract/bindless_instance64_data_tests.cpp)
target_include_directories(arp_bindless_instance64_data_tests PRIVATE . src)
target_link_libraries(arp_bindless_instance64_data_tests PRIVATE arp_rhi)
target_compile_options(arp_bindless_instance64_data_tests PRIVATE -UNDEBUG)
if(EMSCRIPTEN)
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_instance64_data_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_instance64_data_tests>")
    endif()
else()
    add_test(NAME arp_bindless_instance64_data_tests COMMAND arp_bindless_instance64_data_tests)
endif()
add_test(NAME arp_bindless_source_library_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_codegen_tests.py"
    BindlessCodegenTests.test_explicit_source_libraries_share_whole_source_refusals
    BindlessCodegenTests.test_authored_table_bypass_rejected_before_compilation)
add_executable(arp_bindless_instance64_memory_tests tests/contract/bindless_instance64_memory_tests.cpp)
target_link_libraries(arp_bindless_instance64_memory_tests PRIVATE arp_rhi arp_rhi_semantics)
target_compile_options(arp_bindless_instance64_memory_tests PRIVATE -UNDEBUG)
add_dependencies(arp_bindless_instance64_memory_tests arp_bindless_instance64_fixture)
if(EMSCRIPTEN)
    target_link_options(arp_bindless_instance64_memory_tests PRIVATE
        "--embed-file=${bindless_instance64_dir}@/bindless-instance64")
    if(ARP_NODE_EXECUTABLE)
        add_test(NAME arp_bindless_instance64_memory_tests COMMAND "${ARP_NODE_EXECUTABLE}"
            "$<TARGET_FILE:arp_bindless_instance64_memory_tests>"
            /bindless-instance64/GraphicsInstance64.emulated.bindless.yaml
            /bindless-instance64/GraphicsInstance64.native.bindless.yaml)
    endif()
else()
    add_test(NAME arp_bindless_instance64_memory_tests COMMAND arp_bindless_instance64_memory_tests
        "${bindless_instance64_dir}/GraphicsInstance64.emulated.bindless.yaml"
        "${bindless_instance64_dir}/GraphicsInstance64.native.bindless.yaml")
endif()
