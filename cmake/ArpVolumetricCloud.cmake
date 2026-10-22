# Included by the checked-in and generated project. Cloud implementation belongs
# to renderer; no domain implementation is compiled into core.
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_contract.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_execution_plan.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_reference.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/volumetric_cloud_feature.cpp)

set(cloud_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/volumetric_cloud")
set(cloud_shader_outputs)
set(cloud_shader_compiler_args)
if(SLANGC_EXECUTABLE)
    list(APPEND cloud_shader_compiler_args --slangc "${SLANGC_EXECUTABLE}")
endif()
foreach(cloud_shader cloud_raymarch cloud_temporal cloud_composite cloud_demo_present cloud_density_slice)
    if(cloud_shader STREQUAL "cloud_demo_present")
        set(cloud_shader_source "shaders/samples/fog_demo_present.slang")
    else()
        set(cloud_shader_source "shaders/renderer/${cloud_shader}.slang")
    endif()
    set(cloud_reflection "${cloud_shader_dir}/${cloud_shader}.reflection.yaml")
    add_custom_command(OUTPUT "${cloud_reflection}"
        BYPRODUCTS "${cloud_shader_dir}/${cloud_shader}.spv"
            "${cloud_shader_dir}/${cloud_shader}.metal"
            "${cloud_shader_dir}/${cloud_shader}.wgsl"
            "${cloud_shader_dir}/${cloud_shader}.slang"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --module "${cloud_shader}" --out-dir "${cloud_shader_dir}" ${cloud_shader_compiler_args}
        DEPENDS "${cloud_shader_source}"
            shaders/renderer/cloud_common.slang shaders/renderer/volume/medium_integration.slang
            scripts/compile_shaders.py VERBATIM)
    list(APPEND cloud_shader_outputs "${cloud_reflection}")
endforeach()
add_custom_target(arp_volumetric_cloud_shaders DEPENDS ${cloud_shader_outputs})
if(TARGET arp_shader_tests)
    add_dependencies(arp_shader_tests arp_volumetric_cloud_shaders)
    target_compile_definitions(arp_shader_tests PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
endif()
target_sources(arp_assets PRIVATE src/assets/cloud/cloud_asset.cpp)
target_link_libraries(arp_assets PRIVATE arp_fkyaml_headers)
target_sources(arp_resource PRIVATE src/resource/cloud_asset_cooked.cpp)
target_sources(arp_resource PRIVATE src/resource/cloud_asset_resource.cpp)
target_sources(arp_3d PRIVATE src/3d/cloud_types.cpp src/3d/cloud_component.cpp)

add_executable(arp_cloud_asset_tool tools/cloud_asset/main.cpp)
target_link_libraries(arp_cloud_asset_tool PRIVATE arp_resource)
arp_add_mingw_static_runtime(arp_cloud_asset_tool)

add_executable(arp_volumetric_cloud examples/volumetric_cloud/main.cpp src/platform/common/renderdoc_capture_scope.cpp)
target_link_libraries(arp_volumetric_cloud PRIVATE arp_renderer arp_platform ${ARP_RENDERER_RHI_LINK_LIBS})
target_compile_definitions(arp_volumetric_cloud PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
add_dependencies(arp_volumetric_cloud arp_volumetric_cloud_shaders)
arp_add_mingw_static_runtime(arp_volumetric_cloud)
if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
    target_include_directories(arp_volumetric_cloud PRIVATE "C:/Program Files/RenderDoc")
    target_compile_definitions(arp_volumetric_cloud PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
endif()
if(NOT CMAKE_CROSSCOMPILING AND NOT EMSCRIPTEN)
    add_test(NAME arp_volumetric_cloud_cook_smoke COMMAND arp_cloud_asset_tool cook
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cloud/reference.cook.yaml"
        "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml" 4194304)
    set_tests_properties(arp_volumetric_cloud_cook_smoke PROPERTIES FIXTURES_SETUP cloud_reference_cook)
    add_test(NAME arp_volumetric_cloud_example_null COMMAND arp_volumetric_cloud --backend null --frames 3
        --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml")
    set_tests_properties(arp_volumetric_cloud_example_null PROPERTIES FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 60)
endif()

add_executable(arp_volumetric_cloud_tests tests/contract/volumetric_cloud_tests.cpp)
target_link_libraries(arp_volumetric_cloud_tests PRIVATE arp_renderer arp_rhi)
target_include_directories(arp_volumetric_cloud_tests PRIVATE .)
target_compile_definitions(arp_volumetric_cloud_tests PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
add_dependencies(arp_volumetric_cloud_tests arp_volumetric_cloud_shaders)
target_compile_options(arp_volumetric_cloud_tests PRIVATE -UNDEBUG)
add_test(NAME arp_volumetric_cloud_contract COMMAND arp_volumetric_cloud_tests)
add_test(NAME arp_volumetric_cloud_null_smoke COMMAND arp_volumetric_cloud_tests --smoke)
add_executable(arp_volumetric_cloud_feature_tests tests/contract/volumetric_cloud_feature_tests.cpp)
target_link_libraries(arp_volumetric_cloud_feature_tests PRIVATE arp_renderer arp_rhi)
target_include_directories(arp_volumetric_cloud_feature_tests PRIVATE .)
target_compile_options(arp_volumetric_cloud_feature_tests PRIVATE -UNDEBUG)
target_compile_definitions(arp_volumetric_cloud_feature_tests PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
add_dependencies(arp_volumetric_cloud_feature_tests arp_volumetric_cloud_shaders)
add_test(NAME arp_volumetric_cloud_feature_null_smoke COMMAND arp_volumetric_cloud_feature_tests)

add_executable(arp_volumetric_cloud_asset_tests tests/contract/volumetric_cloud_asset_tests.cpp)
target_link_libraries(arp_volumetric_cloud_asset_tests PRIVATE arp_renderer arp_scriptvm_runtime)
target_compile_definitions(arp_volumetric_cloud_asset_tests PRIVATE ARP_CLOUD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_volumetric_cloud_asset_tests PRIVATE -UNDEBUG)
set(cloud_reflection_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/reflection/cloud_contract")
arp_reflection_generate(TARGET arp_volumetric_cloud_asset_tests MODULE cloud_contract
    OUT_JSON "${cloud_reflection_dir}/cloud_contract.reflection.json"
    OUT_CPP "${cloud_reflection_dir}/cloud_contract.reflection.gen.cpp"
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/math/math.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/3d/cloud_types.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/3d/cloud_component.h")
add_test(NAME arp_volumetric_cloud_assets COMMAND arp_volumetric_cloud_asset_tests)
add_test(NAME arp_volumetric_cloud_shader_contract COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/volumetric_cloud_shader_tests.py"
    --directory "${cloud_shader_dir}")

if(TARGET arp_rhi_vulkan AND NOT APPLE AND NOT EMSCRIPTEN)
    if(TARGET arp_rhi_vulkan_staging_upload_pool_tests)
        add_test(NAME arp_rhi_vulkan_staging_policy_native
            COMMAND arp_rhi_vulkan_staging_upload_pool_tests --native-policy)
        set_tests_properties(arp_rhi_vulkan_staging_policy_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
    add_executable(arp_rhi_vulkan_direct_object_budget_tests
        tests/integration/rhi_vulkan_direct_object_budget_tests.cpp)
    target_link_libraries(arp_rhi_vulkan_direct_object_budget_tests PRIVATE arp_renderer arp_rhi_vulkan)
    target_include_directories(arp_rhi_vulkan_direct_object_budget_tests PRIVATE .)
    target_compile_options(arp_rhi_vulkan_direct_object_budget_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_rhi_vulkan_direct_object_budget_tests PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
    add_dependencies(arp_rhi_vulkan_direct_object_budget_tests arp_volumetric_cloud_shaders)
    arp_add_mingw_static_runtime(arp_rhi_vulkan_direct_object_budget_tests)
    add_test(NAME arp_rhi_vulkan_direct_object_budget_contract COMMAND arp_rhi_vulkan_direct_object_budget_tests --contract)
    add_test(NAME arp_rhi_vulkan_direct_object_budget_native COMMAND arp_rhi_vulkan_direct_object_budget_tests)
    set_tests_properties(arp_rhi_vulkan_direct_object_budget_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(arp_volumetric_cloud_vulkan_tests tests/integration/volumetric_cloud_vulkan_tests.cpp)
    target_link_libraries(arp_volumetric_cloud_vulkan_tests PRIVATE arp_renderer arp_rhi_vulkan)
    target_include_directories(arp_volumetric_cloud_vulkan_tests PRIVATE .)
    target_compile_options(arp_volumetric_cloud_vulkan_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_volumetric_cloud_vulkan_tests PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
    add_dependencies(arp_volumetric_cloud_vulkan_tests arp_volumetric_cloud_shaders)
    arp_add_mingw_static_runtime(arp_volumetric_cloud_vulkan_tests)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_volumetric_cloud_vulkan_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_volumetric_cloud_vulkan_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_volumetric_cloud_vulkan_execution COMMAND arp_volumetric_cloud_vulkan_tests)
    set_tests_properties(arp_volumetric_cloud_vulkan_execution PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    add_executable(arp_volumetric_cloud_density_temporal_tests
        tests/integration/volumetric_cloud_density_temporal_tests.cpp)
    target_link_libraries(arp_volumetric_cloud_density_temporal_tests PRIVATE arp_renderer arp_rhi_vulkan)
    target_include_directories(arp_volumetric_cloud_density_temporal_tests PRIVATE .)
    target_compile_options(arp_volumetric_cloud_density_temporal_tests PRIVATE -UNDEBUG)
    target_compile_definitions(arp_volumetric_cloud_density_temporal_tests PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
    add_dependencies(arp_volumetric_cloud_density_temporal_tests arp_volumetric_cloud_shaders)
    arp_add_mingw_static_runtime(arp_volumetric_cloud_density_temporal_tests)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_volumetric_cloud_density_temporal_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_volumetric_cloud_density_temporal_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_volumetric_cloud_density_temporal_vulkan COMMAND arp_volumetric_cloud_density_temporal_tests)
    set_tests_properties(arp_volumetric_cloud_density_temporal_vulkan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    add_test(NAME arp_volumetric_cloud_retained_graphics_vulkan COMMAND arp_volumetric_cloud_density_temporal_tests --retained-graphics)
    set_tests_properties(arp_volumetric_cloud_retained_graphics_vulkan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
    add_test(NAME arp_volumetric_cloud_retained_compute_vulkan COMMAND arp_volumetric_cloud_density_temporal_tests --retained)
    set_tests_properties(arp_volumetric_cloud_retained_compute_vulkan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()
