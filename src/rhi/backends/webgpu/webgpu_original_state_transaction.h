#pragma once
#include "rhi/backends/webgpu/webgpu_original_transfer_state_validation.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalStateTransactionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxStateControls = 0u, maxTransitions = 0u, maxDebugDepth = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

struct WebGpuOriginalReadbackStateTransactionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalPairs = 0u, maxMipMappings = 0u, maxPrivateAccessClaims = 0u;
    std::uint64_t maxUnpublishedResultBytes = 0u;
    WebGpuOriginalStateTransactionBudget states;
};

// OriginalDependencyStateTransaction/1: exact CPU mutation ownership only.
// It cannot manufacture a native command, barrier or recorded proof.
class WebGpuOriginalStateTransaction final : public core::QuotaBoundSharedObjectIdentity {
    using Control = rhi::common::RecordedWholeResourceStateView;
    using States = WebGpuOriginalResourceStates;
    enum class Profile { Transfer, Readback };

    class ConstructionKey {
        friend class WebGpuOriginalStateTransaction;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Entry {
        const States::State* original = nullptr;
        rhi::ResourceState final = rhi::ResourceState::Undefined;
        bool changed = false;
        std::optional<Control::PendingObservation> pending;
        std::optional<Control::Observation> committed;
    };

public:
    enum class Status { Prepared, Pending, Committed, Abandoned, Invalidated };

    WebGpuOriginalStateTransaction(ConstructionKey,
        std::shared_ptr<const States> states,
        const WebGpuOriginalStateTransactionBudget& budget,
        Profile profile)
        : states_(std::move(states)), budget_(budget), profile_(profile)
    {
        entries_.reserve(budget.maxStateControls);
    }

    ~WebGpuOriginalStateTransaction() { abandon(); }

    static const char* validateBudget(const WebGpuOriginalStateTransactionBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-STATE-TRANSACTION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-STATE-TRANSACTION-OPT-IN";
        if (!budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-STATE-TRANSACTION-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalStateTransaction> prepareOriginal(
        std::shared_ptr<const States> states,
        const WebGpuOriginalStateTransactionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareAdmitted(
            std::move(states), budget, std::move(quota), Profile::Transfer, diagnostic);
    }

    static const char* validateBudget(
        const WebGpuOriginalReadbackStateTransactionBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-READBACK-TRANSACTION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-READBACK-TRANSACTION-OPT-IN";
        if (!budget.maxOriginalPairs || !budget.maxMipMappings || !budget.maxPrivateAccessClaims
            || !budget.maxUnpublishedResultBytes)
            return "RHI-WEBGPU-READBACK-TRANSACTION-BUDGET";
        return validateBudget(budget.states);
    }

    static std::shared_ptr<WebGpuOriginalStateTransaction> prepareReadbackOriginal(
        std::shared_ptr<const States> states,
        const WebGpuOriginalReadbackStateTransactionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if ((diagnostic = validateInitialReadbackStream(states)))
            return {};
        const auto& pair =
            states->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
        if (pair->layout()->mipCopies().size() > budget.maxMipMappings
            || pair->destination()->unpublishedResultCapacityBytes()
                > budget.maxUnpublishedResultBytes) {
            diagnostic = "RHI-WEBGPU-READBACK-TRANSACTION-CAPACITY";
            return {};
        }
        return prepareAdmitted(
            std::move(states), budget.states, std::move(quota), Profile::Readback, diagnostic);
    }

private:
    static std::shared_ptr<WebGpuOriginalStateTransaction> prepareAdmitted(
        std::shared_ptr<const States> states,
        const WebGpuOriginalStateTransactionBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        Profile profile,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-QUOTA";
            return {};
        }
        if (!states || !states->hasQuotaBoundSharedOwner(states)) {
            diagnostic = "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
            return {};
        }
        if ((diagnostic = states->validate()))
            return {};
        const auto& inputs = *states->inputs();
        const auto& graph = *inputs.scalars()->usage()->projection();
        if (profile == Profile::Transfer
            && (!graph.transfer() || graph.resources() || !graph.pipelines().empty()
                || !graph.physicalGroups().empty()
                || states->sourceReferences() != graph.transfer()->resources().size()
                || states->states().size() != graph.transfer()->resources().size())) {
            diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-SOURCE-COVERAGE";
            return {};
        }
        if (states->states().size() > budget.maxStateControls) {
            diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-CONTROL-CAPACITY";
            return {};
        }
        std::size_t transitions = 0u;
        for (const auto& d : inputs.descriptors()) {
            const auto* dependency = std::get_if<rhi::ResourceDependencyArguments>(&d.arguments);
            if (dependency && isTransition(dependency->kind())
                && ++transitions > budget.maxTransitions) {
                diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-TRANSITION-CAPACITY";
                return {};
            }
        }
        if ((diagnostic =
                    WebGpuOriginalTransferStateValidation::validate(states, budget.maxDebugDepth)
                        .diagnostic))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuOriginalStateTransaction>(
                ConstructionKey{}, std::move(states), budget, profile);
            const auto& original = *result->states_;
            for (const auto& state : original.states()) {
                rhi::ResourceState final;
                if ((diagnostic = WebGpuOriginalTransferStateValidation::stateBefore(original,
                         state.source.resource,
                         inputs.scalars()->usage()->commands().size(),
                         final)))
                    return {};
                bool changed = false;
                if (profile == Profile::Readback
                    && state.source.resource
                        == result->originalReadback()->destination()->resourceId())
                    changed = true;
                for (const auto& d : inputs.descriptors()) {
                    const auto* dependency =
                        std::get_if<rhi::ResourceDependencyArguments>(&d.arguments);
                    rhi::ResourceBarrierDesc barrier;
                    if (dependency && isTransition(dependency->kind())
                        && dependency->write(dependency->kind(), barrier)
                        && barrier.resource == state.source.resource) {
                        changed = true;
                        break;
                    }
                }
                result->entries_.push_back({&state, final, changed, {}, {}});
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-ALLOCATION";
            return {};
        }
    }

public:
    const char* validate() const noexcept
    {
        if (!states_ || !states_->hasQuotaBoundSharedOwner(states_))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        if (profile_ == Profile::Readback) {
            if (status_ == Status::Abandoned)
                return "RHI-WEBGPU-STATE-TRANSACTION-ABANDONED";
            if (status_ == Status::Invalidated)
                return "RHI-WEBGPU-STATE-TRANSACTION-INVALIDATED";
            if (const auto* id = originalReadback()->validateReadyDeclaration())
                return id;
            if (const auto* id = originalReadback()->destination()->unmappedAccessDiagnostic())
                return id;
        } else if (const auto* id = WebGpuOriginalTransferStateValidation::validateInitialSequence(
                       states_, budget_.maxDebugDepth)
                       .diagnostic)
            return id;
        switch (status_) {
        case Status::Prepared:
            return states_->validate();
        case Status::Abandoned:
            return "RHI-WEBGPU-STATE-TRANSACTION-ABANDONED";
        case Status::Invalidated:
            return "RHI-WEBGPU-STATE-TRANSACTION-INVALIDATED";
        case Status::Pending:
        case Status::Committed:
            break;
        }
        return validateOwnedObservations();
    }

    // Committed CPU declarations never grant native completion or map access.
    static const char* validateCommittedReadbackDeclarationOriginal(
        const std::shared_ptr<const WebGpuOriginalStateTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-STATE-TRANSACTION-OWNER";
        if (!self->isReadbackProfile())
            return "RHI-WEBGPU-READBACK-TRANSACTION-PROFILE";
        if (self->status_ != Status::Committed)
            return "RHI-WEBGPU-READBACK-DECLARATION-COMMITTED-STATUS";
        if (!self->states_ || !self->states_->hasQuotaBoundSharedOwner(self->states_))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        if (const auto* id = self->originalReadback()->validateReadyDeclaration())
            return id;
        return self->validateOwnedObservations();
    }

    static const char* apply(const std::shared_ptr<WebGpuOriginalStateTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-STATE-TRANSACTION-OWNER";
        if (self->status_ != Status::Prepared)
            return "RHI-WEBGPU-STATE-TRANSACTION-STATUS";
        if (const auto* id = self->validate())
            return id;
        for (const auto& d : self->states_->inputs()->descriptors()) {
            const auto* dependency = std::get_if<rhi::ResourceDependencyArguments>(&d.arguments);
            if (!dependency || !isTransition(dependency->kind()))
                continue;
            rhi::ResourceBarrierDesc barrier;
            if (!dependency->write(dependency->kind(), barrier)) {
                self->abandon();
                return "RHI-WEBGPU-ORIGINAL-STATE-DESCRIPTOR";
            }
            const auto* source = self->transitionResource(barrier.resource);
            if (!source
                || (source->buffer ? source->buffer->recordedState().state()
                                   : source->texture->recordedState().state())
                    != barrier.before) {
                self->abandon();
                return "RHI-STATE-SEQUENCE-BEFORE-MISMATCH";
            }
            const auto* id = source->buffer
                ? source->buffer->recordState(barrier.after, &self->journal_)
                : source->texture->recordState(barrier.after, &self->journal_);
            if (id) {
                self->abandon();
                return id;
            }
        }
        if (self->profile_ == Profile::Readback) {
            const auto* id = self->originalReadback()->destination()->nativeResource()->recordState(
                rhi::ResourceState::CopyDst, &self->journal_);
            if (id) {
                self->abandon();
                return id;
            }
        }
        for (auto& entry : self->entries_)
            if (entry.changed) {
                entry.pending = entry.original->source.control.observePending(self->journal_);
                if (!entry.pending || entry.pending->value() != entry.final) {
                    self->abandon();
                    return "RHI-WEBGPU-STATE-TRANSACTION-PENDING-MISMATCH";
                }
            }
        self->status_ = Status::Pending;
        if (const auto* id = self->validate()) {
            self->abandon();
            return id;
        }
        return nullptr;
    }

    static const char* commit(const std::shared_ptr<WebGpuOriginalStateTransaction>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-STATE-TRANSACTION-OWNER";
        if (self->status_ != Status::Pending)
            return "RHI-WEBGPU-STATE-TRANSACTION-STATUS";
        if (const auto* id = self->validate())
            return id;
        // External serialization: committing intrusive records cannot invoke
        // callers or change the pending value/revision that was just verified.
        self->journal_.commit();
        for (auto& entry : self->entries_)
            if (entry.changed)
                entry.committed =
                    Control::Observation{entry.pending->value(), entry.pending->revision()};
        self->status_ = Status::Committed;
        if (const auto* id = self->validate()) {
            self->invalidate();
            return id;
        }
        return nullptr; // CPU metadata commit only, never GPU proof.
    }

    void abandon() noexcept
    {
        if (status_ == Status::Committed || status_ == Status::Invalidated
            || status_ == Status::Abandoned)
            return;
        if (profile_ == Profile::Readback && webGpuNativeDeviceLost()) {
            invalidate();
            return;
        }
        journal_.rollback();
        status_ = Status::Abandoned;
    }

    void invalidate() noexcept
    {
        if (profile_ == Profile::Readback) {
            if (status_ == Status::Invalidated || status_ == Status::Abandoned)
                return;
            const auto& destination = *originalReadback()->destination();
            const auto& source = *destination.source();
            for (std::size_t i = 0u; i < source.retainedNativeAllocationRecordCount(); ++i) {
                const auto* original = source.retainedNativeResourceAt(i);
                if (original->buffer)
                    original->buffer->invalidateRecordedState();
                else
                    original->texture->invalidateRecordedState();
            }
            destination.nativeResource()->invalidateRecordedState();
            journal_.rollback();
            status_ = Status::Invalidated;
            return;
        }
        if (status_ != Status::Committed) {
            abandon();
            return;
        }
        const auto& root = *states_->inputs()->scalars()->usage()->projection()->transfer();
        for (const auto& resource : root.resources()) {
            if (resource.buffer)
                resource.buffer->invalidateRecordedState();
            else
                resource.texture->invalidateRecordedState();
        }
        status_ = Status::Invalidated;
    }

    Status status() const noexcept { return status_; }

    bool isReadbackProfile() const noexcept { return profile_ == Profile::Readback; }

    static const char* mapReadbackMipCurrent(
        const std::shared_ptr<const WebGpuOriginalStateTransaction>& self,
        const std::shared_ptr<const rhi::TextureReadbackInputSnapshot>& input,
        std::size_t index,
        const rhi::TextureReadbackLayout::Mip& original,
        rhi::TextureReadbackLayout::Mip& output) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-STATE-TRANSACTION-OWNER";
        if (!self->isReadbackProfile())
            return "RHI-WEBGPU-READBACK-TRANSACTION-PROFILE";
        if (self->status_ != Status::Pending && self->status_ != Status::Committed)
            return "RHI-WEBGPU-READBACK-TRANSACTION-CURRENT-STATUS";
        if (const auto* id = self->validate())
            return id;
        const auto& layout = self->originalReadback()->layout();
        if (!input || !input->hasQuotaBoundSharedOwner(input)
            || !core::SharedOwnerProjection::sameOwner(input, layout->inputOwner()))
            return "RHI-READBACK-PAIR-INPUT-OWNER";
        if (const auto* id =
                rhi::TextureReadbackLayout::validateOriginalMip(layout, index, original))
            return id;
        output = layout->mipCopies()[index];
        return nullptr;
    }

    const auto& states() const noexcept { return states_; }

    bool journalEmpty() const noexcept { return journal_.empty(); }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return webGpuGenericRecordingUsageDiagnostic;
    }

private:
    const char* validateOwnedObservations() const noexcept
    {
        for (const auto& entry : entries_) {
            const auto& control = entry.original->source.control;
            if (!control.active())
                return "RHI-WEBGPU-ORIGINAL-STATES-EXHAUSTED";
            if (!entry.changed) {
                if (control.pending())
                    return "RHI-WEBGPU-ORIGINAL-STATES-PENDING";
                if (!control.matches(entry.original->observation))
                    return "RHI-WEBGPU-ORIGINAL-STATES-STALE";
            } else if (status_ == Status::Pending) {
                if (!entry.pending || entry.pending->value() != entry.final
                    || !control.matchesPending(*entry.pending, journal_))
                    return "RHI-WEBGPU-STATE-TRANSACTION-PENDING-MISMATCH";
            } else if (!entry.committed || entry.committed->value != entry.final
                || !control.matches(*entry.committed))
                return "RHI-WEBGPU-STATE-TRANSACTION-COMMITTED-MISMATCH";
        }
        return nullptr;
    }

    static const char* validateInitialReadbackStream(
        const std::shared_ptr<const States>& states) noexcept
    {
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        if (const auto* id = states->validate())
            return id;
        const auto& inputs = *states->inputs();
        const auto& graph = *inputs.scalars()->usage()->projection();
        if (graph.transfer() || graph.resources() || !graph.pipelines().empty()
            || !graph.physicalGroups().empty() || !graph.originalUploads().empty()
            || !graph.originalTextureUploads().empty() || graph.originalReadbacks().size() != 1u)
            return "RHI-WEBGPU-READBACK-TRANSACTION-SOURCE-COVERAGE";
        const auto& pair = graph.originalReadbacks()[0];
        const auto& source = *pair->destination()->source();
        if (states->sourceReferences() != source.retainedNativeAllocationRecordCount() + 1u
            || states->states().size() != states->sourceReferences())
            return "RHI-WEBGPU-READBACK-TRANSACTION-CONTROL-COVERAGE";
        using K = rhi::CommandUsageKind;
        const auto& commands = inputs.scalars()->usage()->commands();
        if (commands.size() != 5u || commands[0].kind != K::TextureBarrier
            || commands[1].kind != K::BeginTransferPass || commands[2].kind != K::TextureReadback
            || commands[3].kind != K::EndTransferPass || commands[4].kind != K::Finish
            || graph.references().size() != 3u || inputs.descriptors().size() != 2u
            || graph.references()[0].source
                != WebGpuCommandReferenceSource::OriginalTextureReadbackState)
            return "RHI-WEBGPU-READBACK-TRANSACTION-STREAM";
        if (const auto* id = WebGpuOriginalTransferStateValidation::validate(states, 0u).diagnostic)
            return id;
        rhi::ResourceState final;
        if (const auto* id = WebGpuOriginalTransferStateValidation::stateBefore(
                *states, source.resource().id, commands.size(), final))
            return id;
        return final == rhi::ResourceState::CopySrc ? nullptr
                                                    : "RHI-WEBGPU-ORIGINAL-READBACK-STATE-MISMATCH";
    }

    const std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection>&
    originalReadback() const noexcept
    {
        return states_->inputs()->scalars()->usage()->projection()->originalReadbacks()[0];
    }

    const WebGpuTransferResourceObservation* transitionResource(
        rhi::GpuResourceId resource) const noexcept
    {
        if (profile_ == Profile::Readback) {
            const auto& source = originalReadback()->destination()->source()->resource();
            return source.id == resource ? &source : nullptr;
        }
        return states_->inputs()->scalars()->usage()->projection()->transfer()->find(resource);
    }

    static bool isTransition(rhi::CommandUsageKind kind) noexcept
    {
        return kind == rhi::CommandUsageKind::BufferBarrier
            || kind == rhi::CommandUsageKind::TextureBarrier;
    }

    // Original resources outlive the journal and all pointer observations.
    const std::shared_ptr<const States> states_;
    const WebGpuOriginalStateTransactionBudget budget_;
    const Profile profile_;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    core::MutationJournal journal_;
    Status status_ = Status::Prepared;
};
} // namespace arp::rhi_webgpu
