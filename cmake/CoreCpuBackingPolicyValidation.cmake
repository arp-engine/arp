# CPU-only backing policy contracts; shared by native and Wasm/Node builds.
add_executable(arp_core_cpu_backing_policy_tests tests/unit/core_cpu_backing_policy_tests.cpp)
target_link_libraries(arp_core_cpu_backing_policy_tests PRIVATE arp_core_foundation)
target_compile_options(arp_core_cpu_backing_policy_tests PRIVATE -UNDEBUG)
arp_add_mingw_static_runtime(arp_core_cpu_backing_policy_tests)
if(EMSCRIPTEN)
    target_link_options(arp_core_cpu_backing_policy_tests PRIVATE
        -sASSERTIONS=1 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0 -sABORTING_MALLOC=0)
endif()
add_test(NAME arp_core_cpu_backing_policy_tests COMMAND arp_core_cpu_backing_policy_tests)
add_test(NAME arp_core_cpu_backing_policy_smoke COMMAND arp_core_cpu_backing_policy_tests --smoke)
set_tests_properties(arp_core_cpu_backing_policy_tests arp_core_cpu_backing_policy_smoke
    PROPERTIES TIMEOUT 90 LABELS "core;memory;smoke")
if(NOT EMSCRIPTEN)
    add_test(NAME arp_core_cpu_backing_native_option_tests COMMAND arp_core_cpu_backing_policy_tests)
    set_tests_properties(arp_core_cpu_backing_native_option_tests PROPERTIES
        ENVIRONMENT "MIMALLOC_ARENA_RESERVE=64MiB;ARP_TEST_ARENA_RESERVE_KIB=65536"
        TIMEOUT 90 LABELS "core;memory")
endif()
