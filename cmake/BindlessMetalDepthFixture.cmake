# The same sealed Slang package and depth producer used by Vulkan.
if(TARGET arp_rhi_metal_bindless_graphics_tests AND TARGET arp_bindless_sampled_depth_fixture
    AND TARGET arp_bindless_completed_graphics_fixture)
    add_dependencies(arp_rhi_metal_bindless_graphics_tests arp_bindless_sampled_depth_fixture)
    add_test(NAME arp_rhi_metal_public_depth6_tests COMMAND arp_rhi_metal_bindless_graphics_tests
        --publication "${ARP_COMPLETED_GRAPHICS_FIXTURE_DIR}/RootVertexBuffer.native.bindless.yaml"
        --public-depth "${ARP_SAMPLED_DEPTH_FIXTURE_DIR}/SampledDepth.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_public_depth6_tests PROPERTIES TIMEOUT 120 ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()

if(APPLE AND TARGET arp_rhi_metal_bindless_graphics_tests AND DEFINED storage_root_runtime
    AND TARGET arp_bindless_sampled_depth_fixture AND TARGET arp_bindless_completed_graphics_fixture)
    add_test(NAME arp_rhi_metal_mixed_content15_plan_tests COMMAND arp_rhi_metal_bindless_graphics_tests
        --publication "${ARP_COMPLETED_GRAPHICS_FIXTURE_DIR}/RootVertexBuffer.native.bindless.yaml"
        --public-mixed-content-plan "${ARP_SAMPLED_DEPTH_FIXTURE_DIR}/SampledDepth.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_mixed_content15_plan_tests PROPERTIES TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
    add_test(NAME arp_rhi_metal_public_mixed11_tests COMMAND arp_rhi_metal_bindless_graphics_tests
        --publication "${ARP_COMPLETED_GRAPHICS_FIXTURE_DIR}/RootVertexBuffer.native.bindless.yaml"
        --public-mixed-execution "${ARP_SAMPLED_DEPTH_FIXTURE_DIR}/SampledDepth.native.bindless.yaml")
    set_tests_properties(arp_rhi_metal_public_mixed11_tests PROPERTIES TIMEOUT 180 ENVIRONMENT "MTL_DEBUG_LAYER=1")
    add_test(NAME arp_rhi_metal_public_mixed11_lifecycle_tests COMMAND arp_rhi_metal_bindless_graphics_tests
        --publication "${ARP_COMPLETED_GRAPHICS_FIXTURE_DIR}/RootVertexBuffer.native.bindless.yaml"
        --public-mixed-lifecycle "${ARP_SAMPLED_DEPTH_FIXTURE_DIR}/SampledDepth.native.bindless.yaml")
    # Five terminal scenarios, each with clean and fault output.
    set_tests_properties(arp_rhi_metal_public_mixed11_lifecycle_tests PROPERTIES TIMEOUT 900 ENVIRONMENT "MTL_DEBUG_LAYER=1" RUN_SERIAL TRUE)
endif()
