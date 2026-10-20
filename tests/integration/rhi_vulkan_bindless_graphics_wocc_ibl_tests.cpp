#include "rhi/frontend/bindless_ordinary_opaque_binding.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "tests/fixtures/original_graphics_wocc_ibl_data_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_completed_texture_root_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"
#include "tests/fixtures/vulkan_original_bindless_graphics_content_fixture.h"
#include "tests/fixtures/wocc_ibl_frozen_inputs_fixture.h"

using namespace arp;
using namespace arp::rhi;

class GraphicsWoccIblResources final : public test::VulkanCompletedTextureRootFixture {
public:
    // The source quota owns original pixels and the destination/upload Sources
    // together. The explicit fixture ceiling adds the largest current WOCC
    // asset to the established metadata allowance (one complete Cube64 chain).
    static constexpr auto originalRootCpuBytes =
        test::OriginalGraphicsWoccIblDataFixture::originalRootCpuBytes;
    bool injectFault = false;

    static constexpr std::uint32_t completedTexturePublicationVersion() noexcept { return 5u; }

    using Numeric = test::OriginalGraphicsWoccIblDataFixture::Numeric;

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
        assert(set == 0u && (binding.binding == 3u || binding.binding == 4u));
        if (binding.binding == 4u) {
            Numeric poison;
            poison.fill(-999.f);
            const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
            std::memcpy(native->backendMappedData(), poison.data(), sizeof(poison));
            core::pmr::String diagnostic;
            assert(native->backendFlushMappedWrite(0u, sizeof(poison), diagnostic));
            return;
        }
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        const std::array<float, 4u> frame{17.f, 0.f, 0.f, 0.f};
        std::memcpy(native->backendMappedData(), frame.data(), sizeof(frame));
        core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, sizeof(frame), diagnostic));
    }

    using Data = test::OriginalGraphicsWoccIblDataFixture;
    using Inputs = Data::Inputs;

    inline static std::shared_ptr<const Inputs> inputs;

    std::shared_ptr<const OriginalCommandTextureLease> createCompletedOrdinaryTexture(
        GpuDevice& device,
        TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const DescriptorBindingDesc& binding) const
    {
        assert(set == 2u && !binding.depthTexture);
        auto upload = Data::textureUpload(*inputs, desc, binding);
        return createCompletedTextureUpload(device, desc, quota, std::move(upload), true);
    }

    inline static Numeric directReference;
    inline static bool directReferenceReady = false;

    void configureOrdinaryBuffer(
        BufferDesc& desc, std::uint32_t set, const DescriptorBindingDesc& binding) const
    {
        test::VulkanCompletedTextureRootFixture::configureOrdinaryBuffer(desc, set, binding);
        if (set == 0u && binding.binding == 4u)
            desc.sizeBytes = sizeof(Numeric);
    }

    static void verifyReference(const OriginalCommandBindlessRootSource& root)
    {
        std::uint32_t found = 0u;
        for (const auto& domain : root.ordinaryDomains())
            for (const auto& element : domain.source->frozenElements()) {
                if (domain.set != 0u || element.declaration.write.binding != 4u)
                    continue;
                const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                    element.resourceOwner);
                const auto native = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
                core::pmr::String diagnostic;
                assert(
                    native->backendInvalidateMappedRead(0u, sizeof(directReference), diagnostic));
                std::memcpy(
                    directReference.data(), native->backendMappedData(), sizeof(directReference));
                Data::verifyUnfiltered(directReference, *inputs);
                ++found;
            }
        assert(found == 1u);
        directReferenceReady = true;
        std::puts(
            "BINDLESS_VULKAN_WOCC_IBL_DIRECT_REFERENCE lanes424 unfiltered-cpu-exact80 original-root-public");
    }

    static void verify(const OriginalCommandBindlessRootSource& root)
    {
        assert(directReferenceReady);
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
            for (std::size_t i = 0u; i < actual.size(); ++i) {
                if (actual[i] != directReference[i])
                    std::fprintf(stderr,
                        "Graphics GI lane%zu actual%.9g expected%.9g\n",
                        i,
                        actual[i],
                        directReference[i]);
                assert(std::isfinite(actual[i]) && actual[i] == directReference[i]);
            }
            ++results;
        }
        assert(results == 1u);
        std::puts(
            "BINDLESS_VULKAN_GRAPHICS_WOCC_IBL_NUMERIC lanes424 rgba16f two-cube64-mips7 lut128 bake-inputs vertex-fragment frame17");
    }
};

int main(int argc, char** argv)
{
    assert(argc == 5);
    GraphicsWoccIblResources::inputs = test::WoccIblFrozenInputsFixture::load(
        argv[3], test::WoccIblFrozenInputsFixture::payloadBytes);
    GraphicsWoccIblResources::inputs->exportCold(argv[4]);
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(package);
    auto referencePackage = BindlessShaderPackage::load(
        argv[2], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(referencePackage);
    tests::VulkanHeadlessRuntime runtime(true);
    if (!runtime.runtime())
        return 77;
    tests::RenderDocCaptureScope capture;
    for (std::uint32_t run = 0u; run < 3u; ++run) {
        const bool reference = run == 0u;
        const bool fault = run == 2u;
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = runtime.runtime();
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        auto device = rhi_vulkan::createVulkanRhiDevice(desc);
        assert(device);
        test::VulkanOriginalBindlessGraphicsContentFixture observer;
        observer.execute = true;
        observer.completedReadContractVersion = 2u;
        observer.expectedFaultBits = fault ? 1u : 0u;
        observer.verifyOrdinaryBuffers = reference ? GraphicsWoccIblResources::verifyReference
                                                   : GraphicsWoccIblResources::verify;
        GraphicsWoccIblResources resources;
        resources.injectFault = fault;
        // Charge the queried staging allocation within this runner's declared
        // device ceiling, including native alignment of the six-face upload.
        resources.nativeStagingCeiling = desc.memoryBudgetBytes;
        test::validateOriginalBindlessRootSource(reference ? referencePackage : package,
            std::move(device),
            "unused",
            false,
            observer,
            resources,
            true,
            fault ? "faultFragment" : "cleanFragment");
    }
    GraphicsWoccIblResources::inputs.reset();
    std::puts(
        "BINDLESS_VULKAN_PUBLIC_GRAPHICS_WOCC_IBL_OK clean/fault original-root real-completion retirement");
}
