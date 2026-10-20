#include "tests/fixtures/metal_graphics_gi_fixture.h"
#include "tests/fixtures/original_graphics_wocc_ibl_data_fixture.h"
#include "tests/fixtures/wocc_ibl_frozen_inputs_fixture.h"

using namespace arp;
using namespace arp::rhi;

class MetalWoccIblResources final
    : public test::MetalGraphicsGiResourcesFixture<test::OriginalGraphicsWoccIblDataFixture, true> {
    using Data = test::OriginalGraphicsWoccIblDataFixture;
    using Base = test::MetalGraphicsGiResourcesFixture<Data, true>;

public:
    static constexpr auto originalRootCpuBytes = Data::originalRootCpuBytes;

    void configureOrdinaryBuffer(
        BufferDesc& desc, std::uint32_t set, const DescriptorBindingDesc& binding) const
    {
        Base::configureOrdinaryBuffer(desc, set, binding);
        if (set == 0u && binding.binding == 4u)
            desc.sizeBytes = sizeof(Data::Numeric);
    }

    void initializeOrdinaryBuffer(
        GpuBuffer& buffer, std::uint32_t set, const DescriptorBindingDesc& binding) const
    {
        if (set == 0u && binding.binding == 4u)
            Base::initializeBuffer(buffer, binding, 0u);
        else
            Base::initializeOrdinaryBuffer(buffer, set, binding);
    }

    std::shared_ptr<const OriginalCommandTextureLease> createCompletedOrdinaryTexture(
        GpuDevice& device,
        TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const DescriptorBindingDesc& binding) const
    {
        assert(set == 2u && Data::inputs);
        auto upload = Data::textureUpload(*Data::inputs, desc, binding);
        return createCompletedTextureUpload(device, desc, quota, std::move(upload), true);
    }
};

int main(int argc, char** argv)
{
    assert(argc == 5 || argc == 6);
    using Data = test::OriginalGraphicsWoccIblDataFixture;
    Data::inputs = test::WoccIblFrozenInputsFixture::load(
        argv[3], test::WoccIblFrozenInputsFixture::payloadBytes);
    Data::inputs->exportCold(argv[4]);
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(package);
    auto reference = BindlessShaderPackage::load(
        argv[2], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(reference);
    if (argc == 6) {
        FrameCaptureRequest capture;
        capture.outputPath = argv[5];
        assert(rhi_metal::beginMetalRhiFrameCapture(capture).success());
    }
    for (std::uint32_t run = 0u; run < 3u; ++run) {
        const bool direct = run == 0u, fault = run == 2u;
        auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        desc.enableBindlessGraphicsImages = true;
        auto device = rhi_metal::createMetalRhiDevice(desc);
        assert(device);
        test::MetalOriginalBindlessGraphicsContentFixture observer;
        observer.execute = true;
        observer.ordinaryColorSampling = true;
        observer.completedReadContractVersion = 2u;
        observer.sampledReadStages = PipelineStage::VertexShader | PipelineStage::FragmentShader;
        observer.expectedFaultBits = fault ? 1u : 0u;
        observer.verifyOutputs = direct
            ? test::MetalSampledMaterialReadbackFixture::woccIblReference
            : test::MetalSampledMaterialReadbackFixture::woccIbl;
        MetalWoccIblResources resources;
        resources.observeOriginalNativeAllocation = true;
        resources.nativeStagingCeiling = desc.memoryBudgetBytes;
        test::validateOriginalBindlessRootSource(direct ? reference : package,
            std::move(device),
            "unused",
            false,
            observer,
            resources,
            true,
            fault ? "faultFragment" : "cleanFragment");
    }
    if (argc == 6)
        assert(rhi_metal::endMetalRhiFrameCapture().success());
    Data::inputs.reset();
    std::puts(
        "BINDLESS_METAL_PUBLIC_WOCC_IBL_OK lanes424 unfiltered80-exact direct-reference-bitwise clean/fault original-root completion retirement");
}
