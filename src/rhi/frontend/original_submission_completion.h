#pragma once
#include "core/ownership/monotonic_id_sequence.h"
#include "core/ownership/quota_bound_shared_identity.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/original_submission_completion.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace arp::rhi_metal {
class MetalOriginalSubmissionDomain;
}

namespace arp::rhi {
// Owner-thread resolution substrate. Public construction is Null validator
// only. The concrete Metal canonical issuer has separate private storage access;
// native tickets never escape through this API. Pending reads remain gated.
class OriginalSubmissionCompletionDomain final : public core::QuotaBoundSharedObjectIdentity {
    friend class arp::rhi_metal::MetalOriginalSubmissionDomain;
    friend class OriginalSubmittedBufferReadPins;
    using Budget = semantics::OriginalSubmissionCompletionBudget;
    using Contract = semantics::OriginalSubmissionCompletionContract;
    using Resolve = bool (*)(void*, bool) noexcept;
    using Cancel = void (*)(void*) noexcept;

    class ConstructionKey {
        friend class OriginalSubmissionCompletionDomain;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class Outcome { Pending, Succeeded, Failed, Cancelled };

private:
    enum class Result { Unaccepted, Pending, Succeeded, Failed, Closed };

    struct Record final {
        Record(std::uint64_t id, std::shared_ptr<void> owner, Resolve resolve, Cancel cancel)
            : identity(id), retention(std::move(owner)), resolve(resolve), cancel(cancel)
        {}

        const std::uint64_t identity;
        std::shared_ptr<void> retention;
        const Resolve resolve;
        const Cancel cancel;
        core::pmr::Vector<std::shared_ptr<Record>> predecessors{&core::defaultCpuMemoryResource()};
        std::atomic<Result> result{Result::Unaccepted};
        std::atomic<Outcome> published{Outcome::Pending};
        bool accepted = false;
        std::shared_ptr<void> readPins;
        const char* (*validateReadPins)(const void*) noexcept = nullptr;
        void (*releaseReadPins)(void*) noexcept = nullptr;
    };

public:
    class Ticket final {
        friend class OriginalSubmissionCompletionDomain;
        friend class OriginalSubmittedBufferReadPins;

        Ticket(
            std::weak_ptr<OriginalSubmissionCompletionDomain> domain, std::shared_ptr<Record> node)
            : domain_(std::move(domain)), node_(std::move(node))
        {}

    public:
        Ticket() = default;

        Outcome outcome() const noexcept
        {
            return node_ ? node_->published.load(std::memory_order_acquire) : Outcome::Cancelled;
        }

        bool provesGpuCompletion() const noexcept { return false; }

    private:
        std::weak_ptr<OriginalSubmissionCompletionDomain> domain_;
        std::shared_ptr<Record> node_;
    };

    OriginalSubmissionCompletionDomain(
        ConstructionKey, Budget budget, OriginalDeviceLifetime::Ticket device)
        : budget_(budget), device_(std::move(device)), owner_(std::this_thread::get_id())
    {
        slots_.resize(static_cast<std::size_t>(budget.maxSubmissions));
    }

    ~OriginalSubmissionCompletionDomain()
    {
        // Deferred teardown alone allows terminal cleanup on the final result
        // owner. Every retained result and irreversible host revocation must
        // be known first; public native construction/authority remain closed.
        if (deferredRetirement_) {
            if (!device_.terminallyRevoked() || !allResultsKnown())
                std::terminate();
            resolveRetired();
            return;
        }
        if (owner_ != std::this_thread::get_id())
            std::terminate();
        (void)retireValidator();
    }

    static std::shared_ptr<OriginalSubmissionCompletionDomain> prepareValidator(
        const GpuDevice& device,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        diagnostic = Contract::inspect(budget);
        if (!diagnostic)
            diagnostic = Contract::lowering(device.backend(), budget.contractVersion).diagnostic;
        if (diagnostic)
            return {};
        return prepareStorage(device, budget, quota, diagnostic);
    }

private:
    // Only the concrete backend's canonical private issuer may use storage for
    // native work. Its tickets never escape through the validator API.
    static std::shared_ptr<OriginalSubmissionCompletionDomain> prepareStorage(
        const GpuDevice& device,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        if ((diagnostic = Contract::inspect(budget)))
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes
            || device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-QUOTA";
            return {};
        }
        const auto ticket = device.originalDeviceLifetimeTicket();
        if ((diagnostic = device.validateOriginalDeviceLifetime(ticket)))
            return {};
        const core::pmr::Vector<std::shared_ptr<Record>> shape{&core::defaultCpuMemoryResource()};
        if (budget.maxSubmissions > shape.max_size()) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-STORAGE-SIZE";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto object = core::makeUnique<OriginalSubmissionCompletionDomain>(
                ConstructionKey{}, budget, ticket);
            auto result = core::QuotaBoundSharedObjectIdentity::shareOriginal(std::move(object));
            result->self_ = result;
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-ALLOCATION";
            return {};
        }
    }

public:
    static const char* validateOriginal(
        const std::shared_ptr<OriginalSubmissionCompletionDomain>& domain) noexcept
    {
        if (!domain || !domain->hasQuotaBoundSharedOwner(domain))
            return "RHI-SUBMISSION-COMPLETION-OWNER";
        return domain->diagnostic();
    }

    const char* diagnostic() const noexcept
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-SUBMISSION-COMPLETION-THREAD";
        if (resolving_)
            return "RHI-SUBMISSION-COMPLETION-RESOLVING";
        return retainedReadDiagnostic();
    }

private:
    // Read custody must remain inspectable by the original resolver until its
    // journal finishes. This entry never admits mutation or recursively polls.
    const char* retainedReadDiagnostic() const noexcept
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-SUBMISSION-COMPLETION-THREAD";
        if (retired_)
            return "RHI-SUBMISSION-COMPLETION-RETIRED";
        if (const auto* id = device_.diagnostic())
            return id;
        return originalMetadataQuota()->limitBytes() == budget_.maxCpuMetadataBytes
            ? nullptr
            : "RHI-SUBMISSION-COMPLETION-QUOTA";
    }

public:
    Ticket prepareValidatorCommand(const std::shared_ptr<void>& retention,
        Resolve resolve,
        Cancel cancel,
        const Ticket* predecessors,
        std::size_t count,
        const char*& diagnostic)
    {
        diagnostic = this->diagnostic();
        if (diagnostic)
            return {};
        if (!retention || !retention.use_count() || !resolve || !cancel
            || (count && !predecessors)) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-PARTICIPANT";
            return {};
        }
        if (retention.get() == this
            || core::SharedOwnerProjection::sameOwner(retention, self_.lock())
            || core::SharedOwnerProjection::sameOwner(retention, originalMetadataQuota())) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-PARTICIPANT";
            return {};
        }
        for (const auto& slot : slots_)
            if (slot
                && (slot->retention.get() == retention.get()
                    || core::SharedOwnerProjection::sameOwner(slot->retention, retention))) {
                diagnostic = "RHI-SUBMISSION-COMPLETION-DUPLICATE-PARTICIPANT";
                return {};
            }
        if (active_ == budget_.maxSubmissions) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-SLOT-CAPACITY";
            return {};
        }
        if (count > budget_.maxPredecessorLinks - links_) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-LINK-CAPACITY";
            return {};
        }
        const core::pmr::Vector<std::shared_ptr<Record>> shape{&core::defaultCpuMemoryResource()};
        if (count > shape.max_size()) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-STORAGE-SIZE";
            return {};
        }
        for (std::size_t i = 0u; i < count; ++i) {
            if (!belongs(predecessors[i]) || !predecessors[i].node_->accepted) {
                diagnostic = "RHI-SUBMISSION-COMPLETION-PREDECESSOR";
                return {};
            }
            for (std::size_t j = 0u; j < i; ++j)
                if (predecessors[i].node_ == predecessors[j].node_) {
                    diagnostic = "RHI-SUBMISSION-COMPLETION-DUPLICATE-LINK";
                    return {};
                }
        }
        const auto id = identities_.tryAcquire();
        if (!id) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-IDENTITY-EXHAUSTED";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(originalMetadataQuota());
            auto node = core::makeShared<Record>(id, retention, resolve, cancel);
            node->predecessors.reserve(count);
            for (std::size_t i = 0u; i < count; ++i)
                node->predecessors.push_back(predecessors[i].node_);
            for (auto& slot : slots_)
                if (!slot) {
                    slot = node;
                    ++active_;
                    links_ += count;
                    return Ticket{self_, std::move(node)};
                }
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-ALLOCATION";
            return {};
        }
        diagnostic = "RHI-SUBMISSION-COMPLETION-SLOT-CAPACITY";
        return {};
    }

    const char* acceptValidator(const Ticket& ticket) noexcept
    {
        if (const auto* id = diagnostic())
            return id;
        if (!current(ticket) || ticket.node_->accepted)
            return "RHI-SUBMISSION-COMPLETION-COMMAND";
        if (ticket.node_->readPins)
            if (const auto* id = ticket.node_->validateReadPins(ticket.node_->readPins.get()))
                return id;
        ticket.node_->accepted = true;
        ticket.node_->result.store(Result::Pending, std::memory_order_release);
        return nullptr;
    }

    // Thread-safe test/result handoff only. No mutable journal/epoch/quota reads.
    static const char* deliverValidatorResult(const Ticket& ticket, bool success) noexcept
    {
        if (!ticket.node_)
            return "RHI-SUBMISSION-COMPLETION-COMMAND";
        auto pending = Result::Pending;
        if (ticket.node_->result.compare_exchange_strong(pending,
                success ? Result::Succeeded : Result::Failed,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            if (const auto domain = ticket.domain_.lock()) {
                // Pair with the cold deadline wait's predicate lock. Delivery
                // never reads slots, device epochs, journals or admission state.
                {
                    std::scoped_lock lock(domain->resultMutex_);
                }
                domain->resultChanged_.notify_all();
            }
            return nullptr;
        }
        return pending == Result::Succeeded || pending == Result::Failed
            ? "RHI-SUBMISSION-COMPLETION-DUPLICATE-RESULT"
            : "RHI-SUBMISSION-COMPLETION-RESULT-PHASE";
    }

    const char* cancelValidator(const Ticket& ticket) noexcept
    {
        if (const auto* id = diagnostic())
            return id;
        if (!current(ticket) || ticket.node_->accepted
            || ticket.node_->result.load(std::memory_order_acquire) != Result::Unaccepted)
            return "RHI-SUBMISSION-COMPLETION-CANCEL-PHASE";
        resolving_ = true;
        ticket.node_->cancel(ticket.node_->retention.get());
        finish(ticket.node_, Outcome::Cancelled);
        resolving_ = false;
        return nullptr;
    }

    const char* pollValidator() noexcept
    {
        if (const auto* id = diagnostic())
            return id;
        // Every successful pass retires at least one slot. Strictly older
        // predecessor identities form a DAG; at most slots.size passes suffice.
        for (std::size_t pass = 0u; pass < slots_.size(); ++pass) {
            bool progressed = false;
            for (auto& slot : slots_) {
                if (!slot || !slot->accepted)
                    continue;
                const auto result = slot->result.load(std::memory_order_acquire);
                if (result != Result::Succeeded && result != Result::Failed)
                    continue;
                bool failed = result == Result::Failed, waiting = false;
                for (const auto& predecessor : slot->predecessors) {
                    const auto prior = predecessor->published.load(std::memory_order_acquire);
                    waiting |= prior == Outcome::Pending;
                    failed |= prior == Outcome::Failed || prior == Outcome::Cancelled;
                }
                if (waiting && !failed)
                    continue;
                const auto node = slot;
                if (!failed && node->readPins && node->validateReadPins(node->readPins.get()))
                    failed = true;
                resolving_ = true;
                const bool success = node->resolve(node->retention.get(), !failed);
                finish(node, !failed && success ? Outcome::Succeeded : Outcome::Failed);
                resolving_ = false;
                progressed = true;
            }
            if (!progressed)
                break;
        }
        return nullptr;
    }

    const char* retireValidator() noexcept
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-SUBMISSION-COMPLETION-THREAD";
        if (resolving_)
            return "RHI-SUBMISSION-COMPLETION-RESOLVING";
        if (deferredRetirement_)
            return retireValidatorAfterResults();
        retired_ = true;
        resolving_ = true;
        for (auto& slot : slots_)
            if (slot) {
                const auto node = slot;
                if (node->accepted) {
                    (void)node->resolve(node->retention.get(), false);
                    finish(node, Outcome::Failed);
                } else {
                    node->cancel(node->retention.get());
                    finish(node, Outcome::Cancelled);
                }
            }
        resolving_ = false;
        return nullptr;
    }

    // Null validator delayed-result custody only. Real GPU entry stays closed.
    // The result producers must retain this exact domain until they deliver.
    const char* retireValidatorAfterResults() noexcept
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-SUBMISSION-COMPLETION-THREAD";
        if (resolving_)
            return "RHI-SUBMISSION-COMPLETION-RESOLVING";
        if (!device_.terminallyRevoked())
            return "RHI-SUBMISSION-COMPLETION-RETIRE-DEVICE-ACTIVE";
        if (retired_ && !deferredRetirement_)
            return "RHI-SUBMISSION-COMPLETION-RETIRED";
        retired_ = deferredRetirement_ = true;
        resolveRetired();
        return nullptr;
    }

    const char* drainRetiredValidatorUntil(const semantics::ShutdownDrainRequest& request)
    {
        if (owner_ != std::this_thread::get_id())
            return "RHI-SUBMISSION-COMPLETION-THREAD";
        if (resolving_)
            return "RHI-SUBMISSION-COMPLETION-RESOLVING";
        if (!deferredRetirement_ || !device_.terminallyRevoked())
            return "RHI-SUBMISSION-COMPLETION-RETIRE-PHASE";
        const auto now = std::chrono::steady_clock::now();
        if (!request.hasSupportedVersion() || !request.hasDeadline())
            return semantics::kShutdownDrainInvalidDeadlineDiagnostic;
        if (request.deadline <= now)
            return semantics::kShutdownDrainDeadlineExpiredDiagnostic;
        {
            std::unique_lock lock(resultMutex_);
            if (!resultChanged_.wait_until(lock, request.deadline, [this] {
                    return allResultsKnown();
                }))
                return semantics::kShutdownDrainDeadlineExpiredDiagnostic;
        }
        resolveRetired();
        return nullptr;
    }

private:
    bool allResultsKnown() const noexcept
    {
        for (const auto& slot : slots_)
            if (slot && slot->accepted) {
                const auto result = slot->result.load(std::memory_order_acquire);
                if (result != Result::Succeeded && result != Result::Failed)
                    return false;
            }
        return true;
    }

    void resolveRetired() noexcept
    {
        resolving_ = true;
        for (auto& slot : slots_)
            if (slot) {
                const auto node = slot;
                if (node->accepted) {
                    const auto result = node->result.load(std::memory_order_acquire);
                    if (result != Result::Succeeded && result != Result::Failed)
                        continue;
                    (void)node->resolve(node->retention.get(), false);
                    finish(node, Outcome::Failed);
                } else {
                    node->cancel(node->retention.get());
                    finish(node, Outcome::Cancelled);
                }
            }
        resolving_ = false;
    }

    bool belongs(const Ticket& ticket) const noexcept
    {
        const auto domain = ticket.domain_.lock();
        return ticket.node_ && domain.get() == this
            && core::SharedOwnerProjection::sameOwner(domain, self_.lock());
    }

    bool current(const Ticket& ticket) const noexcept
    {
        if (!belongs(ticket))
            return false;
        for (const auto& slot : slots_)
            if (slot == ticket.node_)
                return true;
        return false;
    }

    void finish(const std::shared_ptr<Record>& node, Outcome outcome) noexcept
    {
        if (node->readPins) {
            node->releaseReadPins(node->readPins.get());
            node->readPins.reset();
        }
        node->result.store(Result::Closed, std::memory_order_release);
        node->published.store(outcome, std::memory_order_release);
        links_ -= node->predecessors.size();
        node->predecessors.clear();
        node->retention.reset();
        for (auto& slot : slots_)
            if (slot == node) {
                slot.reset();
                --active_;
                break;
            }
    }

    const Budget budget_;
    const OriginalDeviceLifetime::Ticket device_;
    const std::thread::id owner_;
    std::weak_ptr<OriginalSubmissionCompletionDomain> self_;
    core::pmr::Vector<std::shared_ptr<Record>> slots_{&core::defaultCpuMemoryResource()};
    core::MonotonicIdSequence identities_;
    std::mutex resultMutex_;
    std::condition_variable resultChanged_;
    std::uint64_t active_ = 0u, links_ = 0u;
    bool retired_ = false, resolving_ = false, deferredRetirement_ = false;
};
} // namespace arp::rhi
