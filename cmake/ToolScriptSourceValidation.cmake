# Tool domain owns source preparation; no implementation enters core/engine.
set(ARP_TOOL_SCRIPT_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_importer/tool_script_sources.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/asset_importer/tool_script_source_host.cpp")
add_library(arp_tool_script_sources STATIC ${ARP_TOOL_SCRIPT_SOURCE_FILES})
target_include_directories(arp_tool_script_sources PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(arp_tool_script_sources PUBLIC arp_scriptvm_runtime arp_scriptvm_frontend)
set(tool_source_identity "${CMAKE_CURRENT_BINARY_DIR}/tool_source_identity.h")
add_custom_target(arp_tool_script_source_identity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/tool_source_identity.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${tool_source_identity}"
    BYPRODUCTS "${tool_source_identity}" VERBATIM)
add_executable(arp_tool_script_source_lifecycle tests/smoke/tool_script_source_lifecycle.cpp)
add_dependencies(arp_tool_script_source_lifecycle arp_tool_script_source_identity)
target_include_directories(arp_tool_script_source_lifecycle PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(arp_tool_script_source_lifecycle PRIVATE arp_tool_script_sources)
target_compile_options(arp_tool_script_source_lifecycle PRIVATE -UNDEBUG)
if(NOT ARP_SCRIPT_AOT_ONLY_RELEASE)
    foreach(tool IN ITEMS arp_asset_importer arp_asset_importer_script_unit_tests
            arp_asset_importer_script_smoke arp_script_source_dependency_tests)
        if(TARGET ${tool})
            # Snapshot implementation remains in its plugin-build owner. The
            # tool uses that archive; no new engine DLL export is introduced.
            target_sources(${tool} PRIVATE ${ARP_TOOL_SCRIPT_SOURCE_FILES})
            if(MINGW AND NOT tool STREQUAL "arp_script_source_dependency_tests")
                # Resolve the archive's foundation calls from the original DLL
                # before considering static dependencies. No second allocator.
                target_link_libraries(${tool} PRIVATE
                    "$<LINK_GROUP:RESCAN,arp_engine,arp_scriptvm_plugin_build>")
                add_test(NAME ${tool}_source_link_contract
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/tool_script_link_contract.py"
                        --executable "$<TARGET_FILE:${tool}>" --nm "${CMAKE_NM}" --objdump "${CMAKE_OBJDUMP}")
                set_tests_properties(${tool}_source_link_contract PROPERTIES TIMEOUT 90 LABELS "tools;io;contract")
            else()
                target_link_libraries(${tool} PRIVATE arp_scriptvm_plugin_build)
            endif()
        endif()
    endforeach()
    if(TARGET arp_script_source_dependency_tests)
        target_link_libraries(arp_script_source_dependency_tests PRIVATE
            arp_scriptvm_frontend arp_scriptvm_ir arp_scriptvm_interpreter)
    endif()
endif()
if(NOT EMSCRIPTEN)
    add_executable(arp_tool_script_source_tests tests/contract/tool_script_sources_tests.cpp)
    target_link_libraries(arp_tool_script_source_tests PRIVATE arp_tool_script_sources)
    target_compile_options(arp_tool_script_source_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_tool_script_source_tests)
    add_test(NAME arp_tool_script_source_tests COMMAND arp_tool_script_source_tests)
    add_test(NAME arp_tool_script_source_smoke COMMAND arp_tool_script_source_tests --smoke)
    set_tests_properties(arp_tool_script_source_tests arp_tool_script_source_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools;scriptvm;io;smoke")
    target_compile_definitions(arp_tool_script_source_lifecycle PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    arp_add_mingw_static_runtime(arp_tool_script_source_lifecycle)
    add_test(NAME arp_tool_script_source_native_smoke COMMAND arp_tool_script_source_lifecycle --native)
    add_test(NAME arp_tool_script_source_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/tool_script_source_http_smoke.py"
            --executable "$<TARGET_FILE:arp_tool_script_source_lifecycle>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/tool-script-source-http")
    set_tests_properties(arp_tool_script_source_native_smoke arp_tool_script_source_http_smoke PROPERTIES
        TIMEOUT 180 LABELS "tools;scriptvm;io;smoke")
else()
    target_link_options(arp_tool_script_source_lifecycle PRIVATE
        -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=268435456 -sALLOW_MEMORY_GROWTH=0
        -sABORTING_MALLOC=0 -sSTACK_SIZE=1048576)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/scripts/browser_progress_transport.js"
        "${CMAKE_CURRENT_BINARY_DIR}/browser_progress_transport.js" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tool_script_source_lifecycle.html"
        "${CMAKE_CURRENT_BINARY_DIR}/tool_script_source_lifecycle.html" COPYONLY)
    find_program(ARP_TOOL_SCRIPT_SOURCE_BROWSER NAMES chrome.exe msedge.exe
        HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
    if(ARP_TOOL_SCRIPT_SOURCE_BROWSER)
        add_test(NAME arp_tool_script_source_fetch_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/tool_script_source_browser_smoke.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_TOOL_SCRIPT_SOURCE_BROWSER}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/tool-script-source-fetch")
        set_tests_properties(arp_tool_script_source_fetch_smoke PROPERTIES
            TIMEOUT 900 RUN_SERIAL TRUE LABELS "tools;scriptvm;io;browser-native;smoke")
    endif()
endif()
