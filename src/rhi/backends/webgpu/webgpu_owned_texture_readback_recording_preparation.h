#pragma once
#include "rhi/backends/webgpu/webgpu_original_texture_readback_graph_preparation.h"

namespace arp::rhi_webgpu {
// OriginalReadbackOwnedAdvancement/1: immutable ownership composition only.
// Native state and execution proof remain in the original recording/map owners.
class WebGpuOwnedTextureReadbackRecordingPreparation final
    : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOwnedTextureReadbackRecordingPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Plan = rhi::TextureReadbackColdBudgetPlan;
    using Destination = WebGpuOriginalTextureReadbackDestination;
    using Recording = WebGpuOrdinaryRecording;
    using Mapping = WebGpuOriginalTextureReadbackMapping;
    using Numerical = WebGpuOriginalTextureReadbackNumericalResult;

public:
    WebGpuOwnedTextureReadbackRecordingPreparation(ConstructionKey,
        std::shared_ptr<const Plan> plan,
        std::shared_ptr<Recording> recording,
        std::shared_ptr<Mapping> mapping,
        std::shared_ptr<Numerical> numerical)
        : plan_(std::move(plan)), recording_(std::move(recording)), mapping_(std::move(mapping)),
          numerical_(std::move(numerical))
    {}

    static std::shared_ptr<const WebGpuOwnedTextureReadbackRecordingPreparation> prepareOriginal(
        const std::shared_ptr<const Plan>& plan,
        const std::shared_ptr<const Destination>& destination,
        const std::shared_ptr<WebGpuOrdinaryCompletionOwner>& completion,
        const std::shared_ptr<const rhi::TextureReadbackStatisticsPlan>& statistics,
        const rhi::ResourceBarrierDesc& barrier,
        std::string_view passLabel,
        const char*& diagnostic)
    {
        WebGpuTextureReadbackColdBudgetLowering::Recipe recipe;
        diagnostic = WebGpuTextureReadbackColdBudgetLowering::lowerOriginal(plan, recipe);
        if (diagnostic)
            return {};
        if (!completion || !completion->sharedOwnerMatches(completion)
            || !completion->retained.accepting()
            || !core::SharedOwnerProjection::sameOwner(completion->originalReadbackPlan(), plan)) {
            diagnostic = "RHI-READBACK-OWNED-RECORDING-COMPLETION-OWNER";
            return {};
        }
        if (!statistics || !statistics->hasQuotaBoundSharedOwner(statistics)
            || !core::SharedOwnerProjection::sameOwner(statistics->originalMetadataQuota(),
                plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)])) {
            diagnostic = "RHI-READBACK-OWNED-RECORDING-STATISTICS-QUOTA";
            return {};
        }
        auto states = WebGpuOriginalTextureReadbackGraphPreparation::prepareOriginal(
            plan, destination, barrier, passLabel, diagnostic);
        if (!states)
            return {};
        const auto& quota = plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)];
        auto recording = Recording::prepareOriginalTextureReadback(
            states, completion, recipe.recording, quota, quota, quota, diagnostic);
        if (!recording)
            return {};
        auto mapping = Mapping::prepareOriginal(
            recording->originalReadbackCopies(), recipe.mapping, quota, quota, diagnostic);
        if (!mapping)
            return {};
        auto numerical = Numerical::prepareOriginal(
            statistics, mapping->resultDeclaration(), recipe.numerical, quota, diagnostic);
        if (!numerical)
            return {};
        if ((diagnostic = Numerical::attachOriginal(numerical, mapping)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(
                core::makeUnique<WebGpuOwnedTextureReadbackRecordingPreparation>(ConstructionKey{},
                    plan,
                    std::move(recording),
                    std::move(mapping),
                    std::move(numerical)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-OWNED-RECORDING-ALLOCATION";
            return {};
        }
    }

    const auto& originalPlan() const noexcept { return plan_; }

    const auto& recording() const noexcept { return recording_; }

    const auto& mapping() const noexcept { return mapping_; }

    const auto& numerical() const noexcept { return numerical_; }

private:
    const std::shared_ptr<const Plan> plan_;
    const std::shared_ptr<Recording> recording_;
    const std::shared_ptr<Mapping> mapping_;
    const std::shared_ptr<Numerical> numerical_;
};
} // namespace arp::rhi_webgpu
