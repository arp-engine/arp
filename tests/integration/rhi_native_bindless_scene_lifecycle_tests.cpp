#include "tests/fixtures/bindless_geometry_package_fixture.h"
#include "tests/fixtures/bindless_native_scene_runner_fixture.h"

#if defined(ARP_BINDLESS_SCENE_VULKAN)
#include "rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_completed_texture_readback_fixture.h"
#include "tests/fixtures/vulkan_completed_texture_root_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"
#elif defined(ARP_BINDLESS_SCENE_METAL)
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"
#include "tests/fixtures/bindless_metal_public_graphics_fixture.h"
#include "tests/fixtures/metal_completed_texture_root_fixture.h"
#else
#error A native scene platform must be selected explicitly
#endif
#include "tests/fixtures/bindless_native_scene_submission_fixture.h"

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace arp::rhi;
    assert(argc == 3);
    const bool terrain = core::StringView(argv[2]) == "terrain";
    assert(terrain || core::StringView(argv[2]) == "pbr");
    test::BindlessGeometryPackageFixture proof(argv[1], terrain, "native");
    BindlessPackageLoadBudget load{4u * 1024u * 1024u, 64u * 1024u * 1024u, 256u, 16u, 256u, 64u};
    load.push = &proof.push;
    load.ordinaryMemory = &proof.memory;
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(argv[1], load, diagnostic);
    if (!package)
        std::fprintf(stderr, "Native scene package: %s\n", diagnostic.c_str());
    assert(package);
#if defined(ARP_BINDLESS_SCENE_VULKAN)
    tests::VulkanHeadlessRuntime runtime(true, test::RootFenceNegativeObservation::resolve);
    if (!runtime.runtime())
        return 77;
    tests::RenderDocCaptureScope capture;
    static auto sceneRuntime = runtime.runtime();
    test::BindlessNativeSceneRunnerFixture::run<test::VulkanCompletedTextureRootFixture,
        rhi_vulkan::VulkanBindlessRootNativeClosure,
        rhi_vulkan::VulkanOriginalBindlessGraphicsPass,
        rhi_vulkan::VulkanOriginalGeometryContent>(
        package,
        64u * 1024u * 1024u,
        [](const GpuTexture& texture,
            const char* entry,
            bool terrain,
            const void* geometry,
            bool fault) {
            semantics::TextureUploadPackedMip mip;
            mip.range = defaultTextureSubresourceRange(texture.desc());
            mip.extent = texture.desc().extent;
            const bool depth = texture.desc().format == TextureFormat::D32Float;
            mip.packedBytesPerRow = kTextureUploadRowPitchAlignment;
            mip.rowCount = 2u;
            const auto bytes = test::VulkanCompletedTextureReadbackFixture::read(texture,
                sceneRuntime,
                std::array{mip},
                2u * mip.packedBytesPerRow,
                depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment);
            const auto rowBytes = depth ? 8u : 16u;
            std::array<std::byte, 32u> packed{};
            for (std::size_t row = 0u; row < 2u; ++row)
                std::memcpy(packed.data() + row * rowBytes,
                    bytes.data() + row * mip.packedBytesPerRow,
                    rowBytes);
            test::BindlessSceneAttachmentFixture::inspectAndReport(
                texture, entry, terrain, geometry, packed.data(), 2u * rowBytes, fault);
        },
        &test::BindlessNativeSceneSubmissionFixture::observe,
        9u,
        [&] {
            rhi_vulkan::VulkanRhiDeviceDesc desc;
            desc.runtime = runtime.runtime();
            desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
            desc.requireNativeResourceAllocation = true;
            desc.memoryBudgetBytes = 64u * 1024u * 1024u;
            return rhi_vulkan::createVulkanRhiDevice(desc);
        });
    sceneRuntime.reset();
#else
    test::BindlessNativeSceneRunnerFixture::run<test::MetalCompletedTextureUploadFixture,
        rhi_metal::MetalRhiBindlessRootNativeClosure,
        rhi_metal::MetalOriginalBindlessGraphicsPass,
        rhi_metal::MetalOriginalGeometryContent>(
        package,
        64u * 1024u * 1024u,
        &test::BindlessSceneAttachmentFixture::observeMetal,
        &test::BindlessNativeSceneSubmissionFixture::observe,
        6u,
        [] {
            auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
            desc.memoryBudgetBytes = 64u * 1024u * 1024u;
            desc.enableBindlessGraphicsImages = true;
            return rhi_metal::createMetalRhiDevice(desc);
        },
        [](const char* entry, bool begin) {
            const auto* directory = std::getenv("ARP_METAL_CAPTURE_PATH");
            if (!directory)
                return;
            FrameCaptureResult result;
            if (begin) {
                FrameCaptureRequest request;
                request.outputPath = directory;
                request.outputPath += "/";
                request.outputPath += entry;
                request.outputPath += ".gputrace";
                result = rhi_metal::beginMetalRhiFrameCapture(request);
            } else
                result = rhi_metal::endMetalRhiFrameCapture();
            if (!result.success())
                std::fprintf(
                    stderr, "Metal scene capture %s: %s\n", entry, result.diagnostics.c_str());
            assert(result.success());
        });
#endif
}
