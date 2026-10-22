set(ARP_SCRIPT_PLUGIN_SOURCES
    src/scriptvm/plugin/cpp_plugin_importer.cpp
    src/scriptvm/plugin/plugin_services.cpp
    src/scriptvm/plugin/plugin_registrar.cpp
    src/scriptvm/plugin/static_plugin_catalog.cpp
    src/scriptvm/plugin/plugin_runtime_scope.cpp)

add_library(arp_scriptvm_direct_native_runtime
    src/scriptvm/aot/aot_v5.cpp
    src/scriptvm/aot/script_value_memory_accountant.cpp)
target_compile_definitions(arp_scriptvm_direct_native_runtime PRIVATE
    ARP_BUILDING_DLL=1)
target_include_directories(arp_scriptvm_direct_native_runtime PUBLIC src)
target_link_libraries(arp_scriptvm_direct_native_runtime PUBLIC
    arp_core arp_reflection)

add_library(arp_scriptvm_semantic_runtime_core_aot
    src/scriptvm/runtime/semantic_runtime.cpp
    src/scriptvm/runtime/value.cpp
    src/scriptvm/runtime/generator_bridge.cpp)
target_compile_definitions(arp_scriptvm_semantic_runtime_core_aot PRIVATE
    ARP_BUILDING_DLL=1 ARP_SCRIPT_AOT_ONLY_RUNTIME=1)
target_include_directories(arp_scriptvm_semantic_runtime_core_aot PUBLIC src)
target_link_libraries(arp_scriptvm_semantic_runtime_core_aot PUBLIC
    arp_core arp_reflection)

add_library(arp_scriptvm_semantic_runtime_aot_only
    src/scriptvm/aot/aot_only_module_values.cpp)
target_compile_definitions(arp_scriptvm_semantic_runtime_aot_only PRIVATE
    ARP_BUILDING_DLL=1 ARP_SCRIPT_AOT_ONLY_RUNTIME=1)
target_include_directories(arp_scriptvm_semantic_runtime_aot_only PUBLIC src)
target_link_libraries(arp_scriptvm_semantic_runtime_aot_only PUBLIC
    arp_scriptvm_semantic_runtime_core_aot)

add_library(arp_scriptvm_host_plugin_runtime
    src/scriptvm/plugin/aot_dynamic_package.cpp
    # Release/Web validate the signed AOT manifest with a runtime-only parser.
    # Keep the source compiler and AOT builder out of this target.
    src/scriptvm/aot/aot_manifest_runtime.cpp
    ${ARP_SCRIPT_PLUGIN_SOURCES}
    src/scriptvm/runtime/module_registration.cpp
    src/scriptvm/runtime/module_registry.cpp)
target_compile_definitions(arp_scriptvm_host_plugin_runtime PRIVATE
    ARP_BUILDING_DLL=1 ARP_SCRIPT_AOT_ONLY_RUNTIME=1)
target_include_directories(arp_scriptvm_host_plugin_runtime PUBLIC src)
target_link_libraries(arp_scriptvm_host_plugin_runtime PUBLIC
    arp_scriptvm_plugin_build
    arp_core arp_reflection arp_scriptvm_direct_native_runtime
    arp_scriptvm_semantic_runtime_core_aot
    arp_fkyaml_headers)

add_library(arp_scriptvm_runtime OBJECT
    ${ARP_SCRIPT_PLUGIN_SOURCES}
    src/scriptvm/runtime/module_registration.cpp
    src/scriptvm/runtime/module_registry.cpp
    src/scriptvm/runtime/semantic_runtime.cpp
    src/scriptvm/runtime/value.cpp
    src/scriptvm/runtime/generator_bridge.cpp)
target_include_directories(arp_scriptvm_runtime PUBLIC src)
target_link_libraries(arp_scriptvm_runtime PUBLIC
    arp_scriptvm_plugin_build
    arp_core arp_reflection arp_fkyaml_headers)

add_library(arp_scriptvm_ir OBJECT
    src/scriptvm/ir/typed_ir.cpp)
target_include_directories(arp_scriptvm_ir PUBLIC src)
target_link_libraries(arp_scriptvm_ir PUBLIC
    arp_core arp_reflection)

add_library(arp_scriptvm_frontend OBJECT
    src/scriptvm/compile/frontend.cpp
    src/scriptvm/compile/lexer.cpp
    src/scriptvm/compile/parser.cpp
    src/scriptvm/compile/sema.cpp)
target_include_directories(arp_scriptvm_frontend PUBLIC src)
target_link_libraries(arp_scriptvm_frontend PUBLIC
    arp_core arp_reflection arp_scriptvm_ir)

add_library(arp_scriptvm_interpreter OBJECT
    src/scriptvm/interpreter/bytecode.cpp
    src/scriptvm/interpreter/scalar_expression.cpp
    src/scriptvm/interpreter/vm_scalar_call.cpp
    src/scriptvm/interpreter/bytecode_lowerer.cpp
    src/scriptvm/interpreter/eval.cpp
    src/scriptvm/interpreter/pipeline.cpp
    src/scriptvm/interpreter/vm.cpp)
target_include_directories(arp_scriptvm_interpreter PUBLIC src)
target_link_libraries(arp_scriptvm_interpreter PUBLIC
    arp_core arp_reflection arp_scriptvm_runtime arp_scriptvm_ir
    arp_scriptvm_frontend)

add_library(arp_scriptvm_editor_runtime STATIC
    src/scriptvm/editor/editor_hot_update.cpp
    $<TARGET_OBJECTS:arp_scriptvm_runtime>
    $<TARGET_OBJECTS:arp_scriptvm_ir>
    $<TARGET_OBJECTS:arp_scriptvm_frontend>
    $<TARGET_OBJECTS:arp_scriptvm_interpreter>)
target_include_directories(arp_scriptvm_editor_runtime PUBLIC src)
target_link_libraries(arp_scriptvm_editor_runtime PUBLIC
    arp_core arp_reflection arp_scriptvm_plugin_build arp_fkyaml_headers)

add_library(arp_scriptvm_aot_tooling OBJECT
    src/scriptvm/aot/aot_codegen_v5.cpp
    src/scriptvm/aot/aot_manifest_runtime.cpp
    src/scriptvm/aot/aot_package.cpp)
target_include_directories(arp_scriptvm_aot_tooling PUBLIC src)
target_link_libraries(arp_scriptvm_aot_tooling PUBLIC
    arp_core arp_reflection arp_scriptvm_direct_native_runtime
    arp_scriptvm_ir arp_scriptvm_frontend arp_fkyaml_headers)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(arp_script_aotc tools/script_aotc/main.cpp)
    target_include_directories(arp_script_aotc PRIVATE src 3rd/fkYAML/include)
    target_link_libraries(arp_script_aotc PRIVATE
        arp_scriptvm_runtime
        arp_scriptvm_ir
        arp_scriptvm_frontend
        arp_scriptvm_aot_tooling
        arp_core
        arp_reflection
        arp_fkyaml_headers)
    # The codegen generator runs during the build itself; a dynamic MinGW
    # runtime would resolve against whichever libstdc++-6.dll comes first on
    # PATH (Git ships an older one), failing with 0xC0000139. Link statically
    # like the other MinGW tool/test targets.
    if(COMMAND arp_add_mingw_static_runtime)
        arp_add_mingw_static_runtime(arp_script_aotc)
    endif()
    if(CMAKE_NM)
        add_custom_command(TARGET arp_script_aotc POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DNM_EXECUTABLE=${CMAKE_NM}
                -DARTIFACT=$<TARGET_FILE:arp_script_aotc>
                -DALLOW_FRONTEND=ON
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotOnlySymbols.cmake"
            VERBATIM)
    endif()
endif()

function(arp_add_script_aot_package)
    cmake_parse_arguments(AOT "EXTERNAL_RUNTIME"
        "TARGET;MANIFEST;OUTPUT_DIR" "MODULES" ${ARGN})
    if(NOT AOT_TARGET OR NOT AOT_MANIFEST OR NOT AOT_MODULES)
        message(FATAL_ERROR
            "arp_add_script_aot_package requires TARGET, MANIFEST and MODULES")
    endif()
    if(NOT AOT_OUTPUT_DIR)
        set(AOT_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${AOT_TARGET}-script-aot")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        if(NOT ARP_SCRIPT_AOTC_HOST)
            set(_aot_default_host
                "${CMAKE_SOURCE_DIR}/build/win-vulkan-release/arp_script_aotc.exe")
            if(EXISTS "${_aot_default_host}")
                set(ARP_SCRIPT_AOTC_HOST "${_aot_default_host}")
            endif()
        endif()
        if(NOT ARP_SCRIPT_AOTC_HOST)
            message(FATAL_ERROR
                "Web AOT packages require ARP_SCRIPT_AOTC_HOST from a native build")
        endif()
        set(_aot_compiler "${ARP_SCRIPT_AOTC_HOST}")
        # The Web build consumes a native host compiler. Make its artifact a
        # first-class dependency so regenerated code cannot lag behind a
        # rebuilt compiler/codegen implementation.
        set(_aot_compiler_dependency
            "${ARP_SCRIPT_AOTC_HOST}" "${AOT_MANIFEST}")
    else()
        set(_aot_compiler "$<TARGET_FILE:arp_script_aotc>")
        set(_aot_compiler_dependency arp_script_aotc "${AOT_MANIFEST}")
    endif()
    set(_aot_outputs
        "${AOT_OUTPUT_DIR}/package_registry.gen.cpp"
        "${AOT_OUTPUT_DIR}/package.aot-v5.yaml"
        "${AOT_OUTPUT_DIR}/package.d"
        "${AOT_OUTPUT_DIR}/package.report.txt")
    set(_aot_sources "${AOT_OUTPUT_DIR}/package_registry.gen.cpp")
    foreach(_module IN LISTS AOT_MODULES)
        string(REGEX REPLACE "[^A-Za-z0-9]" "_" _module_stem "${_module}")
        list(APPEND _aot_outputs
            "${AOT_OUTPUT_DIR}/${_module_stem}.gen.h"
            "${AOT_OUTPUT_DIR}/${_module_stem}.gen.cpp")
        list(APPEND _aot_sources "${AOT_OUTPUT_DIR}/${_module_stem}.gen.cpp")
    endforeach()
    add_custom_command(
        OUTPUT ${_aot_outputs}
        COMMAND "${_aot_compiler}" package "${AOT_MANIFEST}"
            --output "${AOT_OUTPUT_DIR}"
        DEPENDS ${_aot_compiler_dependency}
        DEPFILE "${AOT_OUTPUT_DIR}/package.d"
        VERBATIM)
    target_sources(${AOT_TARGET} PRIVATE ${_aot_sources})
    target_include_directories(${AOT_TARGET} PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${AOT_OUTPUT_DIR}")
    add_custom_command(TARGET ${AOT_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DPACKAGE_DIRECTORY=${AOT_OUTPUT_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotPackage.cmake"
        VERBATIM)
    if(NOT AOT_EXTERNAL_RUNTIME)
        target_link_libraries(${AOT_TARGET} PRIVATE
            arp_scriptvm_direct_native_runtime
            arp_scriptvm_semantic_runtime_core_aot
            arp_scriptvm_semantic_runtime_aot_only
            arp_scriptvm_semantic_runtime_core_aot)
    endif()
endfunction()
