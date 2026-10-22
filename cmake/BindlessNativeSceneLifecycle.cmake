include_guard(GLOBAL)
if(NOT TARGET arp_bindless_pbr_geometry_fixture OR NOT TARGET arp_bindless_terrain_geometry_fixture)
    return()
endif()
add_test(NAME arp_bindless_scene_lifecycle_report_tests COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/contract/bindless_scene_lifecycle_report_tests.py")
foreach(backend vulkan metal)
    if(backend STREQUAL "metal" AND NOT APPLE)
        continue()
    endif()
    # Follow the existing native test targets so a host stub never becomes a
    # platform validation target merely because its backend library exists.
    if(NOT TARGET arp_rhi_${backend}_bindless_graphics_wocc_ibl_tests)
        continue()
    endif()
    set(scene_target arp_rhi_${backend}_bindless_scene_lifecycle_tests)
    add_executable(${scene_target} tests/integration/rhi_native_bindless_scene_lifecycle_tests.cpp)
    if(backend STREQUAL "metal")
        target_sources(${scene_target} PRIVATE tests/fixtures/bindless_scene_attachment_fixture.mm)
    endif()
    target_include_directories(${scene_target} PRIVATE . src)
    target_link_libraries(${scene_target} PRIVATE arp_rhi_${backend})
    string(TOUPPER "${backend}" scene_backend)
    target_compile_definitions(${scene_target} PRIVATE ARP_BINDLESS_SCENE_${scene_backend}=1)
    if(backend STREQUAL "vulkan" AND WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(${scene_target} PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(${scene_target} PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    # The assertion-enabled fixture authenticates the full cold ownership graph
    # repeatedly. Optimize this test TU (as the browser fixture already does)
    # while retaining every assertion, case and production-library build mode.
    target_compile_options(${scene_target} PRIVATE -O1 -UNDEBUG)
    add_dependencies(${scene_target} arp_bindless_pbr_geometry_fixture arp_bindless_terrain_geometry_fixture)
    foreach(family pbr terrain)
        if(family STREQUAL "pbr")
            set(scene_package "${bindless_pbr_geometry_dir}/PbrGeometryFamily.native.bindless.yaml")
        else()
            set(scene_package "${bindless_terrain_geometry_dir}/TerrainGeometryFamily.native.bindless.yaml")
        endif()
        set(scene_test arp_rhi_${backend}_bindless_${family}_scene_lifecycle_tests)
        add_test(NAME ${scene_test} COMMAND ${scene_target} "${scene_package}" "${family}")
        # Full Debug matrices rebuild and authenticate every original Root per
        # case (up to 45 PBR / 27 terrain cases). Preserve all cases; each GPU
        # completion still has its separate 20-second watchdog.
        set_tests_properties(${scene_test} PROPERTIES TIMEOUT 3600 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        if(backend STREQUAL "metal")
            set_tests_properties(${scene_test} PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")
        endif()
    endforeach()
endforeach()
if(EMSCRIPTEN AND TARGET arp_rhi_webgpu_original_bindless_root_browser_smoke)
    # Compile the same scenario loop without unrelated historical workloads.
    # Keep its assertions, public preparation, callback custody and HTML oracle;
    # only the set of instantiated test entry points differs. This bounds the
    # Asyncify linker footprint on development hosts.
    set(scene_browser arp_rhi_webgpu_bindless_scene_browser_smoke)
    set(scene_runner_dir "${CMAKE_BINARY_DIR}/bindless-scene-runner")
    add_executable(${scene_browser} tests/integration/rhi_webgpu_original_bindless_root_browser_smoke.cpp)
    set_target_properties(${scene_browser} PROPERTIES
        OUTPUT_NAME arp_rhi_webgpu_original_bindless_root_browser_smoke
        RUNTIME_OUTPUT_DIRECTORY "${scene_runner_dir}")
    target_link_libraries(${scene_browser} PRIVATE arp_rhi_webgpu arp_rhi)
    target_compile_definitions(${scene_browser} PRIVATE ARP_BINDLESS_WEBGPU_SCENE_ONLY=1)
    target_compile_options(${scene_browser} PRIVATE -O1 -UNDEBUG --use-port=emdawnwebgpu)
    # Asyncify expands the assertion-enabled ownership graph substantially.
    # Emscripten only runs Binaryen's optimizer at link -O2 or above; -O1 on
    # the test TU alone leaves enormous cold functions for the browser JIT.
    # Reset inherited -g before keeping function names (-g2) for attribution:
    # this Emscripten retains GENERATE_DWARF after plain -g followed by -g2.
    # Embedded DWARF
    # disables important Binaryen optimizations; source objects retain their
    # Debug information. Assertions, exceptions and every case remain enabled.
    target_link_options(${scene_browser} PRIVATE
        -O2 -g0 -g2
        "--pre-js=${CMAKE_SOURCE_DIR}/tests/fixtures/webgpu_storage_format_probe.js"
        "--pre-js=${CMAKE_SOURCE_DIR}/tests/fixtures/webgpu_device_bootstrap_pre.js"
        -sASYNCIFY "-sASYNCIFY_STACK_SIZE=${ARP_BINDLESS_WEBGPU_ROOT_ASYNCIFY_STACK_SIZE}"
        -sSTACK_OVERFLOW_CHECK=2 -sASSERTIONS=2
        "-sSTACK_SIZE=${ARP_BINDLESS_WEBGPU_ROOT_STACK_SIZE}"
        -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=67108864)
    set(scene_runner_files)
    foreach(source IN ITEMS tests/fixtures/bindless_webgpu_original_root_runner.html
            scripts/browser_progress_transport.js tests/fixtures/bindless_implicit_reference.js)
        get_filename_component(name "${source}" NAME)
        set(output "${scene_runner_dir}/${name}")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${scene_runner_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${source}" "${output}"
            DEPENDS "${CMAKE_SOURCE_DIR}/${source}" VERBATIM)
        list(APPEND scene_runner_files "${output}")
    endforeach()
    add_custom_target(arp_bindless_webgpu_scene_runner_files DEPENDS ${scene_runner_files})
    add_dependencies(${scene_browser} arp_bindless_webgpu_scene_runner_files)
    add_dependencies(${scene_browser} arp_bindless_pbr_geometry_fixture arp_bindless_terrain_geometry_fixture)
    target_link_options(${scene_browser} PRIVATE
        "--preload-file=${bindless_pbr_geometry_dir}@/bindless-pbr-geometry"
        "--preload-file=${bindless_terrain_geometry_dir}@/bindless-terrain-geometry")
    set_property(TARGET ${scene_browser} APPEND PROPERTY LINK_DEPENDS
        ${bindless_pbr_geometry_outputs} ${bindless_terrain_geometry_outputs}
        "${CMAKE_SOURCE_DIR}/tests/fixtures/webgpu_storage_format_probe.js"
        "${CMAKE_SOURCE_DIR}/tests/fixtures/webgpu_device_bootstrap_pre.js")
    if(ARP_WEBGPU_BROWSER_EXECUTABLE)
        foreach(family pbr terrain)
            add_test(NAME arp_rhi_webgpu_bindless_${family}_scene_lifecycle_browser_smoke
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/contract/bindless_webgpu_rhi_smoke.py"
                    --browser "${ARP_WEBGPU_BROWSER_EXECUTABLE}" --runner "${scene_runner_dir}"
                    --package-root "${CMAKE_BINARY_DIR}"
                    --original-bindless-root-preparation --original-root-scene-family "${family}"
                    --timeout-seconds 3600
                    --output "${CMAKE_BINARY_DIR}/artifacts/bindless-${family}-scene-lifecycle")
            set_tests_properties(arp_rhi_webgpu_bindless_${family}_scene_lifecycle_browser_smoke
                # Same explicit full-matrix Debug budget as Native. Genuine
                # callback drain shares the host's original absolute deadline;
                # no per-wait reset or failure-case removal is permitted.
                PROPERTIES TIMEOUT 3660 RUN_SERIAL TRUE)
        endforeach()
    endif()
endif()
