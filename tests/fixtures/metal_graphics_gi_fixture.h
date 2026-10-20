#pragma once
#include "rhi/backends/metal/metal_native_runtime.h"
#include "tests/fixtures/bindless_metal_public_graphics_fixture.h"
#include "tests/fixtures/metal_completed_texture_root_fixture.h"
#include "tests/fixtures/metal_original_bindless_graphics_content_fixture.h"
#include "tests/fixtures/metal_sampled_material_readback_fixture.h"
#include "tests/fixtures/original_graphics_color_gi_data_fixture.h"

namespace arp::test {
using namespace rhi;

template <class Data, bool Hdr>
class MetalGraphicsGiResourcesFixture : public test::MetalCompletedTextureUploadFixture {
public:
    static constexpr bool enabled = false, tableBufferData = true;

    static constexpr std::uint32_t completedTexturePublicationVersion() noexcept
    {
        return Hdr ? 5u : 1u;
    }

    void configureAdmission(OriginalCommandAdmissionBudget& budget) const
    {
        MetalCompletedTextureUploadFixture::configureAdmission(budget);
        budget.maxCallbackTickets = budget.maxCallbackRegistrations = 0u;
        budget.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 0u;
    }

    template <class Table> void configureBuffer(BufferDesc& desc, const Table&) const
    {
        desc.memory = MemoryUsage::CpuToGpu;
        desc.sizeBytes = sizeof(typename Data::Numeric);
    }

    template <class Table>
    void initializeBuffer(GpuBuffer& buffer, const Table&, std::uint32_t) const
    {
        typename Data::Numeric poison;
        poison.fill(-999.f);
        core::pmr::String diagnostic;
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            poison.data(),
            sizeof(poison),
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
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
        const std::array<float, 4u> frame{17.f, 0.f, 0.f, 0.f};
        core::pmr::String diagnostic;
        assert(buffer.desc().sizeBytes == sizeof(frame)
            && rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
                frame.data(),
                sizeof(frame),
                0u,
                buffer.desc().sizeBytes,
                diagnostic));
    }

    std::shared_ptr<const OriginalCommandTextureLease> createCompletedOrdinaryTexture(
        GpuDevice& device,
        TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const DescriptorBindingDesc& binding) const
    {
        assert(set == 2u && !binding.depthTexture);
        desc.format = Hdr ? TextureFormat::Rgba16Float : TextureFormat::Rgba8Unorm;
        auto upload = static_cast<const test::MetalCompletedTextureUploadFixture&>(*this);
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
        for (const auto& domain : root->ordinaryDomains()) {
            if (domain.set != 2u)
                continue;
            assert(!rhi_metal::metalRhiNativePayloadFor(*domain.source->originalObject()));
            assert(!rhi_metal::prepareMetalRhiOriginalOrdinaryColorDescriptors(root, UINT32_MAX));
            auto forged =
                std::shared_ptr<const OriginalCommandBindlessRootSource>(sources, root.get());
            assert(!rhi_metal::prepareMetalRhiOriginalOrdinaryColorDescriptors(forged, domain.set));
            if constexpr (Hdr) {
                std::size_t images = 0u;
                for (const auto& element : domain.source->frozenElements()) {
                    const auto* source =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &element.resourceOwner);
                    if (!source)
                        continue;
                    const auto& texture = *(*source)->originalParentLease()->originalObject();
                    const auto completed = texture.observeOriginalContent();
                    assert(completed && texture.desc().format == TextureFormat::Rgba16Float);
                    assert(!OriginalCompletedTextureDescriptorContract::validateSource(
                        *source, *completed, 5u));
                    assert(OriginalCompletedTextureDescriptorContract::validateSource(
                        *source, *completed, 1u));
                    for (const auto legacy : {8u, 10u})
                        assert(OriginalCompletedTextureReadPlanContract::inspectViewBinding(
                            texture.desc(),
                            (*source)->originalObject()->desc(),
                            ResourceState::ShaderRead,
                            element.declaration.stages,
                            element.declaration.access,
                            legacy));
                    ++images;
                }
                assert(images == 3u);
            }
        }
    }
};

template <class Data, bool Hdr> int runMetalGraphicsGiFixture(int argc, char** argv)
{
    assert(argc == 2 || argc == 3);
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    if (!package)
        std::fprintf(stderr, "Metal ordinary color package: %s\n", diagnostic.c_str());
    assert(package);
    if (argc == 3) {
        FrameCaptureRequest capture;
        capture.outputPath = argv[2];
        assert(rhi_metal::beginMetalRhiFrameCapture(capture).success());
    }
    // Capture actual submissions before cancellation, preserving the existing
    // documented Metal capture-layer cancellation limitation.
    for (const bool cancel : {false, true}) {
        if (cancel && argc == 3)
            assert(rhi_metal::endMetalRhiFrameCapture().success());
        for (const bool fault : {false, true}) {
            auto desc = test::MetalPublicGraphicsFixture::deviceDesc();
            desc.memoryBudgetBytes = 64u * 1024u * 1024u;
            desc.enableBindlessGraphicsImages = true;
            auto device = rhi_metal::createMetalRhiDevice(desc);
            assert(device);
            test::MetalOriginalBindlessGraphicsContentFixture observer;
            observer.execute = true;
            observer.cancel = cancel;
            observer.ordinaryColorSampling = true;
            observer.completedReadContractVersion = Hdr ? 2u : 0u;
            observer.sampledReadStages =
                PipelineStage::VertexShader | PipelineStage::FragmentShader;
            observer.expectedFaultBits = fault ? 1u : 0u;
            observer.verifyOutputs = Hdr ? test::MetalSampledMaterialReadbackFixture::hdrGi
                                         : test::MetalSampledMaterialReadbackFixture::colorGi;
            MetalGraphicsGiResourcesFixture<Data, Hdr> resources;
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
    }
    std::printf(
        "BINDLESS_METAL_PUBLIC_ORDINARY_GI_OK workload%s clean/fault cancel original-root completion retirement\n",
        Data::packageName);
    return 0;
}

} // namespace arp::test
