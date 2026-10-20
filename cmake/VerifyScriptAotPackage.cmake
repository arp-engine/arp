if(NOT DEFINED PACKAGE_DIRECTORY)
    message(FATAL_ERROR "AOT package audit requires PACKAGE_DIRECTORY")
endif()

if(NOT IS_DIRECTORY "${PACKAGE_DIRECTORY}")
    message(FATAL_ERROR "AOT package directory is missing: ${PACKAGE_DIRECTORY}")
endif()

file(GLOB_RECURSE package_files LIST_DIRECTORIES false
    "${PACKAGE_DIRECTORY}/*")
if(NOT package_files)
    message(FATAL_ERROR "AOT package directory is empty: ${PACKAGE_DIRECTORY}")
endif()

set(text_files)
foreach(file IN LISTS package_files)
    get_filename_component(extension "${file}" EXT)
    string(TOLOWER "${extension}" extension_lower)
    if(extension_lower STREQUAL ".arp"
        OR extension_lower STREQUAL ".bc"
        OR extension_lower STREQUAL ".bytecode")
        message(FATAL_ERROR
            "AOT package contains source or bytecode artifact: ${file}")
    endif()
    if(extension_lower STREQUAL ".cpp"
        OR extension_lower STREQUAL ".h"
        OR extension_lower STREQUAL ".yaml"
        OR extension_lower STREQUAL ".txt")
        list(APPEND text_files "${file}")
    endif()
endforeach()

set(manifest "${PACKAGE_DIRECTORY}/package.aot-v5.yaml")
set(report "${PACKAGE_DIRECTORY}/package.report.txt")
set(registry "${PACKAGE_DIRECTORY}/package_registry.gen.cpp")
foreach(required IN ITEMS "${manifest}" "${report}" "${registry}")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "AOT package output is missing: ${required}")
    endif()
endforeach()

file(READ "${manifest}" manifest_text)
file(READ "${report}" report_text)
file(READ "${registry}" registry_text)

if(NOT manifest_text MATCHES "(^|\n)type: arp\.script\.aot-package/v1([\r\n]|$)")
    message(FATAL_ERROR "AOT package manifest type is not v1")
endif()
if(NOT manifest_text MATCHES "(^|\n)contract-version: 5([\r\n]|$)")
    message(FATAL_ERROR "AOT package manifest is not contract v5")
endif()
if(NOT manifest_text MATCHES "execution-kind: DirectNative")
    message(FATAL_ERROR "AOT package manifest has no DirectNative entries")
endif()
string(REGEX MATCHALL "execution-kind:[ \t]*[^\r\n]+" execution_entries
    "${manifest_text}")
foreach(entry IN LISTS execution_entries)
    if(NOT entry MATCHES "execution-kind:[ \t]*DirectNative([\r\n]|$)")
        message(FATAL_ERROR "AOT package manifest contains a non-DirectNative entry")
    endif()
endforeach()
if(NOT report_text MATCHES "Rejected functions:[ \t]*0([\r\n]|$)"
    OR NOT report_text MATCHES "Generic executor entries:[ \t]*0([\r\n]|$)")
    message(FATAL_ERROR "AOT package report contains rejected or generic entries")
endif()
if(NOT registry_text MATCHES "arp_register_script_aot_package_v5")
    message(FATAL_ERROR "AOT package registry has no v5 registration entry")
endif()

set(forbidden_patterns
    "scriptvm/aot/legacy/"
    "AotModuleDescriptorV1"
    "AotFunctionDescriptorV1"
    "Interpreter"
    "LegacyAotFunctionExecutionKind"
    "IrExecutorPrototypeLegacy"
    "executeAotFunction"
    "executeAotNativeValueFrameMachine"
    "arp::scriptvm::Lexer"
    "arp::scriptvm::Parser"
    "arp::scriptvm::Compiler"
    "arp::scriptvm::Vm::")
foreach(file IN LISTS text_files)
    # package.d is intentionally allowed to name source inputs for incremental
    # builds; it is not linked or loaded by the published runtime.
    get_filename_component(name "${file}" NAME)
    file(READ "${file}" text)
    if(text MATCHES "[A-Za-z]:[/\\\\]"
        OR text MATCHES "(^|[\"' \\t\\r\\n])/[A-Za-z0-9_]")
        message(FATAL_ERROR "AOT package contains an absolute path: ${file}")
    endif()
    if(name STREQUAL "package.d")
        continue()
    endif()
    foreach(pattern IN LISTS forbidden_patterns)
        string(FIND "${text}" "${pattern}" found)
        if(NOT found EQUAL -1)
            message(FATAL_ERROR
                "AOT package contains forbidden '${pattern}': ${file}")
        endif()
    endforeach()
endforeach()

message(STATUS "AOT v5 package audit passed: ${PACKAGE_DIRECTORY}")
