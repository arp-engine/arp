add_executable(arp_audio_memory_import_tests tests/contract/audio_memory_import_tests.cpp)
target_include_directories(arp_audio_memory_import_tests PRIVATE . src)
target_link_libraries(arp_audio_memory_import_tests PRIVATE arp_audio)
target_compile_options(arp_audio_memory_import_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_audio_memory_import_tests)
add_test(NAME arp_audio_memory_import_tests COMMAND arp_audio_memory_import_tests)
add_test(NAME arp_audio_memory_import_smoke COMMAND arp_audio_memory_import_tests --smoke)
set_tests_properties(arp_audio_memory_import_tests arp_audio_memory_import_smoke
    PROPERTIES TIMEOUT 90 LABELS "audio;io;import")
if(NOT EMSCRIPTEN)
    add_test(NAME arp_audio_memory_original_pcm_smoke COMMAND arp_audio_memory_import_tests
        --original "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets")
    set_tests_properties(arp_audio_memory_original_pcm_smoke PROPERTIES TIMEOUT 90 LABELS "audio;io;import")
    add_test(NAME arp_audio_memory_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/audio_memory_import_http_smoke.py"
            "$<TARGET_FILE:arp_audio_memory_import_tests>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets")
    set_tests_properties(arp_audio_memory_http_smoke PROPERTIES TIMEOUT 90 LABELS "audio;io;http;smoke")
endif()
