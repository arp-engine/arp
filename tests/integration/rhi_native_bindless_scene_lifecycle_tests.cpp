#include "tests/fixtures/bindless_native_scene_runner_fixture.h"
#include "tests/fixtures/bindless_geometry_package_fixture.h"

#if defined(ARP_BINDLESS_SCENE_VULKAN)
#include "rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_completed_texture_root_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"
#elif defined(ARP_BINDLESS_SCENE_METAL)
#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"
#include "tests/fixtures/bindless_metal_public_graphics_fixture.h"
#include "tests/fixtures/metal_completed_texture_root_fixture.h"
#else
#error A native scene platform must be selected explicitly
#endif

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace arp::rhi;
    assert(argc == 3);
    const bool terrain = core::StringView(argv[2]) == "terrain";
    assert(terrain || core::StringView(argv[2]) == "pbr");
    test::BindlessGeometryPackageFixture proof(argv[1], terrain, "native");
    BindlessShaderPackage::LoadBudget load{4u * 1024u * 1024u, 64u * 1024u * 1024u, 256u, 16u, 256u, 64u};
    load.push = &proof.push;
    load.ordinaryMemory = &proof.memory;
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        argv[1], load, diagnostic);
    if (!package)
        std::fprintf(stderr, "Native scene package: %s\n", diagnostic.c_str());
    assert(package);
#if defined(ARP_BINDLESS_SCENE_VULKAN)
    tests::VulkanHeadlessRuntime runtime(true);
    if (!runtime.runtime())
        return 77;
    tests::RenderDocCaptureScope capture;
    test::BindlessNativeSceneRunnerFixture::run<test::VulkanCompletedTextureRootFixture,
        rhi_vulkan::VulkanBindlessRootNativeClosure,
        rhi_vulkan::VulkanOriginalBindlessGraphicsPass,
        rhi_vulkan::VulkanOriginalGeometryContent>(package, 64u * 1024u * 1024u, [&] {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = runtime.runtime();
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        return rhi_vulkan::createVulkanRhiDevice(desc);
    });
#else
    test::BindlessNativeSceneRunnerFixture::run<test::MetalCompletedTextureUploadFixture,
        rhi_metal::MetalRhiBindlessRootNativeClosure,
        rhi_metal::MetalOriginalBindlessGraphicsPass,
        rhi_metal::MetalOriginalGeometryContent>(package, 64u * 1024u * 1024u, [] {
        auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        desc.enableBindlessGraphicsImages = true;
        return rhi_metal::createMetalRhiDevice(desc);
    });
#endif
}
