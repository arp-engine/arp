#pragma once
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/original_submission_completion.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/semantics/original_submission_read.h"

namespace arp::rhi {
// Original-control custody prerequisite only. Public construction is Null
// validator-only; no native receipt or Program/pending-resource authority.
class OriginalSubmittedBufferReadPins final : public core::QuotaBoundSharedObjectIdentity {
    friend class OriginalCommandResourceStates;
    using Completion = OriginalSubmissionCompletionDomain;
    using Record = Completion::Record;
    using Outcome = Completion::Outcome;
    using Control = common::RecordedResourceStateControl;
    using Budget = semantics::OriginalSubmissionReadBudget;
    using Usage = semantics::OriginalSubmissionReadUsage;
    using Contract = semantics::OriginalSubmissionReadContract;

    class ConstructionKey {
        friend class OriginalSubmittedBufferReadPins;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Entry {
        std::shared_ptr<const OriginalCommandBufferLease> source;
        std::shared_ptr<Record> writer;
        const core::MutationJournal* journal;
        Control::PendingObservation pending;
        OriginalResourceRegistrationTicket registration;
        std::size_t sourceIndex;
        semantics::ResourceAccessScope scope;
    };

    struct Batch {
        std::shared_ptr<OriginalSubmittedBufferReadPins> issuer;
        core::pmr::Vector<Entry> entries{&core::defaultCpuMemoryResource()};
        std::shared_ptr<const OriginalCommandInputs> inputs;
        std::uint64_t identity = 0u;
        std::uint64_t links = 0u;
        bool charged = false, bound = false;

        ~Batch()
        {
            if (charged) {
                // Unattached cold custody has no reader journal. Its last
                // owner must cancel on the issuer thread, even after revocation.
                if (bound || issuer->owner_ != std::this_thread::get_id())
                    std::terminate();
                releaseBatch(this);
            }
        }
    };

public:
    class Reservation final {
        friend class OriginalSubmittedBufferReadPins;
        friend class OriginalCommandResourceStates;

        explicit Reservation(std::shared_ptr<Batch> batch) : batch_(std::move(batch)) {}

    public:
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&&) = default;
        Reservation& operator=(Reservation&&) = default;

        std::uint64_t identity() const noexcept { return batch_ ? batch_->identity : 0u; }

        bool provesGpuCompletion() const noexcept { return false; }

    private:
        std::shared_ptr<Batch> batch_;
    };

    class Observation final {
        friend class OriginalSubmittedBufferReadPins;
        friend class OriginalCommandResourceStates;

        Observation(const std::shared_ptr<Batch>& batch, std::size_t index)
            : batch_(batch), index_(index), state_(batch->entries[index].pending.value()),
              revision_(batch->entries[index].pending.revision()),
              generation_(batch->entries[index].registration.generation())
        {}

    public:
        Observation(const Observation&) = default;

        const char* diagnostic() const noexcept
        {
            const auto batch = batch_.lock();
            if (!batch)
                return "RHI-SUBMISSION-READ-PIN-EXPIRED";
            // The immutable issuer/owner check must precede every mutable
            // batch/control read, including a foreign observation of release.
            if (const auto* id = batch->issuer->retainedReadDiagnostic())
                return id;
            return batch->charged && index_ < batch->entries.size()
                ? validateBatch(batch.get())
                : "RHI-SUBMISSION-READ-PIN-EXPIRED";
        }

        bool provesGpuCompletion() const noexcept { return false; }

        ResourceState state() const noexcept { return state_; }

        std::uint64_t revision() const noexcept { return revision_; }

        std::uint64_t resourceGeneration() const noexcept { return generation_; }

        const char* validateReadScope(const semantics::ResourceAccessScope& scope,
            std::uint64_t offset,
            std::uint64_t bytes) const noexcept
        {
            if (const auto* id = diagnostic())
                return id;
            const auto batch = batch_.lock();
            if (const auto* id = Contract::inspectBufferRead(
                    batch->entries[index_].source->originalObject()->desc(), offset, bytes, scope))
                return id;
            const auto& reserved = batch->entries[index_].scope;
            return (scope.stages & reserved.stages) == scope.stages ? nullptr
                                                                    : "RHI-SUBMISSION-READ-STAGES";
        }

    private:
        std::weak_ptr<Batch> batch_;
        std::size_t index_;
        ResourceState state_;
        std::uint64_t revision_, generation_;
    };

    struct Borrow {
        std::shared_ptr<const OriginalCommandBufferLease> source;
        Completion::Ticket writer;
        std::shared_ptr<void> writerRetention;
        const core::MutationJournal* writerJournal = nullptr;
        semantics::ResourceAccessScope scope;
        std::uint64_t offsetBytes = 0u, sizeBytes = 0u;
        // Required to extend an already published writer's protected generation.
        const Observation* existing = nullptr;
    };

    OriginalSubmittedBufferReadPins(
        ConstructionKey, const std::shared_ptr<Completion>& completion, Budget budget)
        : completion_(completion), device_(completion->device_), budget_(budget),
          owner_(std::this_thread::get_id())
    {}

    OriginalSubmittedBufferReadPins(const OriginalSubmittedBufferReadPins&) = delete;

    static std::shared_ptr<OriginalSubmittedBufferReadPins> prepareValidator(
        const GpuDevice& device,
        const std::shared_ptr<Completion>& completion,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        diagnostic = Contract::validateBudget(budget);
        if (!diagnostic)
            diagnostic = Contract::pinCustodyLowering(device.backend(), budget.contractVersion);
        if (!diagnostic)
            diagnostic = Completion::validateOriginal(completion);
        if (!diagnostic)
            diagnostic = device.validateOriginalDeviceLifetime(completion->device_);
        if (diagnostic)
            return {};
        const auto lifetime = completion->device_.domain_.lock();
        if (lifetime->owner_ != std::this_thread::get_id()) {
            diagnostic = "RHI-SUBMISSION-READ-THREAD";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes
            || device.overlapsOriginalDeviceCpuOwner(quota)
            || core::SharedOwnerProjection::overlapsOwner(
                quota, completion->originalMetadataQuota())) {
            diagnostic = "RHI-SUBMISSION-READ-QUOTA";
            return {};
        }
        if (lifetime->submittedReadIssuerIssued_) {
            diagnostic = "RHI-SUBMISSION-READ-ISSUER-ALREADY-PREPARED";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = shareOriginal(core::makeUnique<OriginalSubmittedBufferReadPins>(
                ConstructionKey{}, completion, budget));
            result->self_ = result;
            lifetime->submittedReadIssuerIssued_ = true;
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-SUBMISSION-READ-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<OriginalSubmittedBufferReadPins>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-SUBMISSION-READ-ISSUER-OWNER";
        return self->diagnostic();
    }

    const char* diagnostic() const noexcept
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-SUBMISSION-READ-THREAD";
        const auto completion = completion_.lock();
        if (!completion)
            return "RHI-SUBMISSION-READ-COMPLETION-EXPIRED";
        if (const auto* id = completion->diagnostic())
            return id;
        return originalMetadataQuota()->limitBytes() == budget_.maxCpuMetadataBytes
            ? nullptr
            : "RHI-SUBMISSION-READ-QUOTA";
    }

    const char* usageValidator(Usage& out) const noexcept
    {
        if (const auto* id = diagnostic())
            return id;
        out = {commands_, pins_, links_, originalMetadataQuota()->liveBytes()};
        return nullptr;
    }

    // Cold reservation only. Existing completion dependencies must already name
    // each accepted writer; all fallible work precedes any scalar blocker.
    const char* attachValidator(
        const Completion::Ticket& reader, const Borrow* borrows, std::size_t count)
    {
        if (const auto* id = diagnostic())
            return id;
        const auto completion = completion_.lock();
        if (!completion->current(reader) || reader.node_->accepted || reader.node_->readPins)
            return "RHI-SUBMISSION-READ-READER";
        const char* id = nullptr;
        auto batch = prepareBatch(&reader, borrows, count, {}, nullptr, id);
        if (!batch)
            return id;
        bindBatch(reader, batch);
        return nullptr;
    }

    // Source-index binding and custody only. Caller-authored scopes still do
    // not prove complete frozen accesses; Graph/Program admission stays closed.
    std::optional<Reservation> reserveValidator(
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const std::size_t* sourceIndices,
        const Borrow* borrows,
        std::size_t count,
        const char*& diagnostic)
    {
        diagnostic = this->diagnostic();
        if (!diagnostic)
            diagnostic = Contract::coldReservationLowering(Backend::Null, budget_.contractVersion);
        if (!diagnostic)
            diagnostic = OriginalCommandInputs::validateOriginal(inputs);
        if (diagnostic)
            return {};
        if (!count || !sourceIndices || !borrows) {
            diagnostic = "RHI-SUBMISSION-READ-BORROWS";
            return {};
        }
        const auto& originalSources = inputs->originalScalars()->originalUsage()->originalSources();
        if (core::SharedOwnerProjection::overlapsOwner(
                originalMetadataQuota(), inputs->originalMetadataQuota())
            || core::SharedOwnerProjection::overlapsOwner(
                originalMetadataQuota(), originalSources->originalMetadataQuota())) {
            diagnostic = "RHI-SUBMISSION-READ-QUOTA";
            return {};
        }
        const auto& sources = originalSources->sources();
        for (std::size_t i = 0u; i < count; ++i) {
            const auto* source = sourceIndices[i] < sources.size()
                ? std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                      &sources[sourceIndices[i]])
                : nullptr;
            if (!source || *source != borrows[i].source
                || !core::SharedOwnerProjection::sameOwner(*source, borrows[i].source)) {
                diagnostic = "RHI-SUBMISSION-READ-SOURCE-INDEX";
                return {};
            }
        }
        auto batch = prepareBatch(nullptr, borrows, count, inputs, sourceIndices, diagnostic);
        return batch ? std::optional<Reservation>{Reservation{std::move(batch)}} : std::nullopt;
    }

    std::optional<Observation> observeValidator(const Reservation& reservation,
        std::size_t sourceIndex,
        const char*& diagnostic) const noexcept
    {
        diagnostic = inspectReservation(reservation);
        if (diagnostic)
            return {};
        const auto& batch = reservation.batch_;
        for (std::size_t i = 0u; i < batch->entries.size(); ++i)
            if (batch->entries[i].sourceIndex == sourceIndex)
                return Observation{batch, i};
        diagnostic = "RHI-SUBMISSION-READ-SOURCE-INDEX";
        return {};
    }

    const char* attachReservationValidator(const Completion::Ticket& reader,
        const Reservation& reservation,
        const std::shared_ptr<const OriginalCommandInputs>& inputs) noexcept
    {
        if (const auto* id = inspectReservation(reservation))
            return id;
        const auto& batch = reservation.batch_;
        if (batch->bound)
            return "RHI-SUBMISSION-READ-RESERVATION-BOUND";
        if (batch->inputs != inputs
            || !core::SharedOwnerProjection::sameOwner(batch->inputs, inputs))
            return "RHI-SUBMISSION-READ-INPUTS";
        if (const auto* id = OriginalCommandInputs::validateOriginal(inputs))
            return id;
        const auto completion = completion_.lock();
        if (!completion->current(reader) || reader.node_->accepted || reader.node_->readPins)
            return "RHI-SUBMISSION-READ-READER";
        // Exact distinct predecessor set; no unrelated authority may be added
        // between the cold reservation and this single genuine attachment.
        if (reader.node_->predecessors.size() != batch->links)
            return "RHI-SUBMISSION-READ-PREDECESSOR";
        for (const auto& entry : batch->entries) {
            bool linked = false;
            for (const auto& predecessor : reader.node_->predecessors)
                linked |= predecessor == entry.writer;
            if (!linked)
                return "RHI-SUBMISSION-READ-PREDECESSOR";
        }
        bindBatch(reader, batch);
        return nullptr;
    }

private:
    const char* retainedReadDiagnostic() const noexcept
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-SUBMISSION-READ-THREAD";
        const auto completion = completion_.lock();
        if (!completion)
            return "RHI-SUBMISSION-READ-COMPLETION-EXPIRED";
        if (const auto* id = completion->retainedReadDiagnostic())
            return id;
        return originalMetadataQuota()->limitBytes() == budget_.maxCpuMetadataBytes
            ? nullptr
            : "RHI-SUBMISSION-READ-QUOTA";
    }

    const char* inspectReservation(
        const Reservation& reservation, bool retainedRead = false) const noexcept
    {
        if (const auto* id = retainedRead ? retainedReadDiagnostic() : diagnostic())
            return id;
        const auto& batch = reservation.batch_;
        if (!batch || batch->issuer.get() != this || !batch->identity)
            return "RHI-SUBMISSION-READ-RESERVATION";
        return validateBatch(batch.get());
    }

    static void bindBatch(
        const Completion::Ticket& reader, const std::shared_ptr<Batch>& batch) noexcept
    {
        batch->bound = true;
        reader.node_->readPins = batch;
        reader.node_->validateReadPins = validateBatch;
        reader.node_->releaseReadPins = releaseBatch;
    }

    std::shared_ptr<Batch> prepareBatch(const Completion::Ticket* reader,
        const Borrow* borrows,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const std::size_t* sourceIndices,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        const auto fail = [&](const char* id) -> std::shared_ptr<Batch> {
            diagnostic = id;
            return {};
        };
        const auto completion = completion_.lock();
        if (!count || !borrows)
            return fail("RHI-SUBMISSION-READ-BORROWS");
        if (commands_ == budget_.maxPendingCommands)
            return fail("RHI-SUBMISSION-READ-COMMAND-CAPACITY");
        if (count > budget_.maxBufferReadPins - pins_)
            return fail("RHI-SUBMISSION-READ-PIN-CAPACITY");
        if (!identities_.hasRemaining())
            return fail("RHI-SUBMISSION-READ-IDENTITY-EXHAUSTED");
        std::uint64_t links = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            if (const auto* id = inspectBorrow(*completion, reader, borrows[i]))
                return fail(id);
            bool newWriter = true;
            for (std::size_t j = 0u; j < i; ++j) {
                if (borrows[i].source->originalObject() == borrows[j].source->originalObject())
                    return fail("RHI-SUBMISSION-READ-DUPLICATE-BUFFER");
                newWriter &= borrows[i].writer.node_ != borrows[j].writer.node_;
            }
            if (newWriter)
                ++links;
        }
        if (links > budget_.maxPredecessorLinks - links_)
            return fail("RHI-SUBMISSION-READ-LINK-CAPACITY");
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            auto batch = core::makeShared<Batch>();
            batch->issuer = self_.lock();
            batch->inputs = inputs;
            batch->entries.reserve(count);
            batch->links = links;
            for (std::size_t i = 0u; i < count; ++i) {
                const auto& input = borrows[i];
                auto& control = *input.source->originalObject()->originalStateControl_;
                const auto prior = existingEntry(input);
                const auto pending = prior
                    ? std::optional<Control::PendingObservation>(prior->pending)
                    : control.observePending(*input.writerJournal);
                if (!pending)
                    return fail("RHI-SUBMISSION-READ-WRITER-STATE");
                batch->entries.push_back({input.source,
                    input.writer.node_,
                    prior ? prior->journal : input.writerJournal,
                    *pending,
                    input.source->originalObject()->originalResourceRegistration(),
                    sourceIndices ? sourceIndices[i] : SIZE_MAX,
                    input.scope});
            }
            batch->identity = identities_.tryAcquire();
            for (const auto& entry : batch->entries) {
                auto& control = *entry.source->originalObject()->originalStateControl_;
                control.readIssuer_ = this;
                ++control.submittedReaders_;
            }
            ++commands_;
            pins_ += count;
            links_ += links;
            batch->charged = true;
            return batch;
        } catch (const std::bad_alloc&) {
            return fail("RHI-SUBMISSION-READ-ALLOCATION");
        } catch (const std::length_error&) {
            return fail("RHI-SUBMISSION-READ-STORAGE-SIZE");
        }
    }

public:
    std::optional<Observation> observeValidator(const Completion::Ticket& reader,
        const std::shared_ptr<const OriginalCommandBufferLease>& source,
        const char*& diagnostic) const noexcept
    {
        diagnostic = this->diagnostic();
        if (diagnostic)
            return {};
        const auto completion = completion_.lock();
        if (!completion->current(reader) || !reader.node_->readPins) {
            diagnostic = "RHI-SUBMISSION-READ-READER";
            return {};
        }
        const auto batch = std::static_pointer_cast<Batch>(reader.node_->readPins);
        if (batch->issuer.get() != this || (diagnostic = validateBatch(batch.get()))) {
            if (!diagnostic)
                diagnostic = "RHI-SUBMISSION-READ-ISSUER-OWNER";
            return {};
        }
        for (std::size_t i = 0u; i < batch->entries.size(); ++i)
            if (batch->entries[i].source == source
                && core::SharedOwnerProjection::sameOwner(batch->entries[i].source, source))
                return Observation{batch, i};
        diagnostic = "RHI-SUBMISSION-READ-SOURCE";
        return {};
    }

private:
    const Entry* existingEntry(const Borrow& input) const noexcept
    {
        if (!input.existing)
            return nullptr;
        const auto batch = input.existing->batch_.lock();
        if (!batch || !batch->charged || batch->issuer.get() != this
            || input.existing->index_ >= batch->entries.size() || validateBatch(batch.get()))
            return nullptr;
        const auto& entry = batch->entries[input.existing->index_];
        return entry.source == input.source && entry.writer == input.writer.node_
                && core::SharedOwnerProjection::sameOwner(entry.source, input.source)
            ? &entry
            : nullptr;
    }

    const char* inspectBorrow(const Completion& completion,
        const Completion::Ticket* reader,
        const Borrow& input) const noexcept
    {
        if (const auto* id = OriginalCommandBufferLease::validateOriginal(input.source))
            return id;
        const auto& buffer = *input.source->originalObject();
        const auto registration = buffer.originalResourceRegistration();
        if (const auto* id = registration.diagnostic())
            return id;
        if (!registration.matchesIdentity(
                &buffer, buffer.id().value, OriginalRegisteredResourceKind::Buffer))
            return "RHI-SUBMISSION-READ-SOURCE";
        if (core::SharedOwnerProjection::overlapsOwner(
                originalMetadataQuota(), input.source->originalMetadataQuota()))
            return "RHI-SUBMISSION-READ-QUOTA";
        if (const auto* id = Contract::inspectBufferRead(
                buffer.desc(), input.offsetBytes, input.sizeBytes, input.scope))
            return id;
        if (!buffer.hostWritesSealed())
            return "RHI-SUBMISSION-READ-HOST-WRITES";
        auto* control = buffer.originalStateControl_;
        if (buffer.originalStateBindingInvalid_ || !control || !control->active()
            || (control->submittedReaders_ && control->readIssuer_ != this)
            || control->submittedReaders_ == UINT64_MAX)
            return "RHI-SUBMISSION-READ-CONTROL";
        if (!control->domain_.ticket_
            || control->domain_.ticket_->domain_.lock() != device_.domain_.lock())
            return "RHI-SUBMISSION-READ-SOURCE";
        if (!completion.belongs(input.writer) || !input.writer.node_->accepted)
            return "RHI-SUBMISSION-READ-WRITER";
        if (reader) {
            bool linked = false;
            for (const auto& predecessor : reader->node_->predecessors)
                linked |= predecessor == input.writer.node_;
            if (!linked)
                return "RHI-SUBMISSION-READ-PREDECESSOR";
        }
        if (input.existing)
            return existingEntry(input) ? nullptr : "RHI-SUBMISSION-READ-OBSERVATION";
        if (input.writer.outcome() != Outcome::Pending)
            return "RHI-SUBMISSION-READ-OBSERVATION-REQUIRED";
        if (!input.writerRetention || !input.writerJournal
            || input.writerRetention.get() != input.writer.node_->retention.get()
            || !core::SharedOwnerProjection::sameOwner(
                input.writerRetention, input.writer.node_->retention))
            return "RHI-SUBMISSION-READ-WRITER-CUSTODY";
        const auto pending = control->observePending(*input.writerJournal);
        return pending && pending->value() == ResourceState::ShaderRead
            ? nullptr
            : "RHI-SUBMISSION-READ-WRITER-STATE";
    }

    static const char* validateBatch(const void* context) noexcept
    {
        const auto& batch = *static_cast<const Batch*>(context);
        if (const auto* id = batch.issuer->retainedReadDiagnostic())
            return id;
        if (!batch.charged)
            return "RHI-SUBMISSION-READ-PIN-EXPIRED";
        for (const auto& entry : batch.entries) {
            if (const auto* id = OriginalCommandBufferLease::validateOriginal(entry.source))
                return id;
            if (const auto* id = entry.registration.diagnostic())
                return id;
            const auto& buffer = *entry.source->originalObject();
            const auto* control = buffer.originalStateControl_;
            if (!control || control->readIssuer_ != batch.issuer.get()
                || !control->submittedReaders_ || !control->active()
                || !entry.registration.matchesIdentity(
                    &buffer, buffer.id().value, OriginalRegisteredResourceKind::Buffer))
                return "RHI-SUBMISSION-READ-CONTROL";
            const auto outcome = entry.writer->published.load(std::memory_order_acquire);
            if (outcome == Outcome::Pending) {
                // The exact writer retention still owns this journal. Never
                // dereference it after original publication releases retention.
                if (!entry.writer->retention || !entry.journal
                    || !control->matchesPending(entry.pending, *entry.journal))
                    return "RHI-SUBMISSION-READ-WRITER-STATE";
            } else if (outcome != Outcome::Succeeded) {
                return "RHI-SUBMISSION-READ-WRITER-FAILED";
            } else if (!control->matches({entry.pending.value(), entry.pending.revision()})) {
                return "RHI-SUBMISSION-READ-GENERATION";
            }
        }
        return nullptr;
    }

    static void releaseBatch(void* context) noexcept
    {
        auto& batch = *static_cast<Batch*>(context);
        auto& issuer = *batch.issuer;
        if (!batch.charged
            || (issuer.owner_ != std::this_thread::get_id() && !issuer.device_.terminallyRevoked()))
            std::terminate();
        for (const auto& entry : batch.entries) {
            auto& control = *entry.source->originalObject()->originalStateControl_;
            if (control.readIssuer_ != &issuer || !control.submittedReaders_)
                std::terminate();
            if (!--control.submittedReaders_)
                control.readIssuer_ = nullptr;
        }
        --issuer.commands_;
        issuer.pins_ -= batch.entries.size();
        issuer.links_ -= batch.links;
        batch.charged = false;
        batch.entries.clear();
        batch.inputs.reset();
    }

    const std::weak_ptr<Completion> completion_;
    const OriginalDeviceLifetime::Ticket device_;
    const Budget budget_;
    const std::thread::id owner_;
    std::weak_ptr<OriginalSubmittedBufferReadPins> self_;
    core::MonotonicIdSequence identities_;
    std::uint64_t commands_ = 0u, pins_ = 0u, links_ = 0u;
};
} // namespace arp::rhi
