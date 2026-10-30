# Included by the checked-in and generated project. Cloud implementation belongs
# to renderer; no domain implementation is compiled into core.
add_executable(arp_rhi_buffer_placement_tests tests/contract/buffer_placement_tests.cpp)
target_link_libraries(arp_rhi_buffer_placement_tests PRIVATE arp_rhi arp_rhi_semantics)
foreach(placement_backend metal webgpu)
    if(TARGET arp_rhi_${placement_backend})
        target_link_libraries(arp_rhi_buffer_placement_tests PRIVATE arp_rhi_${placement_backend})
        string(TOUPPER "${placement_backend}" placement_backend_define)
        target_compile_definitions(arp_rhi_buffer_placement_tests PRIVATE ARP_PLACEMENT_TEST_${placement_backend_define}=1)
    endif()
endforeach()
target_compile_options(arp_rhi_buffer_placement_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_rhi_buffer_placement_tests)
add_test(NAME arp_rhi_buffer_placement_contract COMMAND arp_rhi_buffer_placement_tests)
add_test(NAME arp_rhi_buffer_placement_null_smoke COMMAND arp_rhi_buffer_placement_tests --null-hidden-frame)

add_executable(arp_command_recording_budget_tests tests/contract/command_recording_budget_tests.cpp)
target_link_libraries(arp_command_recording_budget_tests PRIVATE arp_core)
target_compile_options(arp_command_recording_budget_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_command_recording_budget_tests)
add_test(NAME arp_command_recording_budget_contract COMMAND arp_command_recording_budget_tests)

add_executable(arp_rhi_render_pass_storage_tests tests/contract/rhi_render_pass_storage_tests.cpp)
target_link_libraries(arp_rhi_render_pass_storage_tests PRIVATE arp_rhi arp_rhi_semantics)
target_include_directories(arp_rhi_render_pass_storage_tests PRIVATE .)
target_compile_options(arp_rhi_render_pass_storage_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_rhi_render_pass_storage_tests)
add_test(NAME arp_rhi_render_pass_storage_contract COMMAND arp_rhi_render_pass_storage_tests)
add_test(NAME arp_rhi_render_pass_storage_null_smoke COMMAND arp_rhi_render_pass_storage_tests --null-hidden-frame)
set_tests_properties(arp_rhi_render_pass_storage_null_smoke PROPERTIES LABELS "rhi;null-smoke")

target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_contract.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_scene_binding.cpp)

add_executable(arp_cloud_scene_binding_tests tests/contract/cloud_scene_binding_tests.cpp)
target_link_libraries(arp_cloud_scene_binding_tests PRIVATE arp_renderer arp_rhi)
target_include_directories(arp_cloud_scene_binding_tests PRIVATE .)
target_compile_options(arp_cloud_scene_binding_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_cloud_scene_binding_tests)
add_test(NAME arp_cloud_scene_binding_contract COMMAND arp_cloud_scene_binding_tests)
add_test(NAME arp_cloud_scene_binding_null_smoke COMMAND arp_cloud_scene_binding_tests --null-hidden-frame)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_execution_plan.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_quality.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_frame_parameters.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_prepared_history.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/detail/cloud_prepared_submission.cpp)

target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_prepared_pipelines.cpp)

target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_prepared_resources.cpp)

target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_prepared_descriptors.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/cloud_reference.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/volumetric_cloud_feature.cpp)
target_sources(arp_renderer PRIVATE src/renderer/volumetric_cloud/volumetric_cloud_prepared_graph.cpp)

set(cloud_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/volumetric_cloud")
set(cloud_shader_outputs)
set(cloud_shader_compiler_args)
if(SLANGC_EXECUTABLE)
    list(APPEND cloud_shader_compiler_args --slangc "${SLANGC_EXECUTABLE}")
endif()
foreach(cloud_shader cloud_raymarch cloud_temporal cloud_composite cloud_demo_present cloud_density_slice cloud_asset_slice
        cloud_spatial_resolve cloud_spatial_composite cloud_compact_resolve cloud_compact_composite)
    if(cloud_shader STREQUAL "cloud_demo_present")
        set(cloud_shader_source "shaders/samples/fog_demo_present.slang")
    else()
        set(cloud_shader_source "shaders/renderer/${cloud_shader}.slang")
    endif()
    set(cloud_reflection "${cloud_shader_dir}/${cloud_shader}.reflection.yaml")
    add_custom_command(OUTPUT "${cloud_reflection}"
        BYPRODUCTS "${cloud_shader_dir}/${cloud_shader}.spv"
            "${cloud_shader_dir}/${cloud_shader}.artifact-set.yaml"
            "${cloud_shader_dir}/${cloud_shader}.metal"
            "${cloud_shader_dir}/${cloud_shader}.wgsl"
            "${cloud_shader_dir}/${cloud_shader}.slang"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --module "${cloud_shader}" --out-dir "${cloud_shader_dir}" ${cloud_shader_compiler_args}
        DEPENDS "${cloud_shader_source}"
            shaders/renderer/cloud_ray_integration.slang shaders/renderer/cloud_history.slang
            shaders/renderer/cloud_composite_impl.slang shaders/renderer/cloud_sample_storage.slang shaders/renderer/cloud_spatial_resolve_impl.slang
            shaders/renderer/cloud_spatial_resolve.slang shaders/renderer/cloud_spatial_composite.slang
            shaders/renderer/cloud_common.slang shaders/renderer/volume/medium_integration.slang
            scripts/compile_shaders.py VERBATIM)
    list(APPEND cloud_shader_outputs "${cloud_reflection}")
endforeach()
add_custom_target(arp_volumetric_cloud_shaders DEPENDS ${cloud_shader_outputs})
if(TARGET arp_shader_tests)
    add_dependencies(arp_shader_tests arp_volumetric_cloud_shaders)
    target_compile_definitions(arp_shader_tests PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
endif()
target_sources(arp_assets PRIVATE src/assets/cloud/cloud_asset.cpp src/assets/cloud/cloud_asset_v3.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/assets/cloud/cloud_asset_v3.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off;-frounding-math"
        COMPILE_DEFINITIONS ARP_CLOUD_COOK_V3_PRECISION=1)
endif()
target_link_libraries(arp_assets PRIVATE arp_fkyaml_headers)
target_sources(arp_resource PRIVATE src/resource/cloud_asset_cooked.cpp)
target_sources(arp_resource PRIVATE src/resource/cloud_asset_resource.cpp)
target_sources(arp_3d PRIVATE src/3d/cloud_types.cpp src/3d/cloud_component.cpp)

add_executable(arp_cloud_asset_tool tools/cloud_asset/main.cpp)
target_link_libraries(arp_cloud_asset_tool PRIVATE arp_resource)
arp_add_mingw_static_runtime(arp_cloud_asset_tool)

add_executable(arp_cloud_cook_v3_tests tests/contract/cloud_cook_v3_tests.cpp)
target_link_libraries(arp_cloud_cook_v3_tests PRIVATE arp_resource)
target_compile_options(arp_cloud_cook_v3_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_cloud_cook_v3_tests)
add_test(NAME arp_cloud_cook_v3_contract COMMAND arp_cloud_cook_v3_tests)

if(NOT CMAKE_CROSSCOMPILING AND NOT EMSCRIPTEN AND NOT ARP_SCRIPT_AOT_ONLY_RELEASE)
    # A single shared engine owns reflection, allocator and plugin ABI state.
    add_executable(arp_cloud_dynamic_plugin_smoke tests/integration/cloud_dynamic_plugin_smoke.cpp)
    target_include_directories(arp_cloud_dynamic_plugin_smoke PRIVATE . src)
    target_link_libraries(arp_cloud_dynamic_plugin_smoke PRIVATE arp_engine arp_fkyaml_headers)
    target_compile_options(arp_cloud_dynamic_plugin_smoke PRIVATE -UNDEBUG)
    target_compile_definitions(arp_cloud_dynamic_plugin_smoke PRIVATE
        ARP_CLOUD_PLUGIN_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}"
        ARP_CLOUD_PLUGIN_HOST="$<TARGET_FILE:arp_engine>"
        ARP_CLOUD_PLUGIN_COMPILER="${CMAKE_CXX_COMPILER}"
        ARP_CLOUD_PLUGIN_CODEGEN="$<TARGET_FILE:arp_reflect_codegen>"
        ARP_CLOUD_PLUGIN_FKYAML="${ARP_FKYAML_ROOT}/include")
    add_dependencies(arp_cloud_dynamic_plugin_smoke arp_engine arp_reflect_codegen)
    # The production importer captures full include/compiler trees. Keep its
    # explicit test cache short for MinGW tools that still enforce MAX_PATH.
    set(ARP_CLOUD_PLUGIN_CACHE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/cp" CACHE PATH
        "Explicit cache root for the formal dynamic cloud plugin smoke")
    add_test(NAME arp_cloud_dynamic_plugin_smoke COMMAND arp_cloud_dynamic_plugin_smoke
        "${ARP_CLOUD_PLUGIN_CACHE_ROOT}")
    set_tests_properties(arp_cloud_dynamic_plugin_smoke PROPERTIES TIMEOUT 600 LABELS "cloud;plugin;smoke")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/ArpCloudPreparedShaders.cmake")
add_executable(arp_volumetric_cloud examples/volumetric_cloud/main.cpp
    examples/volumetric_cloud/prepared_offscreen.cpp src/platform/common/renderdoc_capture_scope.cpp)
add_dependencies(arp_volumetric_cloud arp_rhi_original_cloud_graphics_module
    arp_rhi_original_cloud_spatial_graphics_module arp_rhi_original_cloud_compact_graphics_module arp_rhi_original_cloud_compute_entries)
target_compile_definitions(arp_volumetric_cloud PRIVATE
    ARP_CLOUD_PREPARED_COMPUTE_DIR="${original_cloud_compute_dir}"
    ARP_CLOUD_PREPARED_GRAPHICS_DIR="${original_cloud_module_dir}")
target_link_libraries(arp_volumetric_cloud PRIVATE arp_renderer arp_platform ${ARP_RENDERER_RHI_LINK_LIBS})
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_definitions(arp_volumetric_cloud PRIVATE ARP_CLOUD_SHADER_DIR="shaders/volumetric_cloud")
else()
    target_compile_definitions(arp_volumetric_cloud PRIVATE ARP_CLOUD_SHADER_DIR="${cloud_shader_dir}")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(ARP_CLOUD_ASSET_TOOL_HOST "" CACHE FILEPATH "Native cloud cooker for the fixed browser visual fixture")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/cloud-visual.cooked.yaml"
        COMMAND "${CMAKE_COMMAND}" "-DcloudAssetTool=${ARP_CLOUD_ASSET_TOOL_HOST}"
            "-DcloudAssetRecipe=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cloud/scene-reference-v3.cook.yaml"
            "-DcloudAssetOutput=${CMAKE_CURRENT_BINARY_DIR}/cloud-visual.cooked.yaml"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArpCookCloudBrowserAsset.cmake"
        DEPENDS "${ARP_CLOUD_ASSET_TOOL_HOST}" tests/fixtures/cloud/scene-reference-v3.cook.yaml
            cmake/ArpCookCloudBrowserAsset.cmake VERBATIM)
    add_custom_target(arp_cloud_browser_asset DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/cloud-visual.cooked.yaml")
    add_dependencies(arp_volumetric_cloud arp_cloud_browser_asset)
    target_link_options(arp_volumetric_cloud PRIVATE
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_storage_format_probe.js"
        "--pre-js=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgpu_device_bootstrap_pre.js"
        "--preload-file=${cloud_shader_dir}@shaders/volumetric_cloud"
        "--preload-file=${CMAKE_CURRENT_BINARY_DIR}/cloud-visual.cooked.yaml@cloud-reference.cooked.yaml"
        -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=536870912
        -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sEXIT_RUNTIME=1)
    set_property(TARGET arp_volumetric_cloud APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/volumetric_cloud/index.html"
        "${CMAKE_CURRENT_BINARY_DIR}/cloud-visual.cooked.yaml")
    set(ARP_CLOUD_VISUAL_ORACLE_HOST "" CACHE FILEPATH "Native CPU oracle for cloud browser readback acceptance")
    if(ARP_WEBGPU_BROWSER_EXECUTABLE AND ARP_CLOUD_VISUAL_ORACLE_HOST)
        add_test(NAME arp_cloud_webgpu_visual COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_browser_visual_tests.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/cloud-browser-visual"
            --oracle "${ARP_CLOUD_VISUAL_ORACLE_HOST}"
            --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-visual.cooked.yaml")
        set_tests_properties(arp_cloud_webgpu_visual PROPERTIES TIMEOUT 900)
        add_test(NAME arp_cloud_webgpu_ordinary_refusal COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_browser_visual_tests.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/cloud-browser-ordinary-refusal"
            --oracle "${ARP_CLOUD_VISUAL_ORACLE_HOST}"
            --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-visual.cooked.yaml" --ordinary --cases below occlusion)
        set_tests_properties(arp_cloud_webgpu_ordinary_refusal PROPERTIES TIMEOUT 180)
    endif()
    add_custom_command(TARGET arp_volumetric_cloud POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/volumetric_cloud/index.html"
        "$<TARGET_FILE_DIR:arp_volumetric_cloud>/arp_volumetric_cloud.html")
endif()
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
    add_test(NAME arp_volumetric_cloud_v3_cook_smoke COMMAND arp_cloud_asset_tool cook
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cloud/reference-v3.cook.yaml"
        "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference-v3.cooked.yaml" 4194304)
    set_tests_properties(arp_volumetric_cloud_v3_cook_smoke PROPERTIES FIXTURES_SETUP cloud_reference_v3_cook)
    add_test(NAME arp_volumetric_cloud_example_null COMMAND arp_volumetric_cloud --backend null --frames 3
        --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml")
    set_tests_properties(arp_volumetric_cloud_example_null PROPERTIES FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 60)
    add_test(NAME arp_cloud_visual_reference_null_smoke
        COMMAND arp_volumetric_cloud --backend null --frames 3 --reference-case occlusion
            --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference-v3.cooked.yaml")
    set_tests_properties(arp_cloud_visual_reference_null_smoke PROPERTIES
        FIXTURES_REQUIRED cloud_reference_v3_cook TIMEOUT 60)
    add_test(NAME arp_cloud_prepared_demo_contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_prepared_demo_tests.py"
            --exe "$<TARGET_FILE:arp_volumetric_cloud>"
            --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml")
    set_tests_properties(arp_cloud_prepared_demo_contract PROPERTIES
        FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 300)
    if(TARGET arp_rhi_vulkan AND NOT APPLE)
        foreach(scene_mode id alias)
            add_test(NAME arp_cloud_scene_demo_${scene_mode}_native
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_prepared_demo_tests.py"
                    --exe "$<TARGET_FILE:arp_volumetric_cloud>" --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml"
                    --native compact --timings --host-device --scene-mode ${scene_mode})
            set_tests_properties(arp_cloud_scene_demo_${scene_mode}_native PROPERTIES
                FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 180 LABELS "cloud;scene;native-smoke")
        endforeach()
        add_test(NAME arp_cloud_prepared_demo_compact_native
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_prepared_demo_tests.py"
                --exe "$<TARGET_FILE:arp_volumetric_cloud>" --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml"
                --native compact --timings --host-device)
        set_tests_properties(arp_cloud_prepared_demo_compact_native PROPERTIES FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 180)
        foreach(resolve temporal reconstruction)
            add_test(NAME arp_cloud_prepared_demo_${resolve}_native
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_prepared_demo_tests.py"
                    --exe "$<TARGET_FILE:arp_volumetric_cloud>"
                    --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml" --native ${resolve})
            set_tests_properties(arp_cloud_prepared_demo_${resolve}_native PROPERTIES
                FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 180 LABELS "cloud;native-smoke")
            add_test(NAME arp_cloud_prepared_demo_${resolve}_timing_native
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_prepared_demo_tests.py"
                    --exe "$<TARGET_FILE:arp_volumetric_cloud>"
                    --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml" --native ${resolve} --timings)
            set_tests_properties(arp_cloud_prepared_demo_${resolve}_timing_native PROPERTIES
                FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 180 LABELS "cloud;native-smoke;timing")
            add_test(NAME arp_cloud_prepared_demo_${resolve}_host_device_native
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/cloud_prepared_demo_tests.py"
                    --exe "$<TARGET_FILE:arp_volumetric_cloud>"
                    --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml" --native ${resolve} --timings --host-device)
            set_tests_properties(arp_cloud_prepared_demo_${resolve}_host_device_native PROPERTIES
                FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 180 LABELS "cloud;native-smoke;placement")
        endforeach()
    endif()
    foreach(quality reference high medium)
        add_test(NAME arp_volumetric_cloud_example_quality_${quality}_null
            COMMAND arp_volumetric_cloud --backend null --frames 3 --width 64 --height 48
                --quality ${quality} --asset "${CMAKE_CURRENT_BINARY_DIR}/cloud-reference.cooked.yaml")
        set_tests_properties(arp_volumetric_cloud_example_quality_${quality}_null
            PROPERTIES FIXTURES_REQUIRED cloud_reference_cook TIMEOUT 60)
    endforeach()
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

add_executable(arp_cloud_prepared_history_tests tests/contract/cloud_prepared_history_tests.cpp)
target_link_libraries(arp_cloud_prepared_history_tests PRIVATE arp_renderer arp_rhi)
target_include_directories(arp_cloud_prepared_history_tests PRIVATE .)
target_compile_options(arp_cloud_prepared_history_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_cloud_prepared_history_tests)
add_test(NAME arp_cloud_prepared_history_contract COMMAND arp_cloud_prepared_history_tests)
add_executable(arp_cloud_compact_history_tests tests/contract/cloud_compact_history_tests.cpp)
target_link_libraries(arp_cloud_compact_history_tests PRIVATE arp_renderer arp_rhi)
target_include_directories(arp_cloud_compact_history_tests PRIVATE .)
target_compile_options(arp_cloud_compact_history_tests PRIVATE -UNDEBUG)
target_compile_definitions(arp_cloud_compact_history_tests PRIVATE ARP_CLOUD_TEST_PUBLICATION_ROOT="${CMAKE_CURRENT_BINARY_DIR}")
add_dependencies(arp_cloud_compact_history_tests arp_rhi_original_cloud_compute_entries
    arp_rhi_original_cloud_spatial_graphics_module arp_rhi_original_cloud_compact_graphics_module)
arp_add_mingw_static_runtime(arp_cloud_compact_history_tests)
add_test(NAME arp_cloud_compact_history_contract COMMAND arp_cloud_compact_history_tests)
add_test(NAME arp_cloud_compact_history_null_smoke COMMAND arp_cloud_compact_history_tests --null-hidden-frame)

add_dependencies(arp_cloud_prepared_history_tests arp_rhi_original_cloud_compute_entries
    arp_rhi_original_cloud_graphics_module arp_rhi_original_cloud_spatial_graphics_module arp_cloud_foreground_module)
add_test(NAME arp_cloud_ordinary_native_memory_contract
    COMMAND arp_cloud_prepared_history_tests --ordinary-native-memory "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME arp_cloud_spatial_motion_reference
    COMMAND arp_cloud_prepared_history_tests --spatial-motion-reference)
set_tests_properties(arp_cloud_spatial_motion_reference PROPERTIES TIMEOUT 180)
add_test(NAME arp_cloud_quality_integration_cpu
    COMMAND arp_cloud_prepared_history_tests --quality-integration-probe)
set_tests_properties(arp_cloud_quality_integration_cpu PROPERTIES TIMEOUT 300)
add_test(NAME arp_cloud_quality_reconstruction_cpu
    COMMAND arp_cloud_prepared_history_tests --quality-reconstruction-probe)
set_tests_properties(arp_cloud_quality_reconstruction_cpu PROPERTIES TIMEOUT 300)
foreach(camera inside above)
    add_test(NAME arp_cloud_quality_reconstruction_cpu_${camera}
        COMMAND arp_cloud_prepared_history_tests --quality-reconstruction-probe ${camera})
    set_tests_properties(arp_cloud_quality_reconstruction_cpu_${camera} PROPERTIES TIMEOUT 300)
endforeach()

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

target_sources(arp_renderer PRIVATE src/renderer/pipeline/scene_opaque_producer.cpp
    src/renderer/pipeline/scene_opaque_cloud_composition.cpp
    src/renderer/pipeline/scene_pbr_producer.cpp
    src/renderer/pipeline/scene_pbr_texture_publication.cpp
    src/renderer/pipeline/scene_pbr_cloud_composition.cpp)
add_executable(arp_scene_opaque_producer_tests tests/contract/scene_opaque_producer_tests.cpp)
target_link_libraries(arp_scene_opaque_producer_tests PRIVATE arp_renderer arp_rhi arp_scriptvm_runtime)
target_include_directories(arp_scene_opaque_producer_tests PRIVATE .)
target_compile_options(arp_scene_opaque_producer_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_scene_opaque_producer_tests)
add_test(NAME arp_scene_opaque_producer_contract COMMAND arp_scene_opaque_producer_tests)
add_test(NAME arp_scene_opaque_producer_null_smoke COMMAND arp_scene_opaque_producer_tests --null-hidden-frame)

add_executable(arp_scene_pbr_producer_tests tests/contract/scene_pbr_producer_tests.cpp)
target_link_libraries(arp_scene_pbr_producer_tests PRIVATE arp_renderer arp_rhi arp_scriptvm_runtime)
target_include_directories(arp_scene_pbr_producer_tests PRIVATE .)
target_compile_options(arp_scene_pbr_producer_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_scene_pbr_producer_tests)
add_test(NAME arp_scene_pbr_producer_contract COMMAND arp_scene_pbr_producer_tests)
add_test(NAME arp_scene_pbr_producer_null_smoke COMMAND arp_scene_pbr_producer_tests --null-hidden-frame)

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(arp_cloud_visual_readback_oracle tests/tools/cloud_visual_readback_oracle.cpp)
    target_include_directories(arp_cloud_visual_readback_oracle PRIVATE .)
    target_link_libraries(arp_cloud_visual_readback_oracle PRIVATE arp_renderer)
    target_compile_options(arp_cloud_visual_readback_oracle PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_cloud_visual_readback_oracle)
endif()

add_custom_command(OUTPUT "${cloud_shader_dir}/cloud_foreground.reflection.yaml"
    BYPRODUCTS "${cloud_shader_dir}/cloud_foreground.artifact-set.yaml"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
        --fixture cloud_foreground --out-dir "${cloud_shader_dir}" ${cloud_shader_compiler_args}
    DEPENDS tests/fixtures/cloud_foreground.slang scripts/compile_shaders.py VERBATIM)
add_custom_target(arp_cloud_visual_foreground DEPENDS "${cloud_shader_dir}/cloud_foreground.reflection.yaml")
add_dependencies(arp_volumetric_cloud arp_cloud_visual_foreground)
