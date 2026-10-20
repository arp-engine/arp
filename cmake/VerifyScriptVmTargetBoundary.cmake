if(NOT DEFINED NM_EXECUTABLE OR NOT DEFINED ARTIFACT)
    message(FATAL_ERROR "ScriptVM target boundary audit requires NM_EXECUTABLE and ARTIFACT")
endif()

execute_process(
    COMMAND "${NM_EXECUTABLE}" -C "${ARTIFACT}"
    RESULT_VARIABLE audit_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE audit_error)
if(NOT audit_result EQUAL 0)
    message(FATAL_ERROR "ScriptVM target boundary audit failed for ${ARTIFACT}: ${audit_error}")
endif()

set(forbidden_patterns
    "scriptvm/aot/legacy/"
    "AotModuleDescriptorV1"
    "AotFunctionDescriptorV1"
    "executeAotFunction"
    "executeAotNativeValueFrameMachine"
    "IrExecutorPrototypeLegacy"
    "LegacyAotFunctionExecutionKind"
    "AotGenerator"
    "AotExecutionContext"
    "resumeAotGenerator"
    "closeAotGenerator"
    "HybridExecutionDispatcher")
foreach(pattern IN LISTS forbidden_patterns)
    string(FIND "${symbols}" "${pattern}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR
            "ScriptVM production target contains forbidden migration symbol '${pattern}': ${ARTIFACT}")
    endif()
endforeach()

# Public Runtime/Interpreter headers are consumed by ordinary VM and v5
# callers. They may carry only opaque migration handles, never contract-v1
# descriptor or legacy executor types.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(GLOB_RECURSE PUBLIC_HEADERS
    "${REPO_ROOT}/src/scriptvm/runtime/*.h"
    "${REPO_ROOT}/src/scriptvm/interpreter/*.h"
    "${REPO_ROOT}/src/scriptvm/editor/*.h")
set(forbidden_public_types
    "AotModuleDescriptorV1"
    "AotFunctionDescriptorV1"
    "AotGenerator"
    "AotExecutionContext"
    "executeAotFunction")
foreach(header IN LISTS PUBLIC_HEADERS)
    file(READ "${header}" header_text)
    foreach(pattern IN LISTS forbidden_public_types)
        string(FIND "${header_text}" "${pattern}" found_at)
        if(NOT found_at EQUAL -1)
            message(FATAL_ERROR
                "ScriptVM public header exposes forbidden legacy type '${pattern}': ${header}")
        endif()
    endforeach()
endforeach()
