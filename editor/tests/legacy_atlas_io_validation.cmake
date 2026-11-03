# CPU-only byte/lifetime fixtures; no editor application or GPU is launched.
if(WIN32 OR EMSCRIPTEN)
    add_executable(arp_editor_legacy_atlas_io_tests tests/legacy_atlas_io_tests.cpp ui/editor_ui_assets.cpp)
    target_link_libraries(arp_editor_legacy_atlas_io_tests PRIVATE
        arp_editor_shell arp_editor_ui_sources arp_editor_cold_host_read)
    target_compile_options(arp_editor_legacy_atlas_io_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_editor_legacy_atlas_io_tests)
    if(EMSCRIPTEN)
        find_program(ARP_LEGACY_ATLAS_NODE NAMES node REQUIRED)
        set(legacy_atlas_profile web)
        set(legacy_atlas_runner --node "${ARP_LEGACY_ATLAS_NODE}")
        # Explicitly reuse the existing Web Legacy font paths and source bindings.
        set(legacy_atlas_inputs
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/editor_atlas.png"
            "${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/scene_2d/atlas.png"
            "${ARP_EDITOR_ENGINE_ROOT}/3rd/JoltPhysics-5.6.0/Assets/Fonts/Roboto-Regular.ttf"
            "${ARP_EDITOR_WEB_MONO_FONT}" "${ARP_EDITOR_WEB_CJK_FONT}")
        foreach(input IN LISTS legacy_atlas_inputs)
            if(NOT EXISTS "${input}")
                message(FATAL_ERROR "ARP-LEGACY-ATLAS-FIXTURE-INPUT-MISSING: ${input}")
            endif()
        endforeach()
        target_compile_definitions(arp_editor_legacy_atlas_io_tests PRIVATE
            ARP_LEGACY_ATLAS_ROOT="/legacy-atlas-original"
            ARP_LEGACY_ATLAS_WRONG_IMAGE="/legacy-atlas-wrong.png")
        target_link_options(arp_editor_legacy_atlas_io_tests PRIVATE
            # Full Shell registration uses the existing Web Editor's 4 MiB stack.
            -sASSERTIONS=1 -sSTACK_SIZE=4194304 -sSTACK_OVERFLOW_CHECK=2
            -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=0 -sABORTING_MALLOC=0
            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/resources/editor_atlas.png@/legacy-atlas-original/editor_atlas.png"
            "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/scene_2d/atlas.png@/legacy-atlas-wrong.png"
            "--preload-file=${ARP_EDITOR_ENGINE_ROOT}/3rd/JoltPhysics-5.6.0/Assets/Fonts/Roboto-Regular.ttf@/fonts/editor.ttf"
            "--preload-file=${ARP_EDITOR_WEB_MONO_FONT}@/fonts/code.ttf"
            "--preload-file=${ARP_EDITOR_WEB_CJK_FONT}@/fonts/cjk.ttf")
        set_property(TARGET arp_editor_legacy_atlas_io_tests APPEND PROPERTY LINK_DEPENDS ${legacy_atlas_inputs})
    else()
        set(legacy_atlas_profile windows)
        set(legacy_atlas_runner)
        target_compile_definitions(arp_editor_legacy_atlas_io_tests PRIVATE
            ARP_LEGACY_ATLAS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/resources"
            ARP_LEGACY_ATLAS_WRONG_IMAGE="${ARP_EDITOR_ENGINE_ROOT}/tests/fixtures/scene_2d/atlas.png")
    endif()
    add_test(NAME arp_editor_legacy_atlas_io_tests COMMAND arp_editor_legacy_atlas_io_tests)
    add_test(NAME arp_editor_legacy_atlas_io_smoke COMMAND arp_editor_legacy_atlas_io_tests --smoke)
    add_test(NAME arp_editor_legacy_atlas_io_bytes_tests
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy_atlas_io_bytes_tests.py"
            --executable "$<TARGET_FILE:arp_editor_legacy_atlas_io_tests>" ${legacy_atlas_runner}
            --profile "${legacy_atlas_profile}"
            --report "${CMAKE_BINARY_DIR}/unified-io-evidence/i3-legacy-ui-atlas-cold-read/byte-comparison.yaml")
    set_tests_properties(arp_editor_legacy_atlas_io_tests arp_editor_legacy_atlas_io_smoke
        arp_editor_legacy_atlas_io_bytes_tests PROPERTIES
        # Includes 21 full atlas builds and 24 MiB SHA probes in the ordinary smoke.
        TIMEOUT 300 LABELS "editor;io;cpu;legacy-atlas")
endif()
