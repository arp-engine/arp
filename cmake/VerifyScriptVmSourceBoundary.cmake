if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "ScriptVM source boundary audit requires SOURCE_ROOT")
endif()

set(retired_legacy_dir "${SOURCE_ROOT}/src/scriptvm/aot/legacy")
if(EXISTS "${retired_legacy_dir}")
    file(GLOB_RECURSE retired_legacy_entries "${retired_legacy_dir}/*")
    message(FATAL_ERROR
        "Retired ScriptVM Legacy source directory must not exist: ${retired_legacy_entries}")
endif()

set(boundary_files)
file(GLOB_RECURSE runtime_headers
    "${SOURCE_ROOT}/src/scriptvm/runtime/*.h")
file(GLOB_RECURSE runtime_sources
    "${SOURCE_ROOT}/src/scriptvm/runtime/*.cpp")
file(GLOB_RECURSE interpreter_headers
    "${SOURCE_ROOT}/src/scriptvm/interpreter/*.h")
file(GLOB_RECURSE interpreter_sources
    "${SOURCE_ROOT}/src/scriptvm/interpreter/*.cpp")
file(GLOB_RECURSE editor_headers
    "${SOURCE_ROOT}/src/scriptvm/editor/*.h")
file(GLOB_RECURSE editor_sources
    "${SOURCE_ROOT}/src/scriptvm/editor/*.cpp")
file(GLOB production_aot_sources
    "${SOURCE_ROOT}/src/scriptvm/aot/*.h"
    "${SOURCE_ROOT}/src/scriptvm/aot/*.cpp")
list(APPEND boundary_files ${runtime_headers} ${runtime_sources}
    ${interpreter_headers} ${interpreter_sources} ${editor_headers}
    ${editor_sources} ${production_aot_sources})

set(forbidden_patterns
    "scriptvm/aot/legacy/"
    "AotModuleDescriptorV1"
    "AotFunctionDescriptorV1"
    "AotExecutionContext"
    "AotGenerator"
    "executeAotFunction"
    "resumeAotGenerator"
    "closeAotGenerator"
    "HybridExecutionDispatcher"
    "isYieldFromRangeSite"
    "delegatedYieldContinuationPc"
    "ExpectedReflectionClass"
    "sourceText.find(\"registration::class_\")"
    "CreateMutex"
    "Local\\\\arp-process-identity"
    "std::system("
    "popen(")
foreach(file IN LISTS boundary_files)
    file(READ "${file}" source_text)
    foreach(pattern IN LISTS forbidden_patterns)
        string(FIND "${source_text}" "${pattern}" found_at)
        if(NOT found_at EQUAL -1)
            message(FATAL_ERROR
                "ScriptVM production source boundary contains '${pattern}': ${file}")
        endif()
    endforeach()
endforeach()

set(accountant_source
    "${SOURCE_ROOT}/src/scriptvm/aot/script_value_memory_accountant.cpp")
file(READ "${accountant_source}" accountant_source_text)
foreach(pattern IN ITEMS "fprintf(" "stderr" "%p")
    string(FIND "${accountant_source_text}" "${pattern}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR
            "ScriptVM Value accountant contains non-deterministic diagnostic output '${pattern}'")
    endif()
endforeach()

message(STATUS "ScriptVM source boundary audit passed")
