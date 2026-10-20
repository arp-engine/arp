target_sources(arp_network PRIVATE src/network/http/router.cpp src/network/http/server.cpp)
target_sources(arp_network PRIVATE modules/network/http_server_bindings.cpp)

# Cross-compiled browser targets cannot execute native loopback/codegen tools.
# The runtime facade itself still compiles and start() fails explicitly there.
if(EMSCRIPTEN)
    return()
endif()

add_test(NAME arp_network_http_server_reflection_contract
    COMMAND ${Python3_EXECUTABLE} -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/reflection_contract_hash.py"
        --reflection "${CMAKE_CURRENT_BINARY_DIR}/generated/reflection/arp_network/arp_network.reflection.json"
        --manifest "${CMAKE_CURRENT_SOURCE_DIR}/modules/arp.network_module.arpplugin")

add_executable(arp_http_server_example examples/network/http_server.cpp)
target_link_libraries(arp_http_server_example PRIVATE arp_network)
target_include_directories(arp_http_server_example PRIVATE src)
arp_add_mingw_static_runtime(arp_http_server_example)
add_test(NAME arp_http_server_example_smoke
    COMMAND ${Python3_EXECUTABLE} -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/network_http_server_example_smoke.py"
        $<TARGET_FILE:arp_http_server_example>)
set_tests_properties(arp_http_server_example_smoke PROPERTIES TIMEOUT 15)

function(arp_http_test target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE . src modules)
    target_link_libraries(${target} PRIVATE arp_network)
    target_compile_definitions(${target} PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    arp_add_mingw_static_runtime(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 25)
endfunction()

arp_http_test(arp_network_http_router_tests tests/unit/network_http_router_tests.cpp)
arp_http_test(arp_network_http_server_tests tests/integration/network_http_server_tests.cpp)
arp_http_test(arp_network_http_server_loopback_smoke tests/smoke/network_http_server_loopback_smoke.cpp)
foreach(part uri message parser serializer session streaming client_facade)
    arp_http_test(arp_network_http_${part}_tests tests/integration/network_http_${part}_tests.cpp)
endforeach()
arp_http_test(arp_network_http_loopback_smoke tests/smoke/network_http_loopback_smoke.cpp)
arp_http_test(arp_network_allocator_contract_tests tests/contract/network_allocator_contract_tests.cpp)
arp_http_test(arp_network_protocol_scope_contract_tests tests/contract/network_protocol_scope_contract_tests.cpp)
arp_http_test(arp_network_architecture_boundary_tests tests/contract/network_architecture_boundary_tests.cpp)
arp_http_test(arp_network_tls_contract_tests tests/contract/network_tls_contract_tests.cpp)

if(NOT ARP_SCRIPT_AOT_ONLY_RELEASE)
    arp_http_test(arp_scriptvm_http_server_tests tests/unit/scriptvm_http_server_tests.cpp)
    target_link_libraries(arp_scriptvm_http_server_tests PRIVATE arp_scriptvm_runtime)
    arp_http_test(arp_scriptvm_http_server_smoke tests/smoke/scriptvm_http_server_smoke.cpp)
    target_link_libraries(arp_scriptvm_http_server_smoke PRIVATE
        arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend
        arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
    set(http_aot_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/http-server-aot")
    add_custom_command(
        OUTPUT "${http_aot_dir}/http_server.gen.cpp" "${http_aot_dir}/script_aot_v5.gen.h"
        COMMAND $<TARGET_FILE:arp_scriptvm_http_server_smoke> --generate-aot "${http_aot_dir}"
        DEPENDS arp_scriptvm_http_server_smoke examples/network/http_server.arp
        VERBATIM)
    arp_http_test(arp_scriptvm_http_server_aot_smoke tests/smoke/scriptvm_http_server_smoke.cpp)
    target_sources(arp_scriptvm_http_server_aot_smoke PRIVATE "${http_aot_dir}/http_server.gen.cpp")
    target_include_directories(arp_scriptvm_http_server_aot_smoke PRIVATE "${http_aot_dir}")
    target_compile_definitions(arp_scriptvm_http_server_aot_smoke PRIVATE ARP_HTTP_SERVER_AOT=1)
    target_link_libraries(arp_scriptvm_http_server_aot_smoke PRIVATE
        arp_scriptvm_runtime arp_scriptvm_ir arp_scriptvm_frontend
        arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
endif()
