set(ARP_UNITY_IMPORT_AOT_MODULES
    unity_import_common
    importer_base
    package_writer
    character_validator
    fbx_importer
    glb_importer
    texture_importer
    model_importer
    animator_controller_importer
    material_importer
    prefab_importer
    scene_importer
    yaml_importer
    meta_importer
    particle_importer
    character_folder_importer
    character_package_importer
    particle_package_importer
    unity_import)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_sources(arp_asset_importer PRIVATE
        tools/asset_importer/script_runner.cpp)
    target_include_directories(arp_asset_importer PRIVATE
        tools/unity_import 3rd/fkYAML/include)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_library(arp_script_aot_tool_host SHARED
        src/engine/script_aot_tool_host.cpp)
    target_include_directories(arp_script_aot_tool_host PUBLIC src)
    target_compile_definitions(arp_script_aot_tool_host PRIVATE
        ARP_BUILDING_DLL=1)
    set_target_properties(arp_script_aot_tool_host PROPERTIES
        PREFIX "" OUTPUT_NAME arp_script_aot_tool_host)
    if(MINGW)
        target_link_libraries(arp_script_aot_tool_host PRIVATE
            "-Wl,--whole-archive"
            arp_core
            arp_reflection
            arp_scriptvm_direct_native_runtime
            arp_scriptvm_semantic_runtime_core_aot
            arp_scriptvm_host_plugin_runtime
            arp_animation
            arp_assets
            arp_resource
            arp_scene
            arp_input
            arp_audio
            arp_ui_font
            arp_ui_runtime
            "-Wl,--no-whole-archive")
    else()
        target_link_libraries(arp_script_aot_tool_host PRIVATE
            arp_core arp_reflection arp_scriptvm_direct_native_runtime
            arp_scriptvm_semantic_runtime_core_aot
            arp_scriptvm_host_plugin_runtime arp_animation arp_assets
            arp_resource arp_scene arp_input arp_audio arp_ui_font
            arp_ui_runtime)
    endif()
    if(CMAKE_NM)
        add_custom_command(TARGET arp_script_aot_tool_host POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DNM_EXECUTABLE=${CMAKE_NM}
                -DARTIFACT=$<TARGET_FILE:arp_script_aot_tool_host>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
            VERBATIM)
    endif()

    add_library(arp_unity_import_aot_package STATIC)
    arp_add_script_aot_package(
        TARGET arp_unity_import_aot_package
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tools/unity_import/aot.package.yaml"
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/unity-import"
        MODULES ${ARP_UNITY_IMPORT_AOT_MODULES}
        EXTERNAL_RUNTIME)
    if(CMAKE_NM)
        add_custom_command(TARGET arp_unity_import_aot_package POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DNM_EXECUTABLE=${CMAKE_NM}
                -DARTIFACT=$<TARGET_FILE:arp_unity_import_aot_package>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
            VERBATIM)
    endif()

    add_executable(arp_scriptvm_aot_v5_dynamic_package_smoke
        tests/smoke/scriptvm_aot_v5_dynamic_package_smoke.cpp)
    target_include_directories(arp_scriptvm_aot_v5_dynamic_package_smoke PRIVATE src)
    target_compile_definitions(arp_scriptvm_aot_v5_dynamic_package_smoke PRIVATE
        ARP_AOT_DYNAMIC_TEST_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml"
        ARP_AOT_DYNAMIC_TEST_CACHE="${CMAKE_CURRENT_BINARY_DIR}/tests/dynamic-script-aot"
        ARP_AOT_DYNAMIC_TEST_AOTC="$<TARGET_FILE:arp_script_aotc>"
        ARP_AOT_DYNAMIC_TEST_CXX="${CMAKE_CXX_COMPILER}"
        ARP_AOT_DYNAMIC_TEST_NM="${CMAKE_NM}"
        ARP_AOT_DYNAMIC_TEST_OBJDUMP="${CMAKE_OBJDUMP}"
        ARP_AOT_DYNAMIC_TEST_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/src"
        ARP_AOT_DYNAMIC_TEST_HOST="$<TARGET_FILE:arp_script_aot_tool_host>")
    target_link_libraries(arp_scriptvm_aot_v5_dynamic_package_smoke PRIVATE
        arp_script_aot_tool_host arp_core)
    add_dependencies(arp_scriptvm_aot_v5_dynamic_package_smoke
        arp_script_aotc arp_script_aot_tool_host)
    if(CMAKE_NM)
        add_custom_command(TARGET arp_scriptvm_aot_v5_dynamic_package_smoke POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DNM_EXECUTABLE=${CMAKE_NM}
                -DOBJDUMP_EXECUTABLE=${CMAKE_OBJDUMP}
                -DFORBIDDEN_DLL=arp_engine.dll
                -DARTIFACT=$<TARGET_FILE:arp_scriptvm_aot_v5_dynamic_package_smoke>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
            VERBATIM)
    endif()
endif()

if(CMAKE_BUILD_TYPE MATCHES "^[Rr]elease$"
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_definitions(arp_asset_importer PRIVATE
        ARP_SCRIPT_AOT_ONLY_TOOL=1
        ARP_SCRIPT_AOTC_PATH="$<TARGET_FILE:arp_script_aotc>"
        ARP_SCRIPT_AOT_CXX_PATH="${CMAKE_CXX_COMPILER}"
        ARP_SCRIPT_AOT_NM_PATH="${CMAKE_NM}"
        ARP_SCRIPT_AOT_OBJDUMP_PATH="${CMAKE_OBJDUMP}")
    target_link_libraries(arp_asset_importer PRIVATE
        arp_unity_import_aot_package arp_script_aot_tool_host)
    add_dependencies(arp_asset_importer arp_script_aot_tool_host)
    if(CMAKE_NM)
        add_custom_command(TARGET arp_asset_importer POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DNM_EXECUTABLE=${CMAKE_NM}
                -DOBJDUMP_EXECUTABLE=${CMAKE_OBJDUMP}
                -DFORBIDDEN_DLL=arp_engine.dll
                -DARTIFACT=$<TARGET_FILE:arp_asset_importer>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
            VERBATIM)
    endif()
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_libraries(arp_asset_importer PRIVATE arp_engine)
    add_dependencies(arp_asset_importer arp_engine)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(arp_asset_importer_aot_v5_smoke
        tests/smoke/asset_importer_aot_v5_smoke.cpp
        tools/asset_importer/script_runner.cpp)
    target_include_directories(arp_asset_importer_aot_v5_smoke PRIVATE
        . src 3rd/fkYAML/include)
    target_compile_definitions(arp_asset_importer_aot_v5_smoke PRIVATE
        ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ARP_SCRIPT_AOT_ONLY_TOOL=1
        ARP_SCRIPT_AOTC_PATH="$<TARGET_FILE:arp_script_aotc>"
        ARP_SCRIPT_AOT_CXX_PATH="${CMAKE_CXX_COMPILER}"
        ARP_SCRIPT_AOT_NM_PATH="${CMAKE_NM}"
        ARP_SCRIPT_AOT_OBJDUMP_PATH="${CMAKE_OBJDUMP}")
    add_dependencies(arp_asset_importer_aot_v5_smoke arp_script_aotc)
    target_link_libraries(arp_asset_importer_aot_v5_smoke PRIVATE
        arp_unity_import_aot_package arp_script_aot_tool_host)
    add_dependencies(arp_asset_importer_aot_v5_smoke
        arp_script_aot_tool_host)
    if(CMAKE_NM)
        add_custom_command(TARGET arp_asset_importer_aot_v5_smoke POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DNM_EXECUTABLE=${CMAKE_NM}
                -DARTIFACT=$<TARGET_FILE:arp_asset_importer_aot_v5_smoke>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
            VERBATIM)
    endif()
endif()

add_executable(arp_scriptvm_aot_v5_tests
    tests/unit/scriptvm_aot_v5_tests.cpp)
target_include_directories(arp_scriptvm_aot_v5_tests PRIVATE . src)
target_link_libraries(arp_scriptvm_aot_v5_tests PRIVATE arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(arp_scriptvm_aot_v5_tests PRIVATE -UNDEBUG)
endif()

if(CMAKE_NM AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_custom_command(TARGET arp_scriptvm_aot_v5_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DNM_EXECUTABLE=${CMAKE_NM}
            -DARTIFACT=$<TARGET_FILE:arp_scriptvm_aot_v5_tests>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptVmTargetBoundary.cmake"
        VERBATIM)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(arp_scriptvm_aot_v5_tests PRIVATE
        "-sALLOW_MEMORY_GROWTH=0" "-sINITIAL_MEMORY=536870912")
endif()

add_executable(arp_scriptvm_aot_only_link_smoke
    tests/smoke/scriptvm_aot_only_link_smoke.cpp)
target_include_directories(arp_scriptvm_aot_only_link_smoke PRIVATE . src)
target_link_libraries(arp_scriptvm_aot_only_link_smoke PRIVATE
    arp_scriptvm_direct_native_runtime
    arp_scriptvm_semantic_runtime_core_aot)
if(CMAKE_NM AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_custom_command(TARGET arp_scriptvm_aot_only_link_smoke POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DNM_EXECUTABLE=${CMAKE_NM}
            -DARTIFACT=$<TARGET_FILE:arp_scriptvm_aot_only_link_smoke>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
        VERBATIM)
endif()

add_executable(arp_scriptvm_aot_v5_generated_smoke
    tests/smoke/scriptvm_aot_v5_generated_smoke.cpp)
target_include_directories(arp_scriptvm_aot_v5_generated_smoke PRIVATE . src)
arp_add_script_aot_package(
    TARGET arp_scriptvm_aot_v5_generated_smoke
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic"
    MODULES basic math_dep)
if(ARP_SCRIPT_AOT_ONLY_RELEASE
   AND CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # This is the strict Web production-shaped link: the generated package
    # and the engine's full static dependency closure must share one artifact
    # so the release audit can inspect the actual consumer boundary.
    target_link_libraries(arp_scriptvm_aot_v5_generated_smoke PRIVATE
        arp_engine)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    find_program(ARP_SCRIPT_AOT_AUDIT_NODE node REQUIRED)
    target_link_options(arp_scriptvm_aot_v5_generated_smoke PRIVATE
        "-sALLOW_MEMORY_GROWTH=0" "-sINITIAL_MEMORY=134217728"
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/arp_scriptvm_aot_v5_generated_smoke.map")
    add_custom_command(TARGET arp_scriptvm_aot_v5_generated_smoke POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/web/scriptvm_aot_v5_smoke.html"
            "$<TARGET_FILE_DIR:arp_scriptvm_aot_v5_generated_smoke>/scriptvm_aot_v5_smoke.html"
        COMMAND "${ARP_SCRIPT_AOT_AUDIT_NODE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_scriptvm_aot_v5_release.mjs"
            "$<TARGET_FILE:arp_scriptvm_aot_v5_generated_smoke>"
            "$<TARGET_FILE_DIR:arp_scriptvm_aot_v5_generated_smoke>/arp_scriptvm_aot_v5_generated_smoke.wasm"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic/package.aot-v5.yaml"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic/package.report.txt"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic"
            "${CMAKE_CURRENT_BINARY_DIR}/arp_scriptvm_aot_v5_generated_smoke.map"
            "${CMAKE_CURRENT_BINARY_DIR}/scriptvm-aot-v5-release-audit.yaml"
        VERBATIM)
elseif(CMAKE_NM)
    add_custom_command(TARGET arp_scriptvm_aot_v5_generated_smoke POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DNM_EXECUTABLE=${CMAKE_NM}
            -DARTIFACT=$<TARGET_FILE:arp_scriptvm_aot_v5_generated_smoke>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
        VERBATIM)
endif()

# Explicit differential-only target for the reverse DirectNative <-
# Interpreter generator bridge. It links the Interpreter by design and is
# never part of an AOT-only production link closure.
add_executable(arp_scriptvm_aot_v5_cross_backend_smoke
    tests/smoke/scriptvm_aot_v5_cross_backend_smoke.cpp)
target_include_directories(arp_scriptvm_aot_v5_cross_backend_smoke PRIVATE . src)
target_link_libraries(arp_scriptvm_aot_v5_cross_backend_smoke PRIVATE
    arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter
    arp_scriptvm_aot_tooling)
arp_add_script_aot_package(
    TARGET arp_scriptvm_aot_v5_cross_backend_smoke
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic-cross-backend"
    MODULES basic math_dep)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(arp_scriptvm_aot_v5_cross_backend_smoke PRIVATE
        "-sALLOW_MEMORY_GROWTH=0" "-sINITIAL_MEMORY=268435456")
endif()

add_executable(arp_scriptvm_aot_v5_randomized_differential_tests
    tests/unit/scriptvm_aot_v5_randomized_differential_tests.cpp)
target_include_directories(
    arp_scriptvm_aot_v5_randomized_differential_tests PRIVATE . src)
target_link_libraries(
    arp_scriptvm_aot_v5_randomized_differential_tests PRIVATE
    arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter
    arp_scriptvm_aot_tooling)
arp_add_script_aot_package(
    TARGET arp_scriptvm_aot_v5_randomized_differential_tests
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic-differential"
    MODULES basic math_dep
    EXTERNAL_RUNTIME)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(
        arp_scriptvm_aot_v5_randomized_differential_tests PRIVATE -UNDEBUG)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(arp_scriptvm_aot_v5_randomized_differential_tests PRIVATE
        "-sALLOW_MEMORY_GROWTH=0" "-sINITIAL_MEMORY=268435456")
endif()

add_executable(arp_scriptvm_aot_v5_scene_behavior_smoke
    tests/smoke/scriptvm_aot_v5_scene_behavior_smoke.cpp)
target_include_directories(arp_scriptvm_aot_v5_scene_behavior_smoke PRIVATE . src)
target_link_libraries(arp_scriptvm_aot_v5_scene_behavior_smoke PRIVATE arp_scene)
arp_add_script_aot_package(
    TARGET arp_scriptvm_aot_v5_scene_behavior_smoke
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/scene_behavior.package.yaml"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/scene-behavior"
    MODULES scene_behavior)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(arp_scriptvm_aot_v5_scene_behavior_smoke PRIVATE
        "-sALLOW_MEMORY_GROWTH=0" "-sINITIAL_MEMORY=134217728")
elseif(CMAKE_NM)
    add_custom_command(TARGET arp_scriptvm_aot_v5_scene_behavior_smoke POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DNM_EXECUTABLE=${CMAKE_NM}
            -DARTIFACT=$<TARGET_FILE:arp_scriptvm_aot_v5_scene_behavior_smoke>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
        VERBATIM)
endif()
