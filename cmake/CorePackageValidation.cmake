# Exercise existing foundation/IO tests that were not wired into the hand-maintained build.
foreach(source IN ITEMS
    tests/unit/core_curve_tests.cpp
    tests/unit/core_encoding_hash_tests.cpp
    tests/unit/core_completion_queue_tests.cpp
    tests/unit/core_work_service_tests.cpp
    tests/contract/core_file_service_contract_tests.cpp
    tests/smoke/core_file_service_smoke.cpp
    tests/integration/package_manifest_tests.cpp)
    get_filename_component(name "${source}" NAME_WE)
    set(target "arp_${name}")
    if(NOT TARGET ${target})
        add_executable(${target} "${source}")
        target_link_libraries(${target} PRIVATE arp_core)
        target_include_directories(${target} PRIVATE . src)
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        arp_add_mingw_static_runtime(${target})
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES TIMEOUT 60 LABELS "core;package-regression")
    endif()
endforeach()
