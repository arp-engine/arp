#pragma once
#include "rhi/backends/webgpu/webgpu_owned_texture_readback_recording_preparation.h"
#include "rhi/backends/webgpu/webgpu_texture_readback_cold_budget_lowering.h"
#include "rhi/frontend/transfer_readback_owned_preparation.h"

namespace arp::test {
class WebGpuOwnedReadbackContextFixture;
class WebGpuOwnedReadbackAdvancementFixture;
} // namespace arp::test

namespace arp::rhi_webgpu {
// OriginalReadbackAtomicCreationPreparation/1. Private creation prerequisite;
// selected explicit Prototype admission consumes original cold/hot receipts.
// This object cannot issue command or completion proof.
class WebGpuOwnedTextureReadbackPreparation final : public rhi::TextureReadbackOwnedPreparation {
    class ConstructionKey {
        friend class WebGpuOwnedTextureReadbackPreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Plan = rhi::TextureReadbackColdBudgetPlan;
    using Lease = rhi::TextureReadbackSourceLease;
    using Source = WebGpuOriginalTextureReadbackSource;
    using Destination = WebGpuOriginalTextureReadbackDestination;
    using Staging = WebGpuOriginalTextureReadbackDeviceStagingBudget;
    using Publication = WebGpuOriginalTextureReadbackPublication;
    using Contract = rhi::TextureReadbackOwnedPreparationContract;
    using ColdRecording = WebGpuOwnedTextureReadbackRecordingPreparation;
    using Recording = WebGpuOrdinaryRecording;
    using Submission = WebGpuOrdinarySubmission;
    using Mapping = WebGpuOriginalTextureReadbackMapping;
    using Numerical = WebGpuOriginalTextureReadbackNumericalResult;

public:
    WebGpuOwnedTextureReadbackPreparation(ConstructionKey,
        std::shared_ptr<const Plan> plan,
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request,
        std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statistics,
        std::shared_ptr<Destination> destination,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner> completion,
        Publication::Reservation reservation,
        const rhi::TextureReadbackOwnedPreparationIntent& intent)
        : plan_(std::move(plan)), request_(std::move(request)), statistics_(std::move(statistics)),
          destination_(std::move(destination)), completion_(std::move(completion)),
          reservation_(std::move(reservation)), barrier_(intent.sourceBarrier),
          passLabel_(intent.passLabel, &core::defaultCpuMemoryResource())
    {}

    ~WebGpuOwnedTextureReadbackPreparation() override { cancel(); }

    static std::shared_ptr<WebGpuOwnedTextureReadbackPreparation> prepareOriginal(
        const std::shared_ptr<const Plan>& plan,
        const std::shared_ptr<WebGpuRhiResourceRegistry>& registry,
        const std::shared_ptr<const Staging>& staging,
        const std::shared_ptr<Publication>& publication,
        const std::shared_ptr<WebGpuOrdinaryCompletionOwner>& completion,
        core::UniquePtr<rhi::GpuTexture>& candidate,
        std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request,
        std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statistics,
        const rhi::TextureReadbackOwnedPreparationIntent& intent,
        const char*& diagnostic)
    {
        diagnostic = Contract::validateOriginal(plan, candidate, request, statistics, intent);
        if (diagnostic)
            return {};
        if ((diagnostic = Staging::validateOriginal(staging)))
            return {};
        if (!core::SharedOwnerProjection::sameOwner(staging->originalPlan(), plan) || !completion
            || !completion->sharedOwnerMatches(completion) || !completion->retained.accepting()
            || !core::SharedOwnerProjection::sameOwner(completion->originalReadbackPlan(), plan)
            || completion->originalLossDelivered()) {
            diagnostic = "RHI-READBACK-OWNED-PREPARATION-CONTEXT";
            return {};
        }
        WebGpuTextureReadbackColdBudgetLowering::Recipe recipe;
        if ((diagnostic = WebGpuTextureReadbackColdBudgetLowering::lowerOriginal(plan, recipe)))
            return {};
        const auto& quota = plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)];
        auto snapshot = WebGpuOwnedTextureReadbackSnapshot::prepareOriginal(
            plan, request, statistics, intent.content.mode, diagnostic);
        if (!snapshot)
            return {};
        auto reservation =
            Publication::reserveRequestOriginal(publication, request, statistics, diagnostic);
        if (!reservation)
            return {};
        if ((diagnostic = reservation->bindOwnedSnapshotOriginalCold(std::move(snapshot))))
            return {};
        auto lease =
            Lease::prepareColdOriginal(candidate, request, recipe.sourceLease, quota, diagnostic);
        if (!lease)
            return {};
        auto source = Source::prepareColdLeasedSnapshotOriginal(request->originalLayout(),
            registry,
            lease,
            *candidate,
            intent.content,
            recipe.source,
            quota,
            diagnostic);
        if (!source)
            return {};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (completion->nativeDevice() != source->resource().texture->ownerDevice) {
            diagnostic = "RHI-READBACK-OWNED-PREPARATION-NATIVE-DEVICE";
            return {};
        }
#endif
        if (intent.sourceBarrier.before != source->stateObservation().value) {
            diagnostic = "RHI-READBACK-OWNED-PREPARATION-BARRIER-STATE";
            return {};
        }
        auto destination = Destination::prepareDeviceColdOriginal(
            source, *candidate, recipe.destination, staging, diagnostic);
        if (!destination)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = shareOriginal(
                core::makeUnique<WebGpuOwnedTextureReadbackPreparation>(ConstructionKey{},
                    plan,
                    request,
                    statistics,
                    destination,
                    completion,
                    std::move(*reservation),
                    intent));
            if ((diagnostic = result->reservation_.bindColdCandidateDestinationOriginal(
                     destination, *candidate)))
                return {};
            // Every fallible allocation and budget charge is now fixed. Recheck
            // exact candidate/current state immediately before final adoption.
            if ((diagnostic = source->validateColdCandidateOriginal(*candidate)))
                return {};
            if ((diagnostic = Lease::adoptOriginal(lease, candidate)))
                return {};
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-OWNED-PREPARATION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-READBACK-OWNED-PREPARATION-ALLOCATION";
            return {};
        }
    }

    const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>&
    originalRequest() const noexcept override
    {
        return request_;
    }

    Phase phase() const noexcept override
    {
        if (canceled_ || reservation_.shutdownRequestedOriginal())
            return callbacksOutstanding() || quarantinedSubmission() ? Phase::CanceledDraining
                                                                     : Phase::Retired;
        if (coldRecording_) {
            const auto& mapping = coldRecording_->mapping();
            switch (mapping->status()) {
            case Mapping::Status::Published:
                return coldRecording_->numerical()->outcome() == Numerical::Outcome::Completed
                    ? Phase::Published
                    : Phase::Rejected;
            case Mapping::Status::Rejected:
                return Phase::Rejected;
            case Mapping::Status::DeviceLost:
                return Phase::DeviceLost;
            case Mapping::Status::Pending:
            case Mapping::Status::AwaitingScopes:
                return Phase::MappingPending;
            case Mapping::Status::Prepared:
                break;
            }
        }
        if (webGpuNativeDeviceLost())
            return Phase::DeviceLost;
        if (terminalFailure_)
            return Phase::Rejected;
        if (submission_)
            switch (submission_->status()) {
            case Submission::Status::Ready:
                return Phase::SubmissionPrepared;
            case Submission::Status::Pending:
                return Phase::SubmissionPending;
            case Submission::Status::Completed:
                return Phase::SubmissionCompleted;
            case Submission::Status::Unproven:
                return Phase::Rejected;
            case Submission::Status::DeviceLost:
                return Phase::DeviceLost;
            }
        if (coldRecording_)
            switch (coldRecording_->recording()->status()) {
            case Recording::Status::Prepared:
                return Phase::RecordingPrepared;
            case Recording::Status::PreparationPending:
            case Recording::Status::RecordingPending:
                return Phase::RecordingPending;
            case Recording::Status::Recorded:
                return Phase::RecordingReady;
            case Recording::Status::Rejected:
            case Recording::Status::NativeInvocationUncertain:
                return Phase::Rejected;
            case Recording::Status::DeviceLost:
                return Phase::DeviceLost;
            }
        switch (destination_->status()) {
        case Destination::Status::Prepared:
            return Phase::CreationPrepared;
        case Destination::Status::Pending:
            return Phase::CreationPending;
        case Destination::Status::CreationReady:
            return Phase::CreationReady;
        case Destination::Status::Rejected:
            return Phase::Rejected;
        case Destination::Status::DeviceLost:
            return Phase::DeviceLost;
        }
        return Phase::Rejected;
    }

    const char* diagnostic() const noexcept override
    {
        if (failure_)
            return failure_;
        if (phase() != Phase::Rejected && phase() != Phase::DeviceLost)
            return nullptr;
        if (coldRecording_) {
            if (const auto* id = coldRecording_->mapping()->diagnostic())
                return id;
            if (const auto* id = coldRecording_->numerical()->diagnostic())
                return id;
        }
        if (submission_)
            if (const auto* id = submission_->diagnostic())
                return id;
        if (coldRecording_)
            if (const auto* id = coldRecording_->recording()->diagnostic())
                return id;
        return destination_->validate();
    }

    const char* initializeCreation() noexcept override
    {
        if (const auto* id =
                Contract::validateAction(phase(), Contract::Action::InitializeCreation))
            return id;
        return observeHotFailure(reservation_.initializeBoundDestinationOriginal());
    }

    const char* prepareNextCold() override
    {
        const auto current = phase();
        if (current == Phase::CreationReady) {
            const char* id = nullptr;
            auto cold = ColdRecording::prepareOriginal(
                plan_, destination_, completion_, statistics_, barrier_, passLabel_, id);
            if (!cold) {
                failure_ = id;
                return id;
            }
            id = reservation_.bindRecordingAndMappingOriginalCold(
                cold->recording(), cold->mapping(), cold->numerical());
            if (id) {
                failure_ = id;
                return id;
            }
            coldRecording_ = std::move(cold);
            failure_ = nullptr;
            return nullptr;
        }
        if (current == Phase::RecordingReady) {
            WebGpuTextureReadbackColdBudgetLowering::Recipe recipe;
            if (const auto* id =
                    WebGpuTextureReadbackColdBudgetLowering::lowerOriginal(plan_, recipe)) {
                failure_ = id;
                return id;
            }
            const char* id = nullptr;
            auto submission = Submission::prepare(coldRecording_->recording(),
                recipe.submission,
                plan_->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)],
                id);
            if (!submission) {
                failure_ = id;
                return id;
            }
            if ((id = reservation_.bindSubmissionOriginal(submission))) {
                failure_ = id;
                return id;
            }
            submission_ = std::move(submission);
            failure_ = nullptr;
            return nullptr;
        }
        return "RHI-READBACK-OWNED-ACTION-PHASE";
    }

    const char* record() noexcept override
    {
        if (const auto* id = Contract::validateAction(phase(), Contract::Action::Record))
            return id;
        return observeHotFailure(reservation_.recordOriginal());
    }

    const char* submit() noexcept override
    {
        if (const auto* id = Contract::validateAction(phase(), Contract::Action::Submit))
            return id;
        return observeHotFailure(reservation_.submitOriginal());
    }

    const char* startMapping() noexcept override
    {
        if (const auto* id = Contract::validateAction(phase(), Contract::Action::StartMapping))
            return id;
        return observeHotFailure(reservation_.startOwnedOriginal());
    }

    bool cancel() noexcept override
    {
        if (canceled_)
            return false;
        canceled_ = true;
        return reservation_.cancel();
    }

private:
    friend class arp::test::WebGpuOwnedReadbackContextFixture;
    friend class arp::test::WebGpuOwnedReadbackAdvancementFixture;

    const char* observeHotFailure(const char* id) noexcept
    {
        if (id) {
            failure_ = id;
            terminalFailure_ = true;
        }
        return id;
    }

    bool callbacksOutstanding() const noexcept
    {
        if (destination_->creationCallbacksIssued() && !destination_->creationScopesObserved())
            return true;
        if (coldRecording_
            && ((coldRecording_->recording()->readbackRecordingCallbacksIssued()
                    && !coldRecording_->recording()->readbackRecordingCallbacksDrained())
                || (coldRecording_->mapping()->callbacksIssued()
                    && !coldRecording_->mapping()->callbacksDrained())))
            return true;
        return submission_ && submission_->callbacksIssued() && !submission_->callbacksDrained();
    }

    bool quarantinedSubmission() const noexcept
    {
        return submission_ && submission_->status() == Submission::Status::Unproven
            && !completion_->originalLossDelivered();
    }

    const std::shared_ptr<const Plan> plan_;
    const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration> request_;
    const std::shared_ptr<const rhi::TextureReadbackStatisticsPlan> statistics_;
    const std::shared_ptr<Destination> destination_;
    const std::shared_ptr<WebGpuOrdinaryCompletionOwner> completion_;
    Publication::Reservation reservation_;
    const rhi::ResourceBarrierDesc barrier_;
    const core::pmr::String passLabel_;
    std::shared_ptr<const ColdRecording> coldRecording_;
    std::shared_ptr<Submission> submission_;
    const char* failure_ = nullptr;
    bool canceled_ = false, terminalFailure_ = false;
};
} // namespace arp::rhi_webgpu
