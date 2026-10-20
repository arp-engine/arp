#include "rhi/frontend/bindless_ordinary_opaque_binding.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "tests/fixtures/original_graphics_color_gi_data_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_completed_texture_root_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"
#include "tests/fixtures/vulkan_original_bindless_graphics_content_fixture.h"

using namespace arp;
using namespace arp::rhi;

class GraphicsGiResources final : public test::VulkanCompletedTextureRootFixture {
public:
    bool injectFault = false;
    using Data = test::OriginalGraphicsColorGiDataFixture;
    using Numeric = Data::Numeric;

    template <class Table> void configureBuffer(BufferDesc& desc, const Table&) const
    {
        desc.memory = MemoryUsage::CpuToGpu;
        desc.sizeBytes = sizeof(Numeric);
    }

    template <class Table>
    void initializeBuffer(GpuBuffer& buffer, const Table&, std::uint32_t) const
    {
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        Numeric poison;
        poison.fill(-999.f);
        std::memcpy(native->backendMappedData(), poison.data(), sizeof(poison));
        core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, sizeof(poison), diagnostic));
    }

    void initializeOrdinaryBuffer(
        GpuBuffer& buffer, std::uint32_t set, const DescriptorBindingDesc& binding) const
    {
        assert(set == 0u && binding.binding == 3u);
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        const std::array<float, 4u> frame{17.f, 0.f, 0.f, 0.f};
        std::memcpy(native->backendMappedData(), frame.data(), sizeof(frame));
        core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, sizeof(frame), diagnostic));
    }

    std::shared_ptr<const OriginalCommandTextureLease> createCompletedOrdinaryTexture(
        GpuDevice& device,
        TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const DescriptorBindingDesc& binding) const
    {
        assert(set == 2u && !binding.depthTexture);
        auto upload = static_cast<const test::VulkanCompletedTextureRootFixture&>(*this);
        upload.cubeTextures = binding.textureViewDimension == TextureViewDimension::TextureCube;
        upload.arrayTextures = binding.textureViewDimension == TextureViewDimension::Texture2DArray;
        return upload.createCompletedTexture(
            device, desc, quota, binding.binding == 3u ? 2u : binding.binding);
    }

    template <class Root, class Sources>
    void inspectOriginalRoot(const Root& root, const Sources& sources, GpuDevice* device) const
    {
        if (!device) {
            assert(!root && !sources);
            return;
        }
        using Source = OriginalCommandBindlessRootSource;
        for (const auto entry : {"vertexMain", injectFault ? "faultFragment" : "cleanFragment"}) {
            BindlessOrdinaryOpaqueBinding facts;
            const auto* id = Source::originalOrdinaryOpaqueBinding(root, entry, 2u, 0u, facts);
            assert(!id && facts.sourceIndex < sources->sources().size()
                && facts.declaration.textureViewDimension == TextureViewDimension::TextureCube
                && facts.access.resource == facts.parentResource);
        }
    }

    static void verify(const OriginalCommandBindlessRootSource& root)
    {
        std::uint32_t results = 0u;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding != 3u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(element.owner);
            const auto native = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
            Numeric actual;
            core::pmr::String diagnostic;
            assert(lease->layout().sizeBytes == sizeof(actual)
                && native->backendInvalidateMappedRead(0u, sizeof(actual), diagnostic));
            std::memcpy(actual.data(), native->backendMappedData(), sizeof(actual));
            Data::verify(actual);
            ++results;
        }
        assert(results == 1u);
        std::puts(
            "BINDLESS_VULKAN_GRAPHICS_COLOR_GI_NUMERIC lanes76 cube-faces6 mips2 array-layers2 vertex-fragment frame17");
    }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(package);
    tests::VulkanHeadlessRuntime runtime(true);
    if (!runtime.runtime())
        return 77;
    tests::RenderDocCaptureScope capture;
    for (const bool fault : {false, true}) {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = runtime.runtime();
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        auto device = rhi_vulkan::createVulkanRhiDevice(desc);
        assert(device);
        test::VulkanOriginalBindlessGraphicsContentFixture observer;
        observer.execute = true;
        observer.expectedFaultBits = fault ? 1u : 0u;
        observer.verifyOrdinaryBuffers = GraphicsGiResources::verify;
        GraphicsGiResources resources;
        resources.injectFault = fault;
        // Charge the queried staging allocation within this runner's declared
        // device ceiling, including native alignment of the six-face upload.
        resources.nativeStagingCeiling = desc.memoryBudgetBytes;
        test::validateOriginalBindlessRootSource(package,
            std::move(device),
            "unused",
            false,
            observer,
            resources,
            true,
            fault ? "faultFragment" : "cleanFragment");
    }
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_COLOR_GI_OK clean/fault original-root real-completion retirement");
}
