#pragma once

#include "core/atomic_capacity.h"
#include "rhi/gpu_device.h"

namespace arp::rhi {

// BindlessFaultDomain/1 owns completion semantics, not shader authorization.
// A lease retains the dynamic owner (including snapshot descriptor/budget data).
class ARP_API BindlessFaultDomain : public core::SharedObjectIdentity {
public:
    virtual ~BindlessFaultDomain() = default;
    virtual std::uint64_t generation() const noexcept = 0;
    virtual GpuBuffer* faultLatchBuffer() const noexcept = 0;
    virtual GpuBuffer* faultReadbackBuffer() const noexcept = 0;
    virtual ValidationResult validate() const = 0;

    // The fixed single writer reservation is a typed, cold-created owner.
    // Original inventory accounting retains and deduplicates this same budget.
    std::shared_ptr<const core::AtomicCapacityBudget> originalSubmissionBudgetOwner() const noexcept
    {
        return submissions_;
    }

    bool originalJournalMode() const noexcept
    {
        return journalOwned_.load(std::memory_order_acquire);
    }

    ResourceState faultLatchPriorState() const noexcept
    {
        return priorState_.load(std::memory_order_acquire);
    }

protected:
    BindlessFaultDomain() = default;

private:
    friend class BindlessFaultSubmissionLease;
    const std::shared_ptr<core::AtomicCapacityBudget> submissions_ =
        core::makeShared<core::AtomicCapacityBudget>(1u);
    mutable std::atomic<ResourceState> priorState_{ResourceState::Undefined};
    mutable std::atomic<bool> journalOwned_{false};
};

struct BindlessFaultDomainDesc {
    std::uint32_t contractVersion = 1u;
    core::pmr::String label{&core::defaultCpuMemoryResource()};
    std::uint64_t maxResidentBytes = 0u;
};

class ARP_API BindlessOwnedFaultDomain final : public BindlessFaultDomain {
    class ConstructionKey {
        friend class BindlessOwnedFaultDomain;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    explicit BindlessOwnedFaultDomain(ConstructionKey) {}
    enum class Role { Latch, Readback };
    static constexpr std::uint64_t minimumBytes = 2u * sizeof(std::uint32_t);
    static BufferDesc descriptor(Role role);
    static std::shared_ptr<BindlessOwnedFaultDomain> create(
        GpuDevice&, const BindlessFaultDomainDesc&, core::pmr::String& diagnostic);

    std::uint64_t generation() const noexcept override { return generation_; }

    GpuBuffer* faultLatchBuffer() const noexcept override { return latch_.get(); }

    GpuBuffer* faultReadbackBuffer() const noexcept override { return readback_.get(); }

    ValidationResult validate() const override;
    const char* storageDiagnostic() const noexcept;

    std::uint64_t reportedResidentBytes() const noexcept { return residentBytes_; }

private:
    core::UniquePtr<GpuBuffer> latch_, readback_;
    std::uint64_t generation_ = 0u, residentBytes_ = 0u;
};

// Exclusively reserved from clear through actual GPU copy/readback completion.
class ARP_API BindlessFaultSubmissionLease final {
public:
    BindlessFaultSubmissionLease() = default;
    BindlessFaultSubmissionLease(const BindlessFaultSubmissionLease&) = delete;
    BindlessFaultSubmissionLease& operator=(const BindlessFaultSubmissionLease&) = delete;

    BindlessFaultSubmissionLease(BindlessFaultSubmissionLease&& other) noexcept
        : domain_(std::move(other.domain_)), originalJournal_(other.originalJournal_)
    {}

    BindlessFaultSubmissionLease& operator=(BindlessFaultSubmissionLease&& other) noexcept
    {
        if (this != &other) {
            release();
            domain_ = std::move(other.domain_);
            originalJournal_ = other.originalJournal_;
        }
        return *this;
    }

    ~BindlessFaultSubmissionLease() { release(); }

    static BindlessFaultSubmissionLease acquire(
        std::shared_ptr<const BindlessFaultDomain>, core::pmr::String& diagnostic);

    // Original command users reserve ownership only. Resource states belong to
    // their canonical journal; this lease cannot install a legacy prior state.
    static BindlessFaultSubmissionLease acquireOriginalJournal(
        std::shared_ptr<const BindlessFaultDomain>, core::pmr::String& diagnostic);

    bool originalJournalOwned() const noexcept { return domain_ && originalJournal_; }

    const std::shared_ptr<const BindlessFaultDomain>& originalDomain() const noexcept
    {
        return domain_;
    }

    bool valid() const noexcept { return static_cast<bool>(domain_); }

    const GpuBuffer* faultLatchBuffer() const noexcept
    {
        return domain_ ? domain_->faultLatchBuffer() : nullptr;
    }

    ResourceState faultLatchPriorState() const noexcept
    {
        return domain_ ? domain_->faultLatchPriorState() : ResourceState::Undefined;
    }

    void markFaultReadbackCompleted() noexcept;
    void release() noexcept;

private:
    static BindlessFaultSubmissionLease acquireImpl(
        std::shared_ptr<const BindlessFaultDomain>, bool originalJournal, core::pmr::String&);

    explicit BindlessFaultSubmissionLease(std::shared_ptr<const BindlessFaultDomain> domain)
        : domain_(std::move(domain))
    {}

    std::shared_ptr<const BindlessFaultDomain> domain_;
    bool originalJournal_ = false;
};

} // namespace arp::rhi
