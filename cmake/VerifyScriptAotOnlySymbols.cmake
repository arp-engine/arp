if(NOT DEFINED NM_EXECUTABLE OR NOT DEFINED ARTIFACT)
    message(FATAL_ERROR "AOT symbol audit requires NM_EXECUTABLE and ARTIFACT")
endif()

execute_process(
    COMMAND "${NM_EXECUTABLE}" -C "${ARTIFACT}"
    RESULT_VARIABLE audit_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE audit_error)
if(NOT audit_result EQUAL 0)
    message(FATAL_ERROR "AOT symbol audit failed to inspect ${ARTIFACT}: ${audit_error}")
endif()

set(forbidden_patterns
    "scriptvm/aot/legacy/"
    "AotModuleDescriptorV1"
    "AotFunctionDescriptorV1"
    "arp::scriptvm::HybridExecutionDispatcher"
    "arp::scriptvm::AotModuleRegistry"
    "executeAotFunction"
    "executeAotNativeValueFrameMachine"
    "IrExecutorPrototypeLegacy"
    "LegacyAotFunctionExecutionKind"
    "TypedIrExecutor")
if(NOT ALLOW_FRONTEND)
    list(APPEND forbidden_patterns
        "arp::scriptvm::Lexer"
        "arp::scriptvm::Parser"
        "arp::scriptvm::Compiler"
        "arp::scriptvm::ScriptFrontend"
        "arp::scriptvm::ScriptLowerer"
        "arp::scriptvm::parseSource")
endif()
if(NOT ALLOW_COMPILER_RUNTIME)
    list(APPEND forbidden_patterns "arp::scriptvm::Vm::")
endif()
foreach(pattern IN LISTS forbidden_patterns)
    string(FIND "${symbols}" "${pattern}" found_at)
    if(NOT found_at EQUAL -1)
        message(FATAL_ERROR
            "strict AotOnly artifact contains forbidden symbol '${pattern}': ${ARTIFACT}")
    endif()
endforeach()

if(DEFINED OBJDUMP_EXECUTABLE AND DEFINED FORBIDDEN_DLL)
    execute_process(
        COMMAND "${OBJDUMP_EXECUTABLE}" -p "${ARTIFACT}"
        RESULT_VARIABLE import_audit_result
        OUTPUT_VARIABLE imports
        ERROR_VARIABLE import_audit_error)
    if(NOT import_audit_result EQUAL 0)
        message(FATAL_ERROR
            "AOT import audit failed to inspect ${ARTIFACT}: ${import_audit_error}")
    endif()
    string(TOLOWER "${imports}" imports_lower)
    string(TOLOWER "${FORBIDDEN_DLL}" forbidden_dll_lower)
    string(FIND "${imports_lower}" "dll name: ${forbidden_dll_lower}" found_import)
    if(NOT found_import EQUAL -1)
        message(FATAL_ERROR
            "strict AotOnly artifact imports forbidden DLL '${FORBIDDEN_DLL}': ${ARTIFACT}")
    endif()
endif()
