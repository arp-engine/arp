if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR NOT ARP_WEB_RHI STREQUAL "webgl1")
    message(FATAL_ERROR "RHI-WEBGL1-PROTOTYPE-PRODUCT-SELECTION")
endif()
set(ARP_WEBGL_PROGRAM_DIRECTORY "" CACHE PATH "Original host-generated Slang WebGL program publication")
option(ARP_WEBGL1_QUEEN_GAME "Build the playable Queen WebGL1 Prototype" OFF)
if(ARP_WEBGL1_QUEEN_GAME)
    include("${CMAKE_CURRENT_LIST_DIR}/ArpWebGlComponentLibraries.cmake")
    arp_add_webgl_component_libraries()
    include("${CMAKE_CURRENT_LIST_DIR}/ArpBuildAQueenWebGl.cmake")
    return()
endif()
if(NOT EXISTS "${ARP_WEBGL_PROGRAM_DIRECTORY}/webgl1_graphics.webgl-program.yaml")
    message(FATAL_ERROR "RHI-WEBGL1-PROTOTYPE-PRODUCT-ORIGINAL-PROGRAM")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/ArpWebGlComponentLibraries.cmake")
arp_add_webgl_component_libraries()
add_executable(arp_webgl1_runtime_validation "${CMAKE_CURRENT_LIST_DIR}/../tests/integration/webgl_pipeline_smoke.cpp")
target_link_libraries(arp_webgl1_runtime_validation PRIVATE arp_rhi_webgl arp_sprite_product)
target_include_directories(arp_webgl1_runtime_validation PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
target_compile_definitions(arp_webgl1_runtime_validation PRIVATE ARP_SOURCE_DIR="")
target_link_options(arp_webgl1_runtime_validation PRIVATE -sASSERTIONS=1 -sEXIT_RUNTIME=0
    -sEXPORTED_RUNTIME_METHODS=callMain -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=262144
    -sSTACK_SIZE=1048576 -sSTACK_OVERFLOW_CHECK=2
    -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/arp_webgl1_runtime_validation.map"
    "SHELL:--preload-file \"${ARP_WEBGL_PROGRAM_DIRECTORY}@/artifacts\""
    "SHELL:--preload-file \"${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/scene_2d@/tests/fixtures/scene_2d\""
    "SHELL:--preload-file \"${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_clear.yaml@/tests/fixtures/rhi_conformance_clear.yaml\""
    "SHELL:--preload-file \"${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_sampled.yaml@/tests/fixtures/rhi_conformance_sampled.yaml\""
        "SHELL:--preload-file \"${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_pingpong.yaml@/tests/fixtures/rhi_conformance_pingpong.yaml\""
        "SHELL:--preload-file \"${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_storage_mrt.yaml@/tests/fixtures/rhi_conformance_storage_mrt.yaml\"")
set_target_properties(arp_webgl1_runtime_validation PROPERTIES SUFFIX ".js")
arp_webgl_track_preload_inputs(arp_webgl1_runtime_validation
    "${ARP_WEBGL_PROGRAM_DIRECTORY}" "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/scene_2d"
    "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_clear.yaml"
    "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_sampled.yaml"
        "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_pingpong.yaml"
    "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/rhi_conformance_storage_mrt.yaml")
file(READ "${CMAKE_CURRENT_LIST_DIR}/../tests/integration/webgl1_runtime/index.html" page)
string(REPLACE "arp_webgl_context_smoke.js" "arp_webgl1_runtime_validation.js" page "${page}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pipeline.html" "${page}")
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(ARP_WEBGL_RUNTIME_BROWSER NAMES chrome.exe msedge.exe
    HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
if(ARP_WEBGL_RUNTIME_BROWSER)
    foreach(profile IN ITEMS pipeline extension_execution ordered_graphics scene2d gui basic_forward conformance_clear conformance_sampled)
        add_test(NAME arp_webgl_${profile}_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_LIST_DIR}/../tests/contract/webgl_${profile}_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/${profile}-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        set_tests_properties(arp_webgl_${profile}_browser PROPERTIES LABELS "browser-native;webgl1;prototype")
    endforeach()
endif()
message(STATUS "WebGL1 runtime component Prototype product; generic production factory remains closed")
