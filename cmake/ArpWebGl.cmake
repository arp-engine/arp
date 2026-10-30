function(arp_add_webgl_backend_dispatch_tests)
    if(EMSCRIPTEN)
        return()
    endif()
    add_executable(arp_rhi_webgl_backend_dispatch_contract_tests
        tests/contract/rhi_webgl_backend_dispatch_contract_tests.cpp)
    target_include_directories(arp_rhi_webgl_backend_dispatch_contract_tests PRIVATE . src)
    target_link_libraries(arp_rhi_webgl_backend_dispatch_contract_tests PRIVATE arp_rhi arp_rhi_semantics arp_core)
    arp_add_mingw_static_runtime(arp_rhi_webgl_backend_dispatch_contract_tests)
    if(MSVC)
        target_compile_options(arp_rhi_webgl_backend_dispatch_contract_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_rhi_webgl_backend_dispatch_contract_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_rhi_webgl_backend_dispatch_contract_tests COMMAND arp_rhi_webgl_backend_dispatch_contract_tests)
endfunction()
cmake_language(DEFER CALL arp_add_webgl_backend_dispatch_tests)

function(arp_add_retirement_budget_preflight_tests)
    foreach(target IN ITEMS arp_core_budget_vector_preflight_tests arp_rhi_frame_graph_retirement_budget_tests)
        if(target STREQUAL "arp_core_budget_vector_preflight_tests")
            set(source tests/unit/core_budget_vector_preflight_tests.cpp)
        else()
            set(source tests/contract/rhi_frame_graph_retirement_budget_tests.cpp)
        endif()
        add_executable(${target} ${source})
        target_include_directories(${target} PRIVATE . src)
        target_link_libraries(${target} PRIVATE arp_core)
        if(target STREQUAL "arp_rhi_frame_graph_retirement_budget_tests")
            target_link_libraries(${target} PRIVATE arp_rhi arp_rhi_semantics)
        endif()
        arp_add_mingw_static_runtime(${target})
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${target} COMMAND ${target})
    endforeach()
endfunction()
cmake_language(DEFER CALL arp_add_retirement_budget_preflight_tests)

function(arp_add_single_use_reservation_pool_tests)
    add_executable(arp_core_single_use_reservation_pool_tests
        tests/unit/core_single_use_reservation_pool_tests.cpp)
    target_include_directories(arp_core_single_use_reservation_pool_tests PRIVATE . src)
    target_link_libraries(arp_core_single_use_reservation_pool_tests PRIVATE arp_core)
    arp_add_mingw_static_runtime(arp_core_single_use_reservation_pool_tests)
    if(MSVC)
        target_compile_options(arp_core_single_use_reservation_pool_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_core_single_use_reservation_pool_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_core_single_use_reservation_pool_tests
        COMMAND arp_core_single_use_reservation_pool_tests lifecycle)
    foreach(mode IN ITEMS moves close quota reuse)
        add_test(NAME arp_core_single_use_reservation_pool_${mode}_tests
            COMMAND arp_core_single_use_reservation_pool_tests ${mode})
    endforeach()
endfunction()
cmake_language(DEFER CALL arp_add_single_use_reservation_pool_tests)

function(arp_add_allocation_charge_rollback_tests)
    if(NOT EMSCRIPTEN AND TARGET arp_rhi_vulkan AND TARGET arp_rhi_metal)
        add_executable(arp_rhi_allocation_charge_rollback_tests
            tests/contract/rhi_allocation_charge_rollback_tests.cpp)
        target_include_directories(arp_rhi_allocation_charge_rollback_tests PRIVATE
            . src "${ARP_VULKAN_SDK}/Include")
        target_link_libraries(arp_rhi_allocation_charge_rollback_tests PRIVATE
            arp_rhi arp_rhi_vulkan arp_rhi_metal arp_rhi_webgpu)
        arp_add_mingw_static_runtime(arp_rhi_allocation_charge_rollback_tests)
        if(MSVC)
            target_compile_options(arp_rhi_allocation_charge_rollback_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(arp_rhi_allocation_charge_rollback_tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME arp_rhi_allocation_charge_rollback_tests COMMAND arp_rhi_allocation_charge_rollback_tests)
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_allocation_charge_rollback_tests)

function(arp_add_pool_allocation_receipt_tests)
    if(TARGET arp_render_graph)
        add_executable(arp_render_graph_pool_allocation_receipt_tests
            tests/integration/render_graph_pool_allocation_receipt_tests.cpp)
        target_include_directories(arp_render_graph_pool_allocation_receipt_tests PRIVATE . src)
        target_link_libraries(arp_render_graph_pool_allocation_receipt_tests PRIVATE
            arp_render_graph arp_rhi arp_rhi_semantics)
        arp_add_mingw_static_runtime(arp_render_graph_pool_allocation_receipt_tests)
        if(MSVC)
            target_compile_options(arp_render_graph_pool_allocation_receipt_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(arp_render_graph_pool_allocation_receipt_tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME arp_render_graph_pool_allocation_receipt_tests
            COMMAND arp_render_graph_pool_allocation_receipt_tests contract)
        foreach(mode IN ITEMS null quota negative lifetime partitions partitions-quota graph-entry graph-active)
            add_test(NAME arp_render_graph_pool_allocation_receipt_${mode}_tests
                COMMAND arp_render_graph_pool_allocation_receipt_tests ${mode})
        endforeach()
        if(NOT EMSCRIPTEN AND TARGET arp_rhi_vulkan)
            target_compile_definitions(arp_render_graph_pool_allocation_receipt_tests PRIVATE ARP_TEST_VULKAN=1)
            target_include_directories(arp_render_graph_pool_allocation_receipt_tests PRIVATE "${ARP_VULKAN_SDK}/Include")
            target_link_libraries(arp_render_graph_pool_allocation_receipt_tests PRIVATE arp_rhi_vulkan)
            add_test(NAME arp_render_graph_pool_allocation_receipt_native_tests
                COMMAND arp_render_graph_pool_allocation_receipt_tests native)
            set_tests_properties(arp_render_graph_pool_allocation_receipt_native_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
            add_test(NAME arp_render_graph_pool_allocation_receipt_partitions-native_tests
                COMMAND arp_render_graph_pool_allocation_receipt_tests partitions-native)
            set_tests_properties(arp_render_graph_pool_allocation_receipt_partitions-native_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
            add_test(NAME arp_render_graph_pool_allocation_receipt_graph-native_tests
                COMMAND arp_render_graph_pool_allocation_receipt_tests graph-native)
            set_tests_properties(arp_render_graph_pool_allocation_receipt_graph-native_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endif()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_pool_allocation_receipt_tests)

function(arp_add_original_heap_registration_tests)
    add_executable(arp_rhi_original_heap_registration_tests
        tests/integration/rhi_original_heap_registration_tests.cpp)
    target_include_directories(arp_rhi_original_heap_registration_tests PRIVATE . src)
    target_link_libraries(arp_rhi_original_heap_registration_tests PRIVATE arp_rhi)
    arp_add_mingw_static_runtime(arp_rhi_original_heap_registration_tests)
    if(MSVC)
        target_compile_options(arp_rhi_original_heap_registration_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_rhi_original_heap_registration_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_rhi_original_heap_registration_tests
        COMMAND arp_rhi_original_heap_registration_tests contract)
    foreach(mode IN ITEMS null quota)
        add_test(NAME arp_rhi_original_heap_registration_${mode}_tests
            COMMAND arp_rhi_original_heap_registration_tests ${mode})
    endforeach()
    if(NOT EMSCRIPTEN AND TARGET arp_rhi_vulkan)
        target_compile_definitions(arp_rhi_original_heap_registration_tests PRIVATE ARP_TEST_VULKAN=1)
        target_include_directories(arp_rhi_original_heap_registration_tests PRIVATE "${ARP_VULKAN_SDK}/Include")
        target_link_libraries(arp_rhi_original_heap_registration_tests PRIVATE arp_rhi_vulkan)
        add_test(NAME arp_rhi_original_heap_registration_native_tests
            COMMAND arp_rhi_original_heap_registration_tests native)
        set_tests_properties(arp_rhi_original_heap_registration_native_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 30)
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_original_heap_registration_tests)

function(arp_add_pool_heap_lease_tests)
    if(TARGET arp_render_graph)
        add_executable(arp_render_graph_pool_heap_lease_tests
            tests/integration/render_graph_pool_heap_lease_tests.cpp)
        target_include_directories(arp_render_graph_pool_heap_lease_tests PRIVATE . src)
        target_link_libraries(arp_render_graph_pool_heap_lease_tests PRIVATE arp_render_graph arp_rhi)
        arp_add_mingw_static_runtime(arp_render_graph_pool_heap_lease_tests)
        if(MSVC)
            target_compile_options(arp_render_graph_pool_heap_lease_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(arp_render_graph_pool_heap_lease_tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME arp_render_graph_pool_heap_lease_tests
            COMMAND arp_render_graph_pool_heap_lease_tests contract)
        foreach(mode IN ITEMS null lifetime negative quota)
            add_test(NAME arp_render_graph_pool_heap_lease_${mode}_tests
                COMMAND arp_render_graph_pool_heap_lease_tests ${mode})
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_pool_heap_lease_tests)

function(arp_add_partitioned_budget_ledger_tests)
    add_executable(arp_core_partitioned_budget_ledger_tests tests/unit/core_partitioned_budget_ledger_tests.cpp)
    target_include_directories(arp_core_partitioned_budget_ledger_tests PRIVATE . src)
    target_link_libraries(arp_core_partitioned_budget_ledger_tests PRIVATE arp_core)
    arp_add_mingw_static_runtime(arp_core_partitioned_budget_ledger_tests)
    if(MSVC)
        target_compile_options(arp_core_partitioned_budget_ledger_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_core_partitioned_budget_ledger_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_core_partitioned_budget_ledger_tests COMMAND arp_core_partitioned_budget_ledger_tests)
endfunction()
cmake_language(DEFER CALL arp_add_partitioned_budget_ledger_tests)

function(arp_add_core_stable_vector_erase_tests)
    add_executable(arp_core_stable_vector_erase_tests tests/unit/core_stable_vector_erase_tests.cpp)
    target_include_directories(arp_core_stable_vector_erase_tests PRIVATE . src)
    target_link_libraries(arp_core_stable_vector_erase_tests PRIVATE arp_core)
    arp_add_mingw_static_runtime(arp_core_stable_vector_erase_tests)
    if(MSVC)
        target_compile_options(arp_core_stable_vector_erase_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_core_stable_vector_erase_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_core_stable_vector_erase_tests COMMAND arp_core_stable_vector_erase_tests)
endfunction()
cmake_language(DEFER CALL arp_add_core_stable_vector_erase_tests)

function(arp_add_graph_pool_configuration_tests)
    if(TARGET arp_render_graph_resource_management_tests)
        foreach(mode IN ITEMS bytes count boundaries shrink)
            add_test(NAME arp_render_graph_pool_configuration_${mode}_tests
                COMMAND arp_render_graph_resource_management_tests configuration-${mode})
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_graph_pool_configuration_tests)

function(arp_add_graph_pool_publication_tests)
    if(TARGET arp_render_graph)
        add_executable(arp_render_graph_pool_publication_tests
            tests/integration/render_graph_pool_publication_tests.cpp)
        target_include_directories(arp_render_graph_pool_publication_tests PRIVATE . src)
        target_link_libraries(arp_render_graph_pool_publication_tests PRIVATE
            arp_render_graph arp_rhi arp_rhi_semantics)
        arp_add_mingw_static_runtime(arp_render_graph_pool_publication_tests)
        if(MSVC)
            target_compile_options(arp_render_graph_pool_publication_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(arp_render_graph_pool_publication_tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME arp_render_graph_pool_publication_tests
            COMMAND arp_render_graph_pool_publication_tests texture)
        foreach(mode IN ITEMS buffer recycle buffer-recycle alias alias-recycle mixed alias-eviction)
            add_test(NAME arp_render_graph_pool_publication_${mode}_tests
                COMMAND arp_render_graph_pool_publication_tests ${mode})
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_graph_pool_publication_tests)

function(arp_add_graph_pool_metadata_source_tests)
    if(TARGET arp_render_graph)
        add_executable(arp_render_graph_owned_resource_retention_tests
            tests/integration/render_graph_owned_resource_retention_tests.cpp)
        target_include_directories(arp_render_graph_owned_resource_retention_tests PRIVATE . src)
        target_link_libraries(arp_render_graph_owned_resource_retention_tests PRIVATE
            arp_render_graph arp_rhi arp_rhi_semantics)
        arp_add_mingw_static_runtime(arp_render_graph_owned_resource_retention_tests)
        target_compile_options(arp_render_graph_owned_resource_retention_tests PRIVATE
            $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
        add_test(NAME arp_render_graph_owned_resource_retention_tests
            COMMAND arp_render_graph_owned_resource_retention_tests contract)
        foreach(mode IN ITEMS owners failures quota mixed mixed-rejection mixed-quota
                materialization materialization-rejection materialization-quota)
            add_test(NAME arp_render_graph_owned_resource_${mode}_tests
                COMMAND arp_render_graph_owned_resource_retention_tests ${mode})
        endforeach()
        if(TARGET arp_rhi_vulkan)
            target_compile_definitions(arp_render_graph_owned_resource_retention_tests PRIVATE ARP_TEST_VULKAN=1)
            target_include_directories(arp_render_graph_owned_resource_retention_tests PRIVATE "${ARP_VULKAN_SDK}/Include")
            target_link_libraries(arp_render_graph_owned_resource_retention_tests PRIVATE arp_rhi_vulkan)
            add_test(NAME arp_render_graph_owned_resource_native_tests
                COMMAND arp_render_graph_owned_resource_retention_tests native)
            set_tests_properties(arp_render_graph_owned_resource_native_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
            add_test(NAME arp_render_graph_owned_resource_mixed-native_tests
                COMMAND arp_render_graph_owned_resource_retention_tests mixed-native)
            set_tests_properties(arp_render_graph_owned_resource_mixed-native_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
            add_test(NAME arp_render_graph_owned_resource_materialization-native_tests
                COMMAND arp_render_graph_owned_resource_retention_tests materialization-native)
            set_tests_properties(arp_render_graph_owned_resource_materialization-native_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endif()
        add_executable(arp_render_graph_shared_import_retention_tests
            tests/integration/render_graph_shared_import_retention_tests.cpp)
        target_include_directories(arp_render_graph_shared_import_retention_tests PRIVATE . src)
        target_link_libraries(arp_render_graph_shared_import_retention_tests PRIVATE
            arp_render_graph arp_rhi arp_rhi_semantics)
        arp_add_mingw_static_runtime(arp_render_graph_shared_import_retention_tests)
        target_compile_options(arp_render_graph_shared_import_retention_tests PRIVATE
            $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
        add_test(NAME arp_render_graph_shared_import_retention_tests
            COMMAND arp_render_graph_shared_import_retention_tests contract)
        foreach(mode IN ITEMS lifetime rejection quota)
            add_test(NAME arp_render_graph_shared_import_${mode}_tests
                COMMAND arp_render_graph_shared_import_retention_tests ${mode})
        endforeach()
        if(TARGET arp_rhi_vulkan)
            target_compile_definitions(arp_render_graph_shared_import_retention_tests PRIVATE ARP_TEST_VULKAN=1)
            target_include_directories(arp_render_graph_shared_import_retention_tests PRIVATE "${ARP_VULKAN_SDK}/Include")
            target_link_libraries(arp_render_graph_shared_import_retention_tests PRIVATE arp_rhi_vulkan)
            add_test(NAME arp_render_graph_shared_import_native_tests
                COMMAND arp_render_graph_shared_import_retention_tests native)
            set_tests_properties(arp_render_graph_shared_import_native_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endif()
        add_executable(arp_render_graph_pool_metadata_source_tests
            tests/integration/render_graph_pool_metadata_source_tests.cpp)
        target_include_directories(arp_render_graph_pool_metadata_source_tests PRIVATE . src)
        target_link_libraries(arp_render_graph_pool_metadata_source_tests PRIVATE
            arp_render_graph arp_rhi arp_rhi_semantics)
        arp_add_mingw_static_runtime(arp_render_graph_pool_metadata_source_tests)
        if(MSVC)
            target_compile_options(arp_render_graph_pool_metadata_source_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(arp_render_graph_pool_metadata_source_tests PRIVATE -UNDEBUG)
        endif()
        add_test(NAME arp_render_graph_pool_metadata_source_tests
            COMMAND arp_render_graph_pool_metadata_source_tests)
        foreach(mode IN ITEMS source mutations quota assignment storage storage-quota storage-source)
            add_test(NAME arp_render_graph_compiled_retirement_${mode}_tests
                COMMAND arp_render_graph_pool_metadata_source_tests compiled-${mode})
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_graph_pool_metadata_source_tests)

function(arp_add_resource_cache_accounting_test)
    add_executable(arp_rhi_resource_cache_accounting_tests
        tests/contract/rhi_resource_cache_accounting_tests.cpp)
    target_include_directories(arp_rhi_resource_cache_accounting_tests PRIVATE . src)
    target_link_libraries(arp_rhi_resource_cache_accounting_tests PRIVATE arp_rhi arp_rhi_semantics)
    arp_add_mingw_static_runtime(arp_rhi_resource_cache_accounting_tests)
    if(MSVC)
        target_compile_options(arp_rhi_resource_cache_accounting_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_rhi_resource_cache_accounting_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_rhi_resource_cache_accounting_tests COMMAND arp_rhi_resource_cache_accounting_tests)
endfunction()
cmake_language(DEFER CALL arp_add_resource_cache_accounting_test)

function(arp_add_graph_placement_query_test)
    if(TARGET arp_render_graph_transient_aliasing_tests)
        add_custom_target(arp_render_graph_placement_query_contract_tests
            DEPENDS arp_render_graph_transient_aliasing_tests)
        add_test(NAME arp_render_graph_placement_query_contract_tests
            COMMAND arp_render_graph_transient_aliasing_tests --placement-query)
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_graph_placement_query_test)

function(arp_add_webgpu_heap_placement_test)
    if(TARGET arp_rhi_webgpu_device_contract_tests)
        add_custom_target(arp_rhi_webgpu_heap_placement_contract_tests
            DEPENDS arp_rhi_webgpu_device_contract_tests)
        add_test(NAME arp_rhi_webgpu_heap_placement_contract_tests
            COMMAND arp_rhi_webgpu_device_contract_tests --heap-placement)
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_webgpu_heap_placement_test)

function(arp_add_vulkan_heap_placement_tests)
    if(NOT EMSCRIPTEN AND TARGET arp_rhi_vulkan)
        add_executable(arp_rhi_vulkan_ordinary_cache_tests
            tests/integration/rhi_vulkan_ordinary_cache_tests.cpp)
        target_include_directories(arp_rhi_vulkan_ordinary_cache_tests PRIVATE . src "${ARP_VULKAN_SDK}/Include")
        target_link_libraries(arp_rhi_vulkan_ordinary_cache_tests PRIVATE arp_rhi_vulkan arp_render_graph)
        arp_add_mingw_static_runtime(arp_rhi_vulkan_ordinary_cache_tests)
        add_test(NAME arp_rhi_vulkan_ordinary_cache_tests COMMAND arp_rhi_vulkan_ordinary_cache_tests)
        set_tests_properties(arp_rhi_vulkan_ordinary_cache_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
        if(MSVC)
            target_compile_options(arp_rhi_vulkan_ordinary_cache_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(arp_rhi_vulkan_ordinary_cache_tests PRIVATE -UNDEBUG)
        endif()
        add_executable(arp_rhi_vulkan_heap_placement_tests
            tests/integration/rhi_vulkan_heap_placement_tests.cpp)
        target_include_directories(arp_rhi_vulkan_heap_placement_tests PRIVATE . src "${ARP_VULKAN_SDK}/Include")
        target_link_libraries(arp_rhi_vulkan_heap_placement_tests PRIVATE arp_rhi_vulkan)
        arp_add_mingw_static_runtime(arp_rhi_vulkan_heap_placement_tests)
        add_test(NAME arp_rhi_vulkan_heap_placement_tests
            COMMAND arp_rhi_vulkan_heap_placement_tests admission)
        set_tests_properties(arp_rhi_vulkan_heap_placement_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 30)
        if(MSVC)
            target_compile_options(arp_rhi_vulkan_heap_placement_tests PRIVATE /UNDEBUG)
        else()
            target_compile_options(arp_rhi_vulkan_heap_placement_tests PRIVATE -UNDEBUG)
        endif()
        set(placementModes admission immediate query native-positive native-negative native-faults native-entrypoints native-publication-rollback native-provenance native-provenance-publication)
        if(TARGET arp_render_graph)
            target_compile_definitions(arp_rhi_vulkan_heap_placement_tests PRIVATE ARP_TEST_POOL_HEAP_LEASE=1)
            target_link_libraries(arp_rhi_vulkan_heap_placement_tests PRIVATE arp_render_graph)
            list(APPEND placementModes native-pool-lease native-pool-lease-failure native-pool-partitions native-pool-partitions-failure)
        endif()
        foreach(mode IN LISTS placementModes)
            add_test(NAME arp_rhi_vulkan_heap_placement_${mode}_tests
                COMMAND arp_rhi_vulkan_heap_placement_tests ${mode})
            set_tests_properties(arp_rhi_vulkan_heap_placement_${mode}_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_vulkan_heap_placement_tests)

function(arp_add_heap_placement_contract_tests)
    add_executable(arp_rhi_heap_placement_contract_tests
        tests/contract/rhi_heap_placement_contract_tests.cpp)
    target_include_directories(arp_rhi_heap_placement_contract_tests PRIVATE . src)
    target_link_libraries(arp_rhi_heap_placement_contract_tests PRIVATE arp_rhi arp_rhi_semantics)
    arp_add_mingw_static_runtime(arp_rhi_heap_placement_contract_tests)
    if(MSVC)
        target_compile_options(arp_rhi_heap_placement_contract_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_rhi_heap_placement_contract_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_rhi_heap_placement_contract_tests COMMAND arp_rhi_heap_placement_contract_tests)
    add_test(NAME arp_rhi_heap_placement_pure_tests COMMAND arp_rhi_heap_placement_contract_tests pure)
    foreach(mode IN ITEMS admission owner range custody)
        add_test(NAME arp_rhi_heap_placement_null_${mode}_tests
            COMMAND arp_rhi_heap_placement_contract_tests ${mode})
    endforeach()
endfunction()
cmake_language(DEFER CALL arp_add_heap_placement_contract_tests)

# WebGl1Graphics/1: contract-only until real backend execution is validated.
function(arp_add_native_runtime_owner_contract_tests)
if(NOT EMSCRIPTEN AND TARGET arp_rhi_vulkan)
    add_executable(arp_rhi_vulkan_runtime_owner_tests
        tests/contract/rhi_vulkan_runtime_owner_tests.cpp)
    target_include_directories(arp_rhi_vulkan_runtime_owner_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${ARP_VULKAN_SDK}/Include")
    target_compile_definitions(arp_rhi_vulkan_runtime_owner_tests PRIVATE
        ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(arp_rhi_vulkan_runtime_owner_tests PRIVATE arp_rhi_vulkan)
    arp_add_mingw_static_runtime(arp_rhi_vulkan_runtime_owner_tests)
    if(MSVC)
        target_compile_options(arp_rhi_vulkan_runtime_owner_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_rhi_vulkan_runtime_owner_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_rhi_vulkan_runtime_owner_tests
        COMMAND arp_rhi_vulkan_runtime_owner_tests)
endif()
endfunction()
cmake_language(DEFER CALL arp_add_native_runtime_owner_contract_tests)

# Production shader ABI artifacts are independent of the ProbeOnly package.
include("${CMAKE_CURRENT_LIST_DIR}/ArpWebGlRuntime.cmake")
arp_add_webgl_runtime(arp_core arp_shader)
target_link_libraries(arp_rhi_webgl PUBLIC arp_rhi arp_rhi_semantics)
if(NOT EMSCRIPTEN)
    add_executable(arp_rhi_webgl_runtime_tests tests/contract/rhi_webgl_runtime_tests.cpp)
    target_link_libraries(arp_rhi_webgl_runtime_tests PRIVATE arp_rhi_webgl)
    arp_add_mingw_static_runtime(arp_rhi_webgl_runtime_tests)
    target_compile_options(arp_rhi_webgl_runtime_tests PRIVATE -UNDEBUG)
    add_test(NAME arp_rhi_webgl_runtime_tests COMMAND arp_rhi_webgl_runtime_tests)
    add_executable(arp_rhi_webgl_resource_creation_tests tests/contract/rhi_webgl_resource_creation_tests.cpp)
    target_link_libraries(arp_rhi_webgl_resource_creation_tests PRIVATE arp_rhi_webgl)
    arp_add_mingw_static_runtime(arp_rhi_webgl_resource_creation_tests)
    target_compile_options(arp_rhi_webgl_resource_creation_tests PRIVATE -UNDEBUG)
    add_test(NAME arp_rhi_webgl_resource_creation_tests COMMAND arp_rhi_webgl_resource_creation_tests)
endif()

function(arp_add_webgl_shader_program_tests)
    if(EMSCRIPTEN OR NOT SLANGC_EXECUTABLE OR NOT webgl1_spirv_reflect_tool)
        return()
    endif()
    find_file(ARP_SPIRV_CROSS_LIBRARY NAMES spirv-cross-c-shared.dll
        HINTS "${webgl1_shader_tool_dir}")
    if(NOT ARP_SPIRV_CROSS_LIBRARY)
        return()
    endif()
    set(program_dir "${CMAKE_CURRENT_BINARY_DIR}/webgl1-program")
    set(program "${program_dir}/webgl1_graphics.webgl-program.yaml")
    add_custom_command(OUTPUT "${program}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${webgl1_spirv_reflect_tool}"
            --fixture webgl1_graphics --graphics-module --preserve-implicit-sampling
            --glsl-es-100 --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${program_dir}"
            --webgl-optional-uniform Vertex:arpUbo_0_0.m3
            --webgl-optional-uniform Fragment:arpUbo_0_0.m3
        DEPENDS scripts/compile_shaders.py scripts/webgl_shader_artifact.py scripts/spirv_cross_api.py
            scripts/spirv_layout.py scripts/shader_bindings_codegen.py tests/fixtures/webgl1_graphics.slang
            "${SLANGC_EXECUTABLE}" "${webgl1_spirv_reflect_tool}" "${ARP_SPIRV_CROSS_LIBRARY}"
        VERBATIM)
    set(extension_dir "${program_dir}/extensions")
    set(extension_program "${extension_dir}/webgl1_extensions.webgl-program.yaml")
    add_custom_command(OUTPUT "${extension_program}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${webgl1_spirv_reflect_tool}"
            --fixture webgl1_extensions --graphics-module --preserve-implicit-sampling
            --glsl-es-100 --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${extension_dir}"
        DEPENDS scripts/compile_shaders.py scripts/webgl_shader_artifact.py scripts/spirv_cross_api.py
            scripts/spirv_layout.py scripts/shader_bindings_codegen.py tests/fixtures/webgl1_extensions.slang
            "${SLANGC_EXECUTABLE}" "${webgl1_spirv_reflect_tool}" "${ARP_SPIRV_CROSS_LIBRARY}"
        VERBATIM)
    set(sprite_program "${program_dir}/scene_2d_vertices.webgl-program.yaml")
    add_custom_command(OUTPUT "${sprite_program}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${webgl1_spirv_reflect_tool}"
            --module scene_2d_vertices --graphics-module --glsl-es-100
            --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${program_dir}"
        DEPENDS scripts/compile_shaders.py scripts/webgl_shader_artifact.py scripts/spirv_cross_api.py
            scripts/spirv_layout.py scripts/shader_bindings_codegen.py shaders/2d/sprites.slang
            "${SLANGC_EXECUTABLE}" "${webgl1_spirv_reflect_tool}" "${ARP_SPIRV_CROSS_LIBRARY}"
        VERBATIM)
    add_custom_target(arp_webgl_shader_program_artifacts DEPENDS "${program}" "${extension_program}" "${sprite_program}")
    if(TARGET arp_rhi_conformance_sampled_smoke)
        add_dependencies(arp_rhi_conformance_sampled_smoke arp_webgl_shader_program_artifacts)
    endif()
    add_custom_target(arp_scene2d_shader_publication_tests DEPENDS arp_webgl_shader_program_artifacts)
    add_test(NAME arp_scene2d_shader_publication_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/scene2d_shader_publication_test.py"
            --directory "${program_dir}" --reflect "${webgl1_spirv_reflect_tool}")
    set(gui_program "${program_dir}/gui_bitmap.webgl-program.yaml")
    add_custom_command(OUTPUT "${gui_program}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${webgl1_spirv_reflect_tool}"
            --module gui_bitmap --graphics-module --glsl-es-100
            --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${program_dir}"
        DEPENDS scripts/compile_shaders.py scripts/webgl_shader_artifact.py scripts/spirv_cross_api.py
            scripts/spirv_layout.py scripts/shader_bindings_codegen.py shaders/gui/bitmap.slang
            "${SLANGC_EXECUTABLE}" "${webgl1_spirv_reflect_tool}" "${ARP_SPIRV_CROSS_LIBRARY}"
        VERBATIM)
    add_custom_target(arp_gui_bitmap_shader_publication_tests DEPENDS "${gui_program}")
    set(basic_program "${program_dir}/basic_forward.webgl-program.yaml")
    add_custom_command(OUTPUT "${basic_program}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${webgl1_spirv_reflect_tool}"
            --module basic_forward --graphics-module --glsl-es-100
            --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${program_dir}"
        DEPENDS scripts/compile_shaders.py scripts/webgl_shader_artifact.py scripts/spirv_cross_api.py
            scripts/spirv_layout.py scripts/shader_bindings_codegen.py shaders/basic/forward.slang
            "${SLANGC_EXECUTABLE}" "${webgl1_spirv_reflect_tool}" "${ARP_SPIRV_CROSS_LIBRARY}"
        VERBATIM)
    add_custom_target(arp_basic_forward_shader_artifacts DEPENDS "${basic_program}")
    add_custom_target(arp_basic_forward_shader_publication_tests DEPENDS arp_basic_forward_shader_artifacts)
    add_test(NAME arp_basic_forward_shader_publication_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/scene2d_shader_publication_test.py"
            --directory "${program_dir}" --reflect "${webgl1_spirv_reflect_tool}"
            --module basic_forward --push-bytes 80 --vertex-entry vertexMain --push-stage FS)
    add_dependencies(arp_webgl_shader_program_artifacts arp_basic_forward_shader_artifacts)
    set(basic_depth_program "${program_dir}/basic_depth_receiver.webgl-program.yaml")
    add_custom_command(OUTPUT "${basic_depth_program}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${webgl1_spirv_reflect_tool}"
            --module basic_depth_receiver --graphics-module --glsl-es-100
            --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${program_dir}"
        DEPENDS scripts/compile_shaders.py scripts/webgl_shader_artifact.py scripts/spirv_cross_api.py
            scripts/spirv_layout.py scripts/shader_bindings_codegen.py shaders/basic/depth_receiver.slang
            "${SLANGC_EXECUTABLE}" "${webgl1_spirv_reflect_tool}" "${ARP_SPIRV_CROSS_LIBRARY}"
        VERBATIM)
    add_custom_target(arp_basic_depth_shader_artifacts DEPENDS "${basic_depth_program}")
    add_dependencies(arp_webgl_shader_program_artifacts arp_basic_depth_shader_artifacts)
    add_test(NAME arp_basic_depth_shader_publication_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/scene2d_shader_publication_test.py"
            --directory "${program_dir}" --reflect "${webgl1_spirv_reflect_tool}"
            --module basic_depth_receiver --push-bytes 16 --vertex-entry vertexMain --forbid-required-extensions)
    add_dependencies(arp_webgl_shader_program_artifacts arp_gui_bitmap_shader_publication_tests)
    add_test(NAME arp_gui_bitmap_shader_publication_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/scene2d_shader_publication_test.py"
            --directory "${program_dir}" --reflect "${webgl1_spirv_reflect_tool}"
            --module gui_bitmap --push-bytes 64 --vertex-entry vertexMain)
    set(fragment_program "${program_dir}/environment/editor_viewport_environment.webgl-program.yaml")
    add_custom_command(OUTPUT "${fragment_program}"
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.py"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-reflect "${webgl1_spirv_reflect_tool}"
            --module editor_viewport_environment --graphics-module --glsl-es-100
            --spirv-cross-library "${ARP_SPIRV_CROSS_LIBRARY}" --out-dir "${program_dir}/environment"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor/viewport_environment.slang"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/webgl_shader_artifact.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/spirv_cross_api.py"
        VERBATIM)
    add_custom_target(arp_webgl_fragment_shader_artifacts DEPENDS "${fragment_program}")
    add_executable(arp_webgl_fragment_shader_tests tests/contract/webgl_fragment_shader_tests.cpp)
    target_link_libraries(arp_webgl_fragment_shader_tests PRIVATE arp_shader)
    target_compile_definitions(arp_webgl_fragment_shader_tests PRIVATE ARP_WEBGL_FRAGMENT_PROGRAM="${fragment_program}")
    target_compile_options(arp_webgl_fragment_shader_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_webgl_fragment_shader_tests)
    add_dependencies(arp_webgl_fragment_shader_tests arp_webgl_fragment_shader_artifacts)
    add_test(NAME arp_webgl_fragment_shader_tests COMMAND arp_webgl_fragment_shader_tests)
    add_test(NAME arp_webgl_fragment_shader_negative_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/webgl_fragment_shader_negative_tests.py"
            --exe "$<TARGET_FILE:arp_webgl_fragment_shader_tests>" --directory "${program_dir}/environment")
    add_executable(arp_webgl_shader_program_tests tests/contract/webgl_shader_program_tests.cpp)
    target_link_libraries(arp_webgl_shader_program_tests PRIVATE arp_shader)
    target_compile_definitions(arp_webgl_shader_program_tests PRIVATE ARP_WEBGL_PROGRAM_DIR="${program_dir}")
    target_compile_options(arp_webgl_shader_program_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_webgl_shader_program_tests)
    add_dependencies(arp_webgl_shader_program_tests arp_webgl_shader_program_artifacts)
    add_test(NAME arp_webgl_shader_program_tests COMMAND arp_webgl_shader_program_tests)
    add_custom_target(arp_webgl_shader_program_negative_tests DEPENDS arp_webgl_shader_program_tests)
    add_test(NAME arp_webgl_shader_program_negative_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl_shader_program_contract_tests.py"
            --exe "$<TARGET_FILE:arp_webgl_shader_program_tests>" --directory "${program_dir}"
            --library "${ARP_SPIRV_CROSS_LIBRARY}" --slangc "${SLANGC_EXECUTABLE}"
            --reflect "${webgl1_spirv_reflect_tool}")
    find_program(ARP_WEBGL_PROGRAM_BROWSER NAMES chrome.exe msedge.exe
        HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
    if(ARP_WEBGL_PROGRAM_BROWSER)
        add_custom_target(arp_webgl_shader_program_browser_tests DEPENDS arp_webgl_shader_program_tests)
        add_test(NAME arp_webgl_shader_program_browser_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/webgl_shader_program_browser.py"
                --exe "$<TARGET_FILE:arp_webgl_shader_program_tests>" --directory "${program_dir}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/webgl1-program-browser-test" --browser "${ARP_WEBGL_PROGRAM_BROWSER}")
    endif()
    if(TARGET arp_shader_tests)
        add_dependencies(arp_shader_tests arp_webgl_shader_program_artifacts)
        target_compile_definitions(arp_shader_tests PRIVATE ARP_WEBGL_PROGRAM_DIR="${program_dir}")
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_webgl_shader_program_tests)

function(arp_add_device_capability_seed_contract_tests)
if(NOT EMSCRIPTEN AND TARGET arp_rhi_vulkan)
    add_executable(arp_rhi_device_capability_seed_contract_tests
        tests/contract/rhi_device_capability_seed_contract_tests.cpp)
    target_include_directories(arp_rhi_device_capability_seed_contract_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(arp_rhi_device_capability_seed_contract_tests PRIVATE arp_rhi_vulkan)
    arp_add_mingw_static_runtime(arp_rhi_device_capability_seed_contract_tests)
    if(MSVC)
        target_compile_options(arp_rhi_device_capability_seed_contract_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_rhi_device_capability_seed_contract_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME arp_rhi_device_capability_seed_contract_tests
        COMMAND arp_rhi_device_capability_seed_contract_tests)
endif()
if(TARGET arp_rhi_device_validation_tests)
    if(MSVC)
        target_compile_options(arp_rhi_device_validation_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(arp_rhi_device_validation_tests PRIVATE -UNDEBUG)
    endif()
endif()
endfunction()
cmake_language(DEFER CALL arp_add_device_capability_seed_contract_tests)

function(arp_add_shutdown_completion_contract_tests)
    if(TARGET arp_rhi_vulkan_completion_tests)
        foreach(mode IN ITEMS lease placed)
            add_test(NAME arp_rhi_vulkan_heap_owner_${mode}_tests
                COMMAND arp_rhi_vulkan_completion_tests heap-owner-${mode})
            set_tests_properties(arp_rhi_vulkan_heap_owner_${mode}_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endforeach()
        foreach(mode IN ITEMS independent pending wrong-device unproven admission)
            add_test(NAME arp_rhi_vulkan_timeline_owner_${mode}_tests
                COMMAND arp_rhi_vulkan_completion_tests timeline-owner-${mode})
            set_tests_properties(arp_rhi_vulkan_timeline_owner_${mode}_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endforeach()
        foreach(resource IN ITEMS buffer image descriptor sampler allocation-rollback registered-buffer registered-image)
            add_test(NAME arp_rhi_vulkan_resource_owner_${resource}_tests
                COMMAND arp_rhi_vulkan_completion_tests resource-owner-${resource})
            set_tests_properties(arp_rhi_vulkan_resource_owner_${resource}_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endforeach()
        add_test(NAME arp_rhi_vulkan_native_owner_lifetime_tests
            COMMAND arp_rhi_vulkan_completion_tests runtime-owner)
        set_tests_properties(arp_rhi_vulkan_native_owner_lifetime_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 30)
        add_test(NAME arp_rhi_vulkan_scoped_present_retirement_tests
            COMMAND arp_rhi_vulkan_completion_tests scoped-present)
        set_tests_properties(arp_rhi_vulkan_scoped_present_retirement_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 30)
        add_test(NAME arp_rhi_vulkan_shutdown_completion_tests
            COMMAND arp_rhi_vulkan_completion_tests shutdown-pending)
        set_tests_properties(arp_rhi_vulkan_shutdown_completion_tests PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 30)
        foreach(mode IN ITEMS same-runtime other-runtime pending pool-release)
            add_test(NAME arp_rhi_vulkan_staging_${mode}_tests
                COMMAND arp_rhi_vulkan_completion_tests staging-${mode})
            set_tests_properties(arp_rhi_vulkan_staging_${mode}_tests PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 30)
        endforeach()
    endif()
endfunction()
cmake_language(DEFER CALL arp_add_shutdown_completion_contract_tests)

add_executable(arp_rhi_webgl_transfer_lowering_contract_tests
    tests/contract/rhi_webgl_transfer_lowering_contract_tests.cpp)
target_link_libraries(arp_rhi_webgl_transfer_lowering_contract_tests PRIVATE
    arp_rhi arp_rhi_semantics)
arp_add_mingw_static_runtime(arp_rhi_webgl_transfer_lowering_contract_tests)
if(MSVC)
    target_compile_options(arp_rhi_webgl_transfer_lowering_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_webgl_transfer_lowering_contract_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_rhi_webgl_transfer_lowering_contract_tests
    COMMAND arp_rhi_webgl_transfer_lowering_contract_tests)

add_executable(arp_rhi_fallback_status_admission_contract_tests
    tests/contract/rhi_fallback_status_admission_contract_tests.cpp)
target_link_libraries(arp_rhi_fallback_status_admission_contract_tests PRIVATE
    arp_renderer_deployment)
arp_add_mingw_static_runtime(arp_rhi_fallback_status_admission_contract_tests)
if(MSVC)
    target_compile_options(arp_rhi_fallback_status_admission_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_fallback_status_admission_contract_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_rhi_fallback_status_admission_contract_tests
    COMMAND arp_rhi_fallback_status_admission_contract_tests)

add_executable(arp_renderer_deployment_evidence_contract_tests
    tests/contract/renderer_deployment_evidence_contract_tests.cpp
    src/platform/common/render_system_frame_loop.cpp)
target_link_libraries(arp_renderer_deployment_evidence_contract_tests PRIVATE
    arp_renderer_deployment)
arp_add_mingw_static_runtime(arp_renderer_deployment_evidence_contract_tests)
if(MSVC)
    target_compile_options(arp_renderer_deployment_evidence_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_renderer_deployment_evidence_contract_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_renderer_deployment_evidence_contract_tests
    COMMAND arp_renderer_deployment_evidence_contract_tests)

add_executable(arp_shader_compile_target_contract_tests
    tests/contract/shader_compile_target_contract_tests.cpp)
target_link_libraries(arp_shader_compile_target_contract_tests PRIVATE arp_shader)
if(MSVC)
    target_compile_options(arp_shader_compile_target_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_shader_compile_target_contract_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_shader_compile_target_contract_tests
    COMMAND arp_shader_compile_target_contract_tests)

add_executable(arp_scene_runtime_rhi_planner_tests
    tests/integration/scene_runtime_rhi_planner_tests.cpp
    tests/fixtures/scene_runtime_legacy_report_consumer.cpp)
target_include_directories(arp_scene_runtime_rhi_planner_tests PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(arp_scene_runtime_rhi_planner_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_renderer,arp_render_graph,arp_renderer_rhi_contracts,arp_render_graph_rhi_plan,arp_rhi,arp_rhi_semantics>")
else()
    target_link_libraries(arp_scene_runtime_rhi_planner_tests PRIVATE arp_renderer)
endif()
target_link_libraries(arp_scene_runtime_rhi_planner_tests PRIVATE arp_scriptvm_runtime)
target_compile_definitions(arp_scene_runtime_rhi_planner_tests PRIVATE ARP_STATIC_LINK=1)
if(MSVC)
    target_compile_options(arp_scene_runtime_rhi_planner_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_scene_runtime_rhi_planner_tests PRIVATE -UNDEBUG)
endif()

add_executable(arp_renderer_backend_reports_contract_tests
    tests/contract/renderer_backend_reports_contract_tests.cpp)
target_include_directories(arp_renderer_backend_reports_contract_tests PRIVATE src)
target_link_libraries(arp_renderer_backend_reports_contract_tests PRIVATE arp_core arp_fkyaml_headers)
target_compile_definitions(arp_renderer_backend_reports_contract_tests PRIVATE ARP_STATIC_LINK=1)
if(MSVC)
    target_compile_options(arp_renderer_backend_reports_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_renderer_backend_reports_contract_tests PRIVATE -UNDEBUG)
endif()

add_executable(arp_render_system_rhi_production_migration_tests
    tests/integration/render_system_rhi_production_migration_tests.cpp
    src/platform/common/render_system_rhi_production_migration.cpp)
target_include_directories(arp_render_system_rhi_production_migration_tests PRIVATE src)
target_link_libraries(arp_render_system_rhi_production_migration_tests PRIVATE arp_rhi arp_core)
target_compile_definitions(arp_render_system_rhi_production_migration_tests PRIVATE ARP_STATIC_LINK=1)
if(MSVC)
    target_compile_options(arp_render_system_rhi_production_migration_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_render_system_rhi_production_migration_tests PRIVATE -UNDEBUG)
endif()

add_executable(arp_rhi_webgl_descriptor_lowering_contract_tests
    tests/contract/rhi_webgl_descriptor_lowering_contract_tests.cpp)
target_include_directories(arp_rhi_webgl_descriptor_lowering_contract_tests PRIVATE src)
target_link_libraries(arp_rhi_webgl_descriptor_lowering_contract_tests PRIVATE arp_core arp_fkyaml_headers)
if(MSVC)
    target_compile_options(arp_rhi_webgl_descriptor_lowering_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_webgl_descriptor_lowering_contract_tests PRIVATE -UNDEBUG)
endif()

add_executable(arp_rhi_webgl_resource_lifetime_contract_tests
    tests/contract/rhi_webgl_resource_lifetime_contract_tests.cpp)
target_include_directories(arp_rhi_webgl_resource_lifetime_contract_tests PRIVATE src)
target_link_libraries(arp_rhi_webgl_resource_lifetime_contract_tests PRIVATE arp_core arp_fkyaml_headers)
if(MSVC)
    target_compile_options(arp_rhi_webgl_resource_lifetime_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_webgl_resource_lifetime_contract_tests PRIVATE -UNDEBUG)
endif()

add_executable(arp_render_system_backend_selection_contract_tests
    tests/contract/render_system_backend_selection_contract_tests.cpp)
target_include_directories(arp_render_system_backend_selection_contract_tests PRIVATE src)
target_link_libraries(arp_render_system_backend_selection_contract_tests PRIVATE arp_core)
if(MSVC)
    target_compile_options(arp_render_system_backend_selection_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_render_system_backend_selection_contract_tests PRIVATE -UNDEBUG)
endif()
add_executable(arp_render_system_ordered_profile_tests
    tests/contract/render_system_ordered_profile_tests.cpp)
target_link_libraries(arp_render_system_ordered_profile_tests PRIVATE arp_core arp_fkyaml_headers)
if(MSVC)
    target_compile_options(arp_render_system_ordered_profile_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_render_system_ordered_profile_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME arp_render_system_ordered_profile_tests COMMAND arp_render_system_ordered_profile_tests)
add_executable(arp_rhi_ordered_issue_contract_tests
    tests/contract/rhi_ordered_issue_contract_tests.cpp)
target_include_directories(arp_rhi_ordered_issue_contract_tests PRIVATE src)
target_link_libraries(arp_rhi_ordered_issue_contract_tests PRIVATE arp_core)
if(MSVC)
    target_compile_options(arp_rhi_ordered_issue_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_ordered_issue_contract_tests PRIVATE -UNDEBUG)
endif()
add_executable(arp_rhi_webgl_profile_contract_tests
    tests/contract/rhi_webgl_profile_contract_tests.cpp)
add_executable(arp_rhi_webgl_fragment_depth_tests tests/contract/rhi_webgl_fragment_depth_tests.cpp)
target_include_directories(arp_rhi_webgl_fragment_depth_tests PRIVATE src)
target_link_libraries(arp_rhi_webgl_fragment_depth_tests PRIVATE arp_core)
target_compile_options(arp_rhi_webgl_fragment_depth_tests PRIVATE -UNDEBUG)
add_test(NAME arp_rhi_webgl_fragment_depth_tests COMMAND arp_rhi_webgl_fragment_depth_tests)
add_executable(arp_ordered_graphics_catalog_tests tests/contract/ordered_graphics_catalog_tests.cpp)
target_include_directories(arp_ordered_graphics_catalog_tests PRIVATE src)
target_link_libraries(arp_ordered_graphics_catalog_tests PRIVATE arp_core)
target_compile_options(arp_ordered_graphics_catalog_tests PRIVATE -UNDEBUG)
add_test(NAME arp_ordered_graphics_catalog_tests COMMAND arp_ordered_graphics_catalog_tests)
add_executable(arp_rhi_webgl_srgb_tests tests/contract/rhi_webgl_srgb_tests.cpp)
target_include_directories(arp_rhi_webgl_srgb_tests PRIVATE src)
target_link_libraries(arp_rhi_webgl_srgb_tests PRIVATE arp_core)
target_compile_options(arp_rhi_webgl_srgb_tests PRIVATE -UNDEBUG)
add_test(NAME arp_rhi_webgl_srgb_tests COMMAND arp_rhi_webgl_srgb_tests)

add_executable(arp_rhi_conformance_clear_workload_tests
    tests/contract/rhi_conformance_clear_workload_tests.cpp)
target_include_directories(arp_rhi_conformance_clear_workload_tests PRIVATE src)
target_link_libraries(arp_rhi_conformance_clear_workload_tests PRIVATE arp_rhi arp_core)
target_compile_definitions(arp_rhi_conformance_clear_workload_tests PRIVATE
    ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_rhi_conformance_clear_workload_tests PRIVATE -UNDEBUG)
add_test(NAME arp_rhi_conformance_clear_workload_tests COMMAND arp_rhi_conformance_clear_workload_tests)
set_tests_properties(arp_rhi_conformance_clear_workload_tests PROPERTIES LABELS "host-contract;conformance")

add_executable(arp_rhi_conformance_sampled_workload_tests tests/contract/rhi_conformance_sampled_workload_tests.cpp)
target_include_directories(arp_rhi_conformance_sampled_workload_tests PRIVATE src)
target_link_libraries(arp_rhi_conformance_sampled_workload_tests PRIVATE arp_rhi arp_core)
target_compile_definitions(arp_rhi_conformance_sampled_workload_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_rhi_conformance_sampled_workload_tests PRIVATE -UNDEBUG)
add_test(NAME arp_rhi_conformance_sampled_workload_tests COMMAND arp_rhi_conformance_sampled_workload_tests)
set_tests_properties(arp_rhi_conformance_sampled_workload_tests PROPERTIES LABELS "host-contract;conformance")
target_include_directories(arp_rhi_webgl_profile_contract_tests PRIVATE src)
if(NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(arp_rhi_webgl_profile_contract_tests PRIVATE
        "$<LINK_GROUP:RESCAN,arp_rhi,arp_rhi_semantics>")
else()
    target_link_libraries(arp_rhi_webgl_profile_contract_tests PRIVATE arp_rhi arp_rhi_semantics)
endif()
if(MSVC)
    target_compile_options(arp_rhi_webgl_profile_contract_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(arp_rhi_webgl_profile_contract_tests PRIVATE -UNDEBUG)
endif()

# Feasibility shader artifacts are test fixtures, never executable packages.
# Host tools are shared with the existing Slang/reflection compilation path.
get_filename_component(webgl1_shader_tool_dir "${SLANGC_EXECUTABLE}" DIRECTORY)
find_program(SPIRV_CROSS_EXECUTABLE NAMES spirv-cross.exe spirv-cross
    HINTS "${webgl1_shader_tool_dir}")
if(SPIRV_REFLECT_EXECUTABLE)
    set(webgl1_spirv_reflect_tool "${SPIRV_REFLECT_EXECUTABLE}")
else()
    find_program(WEBGL1_SPIRV_REFLECT_EXECUTABLE NAMES spirv-reflect.exe spirv-reflect
        HINTS "${webgl1_shader_tool_dir}")
    set(webgl1_spirv_reflect_tool "${WEBGL1_SPIRV_REFLECT_EXECUTABLE}")
endif()
if(Python3_EXECUTABLE AND SLANGC_EXECUTABLE AND webgl1_spirv_reflect_tool
    AND SPIRV_CROSS_EXECUTABLE AND NOT EMSCRIPTEN)
    set(ARP_WEBGL1_PROBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/webgl1-shader-probe")
    set(webgl1_probe_outputs "${ARP_WEBGL1_PROBE_DIR}/reflection.yaml")
    foreach(stage vert frag)
        foreach(suffix "" .spv .metal .wgsl)
            list(APPEND webgl1_probe_outputs "${ARP_WEBGL1_PROBE_DIR}/probe.${stage}${suffix}")
        endforeach()
    endforeach()
    add_custom_command(OUTPUT ${webgl1_probe_outputs}
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/webgl_shader_codegen.py"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgl1_graphics_probe.slang"
            --out-dir "${ARP_WEBGL1_PROBE_DIR}"
            --slangc "${SLANGC_EXECUTABLE}" --spirv-cross "${SPIRV_CROSS_EXECUTABLE}"
            --spirv-reflect "${webgl1_spirv_reflect_tool}"
        DEPENDS tests/fixtures/webgl1_graphics_probe.slang scripts/webgl_shader_codegen.py
            scripts/compile_shaders.py scripts/shader_bindings_codegen.py scripts/spirv_layout.py
            "${SLANGC_EXECUTABLE}" "${SPIRV_CROSS_EXECUTABLE}" "${webgl1_spirv_reflect_tool}"
        VERBATIM)
    add_custom_target(arp_webgl1_shader_probe_artifacts DEPENDS ${webgl1_probe_outputs})
    add_custom_target(arp_shader_webgl1_contract_tests DEPENDS arp_webgl1_shader_probe_artifacts)
    add_test(NAME arp_shader_webgl1_contract_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/webgl1_shader_probe_contract_tests.py"
            --directory "${ARP_WEBGL1_PROBE_DIR}"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/webgl1_graphics_probe.slang")
endif()
if(NOT EMSCRIPTEN)
    add_test(NAME arp_scene_runtime_rhi_planner_tests COMMAND arp_scene_runtime_rhi_planner_tests)
    add_test(NAME arp_renderer_backend_reports_contract_tests
        COMMAND arp_renderer_backend_reports_contract_tests)
    add_test(NAME arp_render_system_rhi_production_migration_tests
        COMMAND arp_render_system_rhi_production_migration_tests)
    add_test(NAME arp_rhi_webgl_descriptor_lowering_contract_tests
        COMMAND arp_rhi_webgl_descriptor_lowering_contract_tests)
    add_test(NAME arp_rhi_webgl_resource_lifetime_contract_tests
        COMMAND arp_rhi_webgl_resource_lifetime_contract_tests)
    add_test(NAME arp_render_system_backend_selection_contract_tests
        COMMAND arp_render_system_backend_selection_contract_tests)
    add_test(NAME arp_rhi_ordered_issue_contract_tests COMMAND arp_rhi_ordered_issue_contract_tests)
    add_test(NAME arp_rhi_webgl_profile_contract_tests COMMAND arp_rhi_webgl_profile_contract_tests)
    if(Python3_EXECUTABLE)
        add_test(NAME arp_webgl1_visual_validation_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl1_visual_validation_tests.py")
        set_tests_properties(arp_webgl1_visual_validation_tests PROPERTIES LABELS "host-contract;tooling;webgl1")
        include("${CMAKE_CURRENT_LIST_DIR}/ArpWebGlVisualValidation.cmake")
        add_test(NAME arp_webgl_raf_wait_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl_raf_wait_tests.py")
        set_tests_properties(arp_webgl_raf_wait_tests PROPERTIES LABELS "host-contract;tooling")
        add_test(NAME arp_emscripten_preload_package_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/emscripten_preload_package_tests.py")
        add_test(NAME arp_webgl_shader_program_replay_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl_shader_program_replay_tests.py")
        add_test(NAME arp_browser_process_retirement_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/browser_process_retirement_test.py")
        add_test(NAME arp_webgl1_graphics_scope_contract_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl1_graphics_scope_contract_tests.py")
        add_test(NAME arp_preset_build_admission_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/preset_build_admission_tests.py")
        add_test(NAME arp_cmake_build_cache_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/cmake_build_cache_tests.py")
        add_test(NAME arp_web_rhi_build_selection_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/web_rhi_build_selection_tests.py"
                --cmake "${CMAKE_COMMAND}")
        add_test(NAME arp_rhi_backend_inventory_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/rhi_backend_inventory_tests.py")
        add_test(NAME arp_webgl1_probe_build_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl1_probe_build_tests.py")
        add_test(NAME arp_webgl1_probe_report_contract_tests
            COMMAND "${Python3_EXECUTABLE}" -X utf8
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl1_probe_report_contract_tests.py")
    endif()
endif()

add_executable(arp_rhi_conformance_storage_mrt_workload_tests tests/contract/rhi_conformance_storage_mrt_workload_tests.cpp)
target_include_directories(arp_rhi_conformance_storage_mrt_workload_tests PRIVATE src)
target_link_libraries(arp_rhi_conformance_storage_mrt_workload_tests PRIVATE arp_rhi arp_core)
target_compile_definitions(arp_rhi_conformance_storage_mrt_workload_tests PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(arp_rhi_conformance_storage_mrt_workload_tests PRIVATE -UNDEBUG)
add_test(NAME arp_rhi_conformance_storage_mrt_workload_tests COMMAND arp_rhi_conformance_storage_mrt_workload_tests)
set_tests_properties(arp_rhi_conformance_storage_mrt_workload_tests PROPERTIES LABELS "host-contract;conformance")

function(arp_add_webgl_product_performance_tests)
    add_test(NAME arp_webgl_product_performance_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/webgl_product_performance_tests.py")
endfunction()
cmake_language(DEFER CALL arp_add_webgl_product_performance_tests)
