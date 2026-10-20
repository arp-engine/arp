if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    enable_testing()
    add_test(NAME arp_scriptvm_build_graph_contract
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/scriptvm_build_graph_contract.py"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME arp_scriptvm_aot_source_boundary
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptVmSourceBoundary.cmake")
    add_test(NAME arp_scriptvm_aot_v5_tests COMMAND arp_scriptvm_aot_v5_tests)
    add_test(NAME arp_scriptvm_aot_only_link_smoke
        COMMAND arp_scriptvm_aot_only_link_smoke)
    add_test(NAME arp_scriptvm_aot_v5_generated_smoke
        COMMAND arp_scriptvm_aot_v5_generated_smoke)
    add_test(NAME arp_scriptvm_aot_v5_cross_backend_smoke
        COMMAND arp_scriptvm_aot_v5_cross_backend_smoke)
    add_test(NAME arp_scriptvm_aot_v5_randomized_differential
        COMMAND arp_scriptvm_aot_v5_randomized_differential_tests)
    add_test(NAME arp_scriptvm_aot_v5_scene_behavior_smoke
        COMMAND arp_scriptvm_aot_v5_scene_behavior_smoke)
    if(TARGET arp_scriptvm_aot_v5_dynamic_package_smoke)
        add_test(NAME arp_scriptvm_aot_v5_dynamic_package_smoke
            COMMAND arp_scriptvm_aot_v5_dynamic_package_smoke)
    endif()
    if(TARGET arp_scriptvm_editor_hot_update_hidden_smoke)
        add_test(NAME arp_scriptvm_editor_hot_update_hidden_smoke
            COMMAND arp_scriptvm_editor_hot_update_hidden_smoke)
    endif()
    add_test(NAME arp_script_aotc_validate
        COMMAND arp_script_aotc validate
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml")
    add_test(NAME arp_script_aotc_report
        COMMAND arp_script_aotc report
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml"
            --output "${CMAKE_CURRENT_BINARY_DIR}/tests/script-aot-report")
    add_test(NAME arp_script_aotc_unity_import_source_inventory
        COMMAND "${CMAKE_COMMAND}"
            -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/tools/unity_import/aot.package.yaml
            -DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/tools/unity_import
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotSourceInventory.cmake")
    add_test(NAME arp_script_aotc_deterministic_outputs
        COMMAND "${CMAKE_COMMAND}"
            -DAOTC=$<TARGET_FILE:arp_script_aotc>
            -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/aot/basic.package.yaml
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/tests/script-aot-deterministic
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotDeterministicOutputs.cmake")
    if(TARGET arp_asset_importer_aot_v5_smoke)
        add_test(NAME arp_asset_importer_aot_v5_smoke
            COMMAND arp_asset_importer_aot_v5_smoke)
    endif()
else()
    enable_testing()
    add_test(NAME arp_scriptvm_build_graph_contract_wasm
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/tooling/scriptvm_build_graph_contract.py"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME arp_scriptvm_aot_source_boundary_wasm
        COMMAND "${CMAKE_COMMAND}"
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptVmSourceBoundary.cmake")
    find_program(ARP_NODE_EXECUTABLE node)
    if(ARP_NODE_EXECUTABLE)
        enable_testing()
        add_test(NAME arp_scriptvm_aot_v5_tests_wasm
            COMMAND "${ARP_NODE_EXECUTABLE}" "$<TARGET_FILE:arp_scriptvm_aot_v5_tests>")
        add_test(NAME arp_scriptvm_aot_v5_generated_smoke_wasm
            COMMAND "${ARP_NODE_EXECUTABLE}"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_generated_smoke>")
        add_test(NAME arp_scriptvm_aot_v5_cross_backend_smoke_wasm
            COMMAND "${ARP_NODE_EXECUTABLE}"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_cross_backend_smoke>")
        add_test(NAME arp_scriptvm_aot_v5_randomized_differential_wasm
            COMMAND "${ARP_NODE_EXECUTABLE}"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_randomized_differential_tests>")
        add_test(NAME arp_scriptvm_aot_v5_endurance_smoke_wasm
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench/scriptvm_aot_v5_endurance.py"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_randomized_differential_tests>"
                --node "${ARP_NODE_EXECUTABLE}"
                --duration-seconds 1 --seed 0xA075EED)
        add_test(NAME arp_scriptvm_stress_web_performance_corpus
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench/scriptvm_stress_benchmark.py"
                "$<TARGET_FILE:arp_scriptvm_stress_benchmark>"
                --node "${ARP_NODE_EXECUTABLE}"
                --scale 1 --repeat 3)
        add_custom_target(arp_scriptvm_aot_v5_nightly_endurance
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench/scriptvm_aot_v5_endurance.py"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_randomized_differential_tests>"
                --node "${ARP_NODE_EXECUTABLE}"
                --duration-seconds 3600 --seed 0xA075EED
            DEPENDS arp_scriptvm_aot_v5_randomized_differential_tests
            USES_TERMINAL)
        add_custom_target(arp_scriptvm_aot_v5_release_candidate_endurance
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/bench/scriptvm_aot_v5_endurance.py"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_randomized_differential_tests>"
                --node "${ARP_NODE_EXECUTABLE}"
                --duration-seconds 28800 --seed 0xA075EED
            DEPENDS arp_scriptvm_aot_v5_randomized_differential_tests
            USES_TERMINAL)
        add_test(NAME arp_scriptvm_aot_v5_scene_behavior_smoke_wasm
            COMMAND "${ARP_NODE_EXECUTABLE}"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_scene_behavior_smoke>")
        add_test(NAME arp_scriptvm_aot_v5_release_audit
            COMMAND "${ARP_NODE_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_scriptvm_aot_v5_release.mjs"
                "$<TARGET_FILE:arp_scriptvm_aot_v5_generated_smoke>"
                "$<TARGET_FILE_DIR:arp_scriptvm_aot_v5_generated_smoke>/arp_scriptvm_aot_v5_generated_smoke.wasm"
                "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic/package.aot-v5.yaml"
                "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic/package.report.txt"
                "${CMAKE_CURRENT_BINARY_DIR}/generated/script-aot/basic"
                "${CMAKE_CURRENT_BINARY_DIR}/arp_scriptvm_aot_v5_generated_smoke.map"
                "${CMAKE_CURRENT_BINARY_DIR}/scriptvm-aot-v5-release-audit.yaml")
        # Prefer a full Chrome installation when present. Some managed Edge
        # builds exit successfully without producing --dump-dom output in
        # headless mode, which turns a valid browser smoke into an empty
        # result. Edge remains the Windows fallback.
        set(ARP_SCRIPT_BROWSER_EXECUTABLE "")
        if(CMAKE_HOST_WIN32 AND EXISTS "C:/Program Files/Google/Chrome/Application/chrome.exe")
            set(ARP_SCRIPT_BROWSER_EXECUTABLE
                "C:/Program Files/Google/Chrome/Application/chrome.exe")
        elseif(CMAKE_HOST_WIN32 AND EXISTS "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe")
            set(ARP_SCRIPT_BROWSER_EXECUTABLE
                "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe")
        endif()
        if(ARP_SCRIPT_BROWSER_EXECUTABLE)
            add_test(NAME arp_scriptvm_aot_v5_browser_smoke
                COMMAND "${ARP_NODE_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify_scriptvm_aot_v5_browser.mjs"
                    "$<TARGET_FILE_DIR:arp_scriptvm_aot_v5_generated_smoke>"
                    "${ARP_SCRIPT_BROWSER_EXECUTABLE}")
        endif()
        add_test(NAME arp_script_aotc_unity_import_source_inventory
            COMMAND "${CMAKE_COMMAND}"
                -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/tools/unity_import/aot.package.yaml
                -DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/tools/unity_import
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyScriptAotSourceInventory.cmake")
    endif()
endif()
