# RHI-owned tests, independent of cloud/example target registration.
if(TARGET arp_rhi_vulkan AND NOT APPLE AND NOT EMSCRIPTEN)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_compute.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_state.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_clear.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_bindings.cpp)
    target_sources(arp_rhi_vulkan PRIVATE src/rhi/backends/vulkan/vulkan_retained_graphics.cpp)
    add_executable(arp_rhi_vulkan_command_retention_tests
        tests/integration/rhi_vulkan_command_retention_tests.cpp)
    target_link_libraries(arp_rhi_vulkan_command_retention_tests PRIVATE arp_rhi_vulkan)
    target_include_directories(arp_rhi_vulkan_command_retention_tests PRIVATE .)
    target_compile_options(arp_rhi_vulkan_command_retention_tests PRIVATE -UNDEBUG)
    arp_add_mingw_static_runtime(arp_rhi_vulkan_command_retention_tests)
    if(WIN32 AND EXISTS "C:/Program Files/RenderDoc/renderdoc_app.h")
        target_include_directories(arp_rhi_vulkan_command_retention_tests PRIVATE "C:/Program Files/RenderDoc")
        target_compile_definitions(arp_rhi_vulkan_command_retention_tests PRIVATE ARP_RENDERDOC_APP_AVAILABLE=1)
    endif()
    add_test(NAME arp_rhi_vulkan_command_retention_contract
        COMMAND arp_rhi_vulkan_command_retention_tests --contract)
    add_test(NAME arp_rhi_vulkan_command_retention_native
        COMMAND arp_rhi_vulkan_command_retention_tests)
    set_tests_properties(arp_rhi_vulkan_command_retention_native PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()
