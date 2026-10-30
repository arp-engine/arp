# Existing resource consumers exercise the real FileService/AsyncRuntime path.
if(SLANGC_EXECUTABLE AND SPIRV_REFLECT_EXECUTABLE)
    set(arp_contract_shader_outputs)
    foreach(module IN ITEMS rhi_push_contract rhi_pipeline_contract null_hidden_frame_smoke)
        foreach(extension IN ITEMS slang spv metal wgsl reflection.yaml artifact-set.yaml)
            list(APPEND arp_contract_shader_outputs "${CMAKE_BINARY_DIR}/${module}.${extension}")
        endforeach()
    endforeach()
    add_custom_command(OUTPUT ${arp_contract_shader_outputs}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/fixtures/compile_shader_contract_fixtures.py"
            --output "${CMAKE_BINARY_DIR}" --slangc "${SLANGC_EXECUTABLE}"
            --spirv-reflect "${SPIRV_REFLECT_EXECUTABLE}"
        DEPENDS tests/fixtures/compile_shader_contract_fixtures.py tests/fixtures/rhi_push_contract.slang
            tests/fixtures/null_hidden_frame_smoke.slang scripts/compile_shaders.py scripts/shader_bindings_codegen.py
        VERBATIM)
    add_custom_target(arp_generated_contract_shader_fixtures DEPENDS ${arp_contract_shader_outputs})
    foreach(target IN ITEMS arp_rhi_null_hidden_frame_smoke_tests arp_rhi_pipeline_contract_tests
        arp_rhi_command_contract_tests arp_rhi_dynamic_viewport_scissor_contract_tests
        arp_rhi_depth_bias_clamp_contract_tests arp_rhi_webgl_profile_contract_tests arp_render_graph_tests
        arp_rhi_cache_budget_tests arp_rhi_bindless_contract_tests)
        if(TARGET ${target})
            add_dependencies(${target} arp_generated_contract_shader_fixtures)
            if(EMSCRIPTEN)
                foreach(output IN LISTS arp_contract_shader_outputs)
                    get_filename_component(name "${output}" NAME)
                    target_link_options(${target} PRIVATE "SHELL:--preload-file \"${output}@/${name}\"")
                endforeach()
            endif()
        endif()
    endforeach()
endif()

add_test(NAME arp_generated_shader_publication_tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/tooling/generated_shader_publication_tests.py")
set_tests_properties(arp_generated_shader_publication_tests PROPERTIES TIMEOUT 30 LABELS "shader;io;tooling")
add_executable(arp_generated_shader_artifact_set_tests tests/contract/generated_shader_artifact_set_tests.cpp)
target_include_directories(arp_generated_shader_artifact_set_tests PRIVATE . src)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_generated_shader_artifact_set_tests PRIVATE arp_renderer arp_rhi arp_rhi_semantics arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
else()
    target_link_libraries(arp_generated_shader_artifact_set_tests PRIVATE
        arp_renderer "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
        "$<LINK_GROUP:RESCAN,arp_rhi,arp_rhi_semantics>")
endif()
if(NOT MSVC)
    target_compile_options(arp_generated_shader_artifact_set_tests PRIVATE -UNDEBUG)
endif()
arp_add_mingw_static_runtime(arp_generated_shader_artifact_set_tests)
if(NOT EMSCRIPTEN AND TARGET arp_graphics_shader_module_fixture)
    add_dependencies(arp_generated_shader_artifact_set_tests arp_graphics_shader_module_fixture)
    foreach(mode IN ITEMS memory quick http)
        set(check arp_generated_shader_artifact_set_tests)
        set(extra)
        if(mode STREQUAL quick)
            set(check arp_generated_shader_artifact_set_quick_tests)
            set(extra --quick)
        elseif(mode STREQUAL http)
            set(check arp_generated_shader_artifact_set_http_smoke)
            set(extra --http)
        endif()
        add_test(NAME ${check} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/contract/generated_shader_artifact_set_tests.py"
            --executable "$<TARGET_FILE:arp_generated_shader_artifact_set_tests>"
            --fixture "${CMAKE_BINARY_DIR}/graphics-module-fixture" ${extra})
        set_tests_properties(${check} PROPERTIES TIMEOUT 180 LABELS "shader;io;resource")
    endforeach()
endif()
set(ARP_GENERATED_SHADER_SET_FIXTURE_ROOT "" CACHE PATH "Explicit ordinary compiler artifact fixture for Wasm")
if(EMSCRIPTEN AND ARP_GENERATED_SHADER_SET_FIXTURE_ROOT)
    target_link_options(arp_generated_shader_artifact_set_tests PRIVATE
        -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=67108864
        "SHELL:--preload-file \"${ARP_GENERATED_SHADER_SET_FIXTURE_ROOT}@/generated-set-fixture\"")
    add_test(NAME arp_generated_shader_artifact_set_tests
        COMMAND arp_generated_shader_artifact_set_tests --memory /generated-set-fixture)
    add_test(NAME arp_generated_shader_artifact_set_quick_tests
        COMMAND arp_generated_shader_artifact_set_tests --memory /generated-set-fixture --quick)
    set_tests_properties(arp_generated_shader_artifact_set_tests arp_generated_shader_artifact_set_quick_tests
        PROPERTIES TIMEOUT 180 LABELS "shader;io;node")
endif()

foreach(source IN ITEMS tests/contract/ui_document_source_tests.cpp
    tests/contract/ui_document_input_tests.cpp
    tests/integration/ui_text_layout_cache_tests.cpp
    tests/integration/ui_atlas_runtime_cache_tests.cpp
    tests/integration/ui_resource_recovery_plan_tests.cpp
    tests/contract/ui_text_shaping_contract_tests.cpp
    tests/contract/ui_font_source_tests.cpp
    tests/contract/ui_atlas_image_source_tests.cpp
    tests/contract/ui_metadata_source_tests.cpp tests/integration/scene_ui_document_component_tests.cpp)
    get_filename_component(name "${source}" NAME_WE)
    set(target "arp_${name}")
    add_executable(${target} "${source}")
    target_include_directories(${target} PRIVATE . src)
    if(EMSCRIPTEN OR APPLE)
        target_link_libraries(${target} PRIVATE arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
    else()
        target_link_libraries(${target} PRIVATE "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>")
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    arp_add_mingw_static_runtime(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 60 LABELS "ui;io;resource")
endforeach()
set_tests_properties(arp_scene_ui_document_component_tests PROPERTIES LABELS "ui;io;scene")

if(NOT EMSCRIPTEN)
    add_executable(arp_ui_atlas_metadata_resource_tests
        tests/integration/ui_atlas_metadata_resource_tests.cpp)
    target_include_directories(arp_ui_atlas_metadata_resource_tests PRIVATE . src)
    if(APPLE)
        target_link_libraries(arp_ui_atlas_metadata_resource_tests PRIVATE
            arp_ui_runtime arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
    else()
        target_link_libraries(arp_ui_atlas_metadata_resource_tests PRIVATE arp_ui_runtime
            "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>")
    endif()
    target_compile_options(arp_ui_atlas_metadata_resource_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_ui_atlas_metadata_resource_tests)
    add_test(NAME arp_ui_atlas_metadata_resource_tests COMMAND arp_ui_atlas_metadata_resource_tests)
    set_tests_properties(arp_ui_atlas_metadata_resource_tests PROPERTIES TIMEOUT 60 LABELS "ui;io;resource")
endif()

add_executable(arp_ui_startup_bundle_resource_tests
    tests/integration/ui_startup_bundle_resource_tests.cpp)
target_include_directories(arp_ui_startup_bundle_resource_tests PRIVATE . src)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_ui_startup_bundle_resource_tests PRIVATE
        arp_renderer arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
else()
    target_link_libraries(arp_ui_startup_bundle_resource_tests PRIVATE
        arp_renderer
        "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
        "$<LINK_GROUP:RESCAN,arp_rhi,arp_rhi_semantics>")
endif()
if(NOT MSVC)
    target_compile_options(arp_ui_startup_bundle_resource_tests PRIVATE -UNDEBUG)
endif()
arp_add_mingw_static_runtime(arp_ui_startup_bundle_resource_tests)
add_test(NAME arp_ui_startup_bundle_resource_tests COMMAND arp_ui_startup_bundle_resource_tests)
set_tests_properties(arp_ui_startup_bundle_resource_tests PROPERTIES TIMEOUT 60 LABELS "ui;io;resource")

add_executable(arp_ui_resource_reload_tests
    tests/contract/ui_resource_reload_tests.cpp)
target_include_directories(arp_ui_resource_reload_tests PRIVATE . src)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_ui_resource_reload_tests PRIVATE
        arp_renderer arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
else()
    target_link_libraries(arp_ui_resource_reload_tests PRIVATE
        arp_renderer
        "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
        "$<LINK_GROUP:RESCAN,arp_rhi,arp_rhi_semantics>")
endif()
if(NOT MSVC)
    target_compile_options(arp_ui_resource_reload_tests PRIVATE -UNDEBUG)
endif()
arp_add_mingw_static_runtime(arp_ui_resource_reload_tests)
add_test(NAME arp_ui_resource_reload_tests COMMAND arp_ui_resource_reload_tests)
set_tests_properties(arp_ui_resource_reload_tests PROPERTIES TIMEOUT 60 LABELS "ui;io;resource")

add_test(NAME arp_ui_resource_reload_quick_tests COMMAND arp_ui_resource_reload_tests --quick)
set_tests_properties(arp_ui_resource_reload_quick_tests PROPERTIES TIMEOUT 30 LABELS "ui;io;resource;quick")

add_executable(arp_bindless_package_source_tests tests/contract/bindless_package_source_tests.cpp)
target_include_directories(arp_bindless_package_source_tests PRIVATE . src)
if(EMSCRIPTEN OR APPLE)
    target_link_libraries(arp_bindless_package_source_tests PRIVATE arp_renderer
        arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
else()
    target_link_libraries(arp_bindless_package_source_tests PRIVATE arp_renderer
        "$<LINK_GROUP:RESCAN,arp_ui_runtime,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>"
        "$<LINK_GROUP:RESCAN,arp_rhi,arp_rhi_semantics>")
endif()
if(NOT MSVC)
    target_compile_options(arp_bindless_package_source_tests PRIVATE -UNDEBUG)
endif()
arp_add_mingw_static_runtime(arp_bindless_package_source_tests)

if(NOT EMSCRIPTEN AND TARGET arp_bindless_prepared_fixture)
    add_dependencies(arp_bindless_package_source_tests arp_bindless_prepared_fixture)
    foreach(mode IN ITEMS memory http)
        set(extra)
        set(check arp_bindless_package_source_tests)
        if(mode STREQUAL "http")
            set(extra --http)
            set(check arp_bindless_package_source_http_smoke)
        endif()
        add_test(NAME ${check} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/contract/bindless_package_source_tests.py"
            --executable "$<TARGET_FILE:arp_bindless_package_source_tests>"
            --fixture "${ARP_BINDLESS_PREPARED_FIXTURE_DIR}" ${extra})
        set_tests_properties(${check} PROPERTIES TIMEOUT 240 LABELS "shader;io;resource")
    endforeach()
    add_test(NAME arp_bindless_package_source_quick_tests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/contract/bindless_package_source_tests.py"
        --executable "$<TARGET_FILE:arp_bindless_package_source_tests>"
        --fixture "${ARP_BINDLESS_PREPARED_FIXTURE_DIR}" --quick)
    set_tests_properties(arp_bindless_package_source_quick_tests PROPERTIES TIMEOUT 30 LABELS "shader;io;quick")
endif()
set(ARP_UNIFIED_IO_BINDLESS_FIXTURE_ROOT "" CACHE PATH "Explicit generated fixture directory for Wasm source tests")
if(EMSCRIPTEN AND ARP_UNIFIED_IO_BINDLESS_FIXTURE_ROOT)
    # Fixed 64MiB fixture arena includes the 32MiB CPU cap, finite 16MiB byte
    # ledger and allocator/Node runtime space. No runtime heap growth.
    target_link_options(arp_bindless_package_source_tests PRIVATE
        -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=67108864
        "SHELL:--preload-file \"${ARP_UNIFIED_IO_BINDLESS_FIXTURE_ROOT}@/bindless-io-fixture\"")
    add_test(NAME arp_bindless_package_source_tests
        COMMAND arp_bindless_package_source_tests --memory /bindless-io-fixture)
    add_test(NAME arp_bindless_package_source_quick_tests
        COMMAND arp_bindless_package_source_tests --memory /bindless-io-fixture --quick)
    set_tests_properties(arp_bindless_package_source_quick_tests PROPERTIES TIMEOUT 30 LABELS "shader;io;node;quick")
    set_tests_properties(arp_bindless_package_source_tests PROPERTIES TIMEOUT 240 LABELS "shader;io;node")
endif()

set(arp_font_io_fixture "${CMAKE_CURRENT_SOURCE_DIR}/3rd/box2d-3.1.1/samples/data/droid_sans.ttf")
if(NOT EXISTS "${arp_font_io_fixture}")
    message(FATAL_ERROR "Unified IO font fixture is missing")
endif()
foreach(target IN ITEMS arp_ui_font_source_tests arp_ui_text_layout_cache_tests
    arp_ui_atlas_runtime_cache_tests arp_ui_resource_recovery_plan_tests
    arp_ui_text_shaping_contract_tests arp_ui_renderer_integration_tests
    arp_ui_text_rendering_contract_tests arp_ui_startup_bundle_resource_tests
    arp_ui_resource_reload_tests)
    if(EMSCRIPTEN)
        target_compile_definitions(${target} PRIVATE ARP_FONT_IO_FIXTURE_PATH="/font-io-fixture.ttf")
        target_link_options(${target} PRIVATE "SHELL:--preload-file \"${arp_font_io_fixture}@/font-io-fixture.ttf\"")
    else()
        target_compile_definitions(${target} PRIVATE ARP_FONT_IO_FIXTURE_PATH="${arp_font_io_fixture}")
    endif()
endforeach()

# This existing decoder consumer reaches resource reflection registration.
# Keep its static link closure consistent with the resource contract tests.
if(TARGET arp_scene_2d_resource_tests)
    if(EMSCRIPTEN OR APPLE)
        target_link_libraries(arp_scene_2d_resource_tests PRIVATE arp_scriptvm_host_plugin_runtime)
    else()
        target_link_libraries(arp_scene_2d_resource_tests PRIVATE "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>")
    endif()
endif()

foreach(source IN ITEMS
    tests/contract/resource_async_file_service_contract_tests.cpp
    tests/integration/resource_graph_executor_tests.cpp
    tests/contract/resource_staged_source_tests.cpp
    tests/contract/resource_graph_admission_tests.cpp
    tests/contract/resource_prefab_source_tests.cpp
    tests/contract/resource_shader_source_tests.cpp
    tests/contract/resource_failure_matrix_tests.cpp
    tests/smoke/resource_async_load_smoke.cpp)
    get_filename_component(name "${source}" NAME_WE)
    set(target "arp_${name}")
    if(NOT TARGET ${target})
        add_executable(${target} "${source}")
        target_include_directories(${target} PRIVATE . src)
        if(EMSCRIPTEN OR APPLE)
            target_link_libraries(${target} PRIVATE arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
        else()
            target_link_libraries(${target} PRIVATE "$<LINK_GROUP:RESCAN,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>")
        endif()
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        arp_add_mingw_static_runtime(${target})
        if(NOT name STREQUAL "resource_shader_source_tests")
            add_test(NAME ${target} COMMAND ${target})
            set_tests_properties(${target} PROPERTIES TIMEOUT 60 LABELS "resource;io")
        endif()
    endif()
endforeach()

add_test(NAME arp_resource_manager_owner_admission_tests
    COMMAND arp_resource_graph_admission_tests --manager)
add_test(NAME arp_resource_manager_owner_admission_quick_tests
    COMMAND arp_resource_graph_admission_tests --manager-quick)
set_tests_properties(arp_resource_manager_owner_admission_tests
    arp_resource_manager_owner_admission_quick_tests PROPERTIES TIMEOUT 60 LABELS "resource;io;owner")
set_tests_properties(arp_resource_manager_owner_admission_quick_tests
    PROPERTIES LABELS "resource;io;owner;quick")

add_executable(arp_plugin_host_text_tests tests/contract/plugin_host_text_tests.cpp)
target_include_directories(arp_plugin_host_text_tests PRIVATE . src)
target_link_libraries(arp_plugin_host_text_tests PRIVATE arp_scriptvm_plugin_build)
target_compile_options(arp_plugin_host_text_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_plugin_host_text_tests)
add_test(NAME arp_plugin_host_text_tests COMMAND arp_plugin_host_text_tests)
add_test(NAME arp_plugin_host_text_quick_tests COMMAND arp_plugin_host_text_tests --quick)
set_tests_properties(arp_plugin_host_text_tests arp_plugin_host_text_quick_tests
    PROPERTIES TIMEOUT 120 LABELS "scriptvm;io")
if(TARGET arp_scriptvm_aot_v5_dynamic_package_smoke)
    add_test(NAME arp_scriptvm_aot_host_text_smoke
        COMMAND arp_scriptvm_aot_v5_dynamic_package_smoke --host-text-smoke)
    set_tests_properties(arp_scriptvm_aot_host_text_smoke PROPERTIES TIMEOUT 600 LABELS "scriptvm;io;smoke")
endif()

add_executable(arp_module_source_resolution_tests tests/contract/module_source_resolution_tests.cpp)
target_include_directories(arp_module_source_resolution_tests PRIVATE . src)
target_link_libraries(arp_module_source_resolution_tests PRIVATE arp_scriptvm_runtime
    arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
target_compile_options(arp_module_source_resolution_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_module_source_resolution_tests)
add_test(NAME arp_module_source_resolution_tests COMMAND arp_module_source_resolution_tests)
add_test(NAME arp_module_source_resolution_quick_tests COMMAND arp_module_source_resolution_tests --quick)
set_tests_properties(arp_module_source_resolution_tests arp_module_source_resolution_quick_tests
    PROPERTIES TIMEOUT 120 LABELS "scriptvm;io")

add_executable(arp_script_package_source_tests tests/contract/script_package_source_tests.cpp)
target_include_directories(arp_script_package_source_tests PRIVATE . src)
target_link_libraries(arp_script_package_source_tests PRIVATE arp_scriptvm_runtime
    arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
target_compile_options(arp_script_package_source_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_script_package_source_tests)
set(script_source_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml")
if(EMSCRIPTEN)
    target_compile_definitions(arp_script_package_source_tests PRIVATE
        ARP_SCRIPT_SOURCE_MANIFEST="/script-source-package.yaml")
    target_link_options(arp_script_package_source_tests PRIVATE
        "SHELL:--preload-file \"${script_source_manifest}@/script-source-package.yaml\"")
else()
    target_compile_definitions(arp_script_package_source_tests PRIVATE
        ARP_SCRIPT_SOURCE_MANIFEST="${script_source_manifest}")
endif()
add_test(NAME arp_script_package_source_tests COMMAND arp_script_package_source_tests)
add_test(NAME arp_script_package_source_quick_tests COMMAND arp_script_package_source_tests --quick)
set_tests_properties(arp_script_package_source_tests arp_script_package_source_quick_tests
    PROPERTIES TIMEOUT 120 LABELS "scriptvm;io")

add_executable(arp_scene_document_source_tests tests/contract/scene_document_source_tests.cpp)
target_include_directories(arp_scene_document_source_tests PRIVATE . src)
target_link_libraries(arp_scene_document_source_tests PRIVATE
    arp_script_components arp_scene_script_reflection)
target_compile_options(arp_scene_document_source_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_scene_document_source_tests)
set(scene_io_script "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/script_authoring_primary.arp")
if(EMSCRIPTEN)
    target_compile_definitions(arp_scene_document_source_tests PRIVATE
        ARP_SCENE_SCRIPT_IO_FIXTURE="/scene-script-source.arp")
    target_link_options(arp_scene_document_source_tests PRIVATE
        "SHELL:--preload-file \"${scene_io_script}@/scene-script-source.arp\"")
else()
    target_compile_definitions(arp_scene_document_source_tests PRIVATE
        ARP_SCENE_SCRIPT_IO_FIXTURE="${scene_io_script}")
endif()
add_test(NAME arp_scene_document_source_tests COMMAND arp_scene_document_source_tests)
add_test(NAME arp_scene_document_source_quick_tests COMMAND arp_scene_document_source_tests --quick)
set_tests_properties(arp_scene_document_source_tests arp_scene_document_source_quick_tests
    PROPERTIES TIMEOUT 120 LABELS "resource;scene;io")

add_test(NAME arp_resource_package_source_tests
    COMMAND arp_vegetation_catalog_cooked_tests --source-contracts)
set_tests_properties(arp_resource_package_source_tests PROPERTIES
    TIMEOUT 60 LABELS "resource;io")
add_test(NAME arp_resource_package_async_tests
    COMMAND arp_vegetation_catalog_cooked_tests --async-contracts)
set_tests_properties(arp_resource_package_async_tests PROPERTIES
    TIMEOUT 60 LABELS "resource;io")

if(WIN32 AND NOT EMSCRIPTEN)
    add_test(NAME arp_script_package_source_cli_tests
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/script_package_source_cli_tests.py"
            "$<TARGET_FILE:arp_script_aotc>" "$<TARGET_FILE:arp_script_package_source_tests>")
    set_tests_properties(arp_script_package_source_cli_tests PROPERTIES
        TIMEOUT 120 LABELS "scriptvm;io;tooling")
    add_test(NAME arp_script_package_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/script_package_source_http_smoke.py"
            "$<TARGET_FILE:arp_script_package_source_tests>")
    set_tests_properties(arp_script_package_source_http_smoke PROPERTIES
        TIMEOUT 120 LABELS "scriptvm;io;smoke")
    add_test(NAME arp_scene_document_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/scene_document_source_http_smoke.py"
            "$<TARGET_FILE:arp_scene_document_source_tests>")
    set_tests_properties(arp_scene_document_source_http_smoke PROPERTIES
        TIMEOUT 120 LABELS "resource;scene;io;smoke")
    add_test(NAME arp_ui_startup_bundle_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/ui_document_source_http_smoke.py"
            "$<TARGET_FILE:arp_ui_startup_bundle_resource_tests>" --bundle)
    set_tests_properties(arp_ui_startup_bundle_source_http_smoke PROPERTIES TIMEOUT 60 LABELS "ui;io;resource;smoke")
    add_test(NAME arp_ui_font_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/ui_document_source_http_smoke.py"
            "$<TARGET_FILE:arp_ui_font_source_tests>" --font)
    set_tests_properties(arp_ui_font_source_http_smoke PROPERTIES TIMEOUT 60 LABELS "ui;io;resource;smoke")
    add_test(NAME arp_ui_atlas_image_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/ui_document_source_http_smoke.py"
            "$<TARGET_FILE:arp_ui_atlas_image_source_tests>" --image)
    set_tests_properties(arp_ui_atlas_image_source_http_smoke PROPERTIES TIMEOUT 60 LABELS "ui;io;resource;smoke")
    add_test(NAME arp_ui_metadata_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/ui_document_source_http_smoke.py"
            "$<TARGET_FILE:arp_ui_metadata_source_tests>" --metadata)
    set_tests_properties(arp_ui_metadata_source_http_smoke PROPERTIES TIMEOUT 60 LABELS "ui;io;resource;smoke")
    add_test(NAME arp_ui_document_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/ui_document_source_http_smoke.py"
            "$<TARGET_FILE:arp_ui_document_source_tests>")
    set_tests_properties(arp_ui_document_source_http_smoke PROPERTIES TIMEOUT 60 LABELS "ui;io;resource;smoke")
    add_test(NAME arp_resource_graph_admission_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/resource_graph_admission_http_smoke.py"
            "$<TARGET_FILE:arp_resource_graph_admission_tests>")
    add_test(NAME arp_resource_manager_owner_admission_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/resource_graph_admission_http_smoke.py"
            "$<TARGET_FILE:arp_resource_graph_admission_tests>" --manager)
    set_tests_properties(arp_resource_manager_owner_admission_http_smoke
        PROPERTIES TIMEOUT 60 LABELS "resource;io;owner;smoke")
    set_tests_properties(arp_resource_graph_admission_http_smoke PROPERTIES
        TIMEOUT 60 LABELS "resource;io;smoke")
    add_test(NAME arp_resource_prefab_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/resource_prefab_http_smoke.py"
            "$<TARGET_FILE:arp_resource_prefab_source_tests>")
    set_tests_properties(arp_resource_prefab_source_http_smoke PROPERTIES
        TIMEOUT 60 LABELS "resource;io;smoke")
    add_test(NAME arp_resource_package_async_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/resource_package_http_smoke.py"
            "$<TARGET_FILE:arp_vegetation_catalog_cooked_tests>" --async)
    set_tests_properties(arp_resource_package_async_http_smoke PROPERTIES
        TIMEOUT 60 LABELS "resource;io;smoke")
    add_test(NAME arp_resource_package_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/resource_package_http_smoke.py"
            "$<TARGET_FILE:arp_vegetation_catalog_cooked_tests>")
    set_tests_properties(arp_resource_package_http_smoke PROPERTIES
        TIMEOUT 60 LABELS "resource;io;smoke")
    add_test(NAME arp_resource_terrain_http_smoke
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/resource_terrain_http_smoke.py"
            "$<TARGET_FILE:arp_resource_staged_source_tests>")
    set_tests_properties(arp_resource_terrain_http_smoke PROPERTIES
        TIMEOUT 60 LABELS "resource;io;smoke")
endif()

# WebGL artifact fixtures are themselves registered with DEFER. Register their
# consumers afterwards so a configured target never masquerades as a CTest run.
function(arp_add_unified_shader_source_validation)
    if(TARGET arp_resource_shader_source_tests AND TARGET arp_graphics_shader_module_fixture AND TARGET arp_webgl_shader_program_artifacts)
        add_dependencies(arp_resource_shader_source_tests arp_graphics_shader_module_fixture arp_webgl_shader_program_artifacts)
        set(depthArgs)
        if(TARGET arp_original_depth_table_expansion_fixture)
            add_dependencies(arp_resource_shader_source_tests arp_original_depth_table_expansion_fixture)
            target_compile_definitions(arp_resource_shader_source_tests PRIVATE ARP_SHADER_SOURCE_DEPTH_AVAILABLE=1)
            set(depthArgs --depth "${CMAKE_CURRENT_BINARY_DIR}/original-depth-table-expansion/compute")
        endif()
        foreach(mode IN ITEMS memory http)
            if(mode STREQUAL "http")
                set(check arp_resource_shader_source_http_smoke)
                set(extra --http)
                set(labels "resource;io;shader;http;smoke")
            else()
                set(check arp_resource_shader_source_tests)
                set(extra)
                set(labels "resource;io;shader;memory")
            endif()
            add_test(NAME ${check} COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/resource_shader_source_tests.py"
                --executable "$<TARGET_FILE:arp_resource_shader_source_tests>"
                --graphics "${CMAKE_CURRENT_BINARY_DIR}/graphics-module-fixture"
                --webgl "${CMAKE_CURRENT_BINARY_DIR}/webgl1-program" ${depthArgs} ${extra})
            set_tests_properties(${check} PROPERTIES TIMEOUT 180 LABELS "${labels}")
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_unified_shader_source_validation)

set(ARP_UNIFIED_IO_SHADER_FIXTURE_ROOT "" CACHE PATH
    "Explicit offline fixture copy for Emscripten shader source CPU tests")
if(EMSCRIPTEN AND ARP_UNIFIED_IO_SHADER_FIXTURE_ROOT)
    if(NOT IS_DIRECTORY "${ARP_UNIFIED_IO_SHADER_FIXTURE_ROOT}")
        message(FATAL_ERROR "Unified IO shader fixture root does not exist")
    endif()
    target_link_options(arp_resource_shader_source_tests PRIVATE
        "SHELL:--preload-file \"${ARP_UNIFIED_IO_SHADER_FIXTURE_ROOT}@/shader-io-fixture\"")
    add_test(NAME arp_resource_shader_source_tests
        COMMAND arp_resource_shader_source_tests --memory /shader-io-fixture)
    set_tests_properties(arp_resource_shader_source_tests PROPERTIES TIMEOUT 180 LABELS "resource;io;shader;node")
endif()
