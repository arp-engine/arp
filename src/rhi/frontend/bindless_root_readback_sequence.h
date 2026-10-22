#pragma once
#include "rhi/frontend/command_buffer_readback.h"

namespace arp::rhi {
// Native-command custody of the existing buffer bindings. The original
// Result contract authenticates the whole copy set before any owners are
// retained. Publication and native map/fence evidence stay backend-owned.
class OriginalBindlessRootReadbackSequence final {
public:
    using Binding = OriginalCommandBufferReadbackBinding;
    using Owner = std::shared_ptr<const Binding>;
    using Budget = OriginalCommandReadbackResultContract::Budget;

    // Borrowed only during cold preparation. Native custody copies the exact
    // validated owners under its existing metadata quota.
    struct Input {
        const Owner* bindings = nullptr;
        std::size_t count = 0u;
        std::uint64_t maxVisits = 0u;
        Budget budget;
        OriginalBindlessSnapshotScopeContract::Intent snapshotScope;

        bool empty() const noexcept
        {
            return !bindings && !count && !maxVisits && !budget.contractVersion
                && !budget.prototypeOptIn && !budget.maxReadbackCommands
                && !budget.maxNativeCopyCommands && !budget.maxResultDeclarations
                && !budget.maxDeclaredReadbackBytes && !budget.maxUniqueResultBytes
                && !budget.maxCpuResultBytes && snapshotScope.empty();
        }
    };

    explicit OriginalBindlessRootReadbackSequence(Owner fault)
        : OriginalBindlessRootReadbackSequence(std::move(fault), Input{})
    {}

    OriginalBindlessRootReadbackSequence(Owner fault, const Input& input)
    {
        if ((diagnostic_ = inspectInput(fault, input)))
            return;
        if (!input.empty()) {
            initialize(
                input.bindings, input.count, input.maxVisits, input.budget, input.snapshotScope);
            if (!diagnostic_)
                for (std::size_t i = 0u; i < bindings_.size(); ++i)
                    if (bindings_[i].get() == fault.get()
                        && core::SharedOwnerProjection::sameOwner(bindings_[i], fault))
                        faultIndex_ = i;
            return;
        }
        const auto bytes = fault->originalRequest()->byteCapacity();
        initialize(&fault,
            1u,
            fault->originalProgram()->operations().size() + 3u,
            {1u,
                true,
                1u,
                1u,
                1u,
                bytes,
                bytes,
                fault->originalResult()->originalMetadataQuota()->limitBytes()});
    }

    static const char* inspectInput(const Owner& fault, const Input& input) noexcept
    {
        if (const auto* id = Binding::validateOriginal(fault))
            return id;
        if (!fault->isOriginalSnapshotFaultReadback())
            return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-REQUIRED";
        if (input.empty())
            return nullptr;
        const auto inspected = OriginalCommandBufferReadbackResultContract::inspect(
            input.bindings, input.count, input.maxVisits, input.budget, input.snapshotScope);
        if (inspected.diagnostic)
            return inspected.diagnostic;
        std::size_t found = 0u;
        for (std::size_t i = 0u; i < input.count; ++i) {
            if (!input.bindings[i]->isOriginalSnapshotFaultReadback())
                continue;
            if (!core::SharedOwnerProjection::sameOwner(fault, input.bindings[i])
                || fault.get() != input.bindings[i].get()) {
                if (input.snapshotScope.empty())
                    return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-IDENTITY";
                continue;
            }
            ++found;
        }
        if (found != 1u)
            return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-REQUIRED";
        return inspectFaultCoverage(input);
    }

    // Every represented snapshot owns exactly one fault result. A regular
    // result cannot borrow the primary snapshot's lease or hide another latch.
    static const char* inspectFaultCoverage(const Input& input) noexcept
    {
        if (input.snapshotScope.empty())
            return nullptr;
        std::uint64_t visits = 0u;
        for (std::size_t i = 0u; i < input.count; ++i) {
            std::size_t faults = 0u;
            for (std::size_t j = 0u; j < input.count; ++j) {
                if (visits == input.snapshotScope.maxVisits)
                    return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-VISIT-CAPACITY";
                ++visits;
                faults += input.bindings[j]->isOriginalSnapshotFaultReadback()
                    && core::SharedOwnerProjection::sameOwner(input.bindings[i]->originalSnapshot(),
                        input.bindings[j]->originalSnapshot());
            }
            if (faults != 1u)
                return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-COVERAGE";
        }
        return nullptr;
    }

    template <class RootAt>
    static const char* inspectRootCoverage(const Owner& fault,
        const Input& input,
        std::size_t rootCount,
        RootAt&& rootAt,
        const OriginalBindlessSnapshotScopeContract::Intent& scope) noexcept
    {
        if (const auto* id = inspectInput(fault, input))
            return id;
        if (scope.empty() != input.snapshotScope.empty())
            return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-SNAPSHOT-SCOPE";
        if (scope.empty())
            return nullptr;
        if (!rootCount)
            return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-ROOT-COVERAGE";
        std::uint64_t visits = 0u;
        for (std::size_t i = 0u; i < rootCount; ++i) {
            const auto& root = rootAt(i);
            if (const auto* id = OriginalCommandBindlessRootSource::validateOriginal(root))
                return id;
            std::size_t faults = 0u;
            for (std::size_t j = 0u; j < input.count; ++j) {
                if (visits == scope.maxVisits)
                    return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-VISIT-CAPACITY";
                ++visits;
                faults += input.bindings[j]->isOriginalSnapshotFaultReadback()
                    && core::SharedOwnerProjection::sameOwner(
                        root->originalSnapshot(), input.bindings[j]->originalSnapshot());
            }
            if (faults != 1u)
                return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-ROOT-COVERAGE";
        }
        for (std::size_t j = 0u; j < input.count; ++j) {
            bool used = false;
            for (std::size_t i = 0u; i < rootCount; ++i) {
                if (visits == scope.maxVisits)
                    return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-VISIT-CAPACITY";
                ++visits;
                used = used
                    || core::SharedOwnerProjection::sameOwner(
                        rootAt(i)->originalSnapshot(), input.bindings[j]->originalSnapshot());
            }
            if (!used)
                return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-UNCONSUMED-SNAPSHOT";
        }
        return nullptr;
    }

    static std::size_t resultCount(const Input& input) noexcept
    {
        return input.empty() ? 1u : input.count;
    }

    // Call only after inspectInput authenticates every member.
    static std::size_t faultCount(const Owner& fault, const Input& input) noexcept
    {
        std::size_t count = 0u;
        for (std::size_t i = 0u; i < resultCount(input); ++i)
            count += resultAt(fault, input, i)->isOriginalSnapshotFaultReadback();
        return count;
    }

    static const Owner& resultAt(const Owner& fault, const Input& input, std::size_t i) noexcept
    {
        return input.empty() ? fault : input.bindings[i];
    }

    static bool containsCopy(const Owner& fault, const Input& input, std::size_t command) noexcept
    {
        for (std::size_t i = 0u; i < resultCount(input); ++i)
            if (resultAt(fault, input, i)->commandIndex() == command)
                return true;
        return false;
    }

    // A result destination is retained by its authenticated binding rather
    // than by the descriptor closure. This grants only that exact copy operand.
    static bool containsCopyDestination(const Owner& fault,
        const Input& input,
        std::size_t command,
        const std::shared_ptr<const OriginalCommandBufferLease>& destination) noexcept
    {
        for (std::size_t i = 0u; i < resultCount(input); ++i) {
            const auto& binding = resultAt(fault, input, i);
            const auto& original = binding->originalReadbackSource();
            if (binding->commandIndex() == command && original.get() == destination.get()
                && core::SharedOwnerProjection::sameOwner(original, destination))
                return true;
        }
        return false;
    }

    // One finite original map per destination. Even disjoint intervals cannot
    // issue concurrent mapAsync calls for the same physical GPUBuffer.
    static const char* inspectMapDestinations(const Owner& fault, const Input& input) noexcept
    {
        if (const auto* id = inspectInput(fault, input))
            return id;
        const auto count = resultCount(input);
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& binding = resultAt(fault, input, i);
            const auto& request = binding->originalRequest();
            const BufferReadbackRangeContract::Range range{
                binding->originalReadbackSource()->layout().sizeBytes,
                request->offsetBytes(),
                request->byteCapacity()};
            if (const auto* id = range.diagnostic())
                return id;
        }
        const auto unique = core::SharedIdentityProjection::inspect(count,
            count,
            count,
            [&](std::size_t i) noexcept -> core::SharedIdentityProjection::Record {
                const auto& source = resultAt(fault, input, i)->originalReadbackSource();
                return {source->resource().value, 1u, source, source.get()};
            });
        return unique.status == core::SharedIdentityProjection::Status::Complete
                && unique.identities == count
            ? nullptr
            : "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-DESTINATION-ALIAS";
    }

    static const char* inspectWholeDestinations(const Owner& fault, const Input& input) noexcept
    {
        if (const auto* id = inspectMapDestinations(fault, input))
            return id;
        for (std::size_t i = 0u; i < resultCount(input); ++i) {
            const auto& binding = resultAt(fault, input, i);
            const auto& request = binding->originalRequest();
            if (request->offsetBytes()
                || request->byteCapacity() != binding->originalReadbackSource()->layout().sizeBytes)
                return "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-WHOLE-DESTINATION";
        }
        return nullptr;
    }

    static std::uint64_t additionalOwnerBound(const Input& input) noexcept
    {
        // Existing singleton accounting includes the shared stream and Result
        // quota. Each additional binding retains its request/result facades.
        const auto results = resultCount(input);
        const auto count = results ? results - 1u : 0u;
        constexpr std::uint64_t perBinding = Binding::retainedOwnerCount + 2u;
        const auto retained = static_cast<std::uint64_t>(count);
        if (retained > UINT64_MAX / perBinding)
            return UINT64_MAX;
        auto owners = retained * perBinding;
        if (!input.snapshotScope.empty())
            for (std::size_t i = 0u; input.bindings && i < input.count; ++i)
                if (input.bindings[i] && input.bindings[i]->isOriginalSnapshotFaultReadback()) {
                    if (owners == UINT64_MAX)
                        return UINT64_MAX;
                    ++owners; // Conservative bound, including the primary lease.
                }
        return owners;
    }

    OriginalBindlessRootReadbackSequence(
        const Owner* bindings, std::size_t count, std::uint64_t maxVisits, const Budget& budget)
    {
        initialize(bindings, count, maxVisits, budget);
    }

    OriginalBindlessRootReadbackSequence(const OriginalBindlessRootReadbackSequence&) = delete;
    OriginalBindlessRootReadbackSequence& operator=(
        const OriginalBindlessRootReadbackSequence&) = delete;

    const auto& bindings() const noexcept { return bindings_; }

    Input input() const noexcept
    {
        return {bindings_.data(), bindings_.size(), maxVisits_, budget_, snapshotScope_};
    }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const Owner& faultBinding() const noexcept
    {
        static const Owner empty;
        return faultIndex_ < bindings_.size() ? bindings_[faultIndex_] : empty;
    }

    const char* validatePending(
        const std::shared_ptr<const OriginalCommandStateTransaction>& transaction,
        const BindlessFaultSubmissionLease& fault) const noexcept
    {
        if (diagnostic_)
            return diagnostic_;
        for (const auto& binding : bindings_)
            if (const auto* id = Binding::validateOriginalPending(binding, transaction, fault))
                return id;
        return nullptr;
    }

private:
    void initialize(const Owner* bindings,
        std::size_t count,
        std::uint64_t visits,
        const Budget& budget,
        const OriginalBindlessSnapshotScopeContract::Intent& snapshotScope = {})
    {
        diagnostic_ = OriginalCommandBufferReadbackResultContract::inspect(
            bindings, count, visits, budget, snapshotScope)
                          .diagnostic;
        if (diagnostic_)
            return;
        // /1 retains its single-domain boundary. Explicit SnapshotScope retains
        // each distinct latch, with one shared transaction and publication.
        std::size_t fault = SIZE_MAX;
        for (std::size_t i = 0u; i < count; ++i) {
            if (!bindings[i]->isOriginalSnapshotFaultReadback())
                continue;
            if (fault != SIZE_MAX && snapshotScope.empty()) {
                diagnostic_ = "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-DUPLICATE";
                return;
            }
            if (fault == SIZE_MAX)
                fault = i;
        }
        if (fault == SIZE_MAX) {
            diagnostic_ = "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-FAULT-REQUIRED";
            return;
        }
        if ((diagnostic_ = inspectFaultCoverage({bindings, count, visits, budget, snapshotScope})))
            return;
        bindings_.reserve(count);
        for (std::size_t i = 0u; i < count; ++i)
            bindings_.push_back(bindings[i]);
        faultIndex_ = fault;
        maxVisits_ = visits;
        budget_ = budget;
        snapshotScope_ = snapshotScope;
    }

    core::pmr::Vector<Owner> bindings_{&core::defaultCpuMemoryResource()};
    Budget budget_;
    OriginalBindlessSnapshotScopeContract::Intent snapshotScope_;
    std::uint64_t maxVisits_ = 0u;
    std::size_t faultIndex_ = SIZE_MAX;
    const char* diagnostic_ = nullptr;
};

// Additional per-snapshot writer reservations belong to the native command's
// existing quota and lifetime. They cannot commit its journal or publish any
// result. All acquisition succeeds before recording, or every new lease rolls
// back. Release occurs only alongside the primary lease after real drain.
class OriginalBindlessRootFaultCustody final {
public:
    using Sequence = OriginalBindlessRootReadbackSequence;

    const char* prepare(const Sequence& readbacks)
    {
        if (prepared_)
            return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-NOT-EMPTY";
        if (const auto* id = readbacks.diagnostic())
            return id;
        std::size_t count = 0u;
        for (const auto& binding : readbacks.bindings())
            count += binding->isOriginalSnapshotFaultReadback()
                && !core::SharedOwnerProjection::sameOwner(
                    readbacks.faultBinding()->originalSnapshot(), binding->originalSnapshot());
        try {
            leases_.resize(count);
            prepared_ = true;
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-ALLOCATION";
        }
    }

    const char* acquire(const Sequence& readbacks,
        const BindlessFaultSubmissionLease& primary,
        core::pmr::String& diagnostic)
    {
        if (!prepared_ || acquired_)
            return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-NOT-EMPTY";
        if (const auto* id = readbacks.diagnostic())
            return id;
        if (!primary.originalJournalOwned()
            || !core::SharedOwnerProjection::sameOwner(
                primary.originalDomain(), readbacks.faultBinding()->originalSnapshot()))
            return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-PRIMARY";
        std::size_t count = 0u;
        for (const auto& binding : readbacks.bindings())
            count += binding->isOriginalSnapshotFaultReadback()
                && !core::SharedOwnerProjection::sameOwner(
                    primary.originalDomain(), binding->originalSnapshot());
        if (count != leases_.size())
            return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-CAPACITY";
        try {
            std::size_t i = 0u;
            for (const auto& binding : readbacks.bindings()) {
                if (!binding->isOriginalSnapshotFaultReadback()
                    || core::SharedOwnerProjection::sameOwner(
                        primary.originalDomain(), binding->originalSnapshot()))
                    continue;
                auto lease = Sequence::Binding::acquireOriginalFaultCustody(binding, diagnostic);
                if (!lease.valid()) {
                    release();
                    return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-ACQUIRE";
                }
                leases_[i++] = std::move(lease);
            }
            acquired_ = true;
            return nullptr;
        } catch (const std::bad_alloc&) {
            release();
            return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-ALLOCATION";
        }
    }

    const char* validatePending(const Sequence& readbacks,
        const std::shared_ptr<const OriginalCommandStateTransaction>& transaction,
        const BindlessFaultSubmissionLease& primary) const noexcept
    {
        if (const auto* id = readbacks.diagnostic())
            return id;
        if (!prepared_ || !acquired_)
            return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-MISSING";
        for (const auto& binding : readbacks.bindings()) {
            const BindlessFaultSubmissionLease* selected = nullptr;
            if (core::SharedOwnerProjection::sameOwner(
                    primary.originalDomain(), binding->originalSnapshot()))
                selected = &primary;
            for (const auto& lease : leases_)
                if (core::SharedOwnerProjection::sameOwner(
                        lease.originalDomain(), binding->originalSnapshot())) {
                    if (selected)
                        return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-AMBIGUOUS";
                    selected = &lease;
                }
            if (!selected)
                return "RHI-BINDLESS-ROOT-FAULT-CUSTODY-MISSING";
            if (const auto* id =
                    Sequence::Binding::validateOriginalPending(binding, transaction, *selected))
                return id;
        }
        return nullptr;
    }

    void release() noexcept
    {
        for (auto& lease : leases_)
            lease.release();
        acquired_ = false;
    }

private:
    core::pmr::Vector<BindlessFaultSubmissionLease> leases_{&core::defaultCpuMemoryResource()};
    bool prepared_ = false, acquired_ = false;
};
} // namespace arp::rhi
