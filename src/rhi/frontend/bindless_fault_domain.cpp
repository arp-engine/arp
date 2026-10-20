#include "rhi/frontend/bindless_fault_domain.h"

namespace arp::rhi {

BufferDesc BindlessOwnedFaultDomain::descriptor(Role role)
{
    switch (role) {
    case Role::Latch:
        return {{},
            sizeof(std::uint32_t),
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderWrite};
    case Role::Readback:
        return {{},
            sizeof(std::uint32_t),
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst};
    }
    return {};
}

std::shared_ptr<BindlessOwnedFaultDomain> BindlessOwnedFaultDomain::create(
    GpuDevice& device, const BindlessFaultDomainDesc& desc, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (desc.contractVersion != 1u || !desc.maxResidentBytes) {
        diagnostic = "RHI-BINDLESS-FAULT-DOMAIN-DESCRIPTOR";
        return {};
    }
    auto result = core::makeShared<BindlessOwnedFaultDomain>(ConstructionKey{});
    auto latch = descriptor(Role::Latch), readback = descriptor(Role::Readback);
    latch.label = desc.label;
    readback.label = desc.label;
    result->latch_ = device.createBuffer(latch);
    result->readback_ = device.createBuffer(readback);
    if (!result->latch_ || !result->readback_) {
        diagnostic = "RHI-BINDLESS-FAULT-DOMAIN-RESOURCE-CREATION-FAILED";
        return {};
    }
    for (const auto* buffer : {result->latch_.get(), result->readback_.get()}) {
        const auto bytes = buffer->allocation().estimatedSizeBytes;
        if (bytes < buffer->desc().sizeBytes) {
            diagnostic = "RHI-BINDLESS-FAULT-DOMAIN-ACCOUNTING-MISSING";
            return {};
        }
        if (bytes > desc.maxResidentBytes - result->residentBytes_) {
            diagnostic = "RHI-BINDLESS-FAULT-DOMAIN-BUDGET";
            return {};
        }
        result->residentBytes_ += bytes;
    }
    result->latch_->sealHostWrites();
    result->latch_->sealGpuTransferWrites();
    result->readback_->sealHostWrites();
    result->generation_ = allocateGpuResourceId().value;
    if (!result->generation_) {
        diagnostic = "RHI-BINDLESS-GENERATION-EXHAUSTED";
        return {};
    }
    const auto valid = result->validate();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    return result;
}

const char* BindlessOwnedFaultDomain::storageDiagnostic() const noexcept
{
    if (!generation_ || !latch_ || !readback_ || !latch_->id().isValid()
        || !readback_->id().isValid() || latch_->id() == readback_->id()
        || latch_->desc().sizeBytes != sizeof(std::uint32_t)
        || readback_->desc().sizeBytes != sizeof(std::uint32_t)
        || latch_->desc().usage
            != (ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst)
        || readback_->desc().usage != ResourceUsage::CopyDst
        || latch_->desc().memory != MemoryUsage::GpuOnly
        || readback_->desc().memory != MemoryUsage::GpuToCpu || !latch_->hostWritesSealed()
        || !latch_->gpuTransferWritesSealed() || !readback_->hostWritesSealed())
        return "RHI-BINDLESS-FAULT-DOMAIN-INVALID";
    return nullptr;
}

ValidationResult BindlessOwnedFaultDomain::validate() const
{
    if (const auto* id = storageDiagnostic())
        return {false, id};
    return {true, {}};
}

BindlessFaultSubmissionLease BindlessFaultSubmissionLease::acquire(
    std::shared_ptr<const BindlessFaultDomain> domain, core::pmr::String& diagnostic)
{
    return acquireImpl(std::move(domain), false, diagnostic);
}

BindlessFaultSubmissionLease BindlessFaultSubmissionLease::acquireImpl(
    std::shared_ptr<const BindlessFaultDomain> domain,
    bool originalJournal,
    core::pmr::String& diagnostic)
{
    if (domain && !domain->sharedOwnerMatches(domain)) {
        diagnostic = "RHI-BINDLESS-FAULT-SUBMISSION-OWNER-MISMATCH";
        return {};
    }
    // Preserve the published snapshot diagnostic while broadening ownership.
    if (!domain || !domain->generation() || !domain->faultLatchBuffer()
        || !domain->validate().success) {
        diagnostic = "RHI-BINDLESS-FAULT-SUBMISSION-SNAPSHOT-INVALID";
        return {};
    }
    if (!domain->submissions_->tryAcquire(1u)) {
        diagnostic = "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT";
        return {};
    }
    // Recheck under the exclusive reservation: a legacy waiter cannot race a
    // successful original acquisition and revive the old state authority.
    if (!originalJournal && domain->originalJournalMode()) {
        domain->submissions_->release(1u);
        diagnostic = "RHI-BINDLESS-FAULT-SUBMISSION-ORIGINAL-JOURNAL";
        return {};
    }
    if (originalJournal)
        domain->journalOwned_.store(true, std::memory_order_release);
    diagnostic.clear();
    auto lease = BindlessFaultSubmissionLease(std::move(domain));
    lease.originalJournal_ = originalJournal;
    return lease;
}

BindlessFaultSubmissionLease BindlessFaultSubmissionLease::acquireOriginalJournal(
    std::shared_ptr<const BindlessFaultDomain> domain, core::pmr::String& diagnostic)
{
    return acquireImpl(std::move(domain), true, diagnostic);
}

void BindlessFaultSubmissionLease::release() noexcept
{
    if (domain_) {
        domain_->submissions_->release(1u);
        domain_.reset();
    }
}

void BindlessFaultSubmissionLease::markFaultReadbackCompleted() noexcept
{
    if (domain_ && !originalJournal_)
        domain_->priorState_.store(ResourceState::CopySrc, std::memory_order_release);
}

} // namespace arp::rhi
