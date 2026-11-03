function(arp_add_survivors_gameplay_source_validation)
    if(EMSCRIPTEN OR NOT TARGET arp_survivors_gameplay_sources)
        return()
    endif()
    set(directory "${CMAKE_CURRENT_SOURCE_DIR}/examples/survivors/assets")
    add_executable(arp_survivors_gameplay_sources_tests tests/contract/survivors_gameplay_sources_tests.cpp)
    target_include_directories(arp_survivors_gameplay_sources_tests PRIVATE . src)
    target_compile_definitions(arp_survivors_gameplay_sources_tests PRIVATE ARP_SURVIVORS_ASSET_DIR="${directory}")
    target_link_libraries(arp_survivors_gameplay_sources_tests PRIVATE arp_survivors_gameplay_sources)
    target_compile_options(arp_survivors_gameplay_sources_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_survivors_gameplay_sources_tests)
    foreach(mode IN ITEMS tests smoke native_smoke)
        if(mode STREQUAL "smoke")
            set(args --smoke)
        elseif(mode STREQUAL "native_smoke")
            set(args --native)
        else()
            set(args)
        endif()
        add_test(NAME arp_survivors_gameplay_sources_${mode}
            COMMAND arp_survivors_gameplay_sources_tests "${directory}" ${args})
        set_tests_properties(arp_survivors_gameplay_sources_${mode} PROPERTIES TIMEOUT 180 LABELS "example;script;io;source")
    endforeach()
    add_test(NAME arp_survivors_gameplay_sources_http_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_gameplay_sources_http_smoke.py"
            "$<TARGET_FILE:arp_survivors_gameplay_sources_tests>" "${directory}")
    set_tests_properties(arp_survivors_gameplay_sources_http_smoke PROPERTIES TIMEOUT 180 LABELS "example;script;io;source;smoke")
    if(WIN32 AND TARGET arp_survivors)
        foreach(mode IN ITEMS tests vulkan_smoke)
            if(mode STREQUAL "vulkan_smoke")
                set(args --render)
            else()
                set(args)
            endif()
            add_test(NAME arp_survivors_content_host_${mode}
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_content_host_http_smoke.py"
                    --executable "$<TARGET_FILE:arp_survivors>" --assets "${directory}"
                    --output "${CMAKE_BINARY_DIR}/survivors-content-host-${mode}" ${args})
            set_tests_properties(arp_survivors_content_host_${mode} PROPERTIES TIMEOUT 180 LABELS "example;io;resource;host")
        endforeach()
        add_test(NAME arp_survivors_record_host_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_record_host_smoke.py"
                "$<TARGET_FILE:arp_survivors>" "${CMAKE_BINARY_DIR}/survivors-record-host")
        set_tests_properties(arp_survivors_record_host_smoke PROPERTIES TIMEOUT 120
            LABELS "example;io;save;smoke")
        add_test(NAME arp_survivors_gameplay_package_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_gameplay_package_smoke.py"
                "$<TARGET_FILE:arp_survivors>" "${directory}" "${CMAKE_BINARY_DIR}/shaders"
                "${CMAKE_BINARY_DIR}/survivors-gameplay-package-smoke.yaml")
        set_tests_properties(arp_survivors_gameplay_package_smoke PROPERTIES TIMEOUT 90
            LABELS "example;script;io;source;package;smoke")
        foreach(mode IN ITEMS tests vulkan_smoke)
            if(mode STREQUAL "vulkan_smoke")
                set(args --render)
            else()
                set(args)
            endif()
            add_test(NAME arp_survivors_gameplay_host_${mode}
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/survivors_gameplay_host_http_smoke.py"
                    --executable "$<TARGET_FILE:arp_survivors>" --assets "${directory}"
                    --output "${CMAKE_CURRENT_BINARY_DIR}/survivors-gameplay-host-${mode}" ${args})
            set_tests_properties(arp_survivors_gameplay_host_${mode} PROPERTIES TIMEOUT 180
                LABELS "example;script;io;source;host")
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_survivors_gameplay_source_validation)
