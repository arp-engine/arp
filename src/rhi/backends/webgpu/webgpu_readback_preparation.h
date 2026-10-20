#pragma once

#include "core/atomic_capacity.h"
#include "core/memory_budget_ledger.h"
#include "core/runtime/completion_drain_consumer.h"
#include "core/runtime/completion_evidence_batch.h"
#include "core/utf8.h"
#include "rhi/backends/webgpu/webgpu_completed_texture_upload_declaration.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_creation.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_state.h"
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/transfer_readback_cold_budget.h"
#include "rhi/frontend/transfer_readback_content_intent.h"
#include "rhi/frontend/transfer_readback_coverage.h"
#include "rhi/frontend/transfer_readback_input.h"

#include <limits>

// region Webgpu original texture readback source
namespace arp::rhi_webgpu {
struct WebGpuOriginalTextureReadbackSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSourceOwners = 0u, maxMipCopies = 0u;
    std::uint64_t maxRetainedAllocationBytes = 0u, maxCpuMetadataBytes = 0u;
};

struct WebGpuCompletedTextureReadbackSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCompletedGraphs = 0u, maxSourceOwners = 0u, maxReadbackMips = 0u,
                  maxUploadMips = 0u;
    std::uint64_t maxTargetAllocationBytes = 0u, maxRetainedNativeBytes = 0u,
                  maxRetainedContentBytes = 0u, maxCpuMetadataBytes = 0u;
};

// OriginalTextureReadbackSourceBinding/1. Retains the original object and its
// registration; this grants no source content, staging or command/map proof.
class WebGpuOriginalTextureReadbackSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Control = rhi::common::RecordedWholeResourceStateView;
    using Content = rhi::TextureReadbackContentIntent;

public:
    struct Lowering {
        bool validatorOnly = false, nativeBindingImplemented = false;
        const char* diagnostic = "RHI-READBACK-SOURCE-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-READBACK-SOURCE-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-READBACK-SOURCE-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-READBACK-SOURCE-EXECUTION-UNPROVEN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalTextureReadbackSource(ConstructionKey,
        std::shared_ptr<const rhi::TextureReadbackLayout> layout,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        WebGpuTransferResourceObservation observation,
        Control::Observation state)
        : layout_(std::move(layout)), registry_(std::move(registry)),
          observation_(std::move(observation)), state_(state)
    {}

    static const char* validateBudget(const WebGpuOriginalTextureReadbackSourceBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-SOURCE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-SOURCE-OPT-IN";
        if (!b.maxSourceOwners || !b.maxMipCopies || !b.maxRetainedAllocationBytes
            || !b.maxCpuMetadataBytes)
            return "RHI-READBACK-SOURCE-BUDGET";
        return nullptr;
    }

    static const char* validateBudget(const WebGpuCompletedTextureReadbackSourceBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-COMPLETED-SOURCE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-COMPLETED-SOURCE-OPT-IN";
        if (!b.maxCompletedGraphs || !b.maxSourceOwners || !b.maxReadbackMips || !b.maxUploadMips
            || !b.maxTargetAllocationBytes || !b.maxRetainedNativeBytes
            || !b.maxRetainedContentBytes || !b.maxCpuMetadataBytes)
            return "RHI-READBACK-COMPLETED-SOURCE-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuOriginalTextureReadbackSource> prepareCompletedOriginal(
        std::shared_ptr<const rhi::TextureReadbackLayout> layout,
        std::shared_ptr<const WebGpuCompletedTextureUploadDeclaration> completed,
        const WebGpuCompletedTextureReadbackSourceBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-COMPLETED-SOURCE-QUOTA";
            return {};
        }
        if ((diagnostic = WebGpuCompletedTextureUploadDeclaration::validateOriginal(completed)))
            return {};
        if ((diagnostic = rhi::semantics::TextureReadbackContentCoverage::validateOriginal(layout,
                 completed->completedPacking(),
                 {1u, true, b.maxReadbackMips, b.maxUploadMips})))
            return {};
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        diagnostic = "RHI-READBACK-SOURCE-NATIVE-UNAVAILABLE";
        return {};
#else
        const auto& registry = completed->completedRegistry();
        const auto* texture = completed->completedTexture();
        if (!registry || !registry->sharedOwnerMatches(registry) || !texture) {
            diagnostic = "RHI-READBACK-COMPLETED-SOURCE-OBJECT";
            return {};
        }
        WebGpuTransferResourceObservation observation;
        const auto id = texture->id();
        if ((diagnostic = registry->observeTransferResources(&id, 1u, &observation)))
            return {};
        if ((diagnostic = validateCompletedDeclaration(*layout, *completed, observation)))
            return {};
        if ((diagnostic = validateCurrentState(*registry, observation)))
            return {};
        if (observation.ownerCount() + 2u > b.maxSourceOwners
            || observation.allocationBytes > b.maxTargetAllocationBytes
            || completed->retainedNativeAllocationBytes() > b.maxRetainedNativeBytes
            || completed->retainedContentBytes() > b.maxRetainedContentBytes) {
            diagnostic = "RHI-READBACK-COMPLETED-SOURCE-CAPACITY";
            return {};
        }
        const auto state = observation.texture->recordedState().observe();
        if (!state) {
            diagnostic = "RHI-READBACK-SOURCE-STATE-OBSERVATION";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = shareOriginal(core::makeUnique<WebGpuOriginalTextureReadbackSource>(
                ConstructionKey{}, layout, registry, std::move(observation), *state));
            // Only this factory may select the completed profile, after all
            // original declaration/coverage/budget checks. No caller policy bit.
            result->completed_ = std::move(completed);
            if ((diagnostic = result->validate()))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-SOURCE-ALLOCATION";
            return {};
        }
#endif
    }

    static std::shared_ptr<const WebGpuOriginalTextureReadbackSource> prepareOriginal(
        std::shared_ptr<const rhi::TextureReadbackLayout> layout,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        core::UniquePtr<rhi::GpuTexture>& source,
        const WebGpuOriginalTextureReadbackSourceBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-SOURCE-QUOTA";
            return {};
        }
        if (!layout || !layout->hasQuotaBoundSharedOwner(layout)) {
            diagnostic = "RHI-READBACK-SOURCE-LAYOUT-OWNER";
            return {};
        }
        if (!registry || !registry->sharedOwnerMatches(registry)) {
            diagnostic = "RHI-READBACK-SOURCE-REGISTRY-OWNER";
            return {};
        }
        if (!source) {
            diagnostic = "RHI-READBACK-SOURCE-OBJECT-MISSING";
            return {};
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        diagnostic = "RHI-READBACK-SOURCE-NATIVE-UNAVAILABLE";
        return {};
#else
        WebGpuTransferResourceObservation observation;
        std::optional<Control::Observation> state;
        if ((diagnostic = captureOrdinaryOriginal(
                 *layout, *registry, *source, b, 1u, observation, state)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = shareOriginal(core::makeUnique<WebGpuOriginalTextureReadbackSource>(
                ConstructionKey{}, layout, registry, std::move(observation), *state));
            // All fallible work precedes this final allocation-free owner move.
            if ((diagnostic =
                        validateDeclaration(*layout, *registry, *source, result->observation_)))
                return {};
            if (!result->observation_.texture->recordedState().matches(result->state_)) {
                diagnostic = "RHI-READBACK-SOURCE-STATE-STALE";
                return {};
            }
            result->source_ = std::move(source);
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-SOURCE-ALLOCATION";
            return {};
        }
#endif
    }

    static std::shared_ptr<const WebGpuOriginalTextureReadbackSource> prepareLeasedSnapshotOriginal(
        std::shared_ptr<const rhi::TextureReadbackLayout> layout,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::shared_ptr<const rhi::TextureReadbackSourceLease> lease,
        const Content::Descriptor& intent,
        const WebGpuOriginalTextureReadbackSourceBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        const auto decision = Content::lower(rhi::Backend::WebGPU, intent);
        diagnostic = Content::validateDecision(rhi::Backend::WebGPU, intent, decision);
        if (diagnostic)
            return {};
        return prepareLeasedAdmitted(std::move(layout),
            std::move(registry),
            std::move(lease),
            b,
            std::move(quota),
            diagnostic,
            decision);
    }

    static std::shared_ptr<const WebGpuOriginalTextureReadbackSource>
    prepareColdLeasedSnapshotOriginal(std::shared_ptr<const rhi::TextureReadbackLayout> layout,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::shared_ptr<const rhi::TextureReadbackSourceLease> lease,
        const rhi::GpuTexture& candidate,
        const Content::Descriptor& intent,
        const WebGpuOriginalTextureReadbackSourceBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        const auto decision = Content::lower(rhi::Backend::WebGPU, intent);
        diagnostic = Content::validateDecision(rhi::Backend::WebGPU, intent, decision);
        if (diagnostic)
            return {};
        return prepareLeasedAdmitted(std::move(layout),
            std::move(registry),
            std::move(lease),
            b,
            std::move(quota),
            diagnostic,
            decision,
            &candidate);
    }

    static std::shared_ptr<const WebGpuOriginalTextureReadbackSource> prepareLeasedOriginal(
        std::shared_ptr<const rhi::TextureReadbackLayout> layout,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::shared_ptr<const rhi::TextureReadbackSourceLease> lease,
        const WebGpuOriginalTextureReadbackSourceBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareLeasedAdmitted(std::move(layout),
            std::move(registry),
            std::move(lease),
            b,
            std::move(quota),
            diagnostic,
            {});
    }

private:
    static std::shared_ptr<const WebGpuOriginalTextureReadbackSource> prepareLeasedAdmitted(
        std::shared_ptr<const rhi::TextureReadbackLayout> layout,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        std::shared_ptr<const rhi::TextureReadbackSourceLease> lease,
        const WebGpuOriginalTextureReadbackSourceBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic,
        Content::Decision content,
        const rhi::GpuTexture* coldCandidate = nullptr)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-SOURCE-QUOTA";
            return {};
        }
        if (!layout || !layout->hasQuotaBoundSharedOwner(layout)) {
            diagnostic = "RHI-READBACK-SOURCE-LAYOUT-OWNER";
            return {};
        }
        if (!registry || !registry->sharedOwnerMatches(registry)) {
            diagnostic = "RHI-READBACK-SOURCE-REGISTRY-OWNER";
            return {};
        }
        if (!lease || !lease->hasQuotaBoundSharedOwner(lease)) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-OWNER";
            return {};
        }
        if (lease->contractVersion() != 1u) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE";
            return {};
        }
        if ((diagnostic = coldCandidate
                    ? rhi::TextureReadbackSourceLease::validateColdOriginal(lease, *coldCandidate)
                    : rhi::TextureReadbackSourceLease::validateOriginal(
                          lease, lease->originalRequest())))
            return {};
        if (!core::SharedOwnerProjection::sameOwner(
                layout, lease->originalRequest()->originalLayout())) {
            diagnostic = "RHI-READBACK-SOURCE-LEASE-LAYOUT-MISMATCH";
            return {};
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        diagnostic = "RHI-READBACK-SOURCE-NATIVE-UNAVAILABLE";
        return {};
#else
        WebGpuTransferResourceObservation observation;
        std::optional<Control::Observation> state;
        const auto* original = coldCandidate ? coldCandidate : lease->sourceTexture();
        if ((diagnostic = captureOrdinaryOriginal(*layout,
                 *registry,
                 *original,
                 b,
                 rhi::TextureReadbackSourceLease::retainedOwnerCount + 1u,
                 observation,
                 state)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = shareOriginal(core::makeUnique<WebGpuOriginalTextureReadbackSource>(
                ConstructionKey{}, layout, registry, std::move(observation), *state));
            if ((diagnostic = coldCandidate ? rhi::TextureReadbackSourceLease::validateColdOriginal(
                                                  lease, *coldCandidate)
                                            : lease->validate()))
                return {};
            if ((diagnostic =
                        validateDeclaration(*layout, *registry, *original, result->observation_)))
                return {};
            if (!result->observation_.texture->recordedState().matches(result->state_)) {
                diagnostic = "RHI-READBACK-SOURCE-STATE-STALE";
                return {};
            }
            result->leased_ = std::move(lease);
            result->content_ = content;
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-SOURCE-ALLOCATION";
            return {};
        }
#endif
    }

public:
    const char* validateColdCandidateOriginal(const rhi::GpuTexture& candidate) const noexcept
    {
        if (completed_ || source_ || !leased_)
            return "RHI-READBACK-SOURCE-COLD-PROFILE";
        if (const auto* id =
                rhi::TextureReadbackSourceLease::validateColdOriginal(leased_, candidate))
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-SOURCE-NATIVE-UNAVAILABLE";
#else
        if (webGpuNativeDeviceLost())
            return "RHI-READBACK-SOURCE-DEVICE-LOST";
        if (const auto* id = validateDeclaration(*layout_, *registry_, candidate, observation_))
            return id;
        if (const auto* id = validateCurrentState(*registry_, observation_))
            return id;
        if (!observation_.texture->recordedState().matches(state_))
            return "RHI-READBACK-SOURCE-STATE-STALE";
        return Content::validateDecision(
            rhi::Backend::WebGPU, {1u, true, Content::Mode::QueueOrderedSnapshot}, content_);
#endif
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateReadyDeclaration())
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-SOURCE-NATIVE-UNAVAILABLE";
#else
        if (const auto* id = validateCurrentState(*registry_, observation_))
            return id;
        return observation_.texture->recordedState().matches(state_)
            ? nullptr
            : "RHI-READBACK-SOURCE-STATE-STALE";
#endif
    }

    // Original identity/content declaration only. A current transaction must
    // independently prove access and exact pending/committed journal ownership.
    const char* validateReadyDeclaration() const noexcept
    {
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-SOURCE-NATIVE-UNAVAILABLE";
#else
        if (webGpuNativeDeviceLost())
            return "RHI-READBACK-SOURCE-DEVICE-LOST";
        if (completed_) {
            if (const auto* id =
                    WebGpuCompletedTextureUploadDeclaration::validateOriginal(completed_))
                return id;
            if (const auto* id = validateCompletedDeclaration(*layout_, *completed_, observation_))
                return id;
        } else {
            if (leased_)
                if (const auto* id = leased_->validate())
                    return id;
            const auto* original = sourceTexture();
            if (!original)
                return "RHI-READBACK-SOURCE-OBJECT-MISSING";
            if (const auto* id = validateDeclaration(*layout_, *registry_, *original, observation_))
                return id;
        }
        return nullptr;
#endif
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-READBACK-SOURCE-EXECUTION-UNPROVEN";
    }

    const auto& layout() const noexcept { return layout_; }

    const auto& registry() const noexcept { return registry_; }

    const auto& resource() const noexcept { return observation_; }

    const auto& stateObservation() const noexcept { return state_; }

    const rhi::GpuTexture* sourceTexture() const noexcept
    {
        return completed_ ? completed_->completedTexture()
            : leased_     ? leased_->sourceTexture()
                          : source_.get();
    }

    const auto& publicSourceLease() const noexcept { return leased_; }

    bool hasCompletedUpload() const noexcept { return bool(completed_); }

    Content::Mode contentMode() const noexcept
    {
        return completed_ ? Content::Mode::CompletedOriginalUpload : content_.mode;
    }

    const char* validateReadbackContentProfile() const noexcept
    {
        if (completed_)
            return WebGpuCompletedTextureUploadDeclaration::validateOriginal(completed_);
        if (!leased_ || content_.mode != Content::Mode::QueueOrderedSnapshot)
            return "RHI-READBACK-PAIR-SOURCE-PROFILE";
        if (const auto* id = rhi::TextureReadbackSourceLease::validateOriginal(
                leased_, leased_->originalRequest()))
            return id;
        return Content::validateDecision(
            rhi::Backend::WebGPU, {1u, true, Content::Mode::QueueOrderedSnapshot}, content_);
    }

    std::uint32_t retainedCompletedGraphCount() const noexcept { return completed_ ? 1u : 0u; }

    std::uint32_t retainedSourceOwnerCount() const noexcept
    {
        return observation_.ownerCount()
            + (leased_ ? rhi::TextureReadbackSourceLease::retainedOwnerCount + 1u
                       : 1u + (completed_ ? 1u : 0u));
    }

    std::uint64_t retainedNativeAllocationBytes() const noexcept
    {
        return completed_ ? completed_->retainedNativeAllocationBytes()
                          : observation_.allocationBytes;
    }

    std::uint64_t retainedContentBytes() const noexcept
    {
        return completed_ ? completed_->retainedContentBytes() : 0u;
    }

    std::size_t retainedNativeAllocationRecordCount() const noexcept
    {
        return completed_ ? completed_->retainedNativeAllocationRecordCount() : 1u;
    }

    core::SharedAllocationProjection::Record retainedNativeAllocationRecordAt(
        std::size_t index) const noexcept
    {
        if (completed_)
            return completed_->retainedNativeAllocationRecordAt(index);
        return index == 0u ? observation_.allocationRecord()
                           : core::SharedAllocationProjection::Record{};
    }

    const WebGpuTransferResourceObservation* retainedNativeResourceAt(
        std::size_t index) const noexcept
    {
        return completed_ ? completed_->retainedNativeResourceAt(index)
            : index == 0u ? &observation_
                          : nullptr;
    }

private:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static const char* captureOrdinaryOriginal(const rhi::TextureReadbackLayout& layout,
        const WebGpuRhiResourceRegistry& registry,
        const rhi::GpuTexture& source,
        const WebGpuOriginalTextureReadbackSourceBudget& b,
        std::uint32_t cpuOwners,
        WebGpuTransferResourceObservation& observation,
        std::optional<Control::Observation>& state) noexcept
    {
        if (webGpuNativeDeviceLost())
            return "RHI-READBACK-SOURCE-DEVICE-LOST";
        if (source.id() != layout.inputOwner()->scalarFields().sourceResource()
            || !layout.sourceLayout().matches(source.desc()))
            return "RHI-READBACK-SOURCE-INPUT-MISMATCH";
        const auto id = source.id();
        if (const auto* diagnostic = registry.observeTransferResources(&id, 1u, &observation))
            return diagnostic;
        if (const auto* diagnostic = validateDeclaration(layout, registry, source, observation))
            return diagnostic;
        if (const auto* diagnostic = validateCurrentState(registry, observation))
            return diagnostic;
        if (observation.ownerCount() + cpuOwners > b.maxSourceOwners
            || layout.mipCopies().size() > b.maxMipCopies
            || observation.allocationBytes > b.maxRetainedAllocationBytes)
            return "RHI-READBACK-SOURCE-CAPACITY";
        state = observation.texture->recordedState().observe();
        return state ? nullptr : "RHI-READBACK-SOURCE-STATE-OBSERVATION";
    }

    static const char* validateDeclaration(const rhi::TextureReadbackLayout& layout,
        const WebGpuRhiResourceRegistry& registry,
        const rhi::GpuTexture& source,
        const WebGpuTransferResourceObservation& observation) noexcept
    {
        if (!observation.texture || observation.buffer || observation.id != source.id()
            || !core::SharedOwnerProjection::sameOwner(
                webGpuNativePayloadFor(source), observation.texture)
            || !layout.sourceLayout().matches(source.desc())
            || !layout.sourceLayout().matches(observation.textureDesc))
            return "RHI-READBACK-SOURCE-ORIGINAL-OBJECT";
        if (const auto* id = observation.texture->ordinaryContentAccessDiagnostic())
            return id;
        return validateLiveDeclaration(registry, observation);
    }

    static const char* validateCompletedDeclaration(const rhi::TextureReadbackLayout& layout,
        const WebGpuCompletedTextureUploadDeclaration& completed,
        const WebGpuTransferResourceObservation& observation) noexcept
    {
        const auto* source = completed.completedTexture();
        const auto& original = completed.completedTextureResource();
        if (!source || !observation.texture || observation.buffer || observation.id != source->id()
            || !core::SharedOwnerProjection::sameOwner(
                webGpuNativePayloadFor(*source), observation.texture)
            || !original.sameOwnersAndLayout(observation)
            || !layout.sourceLayout().matches(source->desc())
            || !layout.sourceLayout().matches(observation.textureDesc)
            || !observation.texture->contentAccessPolicy().originalUploadExclusive())
            return "RHI-READBACK-COMPLETED-SOURCE-ORIGINAL-OBJECT";
        return validateLiveDeclaration(*completed.completedRegistry(), observation);
    }

    static const char* validateLiveDeclaration(const WebGpuRhiResourceRegistry& registry,
        const WebGpuTransferResourceObservation& observation) noexcept
    {
        if (const auto* id = registry.validateTransferResources(&observation, 1u))
            return id;
        const auto& control = observation.texture->recordedState();
        if (!control.active())
            return "RHI-READBACK-SOURCE-STATE-INVALIDATED";
        if (!rhi::semantics::isKnownResourceState(control.state()))
            return "RHI-READBACK-SOURCE-STATE-UNKNOWN";
        return nullptr;
    }

    static const char* validateCurrentState(const WebGpuRhiResourceRegistry& registry,
        const WebGpuTransferResourceObservation& observation) noexcept
    {
        if (const auto* id = validateLiveDeclaration(registry, observation))
            return id;
        return observation.texture->recordedState().pending() ? "RHI-READBACK-SOURCE-STATE-PENDING"
                                                              : nullptr;
    }
#endif
    const std::shared_ptr<const rhi::TextureReadbackLayout> layout_;
    const std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    const WebGpuTransferResourceObservation observation_;
    const Control::Observation state_;
    core::UniquePtr<rhi::GpuTexture> source_;
    std::shared_ptr<const rhi::TextureReadbackSourceLease> leased_;
    Content::Decision content_;
    std::shared_ptr<const WebGpuCompletedTextureUploadDeclaration> completed_;
};

// endregion Webgpu original texture readback source

// region Webgpu original texture readback device staging budget

// Private byte lifetime only. This owner cannot record, submit, map or signal.
class WebGpuOriginalTextureReadbackDeviceStagingBudget final
    : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackDeviceStagingBudget;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Plan = rhi::TextureReadbackColdBudgetPlan;

public:
    struct Outcome {
        bool implemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-READBACK-DEVICE-STAGING-BACKEND-UNIMPLEMENTED";
    };

    static constexpr Outcome lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {false, true, "RHI-READBACK-DEVICE-STAGING-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {};
        case rhi::Backend::WebGPU:
            return {true, false, "RHI-READBACK-DEVICE-STAGING-PRIVATE-ONLY"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    class Charge final : public core::QuotaBoundSharedObjectIdentity {
        friend class WebGpuOriginalTextureReadbackDeviceStagingBudget;

    public:
        Charge(ConstructionKey,
            std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget> owner,
            std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
            std::uint64_t bytes)
            : owner_(std::move(owner)), source_(std::move(source)), lease_(owner_->capacity_, bytes)
        {}

        bool charged() const noexcept { return lease_.charged(); }

        std::uint64_t bytes() const noexcept { return lease_.units(); }

        bool nativeExecutionProven() const noexcept { return false; }

    private:
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget> owner_;
        const std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source_;
        core::AtomicCapacityLease lease_;
    };

    class AllocationLifetime final {
    public:
        AllocationLifetime(
            ConstructionKey, std::shared_ptr<void> allocation, std::shared_ptr<const Charge> charge)
            : charge_(std::move(charge)), allocation_(std::move(allocation))
        {}

    private:
        // Release the original ledger token before returning aggregate capacity.
        const std::shared_ptr<const Charge> charge_;
        const std::shared_ptr<void> allocation_;
    };

    WebGpuOriginalTextureReadbackDeviceStagingBudget(ConstructionKey,
        std::shared_ptr<const Plan> plan,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry)
        : plan_(std::move(plan)), registry_(std::move(registry)),
          capacity_(
              core::makeShared<core::AtomicCapacityBudget>(plan_->budget().maxDeviceStagingBytes))
    {}

    static auto prepareOriginal(std::shared_ptr<const Plan> plan,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        const char*& diagnostic)
        -> std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>
    {
        diagnostic = Plan::validateOriginal(plan);
        if (diagnostic)
            return {};
        if (!registry || !registry->sharedOwnerMatches(registry)) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-REGISTRY-OWNER";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(
                plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Device)]);
            return shareOriginal(core::makeUnique<WebGpuOriginalTextureReadbackDeviceStagingBudget>(
                ConstructionKey{}, std::move(plan), std::move(registry)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>&
            self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-DEVICE-STAGING-OWNER";
        if (const auto* id = Plan::validateOriginal(self->plan_))
            return id;
        return self->registry_ && self->registry_->sharedOwnerMatches(self->registry_)
                && self->capacity_->capacity() == self->plan_->budget().maxDeviceStagingBytes
            ? nullptr
            : "RHI-READBACK-DEVICE-STAGING-REGISTRY-OWNER";
    }

    static auto reserveOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>& self,
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const char*& diagnostic) -> std::shared_ptr<const Charge>
    {
        return reserveAdmitted(self, std::move(source), diagnostic, nullptr);
    }

    static auto reserveColdOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>& self,
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const rhi::GpuTexture& candidate,
        const char*& diagnostic) -> std::shared_ptr<const Charge>
    {
        return reserveAdmitted(self, std::move(source), diagnostic, &candidate);
    }

private:
    static auto reserveAdmitted(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>& self,
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const char*& diagnostic,
        const rhi::GpuTexture* coldCandidate) -> std::shared_ptr<const Charge>
    {
        diagnostic = validateOriginal(self);
        if (diagnostic)
            return {};
        if (!source || !source->hasQuotaBoundSharedOwner(source)) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-SOURCE-OWNER";
            return {};
        }
        if ((diagnostic = coldCandidate ? source->validateColdCandidateOriginal(*coldCandidate)
                                        : source->validate()))
            return {};
        if (!core::SharedOwnerProjection::sameOwner(self->registry_, source->registry())) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-REGISTRY-MISMATCH";
            return {};
        }
        const auto& lease = source->publicSourceLease();
        if (!lease || source->hasCompletedUpload()) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-LEASE-REQUIRED";
            return {};
        }
        const auto& request = lease->originalRequest();
        if ((diagnostic = rhi::TextureReadbackRequestDeclaration::validateOriginal(
                 request, source->layout())))
            return {};
        const auto bytes = source->layout()->inputOwner()->scalarFields().byteCapacity();
        const auto& b = self->plan_->budget();
        if (!bytes || bytes > b.maxStagingBytesPerRequest || bytes > b.maxResultBytesPerRequest
            || source->retainedNativeAllocationBytes() > b.maxSourceAllocationBytes
            || source->retainedSourceOwnerCount() + 1u > b.maxResourceOwners
            || source->layout()->mipCopies().size() > b.maxMipCopies
            || request->label().size() > b.maxLabelBytesPerRequest) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-REQUEST-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(
                self->plan_->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)]);
            auto charge =
                core::makeUnique<Charge>(ConstructionKey{}, self, std::move(source), bytes);
            if (!charge->charged()) {
                diagnostic = "RHI-READBACK-DEVICE-STAGING-CAPACITY";
                return {};
            }
            return Charge::shareOriginal(std::move(charge));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-ALLOCATION";
            return {};
        }
    }

public:
    static auto retainAllocationOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>& self,
        std::shared_ptr<void> allocation,
        std::shared_ptr<const Charge> charge,
        const char*& diagnostic) -> std::shared_ptr<void>
    {
        diagnostic = validateOriginal(self);
        if (diagnostic)
            return {};
        if (!allocation || !charge || !charge->hasQuotaBoundSharedOwner(charge)
            || !charge->charged()
            || !core::SharedOwnerProjection::sameOwner(self, charge->owner_)) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-ALLOCATION-OWNER";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(
                self->plan_->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Resource)]);
            return core::makeShared<AllocationLifetime>(
                ConstructionKey{}, std::move(allocation), std::move(charge));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-ALLOCATION";
            return {};
        }
    }

    const auto& originalPlan() const noexcept { return plan_; }

    std::uint64_t usedBytes() const noexcept { return capacity_->used(); }

    std::uint64_t peakBytes() const noexcept { return capacity_->peak(); }

    std::uint64_t capacityRejections() const noexcept { return capacity_->rejectionCount(); }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::shared_ptr<const Plan> plan_;
    const std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    const std::shared_ptr<core::AtomicCapacityBudget> capacity_;
};

// endregion Webgpu original texture readback device staging budget

// region Webgpu original texture readback destination

struct WebGpuOriginalTextureReadbackDestinationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSourceOwners = 0u, maxCallbacks = 0u;
    std::uint64_t maxRetainedSourceBytes = 0u, maxGpuBufferBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u, maxCpuResourceBytes = 0u, maxCpuResultBytes = 0u;
};

struct WebGpuCompletedTextureReadbackDestinationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCompletedGraphs = 0u;
    std::uint64_t maxRetainedContentBytes = 0u;
    WebGpuOriginalTextureReadbackDestinationBudget destination;
};

// OriginalTextureReadbackDestination/1. Private staging and unpublished result
// capacity only; creation evidence cannot issue read/copy/map/command authority.
class WebGpuOriginalTextureReadbackDestination final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackDestination;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    friend class WebGpuOriginalTextureReadbackPublication;
    using CreationDrainConsumer = core::runtime::CompletionDrainConsumer;

    struct Callback {
        std::shared_ptr<WebGpuOriginalTextureReadbackDestination> owner;
        std::uint32_t participant = 0u;
    };
    enum class SourceProfile { Ordinary, CompletedUpload };

public:
    static constexpr std::uint32_t callbackCount = WebGpuNativeErrorScopeBatch::count;
    // Normative WebGPU mapAsync size alignment; entire capacity at offset zero.
    static constexpr std::uint64_t mapSizeAlignment = 4u;
    enum class Status { Prepared, Pending, CreationReady, Rejected, DeviceLost };

    struct Lowering {
        bool validatorOnly = false, nativeCreationImplemented = false;
        const char* diagnostic = "RHI-READBACK-DESTINATION-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-READBACK-DESTINATION-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-READBACK-DESTINATION-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-READBACK-DESTINATION-EXECUTION-UNPROVEN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalTextureReadbackDestination(ConstructionKey,
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota)
        : source_(std::move(source)), resourceQuota_(std::move(resourceQuota))
    {}

    ~WebGpuOriginalTextureReadbackDestination()
    {
        native_.reset();
        nativeCreation_.reset();
        allocation_ = WebGpuRhiAllocation{};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (device_ && !webGpuNativeDeviceLost())
            wgpuDeviceRelease(device_);
#endif
    }

    static const char* validateBudget(
        const WebGpuOriginalTextureReadbackDestinationBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-DESTINATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-DESTINATION-OPT-IN";
        if (!b.maxSourceOwners || b.maxCallbacks < callbackCount || !b.maxRetainedSourceBytes
            || !b.maxGpuBufferBytes || !b.maxCpuMetadataBytes || !b.maxCpuResourceBytes
            || !b.maxCpuResultBytes)
            return "RHI-READBACK-DESTINATION-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackDestination> prepareOriginal(
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const WebGpuOriginalTextureReadbackDestinationBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        std::shared_ptr<core::CpuMemoryQuota> resultQuota,
        const char*& diagnostic)
    {
        return prepareAdmitted(std::move(source),
            b,
            std::move(metadataQuota),
            std::move(resourceQuota),
            std::move(resultQuota),
            SourceProfile::Ordinary,
            diagnostic);
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackDestination> prepareDeviceOriginal(
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const WebGpuOriginalTextureReadbackDestinationBudget& b,
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>& owner,
        const char*& diagnostic)
    {
        return prepareDeviceAdmitted(std::move(source), b, owner, diagnostic, nullptr);
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackDestination> prepareDeviceColdOriginal(
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const rhi::GpuTexture& candidate,
        const WebGpuOriginalTextureReadbackDestinationBudget& b,
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>& owner,
        const char*& diagnostic)
    {
        return prepareDeviceAdmitted(std::move(source), b, owner, diagnostic, &candidate);
    }

private:
    static std::shared_ptr<WebGpuOriginalTextureReadbackDestination> prepareDeviceAdmitted(
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const WebGpuOriginalTextureReadbackDestinationBudget& b,
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>& owner,
        const char*& diagnostic,
        const rhi::GpuTexture* coldCandidate)
    {
        diagnostic = WebGpuOriginalTextureReadbackDeviceStagingBudget::validateOriginal(owner);
        if (diagnostic)
            return {};
        const auto& plan = owner->originalPlan();
        const auto& publicBudget = plan->budget();
        if (b.contractVersion != publicBudget.contractVersion
            || b.prototypeOptIn != publicBudget.prototypeOptIn
            || b.maxSourceOwners != publicBudget.maxResourceOwners
            || b.maxCallbacks != publicBudget.maxCreationCallbacks
            || b.maxRetainedSourceBytes != publicBudget.maxSourceAllocationBytes
            || b.maxGpuBufferBytes != publicBudget.maxStagingBytesPerRequest
            || b.maxCpuMetadataBytes != publicBudget.maxCpuRequestBytes
            || b.maxCpuResourceBytes != publicBudget.maxCpuResourceBytes
            || b.maxCpuResultBytes != publicBudget.maxCpuResultBytes) {
            diagnostic = "RHI-READBACK-DEVICE-STAGING-LOWERING-MISMATCH";
            return {};
        }
        const auto& quotas = plan->quotas();
        return prepareAdmitted(std::move(source),
            b,
            quotas[static_cast<std::size_t>(
                rhi::TextureReadbackColdBudgetPlan::CpuDomain::Request)],
            quotas[static_cast<std::size_t>(
                rhi::TextureReadbackColdBudgetPlan::CpuDomain::Resource)],
            quotas[static_cast<std::size_t>(rhi::TextureReadbackColdBudgetPlan::CpuDomain::Result)],
            SourceProfile::Ordinary,
            diagnostic,
            owner,
            coldCandidate);
    }

public:
    static const char* validateBudget(
        const WebGpuCompletedTextureReadbackDestinationBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-COMPLETED-DESTINATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-COMPLETED-DESTINATION-OPT-IN";
        if (!b.maxCompletedGraphs || !b.maxRetainedContentBytes)
            return "RHI-READBACK-COMPLETED-DESTINATION-BUDGET";
        return validateBudget(b.destination);
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackDestination> prepareCompletedOriginal(
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const WebGpuCompletedTextureReadbackDestinationBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        std::shared_ptr<core::CpuMemoryQuota> resultQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!source || !source->hasQuotaBoundSharedOwner(source)) {
            diagnostic = "RHI-READBACK-DESTINATION-SOURCE-OWNER";
            return {};
        }
        if ((diagnostic = source->validate()))
            return {};
        if (!source->hasCompletedUpload()) {
            diagnostic = "RHI-READBACK-DESTINATION-SOURCE-PROFILE";
            return {};
        }
        if (source->retainedCompletedGraphCount() > b.maxCompletedGraphs
            || source->retainedContentBytes() > b.maxRetainedContentBytes) {
            diagnostic = "RHI-READBACK-COMPLETED-DESTINATION-CONTENT-CAPACITY";
            return {};
        }
        return prepareAdmitted(std::move(source),
            b.destination,
            std::move(metadataQuota),
            std::move(resourceQuota),
            std::move(resultQuota),
            SourceProfile::CompletedUpload,
            diagnostic);
    }

private:
    static std::shared_ptr<WebGpuOriginalTextureReadbackDestination> prepareAdmitted(
        std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source,
        const WebGpuOriginalTextureReadbackDestinationBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        std::shared_ptr<core::CpuMemoryQuota> resultQuota,
        SourceProfile profile,
        const char*& diagnostic,
        const std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget>&
            deviceBudget = {},
        const rhi::GpuTexture* coldCandidate = nullptr)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!metadataQuota || metadataQuota->limitBytes() != b.maxCpuMetadataBytes || !resourceQuota
            || resourceQuota->limitBytes() != b.maxCpuResourceBytes || !resultQuota
            || resultQuota->limitBytes() != b.maxCpuResultBytes) {
            diagnostic = "RHI-READBACK-DESTINATION-QUOTA";
            return {};
        }
        if (!source || !source->hasQuotaBoundSharedOwner(source)) {
            diagnostic = "RHI-READBACK-DESTINATION-SOURCE-OWNER";
            return {};
        }
        if ((diagnostic = coldCandidate ? source->validateColdCandidateOriginal(*coldCandidate)
                                        : source->validate()))
            return {};
        if (source->hasCompletedUpload() != (profile == SourceProfile::CompletedUpload)) {
            diagnostic = "RHI-READBACK-DESTINATION-SOURCE-PROFILE";
            return {};
        }
        const auto label = source->layout()->inputOwner()->label();
        if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label)) {
            diagnostic = "RHI-READBACK-DESTINATION-LABEL";
            return {};
        }
        rhi::ReadbackTextureDesc d;
        source->layout()->inputOwner()->scalarFields().writeScalarFields(d);
        if (source->retainedSourceOwnerCount() + 1u > b.maxSourceOwners
            || source->retainedNativeAllocationBytes() > b.maxRetainedSourceBytes
            || d.sizeBytes > b.maxGpuBufferBytes || d.sizeBytes > b.maxCpuResultBytes) {
            diagnostic = "RHI-READBACK-DESTINATION-CAPACITY";
            return {};
        }
        if (!d.sizeBytes || d.sizeBytes % mapSizeAlignment) {
            diagnostic = "RHI-READBACK-DESTINATION-MAP-ALIGNMENT";
            return {};
        }
        if (d.sizeBytes > std::numeric_limits<std::size_t>::max()) {
            diagnostic = "RHI-READBACK-DESTINATION-CPU-SIZE";
            return {};
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        diagnostic = "RHI-READBACK-DESTINATION-NATIVE-UNAVAILABLE";
        return {};
#else
        auto limits = WGPU_LIMITS_INIT;
        if (wgpuDeviceGetLimits(source->resource().texture->ownerDevice, &limits)
                != WGPUStatus_Success
            || !limits.maxBufferSize) {
            diagnostic = "RHI-READBACK-DESTINATION-NATIVE-LIMITS";
            return {};
        }
        if (d.sizeBytes > limits.maxBufferSize) {
            diagnostic = "RHI-READBACK-DESTINATION-NATIVE-LIMITS";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope metadata(metadataQuota);
            std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget::Charge>
                aggregate;
            if (deviceBudget) {
                aggregate = coldCandidate
                    ? WebGpuOriginalTextureReadbackDeviceStagingBudget::reserveColdOriginal(
                          deviceBudget, source, *coldCandidate, diagnostic)
                    : WebGpuOriginalTextureReadbackDeviceStagingBudget::reserveOriginal(
                          deviceBudget, source, diagnostic);
                if (!aggregate)
                    return {};
            }
            auto result = core::makeUnique<WebGpuOriginalTextureReadbackDestination>(
                ConstructionKey{}, source, resourceQuota);
            result->deviceBudget_ = deviceBudget;
            for (std::uint32_t i = 0u; i < callbackCount; ++i) {
                result->callbacks_[i] = core::makeUnique<Callback>();
                result->callbacks_[i]->participant = i;
            }
            result->resourceId_ = d.destinationReadbackBuffer;
            {
                core::CpuMemoryQuotaScope resource(resourceQuota);
                result->desc_.label = source->layout()->inputOwner()->label();
                result->desc_.sizeBytes = d.sizeBytes;
                result->desc_.usage = rhi::ResourceUsage::CopyDst;
                result->desc_.memory = rhi::MemoryUsage::GpuToCpu;
                result->desc_.initialState = rhi::ResourceState::CopyDst;
                result->allocator_ = core::makeUnique<WebGpuRhiAllocator>(
                    WebGpuRhiAllocatorDesc{{}, b.maxGpuBufferBytes, false});
                result->allocation_ =
                    result->allocator_->allocate({{}, d.sizeBytes, rhi::MemoryUsage::GpuToCpu});
            }
            if (!result->allocation_.success || !result->allocation_.releaseToken) {
                diagnostic = "RHI-READBACK-DESTINATION-GPU-BUDGET";
                return {};
            }
            if (deviceBudget) {
                auto lifetime =
                    WebGpuOriginalTextureReadbackDeviceStagingBudget::retainAllocationOriginal(
                        deviceBudget,
                        result->allocation_.releaseToken,
                        std::move(aggregate),
                        diagnostic);
                if (!lifetime)
                    return {};
                result->allocation_.releaseToken = std::move(lifetime);
            }
            result->nativeCreation_ = WebGpuNativeBufferCreation::prepareReadbackOriginal(
                result->desc_, source->resource().texture->ownerDevice, resourceQuota, diagnostic);
            if (!result->nativeCreation_)
                return {};
            {
                core::CpuMemoryQuotaScope payload(resultQuota);
                result->result_.resize(static_cast<std::size_t>(d.sizeBytes));
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-DESTINATION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-READBACK-DESTINATION-ALLOCATION";
            return {};
        }
#endif
    }

public:
    static const char* initialize(
        const std::shared_ptr<WebGpuOriginalTextureReadbackDestination>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-DESTINATION-OWNER";
        if (webGpuNativeDeviceLost())
            return "RHI-READBACK-DESTINATION-DEVICE-LOST";
        if (self->status_ != Status::Prepared)
            return "RHI-READBACK-DESTINATION-STATE";
        if (const auto* id = self->source_->validate())
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-DESTINATION-NATIVE-UNAVAILABLE";
#else
        self->device_ = emscripten_webgpu_get_device();
        if (!self->device_ || self->device_ != self->source_->resource().texture->ownerDevice
            || !self->nativeCreation_ || self->nativeCreation_->nativeDevice() != self->device_) {
            self->failure_ = "RHI-READBACK-DESTINATION-NATIVE-DEVICE";
            self->status_ = Status::Rejected;
            return self->failure_;
        }
        self->status_ = Status::Pending;
        self->creationIssued_ = true;
        WebGpuNativeErrorScopeBatch::push(self->device_);
        self->failure_ = WebGpuNativeBufferCreation::initialize(self->nativeCreation_);
        self->native_ = self->nativeCreation_->nativePayload();
        if (!self->failure_ && self->native_) {
            self->native_->allocationReleaseToken = self->allocation_.releaseToken;
            self->originalBuffer_ = self->native_->buffer;
            self->failure_ = self->validateNativeDeclaration();
        }
        if (self->failure_)
            self->status_ = Status::Rejected;
        WebGpuNativeErrorScopeBatch::pop(self->device_, self, self->callbacks_, 0u, &completeScope);
        return self->status_ == Status::Rejected ? self->failure_ : nullptr;
#endif
    }

    Status status() const noexcept
    {
        return webGpuNativeDeviceLost() ? Status::DeviceLost : status_;
    }

    const char* validate() const noexcept
    {
        switch (status()) {
        case Status::Prepared:
            return "RHI-READBACK-DESTINATION-UNINITIALIZED";
        case Status::Pending:
            return "RHI-READBACK-DESTINATION-PENDING";
        case Status::CreationReady:
            return validateNativeDeclaration();
        case Status::Rejected:
            return failure_ ? failure_ : "RHI-READBACK-DESTINATION-NATIVE-ERROR";
        case Status::DeviceLost:
            return "RHI-READBACK-DESTINATION-DEVICE-LOST";
        }
        return "RHI-READBACK-DESTINATION-STATE";
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-READBACK-DESTINATION-EXECUTION-UNPROVEN";
    }

    // Creation/identity evidence is independent of current copy/map access.
    const char* validateReadyDeclaration() const noexcept
    {
        switch (status()) {
        case Status::Prepared:
            return "RHI-READBACK-DESTINATION-UNINITIALIZED";
        case Status::Pending:
            return "RHI-READBACK-DESTINATION-PENDING";
        case Status::CreationReady:
            if (!evidence_.proven())
                return "RHI-READBACK-DESTINATION-CREATION-UNPROVEN";
            if (const auto* id = source_->validateReadyDeclaration())
                return id;
            return validateNativeIdentity();
        case Status::Rejected:
            return failure_ ? failure_ : "RHI-READBACK-DESTINATION-NATIVE-ERROR";
        case Status::DeviceLost:
            return "RHI-READBACK-DESTINATION-DEVICE-LOST";
        }
        return "RHI-READBACK-DESTINATION-STATE";
    }

    // Unmapped native buffer admission only; current L2 state/access ownership
    // is still the transaction's responsibility, never inferred from map state.
    const char* unmappedAccessDiagnostic() const noexcept
    {
        if (const auto* id = validateReadyDeclaration())
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-DESTINATION-NATIVE-UNAVAILABLE";
#else
        return WebGpuNativeBufferMapState::matches(native_->buffer, WGPUBufferMapState_Unmapped)
            ? nullptr
            : "RHI-READBACK-DESTINATION-NATIVE-LAYOUT";
#endif
    }

    const auto& source() const noexcept { return source_; }

    const auto& nativeResource() const noexcept { return native_; }

    const auto& descriptor() const noexcept { return desc_; }

    rhi::GpuResourceId resourceId() const noexcept { return resourceId_; }

    rhi::MemoryStats memoryStats() const noexcept { return allocator_->memoryStats(); }

    const auto& originalDeviceStagingBudget() const noexcept { return deviceBudget_; }

    bool creationCallbacksIssued() const noexcept { return creationIssued_; }

    bool creationScopeObserved(std::uint32_t participant) const noexcept
    {
        return evidence_.hasReceived(participant);
    }

    bool creationScopesObserved() const noexcept { return evidence_.allReceived(); }

    std::uint64_t unpublishedResultCapacityBytes() const noexcept { return result_.size(); }

    bool resultPublished() const noexcept { return resultPublication_.isComplete(); }

    core::SharedAllocationProjection::Record allocationRecord() const noexcept
    {
        return {allocation_.releaseToken, native_, allocation_.sizeBytes};
    }

private:
    friend class WebGpuOriginalTextureReadbackMapping;
    friend class WebGpuOriginalTextureReadbackResult;

    const char* validateNativeDeclaration() const noexcept
    {
        if (const auto* id = source_->validate())
            return id;
        if (const auto* id = validateNativeIdentity())
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-DESTINATION-NATIVE-UNAVAILABLE";
#else
        if (native_->recordedState().pending()
            || native_->recordedState().state() != rhi::ResourceState::CopyDst)
            return "RHI-READBACK-DESTINATION-IDENTITY-CHANGED";
        if (evidence_.proven()
            && !WebGpuNativeBufferMapState::matches(native_->buffer, WGPUBufferMapState_Unmapped))
            return "RHI-READBACK-DESTINATION-NATIVE-LAYOUT";
        return nullptr;
#endif
    }

    const char* validateNativeIdentity() const noexcept
    {
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-DESTINATION-NATIVE-UNAVAILABLE";
#else
        if (webGpuNativeDeviceLost())
            return "RHI-READBACK-DESTINATION-DEVICE-LOST";
        if (!native_ || !native_->sharedOwnerMatches(native_) || !native_->buffer
            || native_->buffer != originalBuffer_ || !device_ || native_->ownerDevice != device_
            || device_ != source_->resource().texture->ownerDevice
            || native_->sizeBytes != desc_.sizeBytes
            || native_->creationState() != desc_.initialState || !native_->recordedState().active()
            || !rhi::semantics::isKnownResourceState(native_->recordedState().state())
            || !core::SharedOwnerProjection::sameOwner(
                allocation_.releaseToken, native_->allocationReleaseToken)
            || !allocation_.success || allocation_.sizeBytes != desc_.sizeBytes
            || result_.size() != desc_.sizeBytes)
            return "RHI-READBACK-DESTINATION-IDENTITY-CHANGED";
        if (evidence_.proven()
            && (wgpuBufferGetSize(native_->buffer) != desc_.sizeBytes
                || wgpuBufferGetUsage(native_->buffer)
                    != (WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead)))
            return "RHI-READBACK-DESTINATION-NATIVE-LAYOUT";
        return nullptr;
#endif
    }

    void observe(std::uint32_t participant, bool proven) noexcept
    {
        const bool accepted = evidence_.observe(participant, proven);
        if (status() != Status::Pending)
            return;
        if (!accepted || !proven) {
            failure_ = "RHI-READBACK-DESTINATION-NATIVE-ERROR";
            status_ = Status::Rejected;
            return;
        }
        if (!evidence_.allReceived())
            return;
        failure_ = validateNativeDeclaration();
        status_ = !failure_ && evidence_.proven() ? Status::CreationReady : Status::Rejected;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void completeScope(WGPUPopErrorScopeStatus status,
        WGPUErrorType type,
        WGPUStringView,
        void* data,
        void*) noexcept
    {
        auto* original = static_cast<Callback*>(data);
        core::UniquePtr<Callback> callback(original,
            core::CpuAllocatedDeleter<Callback>{
                original, &core::destroyCpuAllocatedUnique<Callback>});
        if (callback && callback->owner) {
            callback->owner->observe(callback->participant,
                status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
            CreationDrainConsumer::consumeOnce(
                callback->owner->evidence_.allReceived(), callback->owner->creationDrainConsumer_);
        }
    }

    WGPUDevice device_ = nullptr;
    WGPUBuffer originalBuffer_ = nullptr;
#endif
    std::shared_ptr<CreationDrainConsumer> creationDrainConsumer_;
    std::shared_ptr<const WebGpuOriginalTextureReadbackDeviceStagingBudget> deviceBudget_;
    bool creationIssued_ = false;
    const std::shared_ptr<const WebGpuOriginalTextureReadbackSource> source_;
    const std::shared_ptr<core::CpuMemoryQuota> resourceQuota_;
    rhi::GpuResourceId resourceId_;
    rhi::BufferDesc desc_;
    core::UniquePtr<WebGpuRhiAllocator> allocator_;
    WebGpuRhiAllocation allocation_;
    std::shared_ptr<WebGpuNativeBufferPayload> native_;
    std::shared_ptr<WebGpuNativeBufferCreation> nativeCreation_;
    mutable core::runtime::CompletionSignal resultPublication_;
    mutable core::pmr::Vector<unsigned char> result_{&core::defaultCpuMemoryResource()};
    std::array<core::UniquePtr<Callback>, callbackCount> callbacks_;
    core::runtime::CompletionEvidenceBatch evidence_{callbackCount};
    Status status_ = Status::Prepared;
    const char* failure_ = nullptr;
};

// endregion Webgpu original texture readback destination

// region Webgpu original texture readback pair projection

struct WebGpuOriginalTextureReadbackPairBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalPairs = 0u, maxCompletedGraphs = 0u, maxProjectedOwners = 0u,
                  maxMipCopies = 0u, maxAllocationRecords = 0u;
    std::uint64_t maxRetainedNativeBytes = 0u, maxRetainedContentBytes = 0u,
                  maxUnpublishedResultBytes = 0u, maxCpuMetadataBytes = 0u;
};

// OriginalTextureReadbackPairProjection/1. The destination already owns the
// exact pair and immutable physical mip facts. Reuse that closure; no second
// geometry, mip array, native object, pending state or command/map proof.
class WebGpuOriginalTextureReadbackPairProjection final
    : public core::QuotaBoundSharedObjectIdentity {
    using Destination = WebGpuOriginalTextureReadbackDestination;

    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackPairProjection;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Mip = rhi::TextureReadbackLayout::Mip;

    struct Lowering {
        bool validatorOnly = false, nativeFactBindingImplemented = false;
        const char* diagnostic = "RHI-READBACK-PAIR-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-READBACK-PAIR-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-READBACK-PAIR-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-READBACK-PAIR-EXECUTION-UNPROVEN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalTextureReadbackPairProjection(ConstructionKey,
        std::shared_ptr<const Destination> destination,
        std::uint32_t owners,
        std::uint64_t nativeBytes)
        : destination_(std::move(destination)), owners_(owners), nativeBytes_(nativeBytes)
    {}

    static const char* validateBudget(const WebGpuOriginalTextureReadbackPairBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-PAIR-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-PAIR-OPT-IN";
        if (!b.maxOriginalPairs || !b.maxCompletedGraphs || !b.maxProjectedOwners || !b.maxMipCopies
            || !b.maxAllocationRecords || !b.maxRetainedNativeBytes || !b.maxRetainedContentBytes
            || !b.maxUnpublishedResultBytes || !b.maxCpuMetadataBytes)
            return "RHI-READBACK-PAIR-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection> prepareOriginal(
        std::shared_ptr<const Destination> destination,
        const WebGpuOriginalTextureReadbackPairBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-PAIR-QUOTA";
            return {};
        }
        if ((diagnostic = validatePair(destination)))
            return {};
        const auto& source = *destination->source();
        // Native ledger totals have independent budgets at each factory. Sum
        // their original charges with the existing core overflow guard.
        const auto bufferBytes = destination->memoryStats().allocatedBytes;
        if (!core::MemoryBudgetLedger::canAdd(
                source.retainedNativeAllocationBytes(), bufferBytes)) {
            diagnostic = "RHI-READBACK-PAIR-BYTE-OVERFLOW";
            return {};
        }
        const auto bytes = source.retainedNativeAllocationBytes() + bufferBytes;
        const auto owners = source.retainedSourceOwnerCount() + 4u;
        if (source.retainedCompletedGraphCount() > b.maxCompletedGraphs
            || owners > b.maxProjectedOwners || source.layout()->mipCopies().size() > b.maxMipCopies
            || bytes > b.maxRetainedNativeBytes
            || source.retainedContentBytes() > b.maxRetainedContentBytes
            || destination->unpublishedResultCapacityBytes() > b.maxUnpublishedResultBytes) {
            diagnostic = "RHI-READBACK-PAIR-CAPACITY";
            return {};
        }
        const auto sourceRecords = source.retainedNativeAllocationRecordCount();
        if (sourceRecords >= b.maxAllocationRecords) {
            diagnostic = "RHI-READBACK-PAIR-ALLOCATION-COUNT";
            return {};
        }
        const auto inventory = core::SharedAllocationProjection::inspect(sourceRecords + 1u,
            b.maxAllocationRecords,
            b.maxRetainedNativeBytes,
            [&](std::size_t index) {
                return index < sourceRecords ? source.retainedNativeAllocationRecordAt(index)
                                             : destination->allocationRecord();
            });
        if (inventory.status != core::SharedAllocationProjection::Status::Complete) {
            diagnostic = "RHI-READBACK-PAIR-ALLOCATION-INVENTORY";
            return {};
        }
        if (inventory.bytes != bytes) {
            diagnostic = "RHI-READBACK-PAIR-ALLOCATION-TOPOLOGY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<WebGpuOriginalTextureReadbackPairProjection>(
                ConstructionKey{}, std::move(destination), owners, bytes));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-PAIR-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept { return validatePair(destination_); }

    const char* validateReadyDeclaration() const noexcept
    {
        if (!destination_ || !destination_->hasQuotaBoundSharedOwner(destination_))
            return "RHI-READBACK-PAIR-DESTINATION-OWNER";
        if (const auto* id = destination_->validateReadyDeclaration())
            return id;
        return validateNativePairFacts(*destination_);
    }

    const auto& destination() const noexcept { return destination_; }

    const auto& layout() const noexcept { return destination_->source()->layout(); }

    std::uint32_t projectedOwnerCount() const noexcept { return owners_; }

    std::uint64_t retainedNativeAllocationBytes() const noexcept { return nativeBytes_; }

    std::size_t allocationRecordCount() const noexcept
    {
        return destination_->source()->retainedNativeAllocationRecordCount() + 1u;
    }

    core::SharedAllocationProjection::Record allocationRecordAt(std::size_t index) const noexcept
    {
        const auto& source = *destination_->source();
        const auto count = source.retainedNativeAllocationRecordCount();
        if (index < count)
            return source.retainedNativeAllocationRecordAt(index);
        return index == count ? destination_->allocationRecord()
                              : core::SharedAllocationProjection::Record{};
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-READBACK-PAIR-EXECUTION-UNPROVEN";
    }

    static const char* mapMipOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection>& self,
        const std::shared_ptr<const rhi::TextureReadbackInputSnapshot>& input,
        std::size_t index,
        const Mip& observed,
        void* expectedDevice,
        Mip& output) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-PAIR-OWNER";
        if (const auto* id = self->validate())
            return id;
        if (!input || !input->hasQuotaBoundSharedOwner(input)
            || !core::SharedOwnerProjection::sameOwner(input, self->layout()->inputOwner()))
            return "RHI-READBACK-PAIR-INPUT-OWNER";
        if (const auto* id =
                rhi::TextureReadbackLayout::validateOriginalMip(self->layout(), index, observed))
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        (void)expectedDevice;
        (void)output;
        return "RHI-READBACK-PAIR-NATIVE-UNAVAILABLE";
#else
        if (!expectedDevice || self->destination_->nativeResource()->ownerDevice != expectedDevice
            || self->destination_->source()->resource().texture->ownerDevice != expectedDevice)
            return "RHI-READBACK-PAIR-DEVICE";
        output = self->layout()->mipCopies()[index];
        return nullptr;
#endif
    }

private:
    static const char* validatePair(const std::shared_ptr<const Destination>& destination) noexcept
    {
        if (!destination || !destination->hasQuotaBoundSharedOwner(destination))
            return "RHI-READBACK-PAIR-DESTINATION-OWNER";
        if (const auto* id = destination->validate())
            return id;
        return validateNativePairFacts(*destination);
    }

    static const char* validateNativePairFacts(const Destination& destination) noexcept
    {
        const auto& source = *destination.source();
        if (const auto* id = source.validateReadbackContentProfile())
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-PAIR-NATIVE-UNAVAILABLE";
#elif !defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        return "RHI-READBACK-PAIR-NATIVE-API-UNAVAILABLE";
#else
        const auto& texture = *source.resource().texture;
        const auto& buffer = *destination.nativeResource();
        const auto dimension = source.layout()->sourceLayout().extent.depth > 1u
            ? WGPUTextureDimension_3D
            : WGPUTextureDimension_2D;
        if (!texture.texture || !buffer.buffer || !texture.ownerDevice
            || texture.ownerDevice != buffer.ownerDevice || texture.dimension != dimension
            || texture.rhiFormat != source.layout()->sourceLayout().format
            || buffer.sizeBytes != destination.unpublishedResultCapacityBytes())
            return "RHI-READBACK-PAIR-NATIVE-FACTS";
        // The original layout factory proved the complete padded footprint and
        // original physical geometry. Reuse native texel capability diagnostics.
        for (const auto& mip : source.layout()->mipCopies())
            if (const auto* id = WebGpuNativeTransferCopyLowering::textureDiagnostic(
                    texture, mip.range, mip.extent))
                return id;
        return nullptr;
#endif
    }

    const std::shared_ptr<const Destination> destination_;
    const std::uint32_t owners_;
    const std::uint64_t nativeBytes_;
};
} // namespace arp::rhi_webgpu

// endregion Webgpu original texture readback pair projection
