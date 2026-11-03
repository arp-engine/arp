# CPU-only audio import from actual Fetch; no device or graphics creation.
set(audio_root "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(arp_audio_import_cpu_web STATIC
    "${audio_root}/src/audio/audio_importer.cpp"
    "${audio_root}/src/audio/miniaudio_backend.cpp"
    "${audio_root}/src/audio/audio_system.cpp")
target_include_directories(arp_audio_import_cpu_web PRIVATE "${audio_root}/3rd/miniaudio")
target_link_libraries(arp_audio_import_cpu_web PUBLIC arp_core)
add_executable(arp_audio_memory_import_tests "${audio_root}/tests/contract/audio_memory_import_tests.cpp")
target_include_directories(arp_audio_memory_import_tests PRIVATE "${audio_root}")
target_link_libraries(arp_audio_memory_import_tests PRIVATE arp_audio_import_cpu_web)
target_compile_options(arp_audio_memory_import_tests PRIVATE -UNDEBUG)
target_link_options(arp_audio_memory_import_tests PRIVATE
    -sASSERTIONS=1 -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0)
add_test(NAME arp_audio_memory_import_tests COMMAND arp_audio_memory_import_tests)
add_test(NAME arp_audio_memory_import_smoke COMMAND arp_audio_memory_import_tests --smoke)
set_tests_properties(arp_audio_memory_import_tests arp_audio_memory_import_smoke
    PROPERTIES TIMEOUT 90 LABELS "audio;io;node")
add_executable(arp_audio_memory_source_lifecycle "${audio_root}/tests/smoke/audio_memory_source_lifecycle.cpp")
target_include_directories(arp_audio_memory_source_lifecycle PRIVATE "${audio_root}")
target_link_libraries(arp_audio_memory_source_lifecycle PRIVATE arp_audio_import_cpu_web)
target_link_options(arp_audio_memory_source_lifecycle PRIVATE
    -sASSERTIONS=1 -sEXIT_RUNTIME=0 -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0)
configure_file("${audio_root}/tests/fixtures/audio_memory_source_lifecycle.html"
    "${CMAKE_CURRENT_BINARY_DIR}/audio_memory_source_lifecycle.html" COPYONLY)
if(ARP_WEBGL_RUNTIME_BROWSER)
    add_test(NAME arp_audio_memory_fetch_smoke
        COMMAND "${Python3_EXECUTABLE}" "${audio_root}/tests/contract/audio_memory_browser_smoke.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}" --browser "${ARP_WEBGL_RUNTIME_BROWSER}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/audio-memory-fetch-ctest")
    set_tests_properties(arp_audio_memory_fetch_smoke PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE
        LABELS "audio;io;browser-native;smoke")
endif()
