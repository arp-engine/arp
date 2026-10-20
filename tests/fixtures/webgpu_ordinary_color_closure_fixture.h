#pragma once
#include "original_bindless_graphics_stream_fixture.h"
#include "original_graphics_color_gi_data_fixture.h"
#include "original_graphics_hdr_gi_data_fixture.h"
#include "webgpu_original_completed_content_fixture.h"

namespace arp::test {
template <class Data, bool Hdr>
struct WebGpuOrdinaryColorResourcesFixture : WebGpuOriginalCompletedContentRequest {
    static constexpr bool tableBufferData = true;

    static constexpr std::uint32_t completedTexturePublicationVersion() noexcept { return 6u; }

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        request.originalPreparation = {11u, true};
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table&) const
    {
        desc.sizeBytes = sizeof(typename Data::Numeric);
    }

    template <class Table> void initializeBuffer(rhi::GpuBuffer&, const Table&, std::uint32_t) const
    {}

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        // Three authored ordinary sampled reads, beside the existing attachment.
        budget.maxStateObservations += 3u;
        budget.maxTextureRanges += 3u;
        budget.maxCommands += 3u;
        budget.maxReferences += 3u;
        budget.maxInputDescriptors += 3u;
    }

    std::shared_ptr<const rhi::OriginalCommandTextureLease> createCompletedOrdinaryTexture(
        rhi::GpuDevice& device,
        rhi::TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const rhi::DescriptorBindingDesc& binding) const
    {
        assert(set == 2u && !binding.depthTexture);
        desc.format = Hdr ? rhi::TextureFormat::Rgba16Float : rhi::TextureFormat::Rgba8Unorm;
        auto upload = static_cast<const WebGpuOriginalCompletedPreparationRequest&>(*this);
        upload.initializeTexturePixels = Data::initializeTexturePixels;
        return upload.createCompletedTexture(
            device, desc, quota, binding.binding == 3u ? 2u : binding.binding);
    }
};

// Genuine upload completion followed by cold physical correspondence and the
// original Content9/profile3 plan. No draw/submit is issued by this fixture.
class WebGpuOrdinaryColorClosureFixture final {
    using Closure = rhi_webgpu::WebGpuBindlessRootNativeClosure;
    static constexpr std::uint64_t bytes = 65536u;

public:
    static constexpr bool completedTextures = true;

    template <class Root, class Inventory>
    void operator()(const Root& root, const Inventory& inventory, rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        if (!device) {
            assert(retained_ && Closure::validateOriginal(retained_));
            retained_.reset();
            assert(!quota_->liveBytes());
            std::puts(
                "BINDLESS_RHI_WEBGPU_ORDINARY_COLOR_CLOSURE17_RETIRE_OK original-owners-expired quota0 no-execution");
            return;
        }
        const char* id = nullptr;
        std::uint32_t entries = static_cast<std::uint32_t>(root->elements().size()) + 1u;
        std::uint32_t views = 0u, samplers = 0u;
        bool hdr = false;
        for (const auto& domain : root->ordinaryDomains()) {
            entries += static_cast<std::uint32_t>(domain.source->frozenElements().size());
            auto payload = prepareWebGpuOriginalOrdinaryColorDescriptors(root, domain.set, 17u);
            assert(payload);
            for (const auto& element : domain.source->frozenElements()) {
                if (const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &element.resourceOwner)) {
                    ++views;
                    const auto& desc = (*view)->originalParentLease()->originalObject()->desc();
                    hdr = desc.format == TextureFormat::Rgba16Float;
                    assert(!webGpuNativePayloadFor(*domain.source->originalObject())
                        && desc.mipLevelCount == 2u);
                } else if (std::holds_alternative<
                               std::shared_ptr<const OriginalCommandSamplerLease>>(
                               element.resourceOwner))
                    ++samplers;
            }
        }
        assert(views == 3u && samplers == 1u);
        assert(!prepareWebGpuOriginalOrdinaryColorDescriptors(root, UINT32_MAX, 17u));
        assert(!prepareWebGpuOriginalOrdinaryColorDescriptors(root, 2u, 4u));
        auto forgedRoot =
            std::shared_ptr<const OriginalCommandBindlessRootSource>(quota_, root.get());
        assert(!prepareWebGpuOriginalOrdinaryColorDescriptors(forgedRoot, 2u, 17u));
        forgedRoot.reset();
        std::uint32_t cpuOwners = 0u;
        assert(!inventory->cpuOwnerCount(3u, UINT32_MAX, cpuOwners));
        const auto nativeCeiling = 2u * entries + 3u + 2u
            + 2u * static_cast<std::uint32_t>(root->ordinaryDomains().size());
        OriginalBindlessRootNativeClosureBudget budget{
            17u, true, entries + 1u, nativeCeiling, cpuOwners + nativeCeiling, bytes};
        budget.maxAttachmentViews = 1u;
        const auto factory = [&] {
            return Closure::prepareOriginal(root, inventory, *device, budget, quota_, id);
        };
        CpuQuotaFactoryFixture pressure(quota_);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        retained_ = factory();
        if (id)
            std::fprintf(stderr, "ordinary color Closure17: %s\n", id);
        assert(retained_ && !id && !retained_->nativeExecutionProven()
            && retained_->resourceEntryCount() == entries + 1u
            && retained_->attachmentViewCount() == 1u);
        budget.maxNativeOwners = retained_->nativeOwnerCount();
        budget.maxRetainedOwners = retained_->retainedOwnerCount();
        auto exact = factory();
        assert(exact && !id);
        exact.reset();
        const auto before = quota_->liveBytes();
        for (unsigned field = 0u; field < 4u; ++field) {
            auto invalid = budget;
            if (field == 0u)
                --invalid.maxResourceEntries;
            else if (field == 1u)
                --invalid.maxNativeOwners;
            else if (field == 2u)
                --invalid.maxRetainedOwners;
            else
                --invalid.maxAttachmentViews;
            assert(!Closure::prepareOriginal(root, inventory, *device, invalid, quota_, id) && id
                && quota_->liveBytes() == before);
        }
        auto forged = std::shared_ptr<const Closure>(quota_, retained_.get());
        assert(Closure::validateOriginal(forged));
        forged.reset();
        auto payload = prepareWebGpuOriginalOrdinaryColorDescriptors(root, 2u, 17u);
        assert(payload && payload->completedTexturePublications.size() == 3u);
        auto& proof = payload->completedTexturePublications.front();
        const auto originalSource = proof.source;
        proof.source =
            std::shared_ptr<const OriginalCommandTextureViewSource>(quota_, originalSource.get());
        assert(retained_->validate() && !factory());
        proof.source = originalSource;
        for (auto& binding : payload->bindings) {
            if (binding.textureView) {
                const auto original = binding.textureView;
                binding.textureView =
                    std::shared_ptr<WebGpuNativeTextureViewPayload>(quota_, original.get());
                assert(retained_->validate() && !factory());
                binding.textureView = original;
            } else if (binding.sampler) {
                const auto handle = binding.sampler->sampler;
                binding.sampler->sampler = nullptr;
                assert(retained_->validate() && !factory());
                binding.sampler->sampler = handle;
            }
            assert(!retained_->validate());
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota_}, [&]() noexcept {
            return Closure::validateOriginal(retained_);
        }));
        OriginalBindlessGraphicsStreamFixture::execute(
            root, inventory, *device, *this, nullptr, 3u);
        std::printf(
            "BINDLESS_RHI_WEBGPU_ORDINARY_COLOR_CLOSURE17_OK format%s images3 sampler1 2D-array-Cube original-upload-publication6 Root2 same-inventory exact-less-pressure forged-mutated-refused hot0 no-execution\n",
            hdr ? "RGBA16F" : "RGBA8");
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root&,
        const Inventory&,
        const Binding& binding,
        const Transaction& transaction,
        rhi::GpuDevice&) const
    {
        assert(transaction->journalEmpty()
            && transaction->originalBindlessCompletedReadContractVersion() == 3u
            && transaction->executionDiagnostic(rhi::Backend::WebGPU)
            && !binding->originalResult()->publishedBytes());
        const auto* counts = transaction->originalMixedContentCounts();
        assert(counts && counts->completedControls == 3u && counts->attachmentControls == 1u
            && counts->drawReads == 3u);
    }

private:
    mutable std::shared_ptr<const Closure> retained_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_ =
        core::makeShared<core::CpuMemoryQuota>(bytes);
};
} // namespace arp::test
