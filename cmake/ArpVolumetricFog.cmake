# Reusable fog targets; included by both the checked-in and generated project.
add_executable(arp_prototype_command_batch_tests tests/contract/prototype_command_batch_tests.cpp)
target_link_libraries(arp_prototype_command_batch_tests PRIVATE arp_rhi arp_rhi_webgpu)
target_include_directories(arp_prototype_command_batch_tests PRIVATE .)
target_compile_options(arp_prototype_command_batch_tests PRIVATE -UNDEBUG)
add_test(NAME arp_prototype_command_batch_contract COMMAND arp_prototype_command_batch_tests)
add_executable(arp_prototype_command_batch_lowering_tests
    tests/contract/prototype_command_batch_lowering_tests.cpp)
target_link_libraries(arp_prototype_command_batch_lowering_tests PRIVATE arp_core)
target_compile_options(arp_prototype_command_batch_lowering_tests PRIVATE -UNDEBUG)
add_test(NAME arp_prototype_command_batch_lowering_contract
    COMMAND arp_prototype_command_batch_lowering_tests)
set(fog_shader_names fog_inject fog_scatter fog_scatter_spatial fog_temporal fog_integrate fog_composite
    fog_demo_scene fog_demo_shadow fog_demo_glass fog_demo_plain_glass fog_demo_present)
set(fog_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/volumetric_fog")
set(fog_shader_outputs)
set(fog_shader_compiler_args)
if(SLANGC_EXECUTABLE)
    list(APPEND fog_shader_compiler_args --slangc "${SLANGC_EXECUTABLE}")
endif()
if(SPIRV_REFLECT_EXECUTABLE)
    list(APPEND fog_shader_compiler_args --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}")
endif()
foreach(fog_shader IN LISTS fog_shader_names)
    if(fog_shader MATCHES "^fog_demo_")
        set(fog_source_dir samples)
    else()
        set(fog_source_dir renderer)
    endif()
    set(fog_reflection "${fog_shader_dir}/${fog_shader}.reflection.yaml")
    add_custom_command(OUTPUT "${fog_reflection}"
        BYPRODUCTS "${fog_shader_dir}/${fog_shader}.artifact-set.yaml"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --module "${fog_shader}" --out-dir "${fog_shader_dir}" ${fog_shader_compiler_args}
        DEPENDS "shaders/${fog_source_dir}/${fog_shader}.slang" shaders/common/volumetric_fog.slang
            shaders/renderer/volume/medium_integration.slang
            shaders/core/noise3.slang shaders/core/box_geometry.slang
            shaders/renderer/fog_scatter.slang shaders/renderer/fog_scatter_common.slang
            shaders/renderer/ddgi_sampling.slang
            shaders/renderer/ddgi_visibility.slang shaders/renderer/ddgi_depth_cache.slang
            shaders/samples/fog_glass_geometry.slang shaders/samples/fog_scene_geometry.slang
            scripts/compile_shaders.py
        VERBATIM)
    list(APPEND fog_shader_outputs "${fog_reflection}")
endforeach()
add_custom_target(arp_volumetric_fog_shaders DEPENDS ${fog_shader_outputs})
if(TARGET arp_shader_tests)
    add_dependencies(arp_shader_tests arp_volumetric_fog_shaders)
    target_compile_definitions(arp_shader_tests PRIVATE ARP_FOG_SHADER_DIR="${fog_shader_dir}")
endif()

add_executable(arp_volumetric_fog_tests tests/contract/volumetric_fog_tests.cpp)
target_link_libraries(arp_volumetric_fog_tests PRIVATE arp_renderer arp_rhi arp_scriptvm_runtime)
target_compile_definitions(arp_volumetric_fog_tests PRIVATE
    ARP_FOG_SHADER_DIR="${fog_shader_dir}")
if(MSVC)
    target_compile_options(arp_volumetric_fog_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_volumetric_fog_tests PRIVATE -UNDEBUG)
endif()
add_dependencies(arp_volumetric_fog_tests arp_volumetric_fog_shaders)
add_test(NAME arp_volumetric_fog_contract COMMAND arp_volumetric_fog_tests)
add_test(NAME arp_volumetric_fog_null_smoke COMMAND arp_volumetric_fog_tests --smoke)
add_test(NAME arp_volumetric_fog_shader_contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/volumetric_fog_shader_tests.py"
    --directory "${fog_shader_dir}")

set(fog_reflection_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/reflection/fog_contract")
add_custom_command(OUTPUT "${fog_reflection_dir}/fog_contract.reflection.gen.cpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${fog_reflection_dir}"
    COMMAND "$<TARGET_FILE:arp_reflect_codegen>" --module fog_contract
        --out-json "${fog_reflection_dir}/fog_contract.reflection.json"
        --out-cpp "${fog_reflection_dir}/fog_contract.reflection.gen.cpp"
        --header "${CMAKE_CURRENT_SOURCE_DIR}/src/core/math/math.h"
        --header "${CMAKE_CURRENT_SOURCE_DIR}/src/3d/fog_components.h"
    DEPENDS arp_reflect_codegen src/core/math/math.h src/3d/fog_components.h VERBATIM)
target_sources(arp_volumetric_fog_tests PRIVATE "${fog_reflection_dir}/fog_contract.reflection.gen.cpp")

add_executable(arp_volumetric_fog examples/volumetric_fog/main.cpp
    src/platform/common/renderdoc_capture_scope.cpp)
target_include_directories(arp_volumetric_fog PRIVATE . src)
target_link_libraries(arp_volumetric_fog PRIVATE arp_renderer arp_platform
    ${ARP_RENDERER_RHI_LINK_LIBS})
add_dependencies(arp_volumetric_fog arp_volumetric_fog_shaders)
arp_add_mingw_static_runtime(arp_volumetric_fog)
if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
    target_include_directories(arp_volumetric_fog PRIVATE "C:/Program Files/RenderDoc")
    target_compile_definitions(arp_volumetric_fog PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_definitions(arp_volumetric_fog PRIVATE ARP_FOG_SHADER_DIR="shaders/volumetric_fog")
    target_link_options(arp_volumetric_fog PRIVATE
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_storage_format_probe.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_device_bootstrap_pre.js"
        "--preload-file=${fog_shader_dir}@shaders/volumetric_fog"
        -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=536870912
        -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sEXIT_RUNTIME=1)
    set_property(TARGET arp_volumetric_fog APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/volumetric_fog/index.html")
    add_custom_command(TARGET arp_volumetric_fog POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/volumetric_fog/index.html"
        "$<TARGET_FILE_DIR:arp_volumetric_fog>/arp_volumetric_fog.html")
    target_compile_options(arp_prototype_command_batch_tests PRIVATE --use-port=emdawnwebgpu)
    target_link_options(arp_prototype_command_batch_tests PRIVATE
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_storage_format_probe.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_device_bootstrap_pre.js"
        -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=134217728 -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sEXIT_RUNTIME=1)
    set_property(TARGET arp_prototype_command_batch_tests APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/prototype_command_batch_browser.html")
    add_custom_command(TARGET arp_prototype_command_batch_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/prototype_command_batch_browser.html"
        "$<TARGET_FILE_DIR:arp_prototype_command_batch_tests>/prototype_command_batch.html")
    if(ARP_WEBGPU_BROWSER_EXECUTABLE)
        add_test(NAME arp_prototype_command_batch_browser_lifecycle COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/volumetric_fog_browser_tests.py"
            --directory "$<TARGET_FILE_DIR:arp_prototype_command_batch_tests>"
            --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/prototype-batch-browser-lifecycle" --lifecycle-only)
        set_tests_properties(arp_prototype_command_batch_browser_lifecycle PROPERTIES TIMEOUT 70)
        add_test(NAME arp_volumetric_fog_browser_execution COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/volumetric_fog_browser_tests.py"
            --directory "$<TARGET_FILE_DIR:arp_volumetric_fog>"
            --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/volumetric-fog-browser-execution")
        set_tests_properties(arp_volumetric_fog_browser_execution PROPERTIES TIMEOUT 490)
        add_test(NAME arp_volumetric_fog_browser_diagnostic COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/volumetric_fog_browser_tests.py"
            --directory "$<TARGET_FILE_DIR:arp_volumetric_fog>"
            --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/volumetric-fog-browser"
            --expect-diagnostic RHI-WEBGPU-COMMAND-RECORDING-USAGE-UNPROVEN)
        set_tests_properties(arp_volumetric_fog_browser_diagnostic PROPERTIES TIMEOUT 490)
    endif()
else()
    target_compile_definitions(arp_volumetric_fog PRIVATE
        ARP_FOG_SHADER_DIR="${fog_shader_dir}")
endif()
add_test(NAME arp_volumetric_fog_example_null
    COMMAND arp_volumetric_fog --backend null --frames 3 --fixed-time 1)
set_tests_properties(arp_volumetric_fog_example_null PROPERTIES TIMEOUT 60)
add_test(NAME arp_volumetric_fog_slits_null
    COMMAND arp_volumetric_fog --backend null --slits --camera side --motion 0.12
        --point-shadow --pixels 12 --slices 112 --frames 3)
set_tests_properties(arp_volumetric_fog_slits_null PROPERTIES TIMEOUT 60)

if(APPLE AND TARGET arp_rhi_metal)
    add_executable(arp_volumetric_fog_metal_tests tests/integration/volumetric_fog_metal_tests.mm)
    target_include_directories(arp_volumetric_fog_metal_tests PRIVATE . src)
    target_link_libraries(arp_volumetric_fog_metal_tests PRIVATE arp_renderer arp_rhi_metal)
    target_compile_definitions(arp_volumetric_fog_metal_tests PRIVATE ARP_FOG_SHADER_DIR="${fog_shader_dir}")
    target_compile_options(arp_volumetric_fog_metal_tests PRIVATE -UNDEBUG)
    add_dependencies(arp_volumetric_fog_metal_tests arp_volumetric_fog_shaders)
    add_test(NAME arp_volumetric_fog_metal_execution COMMAND arp_volumetric_fog_metal_tests)
    set_tests_properties(arp_volumetric_fog_metal_execution PROPERTIES TIMEOUT 60)
endif()
