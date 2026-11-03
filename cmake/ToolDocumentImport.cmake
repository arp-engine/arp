if(NOT EMSCRIPTEN)
    add_library(arp_tool_document_import STATIC tools/asset_importer/document_import.cpp)
    target_include_directories(arp_tool_document_import PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    # Formal UI publication needs the separately proposed canonical DLL API.
    # Until approved, fail explicitly instead of linking a second UI/core runtime.
    target_compile_definitions(arp_tool_document_import PRIVATE
        ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" ARP_TOOL_UI_SHARED_AUTHORING=0)
    target_link_libraries(arp_tool_document_import PUBLIC arp_cold_tool_files)
    add_library(arp_tool_ui_document_compiler STATIC tools/asset_importer/ui_document_compiler.cpp)
    target_include_directories(arp_tool_ui_document_compiler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(arp_tool_ui_document_compiler PRIVATE ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(arp_tool_ui_document_compiler PUBLIC arp_ui_runtime arp_gui arp_assets arp_scriptvm_host_plugin_runtime)
    if(ARP_SCRIPT_AOT_ONLY_RELEASE)
        target_link_libraries(arp_asset_importer PRIVATE arp_tool_document_import)
    elseif(MINGW)
        target_link_libraries(arp_asset_importer PRIVATE "$<LINK_GROUP:RESCAN,arp_engine,arp_tool_document_import,arp_cold_tool_files>")
    else()
        target_link_libraries(arp_asset_importer PRIVATE arp_tool_document_import)
    endif()
    # Standalone candidate exercises the existing static UI API, never the DLL host.
    add_executable(arp_tool_document_import_tests tests/contract/tool_document_import_tests.cpp
        tools/asset_importer/document_import.cpp)
    target_compile_definitions(arp_tool_document_import_tests PRIVATE
        ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" ARP_TOOL_UI_SHARED_AUTHORING=1)
    target_link_libraries(arp_tool_document_import_tests PRIVATE arp_cold_tool_files arp_tool_ui_document_compiler)
    target_compile_options(arp_tool_document_import_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_tool_document_import_tests)
    add_test(NAME arp_tool_document_import_tests COMMAND arp_tool_document_import_tests)
    add_test(NAME arp_tool_document_import_smoke COMMAND arp_tool_document_import_tests --smoke)
    set_tests_properties(arp_tool_document_import_tests arp_tool_document_import_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools;ui;io;smoke")
    add_executable(arp_tool_document_read_tests tests/contract/tool_document_read_tests.cpp)
    target_compile_options(arp_tool_document_read_tests PRIVATE -UNDEBUG)
    if(MINGW)
        target_link_libraries(arp_tool_document_read_tests PRIVATE
            "$<LINK_GROUP:RESCAN,arp_engine,arp_tool_document_import,arp_cold_tool_files>")
    else()
        target_link_libraries(arp_tool_document_read_tests PRIVATE arp_engine arp_tool_document_import)
    endif()
    arp_add_mingw_static_runtime(arp_tool_document_read_tests)
    add_test(NAME arp_tool_document_read_tests COMMAND arp_tool_document_read_tests)
    add_test(NAME arp_tool_document_read_smoke COMMAND arp_tool_document_read_tests --smoke)
    set_tests_properties(arp_tool_document_read_tests arp_tool_document_read_smoke PROPERTIES
        TIMEOUT 60 LABELS "tools;io;smoke")
    if(WIN32)
        foreach(mode IN ITEMS native http)
            add_test(NAME arp_tool_document_read_${mode}_smoke
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/tool_document_read_cli_smoke.py"
                    --executable "$<TARGET_FILE:arp_asset_importer>"
                    --output "${CMAKE_CURRENT_BINARY_DIR}/tool-document-read-${mode}" --mode ${mode})
            set_tests_properties(arp_tool_document_read_${mode}_smoke PROPERTIES
                TIMEOUT 180 LABELS "tools;io;smoke")
            add_test(NAME arp_tool_document_${mode}_smoke
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/contract/tool_document_cli_smoke.py"
                    --executable "$<TARGET_FILE:arp_asset_importer>"
                    --output "${CMAKE_CURRENT_BINARY_DIR}/tool-documents-${mode}" --mode ${mode})
            set_tests_properties(arp_tool_document_${mode}_smoke PROPERTIES
                TIMEOUT 180 LABELS "tools;ui;io;smoke")
        endforeach()
        set_tests_properties(arp_tool_document_native_smoke PROPERTIES FIXTURES_SETUP tool_document_native)
    endif()
    if(TARGET arp_ui_text_vulkan_hidden_smoke)
        add_executable(arp_tool_ui_document_vulkan_smoke tests/smoke/ui_text_vulkan_hidden_smoke.cpp
            src/platform/common/platform_lifecycle_state.cpp)
        target_include_directories(arp_tool_ui_document_vulkan_smoke PRIVATE . src)
        target_compile_definitions(arp_tool_ui_document_vulkan_smoke PRIVATE
            ARP_UI_TOOL_DOCUMENT_VISUAL=1 ARP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            ARP_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
        if(EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
            target_include_directories(arp_tool_ui_document_vulkan_smoke PRIVATE "C:/Program Files/RenderDoc")
            target_compile_definitions(arp_tool_ui_document_vulkan_smoke PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
        endif()
        target_compile_options(arp_tool_ui_document_vulkan_smoke PRIVATE -UNDEBUG)
        target_link_libraries(arp_tool_ui_document_vulkan_smoke PRIVATE arp_cold_tool_files
            arp_platform arp_renderer arp_render_graph arp_rhi arp_rhi_semantics
            "$<LINK_GROUP:RESCAN,arp_gui,arp_ui_runtime,arp_resource>" arp_scriptvm_runtime
            arp_scriptvm_ir arp_scriptvm_frontend arp_scriptvm_interpreter arp_scriptvm_aot_tooling)
        arp_add_mingw_static_runtime(arp_tool_ui_document_vulkan_smoke)
        add_dependencies(arp_tool_ui_document_vulkan_smoke arp_compile_shaders)
        add_test(NAME arp_tool_ui_document_vulkan_smoke COMMAND "${CMAKE_COMMAND}" -E env
            "ARP_UI_TOOL_DOCUMENT=${CMAKE_CURRENT_BINARY_DIR}/tool-documents-native/generated.arpui"
            "$<TARGET_FILE:arp_tool_ui_document_vulkan_smoke>")
        set_tests_properties(arp_tool_ui_document_vulkan_smoke PROPERTIES
            TIMEOUT 60 RUN_SERIAL TRUE FIXTURES_REQUIRED tool_document_native)
    endif()
endif()
