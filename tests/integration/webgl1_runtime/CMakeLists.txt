cmake_minimum_required(VERSION 3.24)
project(ArpWebGlContextSmoke LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "WebGL runtime smoke requires the actual Emscripten toolchain")
endif()
get_filename_component(arp_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
include("${arp_root}/cmake/ArpWebGlComponentLibraries.cmake")
arp_add_webgl_component_libraries()
add_executable(arp_webgl_context_smoke "${arp_root}/tests/integration/webgl_owned_runtime_smoke.cpp")
target_link_libraries(arp_webgl_context_smoke PRIVATE arp_rhi_webgl)
target_link_options(arp_webgl_context_smoke PRIVATE -sASSERTIONS=1 -sEXIT_RUNTIME=0)
set_target_properties(arp_webgl_context_smoke PROPERTIES SUFFIX ".js")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/index.html" "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
add_executable(arp_webgl_resources_smoke
    "${arp_root}/tests/integration/webgl_resources_smoke.cpp"
)
target_link_libraries(arp_webgl_resources_smoke PRIVATE arp_rhi_webgl)
target_link_options(arp_webgl_resources_smoke PRIVATE -sASSERTIONS=1 -sEXIT_RUNTIME=0)
set_target_properties(arp_webgl_resources_smoke PROPERTIES SUFFIX ".js")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/index.html" resource_page)
string(REPLACE "arp_webgl_context_smoke.js" "arp_webgl_resources_smoke.js" resource_page "${resource_page}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resources.html" "${resource_page}")
set(ARP_WEBGL_PROGRAM_DIRECTORY "" CACHE PATH "Original host-generated P1 program artifact directory")
if(ARP_WEBGL_PROGRAM_DIRECTORY)
    if(NOT EXISTS "${ARP_WEBGL_PROGRAM_DIRECTORY}/webgl1_graphics.webgl-program.yaml")
        message(FATAL_ERROR "Original WebGL1 program publication is missing")
    endif()
    add_executable(arp_webgl_owned_program_smoke
        "${arp_root}/tests/integration/webgl_owned_program_smoke.cpp"
    )
    target_link_libraries(arp_webgl_owned_program_smoke PRIVATE arp_rhi_webgl)
    target_link_options(arp_webgl_owned_program_smoke PRIVATE -sASSERTIONS=1 -sEXIT_RUNTIME=0
        -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0
        "--preload-file" "${ARP_WEBGL_PROGRAM_DIRECTORY}@/artifacts")
    set_target_properties(arp_webgl_owned_program_smoke PROPERTIES SUFFIX ".js")
    arp_webgl_track_preload_inputs(arp_webgl_owned_program_smoke "${ARP_WEBGL_PROGRAM_DIRECTORY}")
    string(REPLACE "arp_webgl_resources_smoke.js" "arp_webgl_owned_program_smoke.js" program_page "${resource_page}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/program.html" "${program_page}")
    add_executable(arp_webgl_pipeline_smoke
        "${arp_root}/tests/integration/webgl_pipeline_smoke.cpp"
    )
    target_link_libraries(arp_webgl_pipeline_smoke PRIVATE arp_rhi_webgl arp_sprite_product)
    target_include_directories(arp_webgl_pipeline_smoke PRIVATE "${arp_root}")
    target_compile_definitions(arp_webgl_pipeline_smoke PRIVATE ARP_SOURCE_DIR="")
    target_link_options(arp_webgl_pipeline_smoke PRIVATE -sASSERTIONS=1 -sEXIT_RUNTIME=0
        -sEXPORTED_RUNTIME_METHODS=callMain -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=262144
        -sSTACK_SIZE=1048576 -sSTACK_OVERFLOW_CHECK=2
        -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=0
        "SHELL:--preload-file \"${ARP_WEBGL_PROGRAM_DIRECTORY}@/artifacts\""
        "SHELL:--preload-file \"${arp_root}/tests/fixtures/scene_2d@/tests/fixtures/scene_2d\""
        "SHELL:--preload-file \"${arp_root}/tests/fixtures/rhi_conformance_clear.yaml@/tests/fixtures/rhi_conformance_clear.yaml\""
        "SHELL:--preload-file \"${arp_root}/tests/fixtures/rhi_conformance_sampled.yaml@/tests/fixtures/rhi_conformance_sampled.yaml\""
        "SHELL:--preload-file \"${arp_root}/tests/fixtures/rhi_conformance_pingpong.yaml@/tests/fixtures/rhi_conformance_pingpong.yaml\"")
    set_target_properties(arp_webgl_pipeline_smoke PROPERTIES SUFFIX ".js")
    arp_webgl_track_preload_inputs(arp_webgl_pipeline_smoke
        "${ARP_WEBGL_PROGRAM_DIRECTORY}" "${arp_root}/tests/fixtures/scene_2d"
        "${arp_root}/tests/fixtures/rhi_conformance_clear.yaml"
        "${arp_root}/tests/fixtures/rhi_conformance_sampled.yaml"
        "${arp_root}/tests/fixtures/rhi_conformance_pingpong.yaml")
    string(REPLACE "arp_webgl_resources_smoke.js" "arp_webgl_pipeline_smoke.js" pipeline_page "${resource_page}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pipeline.html" "${pipeline_page}")
endif()
enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(ARP_WEBGL_RUNTIME_BROWSER NAMES chrome.exe msedge.exe
    HINTS "C:/Program Files/Google/Chrome/Application" "C:/Program Files (x86)/Microsoft/Edge/Application")
if(ARP_WEBGL_RUNTIME_BROWSER)
    add_test(NAME arp_webgl_owned_runtime_browser
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_owned_runtime_browser.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/browser-ctest"
            --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
    add_test(NAME arp_webgl_resources_browser
        COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_resources_browser.py"
            --directory "${CMAKE_CURRENT_BINARY_DIR}"
            --output "${CMAKE_CURRENT_BINARY_DIR}/resources-browser-ctest"
            --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
    if(TARGET arp_webgl_owned_program_smoke)
        add_test(NAME arp_webgl_conformance_sampled_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_conformance_sampled_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/conformance-sampled-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        set_tests_properties(arp_webgl_conformance_sampled_browser PROPERTIES LABELS "browser-native;webgl1;prototype;conformance")
        add_test(NAME arp_webgl_conformance_clear_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_conformance_clear_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/conformance-clear-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        set_tests_properties(arp_webgl_conformance_clear_browser PROPERTIES LABELS "browser-native;webgl1;prototype;conformance")
        add_test(NAME arp_webgl_owned_program_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_owned_program_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/program-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        add_test(NAME arp_webgl_extension_execution_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_extension_execution_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/extensions-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        add_test(NAME arp_webgl_pipeline_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_pipeline_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/pipeline-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        add_test(NAME arp_webgl_ordered_graphics_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_ordered_graphics_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/ordered-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        add_test(NAME arp_webgl_scene2d_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_scene2d_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/scene2d-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        add_test(NAME arp_webgl_gui_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_gui_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/gui-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
        add_test(NAME arp_webgl_basic_forward_browser
            COMMAND "${Python3_EXECUTABLE}" -X utf8 "${arp_root}/tests/contract/webgl_basic_forward_browser.py"
                --directory "${CMAKE_CURRENT_BINARY_DIR}"
                --output "${CMAKE_CURRENT_BINARY_DIR}/basic-forward-browser-ctest"
                --browser "${ARP_WEBGL_RUNTIME_BROWSER}")
    endif()
endif()
