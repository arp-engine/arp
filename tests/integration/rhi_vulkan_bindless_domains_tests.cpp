#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "tests/fixtures/original_bindless_root_source_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_completed_texture_root_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <iostream>

using namespace arp;
using namespace arp::rhi;

class VulkanColorGiFixture final : public test::VulkanCompletedTextureRootFixture {
public:
    template <class Table> void configureBuffer(BufferDesc& desc, const Table&) const
    {
        desc.memory = MemoryUsage::CpuToGpu;
        desc.sizeBytes = 18u * sizeof(float);
    }

    void configureOrdinaryBuffer(
        BufferDesc& desc, std::uint32_t, const DescriptorBindingDesc&) const
    {
        desc.memory = MemoryUsage::CpuToGpu;
    }

    void initializeOrdinaryBuffer(
        GpuBuffer& buffer, std::uint32_t set, const DescriptorBindingDesc& binding) const
    {
        assert(set == 0u && binding.binding == 3u);
        const auto native = rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
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
        const auto dataIndex = binding.binding == 3u ? 2u : binding.binding;
        return upload.createCompletedTexture(device, desc, quota, dataIndex);
    }

    static void verify(const OriginalCommandBindlessRootSource& root)
    {
        assert(root.sourceContractVersion() == 2u && root.ordinaryDomains().size() == 2u);
        std::array<float, 18u> expected{};
        for (std::uint32_t face = 0u; face < 6u; ++face) {
            expected[face * 2u] = 17.f + float(40u + 8u * face) / 255.f;
            expected[face * 2u + 1u] = 17.f + float(32u + 8u * face) / 255.f;
        }
        expected[12] = 17.f + 32.f / 255.f;
        expected[13] = 17.f + 64.f / 255.f;
        expected[14] = 17.f + 48.f / 255.f;
        expected[15] = 17.f + 96.f / 255.f;
        expected[16] = 17.f + 72.f / 255.f;
        expected[17] = 17.f + 120.f / 255.f;
        std::uint32_t count = 0u;
        for (const auto& element : root.elements())
            if (element.declaration.write.binding == 3u) {
                const auto& lease =
                    std::get<std::shared_ptr<const OriginalCommandBufferLease>>(element.owner);
                const auto native = rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
                assert(native);
                core::pmr::String diagnostic;
                assert(native->backendInvalidateMappedRead(0u, sizeof(expected), diagnostic));
                std::array<float, 18u> actual{};
                std::memcpy(actual.data(), native->backendMappedData(), sizeof(actual));
                for (std::size_t i = 0u; i < actual.size(); ++i) {
                    if (std::abs(actual[i] - expected[i]) >= 0.00001f)
                        std::cerr << "GI output " << i << ": " << actual[i] << " expected "
                                  << expected[i] << '\n';
                    assert(std::abs(actual[i] - expected[i]) < 0.00001f);
                }
                ++count;
            }
        assert(count == 2u);
        std::puts(
            "BINDLESS_VULKAN_COLOR_GI_NUMERIC cube-faces6 mips2 LUT2D array-layers2 independent-sampler frame17 outputs18x2");
    }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(package);
    for (const auto& domain : package->artifact().additionalDomains)
        for (auto binding : domain.layout.bindings) {
            assert(!rhi_vulkan::inspectVulkanBindlessOrdinaryBinding(binding));
            if (binding.type == DescriptorType::SampledTexture
                || binding.type == DescriptorType::Sampler) {
                const bool image = binding.type == DescriptorType::SampledTexture;
                for (const auto stage : {ShaderStage::Vertex,
                         ShaderStage::Fragment,
                         ShaderStage::Vertex | ShaderStage::Fragment}) {
                    binding.stages = stage;
                    assert(!rhi_vulkan::inspectVulkanBindlessOrdinaryBinding(binding));
                }
                binding.stages = ShaderStage::None;
                assert(core::StringView(rhi_vulkan::inspectVulkanBindlessOrdinaryBinding(binding))
                    == (image ? "RHI-VULKAN-BINDLESS-ORDINARY-IMAGE-STAGE-UNSUPPORTED"
                              : "RHI-VULKAN-BINDLESS-ORDINARY-SAMPLER-STAGE-UNSUPPORTED"));
                binding.stages = ShaderStage::Compute;
                if (image)
                    binding.depthTexture = true;
                else
                    binding.comparisonSampler = true;
                assert(core::StringView(rhi_vulkan::inspectVulkanBindlessOrdinaryBinding(binding))
                    == (image ? "RHI-VULKAN-BINDLESS-ORDINARY-IMAGE-UNSUPPORTED"
                              : "RHI-VULKAN-BINDLESS-ORDINARY-COMPARISON-UNSUPPORTED"));
            }
        }
    tests::VulkanHeadlessRuntime native(true, test::RootFenceNegativeObservation::resolve);
    if (!native.runtime())
        return 77;
    tests::RenderDocCaptureScope capture;
    const auto run = [&](VulkanColorGiFixture fixture) {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = native.runtime();
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        auto device = rhi_vulkan::createVulkanRhiDevice(desc);
        assert(device);
        fixture.nativeStagingCeiling = desc.memoryBudgetBytes;
        fixture.dispatchX = 2u;
        fixture.expectedSamplerOwners = 1u;
        fixture.verifyOutputs = VulkanColorGiFixture::verify;
        test::validateOriginalBindlessRootSource(package,
            std::move(device),
            fixture.fault ? "faultMain" : "cleanMain",
            false,
            fixture,
            fixture);
    };
    for (unsigned mode = 0u; mode < 3u; ++mode) {
        VulkanColorGiFixture fixture;
        fixture.fault = mode == 1u;
        fixture.abandon = mode == 2u;
        run(fixture);
    }
    for (const auto& scenario :
        std::array<test::VulkanOriginalRootTerminalScenario, 5u>{{{false, false, false, false},
            {false, true, false, false},
            {true, false, false, false},
            {true, true, true, false},
            {false, true, false, true}}}) {
        VulkanColorGiFixture fixture;
        fixture.terminalCases = true;
        fixture.terminal = scenario;
        fixture.repeatAfterCompletion = false;
        run(fixture);
    }
    std::puts(
        "BINDLESS_VULKAN_COLOR_GI_ROOT2 completed-original-uploads ordinary-sets real-fence fault0/1 cancellation retirement");
}
