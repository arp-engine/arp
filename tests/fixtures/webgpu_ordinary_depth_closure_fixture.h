#pragma once
#include "original_ordinary_depth_root_fixture.h"
#include "rhi/backends/webgpu/webgpu_bindless_root.h"
#include "webgpu_original_completed_preparation_fixture.h"

namespace arp::test {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
// Consumes the existing genuine depth producer and completed color uploader.
// This freezes the original public Root; no shader work or completion is issued.
class WebGpuOrdinaryDepthClosureFixture final {
public:
    using Closure = rhi_webgpu::WebGpuBindlessRootNativeClosure;

    static rhi::OriginalBindlessRootNativeClosureBudget nativeBudget(
        const OriginalOrdinaryDepthRootFixture::Prepared& prepared,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& depth,
        std::uint32_t version)
    {
        using namespace rhi;
        const auto& root = prepared.root;
        const auto& inventory = prepared.inventory;
        std::uint32_t entries = root->elements().size() + 1u;
        for (const auto& domain : root->ordinaryDomains())
            entries += domain.source->frozenElements().size();
        std::uint32_t owners = 0u;
        assert(!inventory->cpuOwnerCount(3u, UINT32_MAX, owners));
        const auto nativeCeiling = 2u * entries + 3u + 2u + 2u * root->ordinaryDomains().size();
        OriginalBindlessRootNativeClosureBudget budget{version,
            true,
            entries + 1u,
            static_cast<std::uint32_t>(nativeCeiling),
            static_cast<std::uint32_t>(owners + nativeCeiling
                + OriginalCompletedDepthTextureSource::completeRetainedOwnerCount),
            depth->originalMetadataQuota()->limitBytes()};
        budget.maxAttachmentViews = 1u;
        std::uint64_t texels = 0u;
        assert(!semantics::TextureUploadDestinationValidation::footprint(
            depth->originalViewSource()->originalParentLease()->originalObject()->desc(), texels));
        budget.completedDepthInputs = {1u,
            true,
            depth->cells().size(),
            texels,
            OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        return budget;
    }

    static std::shared_ptr<const Closure> verify(rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& depth)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const auto& quota = depth->originalMetadataQuota();
        const auto bytes = quota->limitBytes();
        const char* id = nullptr;
        WebGpuOriginalCompletedPreparationRequest uploader;
        TextureDesc desc;
        desc.format = TextureFormat::Rgba8Unorm;
        auto colorParent = uploader.createCompletedTexture(device, desc, quota, 0u);
        assert(colorParent);
        TextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.usage = TextureUsage::Sampled;
        viewDesc.mipLevelCount = desc.mipLevelCount;
        auto colorView = device.createTextureView(*colorParent->originalObject(), viewDesc);
        assert(colorView);
        auto lease = OriginalCommandTextureViewLease::prepareOriginal(
            colorView, {1u, true, 2u, bytes}, quota, id);
        assert(lease && !id);
        auto color = OriginalCommandTextureViewSource::prepareOriginal(lease,
            colorParent,
            {1u, true, OriginalCommandTextureViewSource::completeRetainedOwnerCount, bytes},
            quota,
            id);
        assert(color && !id);
        std::shared_ptr<const Closure> retained;
        for (const auto fault : {false, true}) {
            auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            {
                const auto prepared = OriginalOrdinaryDepthRootFixture::prepare(
                    device, package, depth, color, fault, 2u, true);
                OriginalOrdinaryDepthRootFixture::verifyCombinedPlan(prepared, device, true);
                const auto& root = prepared.root;
                const auto& inventory = prepared.inventory;
                assert(!root->originalSnapshot()->plan().executable
                    && !OriginalBindlessRootNativeClosureContract::
                        canonicalGraphicsDepthRootDiagnostic(*root, 19u));
                std::uint32_t entries = root->elements().size() + 1u;
                for (const auto& domain : root->ordinaryDomains())
                    entries += domain.source->frozenElements().size();
                auto budget = nativeBudget(prepared, depth, 19u);
                const auto factory = [&] {
                    return Closure::prepareOriginal(
                        root, inventory, device, budget, nativeQuota, id);
                };
                CpuQuotaFactoryFixture pressure(nativeQuota);
                const auto peak = pressure.measure(factory);
                pressure.verify(peak, factory);
                auto closure = factory();
                if (id)
                    std::fprintf(stderr, "ordinary depth Closure19: %s\n", id);
                assert(closure && !id && !closure->nativeExecutionProven()
                    && closure->resourceEntryCount() == entries + 1u
                    && closure->attachmentViewCount() == 1u);
                budget.maxNativeOwners = closure->nativeOwnerCount();
                budget.maxRetainedOwners = closure->retainedOwnerCount();
                {
                    const auto exact = factory();
                    assert(exact && !id);
                }
                const auto baseline = nativeQuota->liveBytes();
                for (unsigned field = 0u; field < 7u; ++field) {
                    auto invalid = budget;
                    switch (field) {
                    case 0u:
                        --invalid.maxResourceEntries;
                        break;
                    case 1u:
                        --invalid.maxNativeOwners;
                        break;
                    case 2u:
                        --invalid.maxRetainedOwners;
                        break;
                    case 3u:
                        --invalid.maxAttachmentViews;
                        break;
                    case 4u:
                        --invalid.completedDepthInputs.maxCells;
                        break;
                    case 5u:
                        --invalid.completedDepthInputs.maxTextureTexelBytes;
                        break;
                    case 6u:
                        --invalid.completedDepthInputs.maxRetainedOwners;
                        break;
                    }
                    assert(
                        !Closure::prepareOriginal(root, inventory, device, invalid, nativeQuota, id)
                        && id && nativeQuota->liveBytes() == baseline);
                }
                assert(!prepareWebGpuOriginalOrdinaryColorDescriptors(root, 2u, 19u));
                assert(!prepareWebGpuOriginalOrdinaryGraphicsDescriptors(root, 2u, 17u));
                assert(!prepareWebGpuOriginalOrdinaryGraphicsDescriptors(root, 2u, 18u));
                auto payload = prepareWebGpuOriginalOrdinaryGraphicsDescriptors(root, 2u, 19u);
                assert(payload && payload->bindings.size() == 5u
                    && payload->completedTexturePublications.size() == 2u);
                auto proof = std::find_if(payload->completedTexturePublications.begin(),
                    payload->completedTexturePublications.end(),
                    [](const auto& p) {
                        return bool(p.depthContent);
                    });
                assert(proof != payload->completedTexturePublications.end());
                const auto original = proof->depthContent;
                proof->depthContent = std::shared_ptr<const OriginalCompletedDepthTextureSource>(
                    nativeQuota, original.get());
                assert(closure->validate() && !factory());
                proof->depthContent = original;
                auto equivalent = OriginalCompletedDepthTextureSource::prepareOriginal(
                    depth->originalViewSource(), depth->budget(), quota, id);
                assert(equivalent && !id && equivalent.get() != original.get());
                proof->depthContent = equivalent;
                assert(closure->validate() && !factory());
                proof->depthContent = original;
                equivalent.reset();
                for (auto& binding : payload->bindings) {
                    if (binding.textureView) {
                        const auto actual = binding.textureView;
                        binding.textureView = std::shared_ptr<WebGpuNativeTextureViewPayload>(
                            nativeQuota, actual.get());
                        assert(closure->validate() && !factory());
                        binding.textureView = actual;
                    } else if (binding.sampler) {
                        const auto actual = binding.sampler->sampler;
                        binding.sampler->sampler = nullptr;
                        assert(closure->validate() && !factory());
                        binding.sampler->sampler = actual;
                    }
                    assert(!closure->validate());
                }
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{nativeQuota}, [&]() noexcept {
                        return Closure::validateOriginal(closure);
                    }));
                if (fault)
                    retained = std::move(closure);
            }
            if (!fault)
                assert(!nativeQuota->liveBytes());
        }
        std::puts(
            "BINDLESS_WEBGPU_ORDINARY_DEPTH_CLOSURE19_OK clean/fault Root2 Source6 Input1 canonical-depth2 color1 independent-comparison2 exact-less-pressure forged-equivalent-refused hot0 no-execution");
        return retained;
    }
};
#endif
} // namespace arp::test
