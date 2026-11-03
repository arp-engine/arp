# CPU-only original font/resource decoders; no graphics fixture or Shader compiler.
add_executable(arp_queen_web_font_sources_tests tests/contract/queen_web_font_sources_tests.cpp
    examples/build_a_queen/web_font_source_task.cpp examples/common/startup_resource_task.cpp)
target_include_directories(arp_queen_web_font_sources_tests PRIVATE . src)
if(APPLE OR EMSCRIPTEN)
    target_link_libraries(arp_queen_web_font_sources_tests PRIVATE
        arp_2d_resources arp_2d arp_resource arp_scriptvm_host_plugin_runtime arp_3d)
else()
    target_link_libraries(arp_queen_web_font_sources_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_2d_resources,arp_2d,arp_resource,arp_scriptvm_host_plugin_runtime,arp_3d>")
endif()
target_compile_options(arp_queen_web_font_sources_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_queen_web_font_sources_tests)
add_test(NAME arp_queen_web_font_sources_tests COMMAND arp_queen_web_font_sources_tests "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/web")
add_test(NAME arp_queen_web_font_sources_smoke COMMAND arp_queen_web_font_sources_tests "${CMAKE_CURRENT_SOURCE_DIR}/examples/build_a_queen/assets/web" --smoke)
set_tests_properties(arp_queen_web_font_sources_tests arp_queen_web_font_sources_smoke
    PROPERTIES TIMEOUT 90 LABELS "ui;resource;io;example")
