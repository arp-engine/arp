if(EMSCRIPTEN)
    return()
endif()

add_executable(arp_survivors_codegen_io_tests tests/contract/survivors_codegen_io_tests.cpp)
target_link_libraries(arp_survivors_codegen_io_tests PRIVATE arp_survivors_codegen_io
    arp_scene_script_reflection arp_scene2d_script_reflection ${arp_scene_2d_resource_link_libs})
target_compile_definitions(arp_survivors_codegen_io_tests PRIVATE
    ARP_SURVIVORS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets")
target_compile_options(arp_survivors_codegen_io_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_survivors_codegen_io_tests)
add_test(NAME arp_survivors_codegen_io_tests COMMAND arp_survivors_codegen_io_tests)
add_test(NAME arp_survivors_codegen_io_smoke COMMAND arp_survivors_codegen_io_tests --smoke)
set_tests_properties(arp_survivors_codegen_io_tests arp_survivors_codegen_io_smoke
    PROPERTIES TIMEOUT 180 LABELS "example;tools;io;cpu")
add_test(NAME arp_survivors_codegen_cli_smoke
    COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_codegen_cli_smoke.py"
        --executable "$<TARGET_FILE:arp_survivors_script_codegen>"
        --assets "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets"
        --output "${CMAKE_BINARY_DIR}/survivors-codegen-cli")
set_tests_properties(arp_survivors_codegen_cli_smoke PROPERTIES TIMEOUT 180 LABELS "example;tools;io;cpu;http;smoke")
