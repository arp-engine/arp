#pragma once

#include "rhi/frontend/query_pool_arguments.h"
#include "rhi/gpu_device.h"

namespace arp::rhi {
struct OriginalCommandQueryPoolSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t registrationGeneration = 0u, sourceCpuBytes = 0u;
    std::uint32_t maxRetainedOwners = 0u;
};

// Canonical registered wrapper/input custody. No command or slot-state owner.
class OriginalCommandQueryPoolSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandQueryPoolSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto domain = CommandReferenceDomain::Query;
    static constexpr std::uint32_t retainedOwnerCount = 0u, cpuOwnerSlots = 3u;

    OriginalCommandQueryPoolSource(ConstructionKey,
        std::shared_ptr<const QueryPool> object,
        OriginalCommandQueryPoolSourceBudget budget)
        : object_(std::move(object)), budget_(budget)
    {}

    static const char* validateBudget(const OriginalCommandQueryPoolSourceBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-QUERY-SOURCE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-QUERY-SOURCE-OPT-IN";
        if (!b.registrationGeneration || !b.sourceCpuBytes)
            return "RHI-ORIGINAL-QUERY-SOURCE-BUDGET";
        return b.maxRetainedOwners >= cpuOwnerSlots ? nullptr
                                                    : "RHI-ORIGINAL-QUERY-SOURCE-OWNER-CAPACITY";
    }

    static std::shared_ptr<const OriginalCommandQueryPoolSource> prepareOriginal(
        std::shared_ptr<const QueryPool> object,
        const OriginalCommandQueryPoolSourceBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const GpuDevice& device,
        const char*& diagnostic)
    {
        diagnostic = validateObject(object, budget, quota);
        if (diagnostic)
            return {};
        if ((diagnostic = device.validateOriginalResourceRegistration(*object)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<OriginalCommandQueryPoolSource>(
                ConstructionKey{}, std::move(object), budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-QUERY-SOURCE-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandQueryPoolSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-QUERY-SOURCE-OWNER";
        return validateObject(self->object_, self->budget_, self->originalMetadataQuota());
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandQueryPoolSource>& self,
        const GpuDevice& device) noexcept
    {
        if (const auto* id = validateOriginal(self))
            return id;
        return device.validateOriginalResourceRegistration(*self->object_);
    }

    const QueryPool* originalObject() const noexcept { return object_.get(); }

    const auto& originalPool() const noexcept { return object_; }

    auto originalInput() const noexcept { return object_->originalInput(); }

    const char* independentCpuQuotaDiagnostic(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (const auto* id =
                object_->originalResourceRegistration().independentCpuQuotaDiagnostic(quota))
            return id;
        return !quota
                || core::SharedOwnerProjection::overlapsOwner(
                    quota, object_->originalQueryOwnerQuota())
            ? "RHI-ORIGINAL-QUERY-SOURCE-QUOTA-DOMAIN"
            : nullptr;
    }

    std::shared_ptr<const void> cpuOwner(std::size_t slot) const noexcept
    {
        if (slot == 1u)
            return object_;
        if (slot == 2u)
            return originalInput();
        return {};
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateObject(const std::shared_ptr<const QueryPool>& object,
        const OriginalCommandQueryPoolSourceBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = validateBudget(budget))
            return id;
        if (!quota || quota->limitBytes() != budget.sourceCpuBytes)
            return "RHI-ORIGINAL-QUERY-SOURCE-QUOTA";
        if (!object || !object->matchesOriginalFactoryOwner(object))
            return "RHI-ORIGINAL-QUERY-SOURCE-WRAPPER-OWNER";
        const auto input = object->originalInput();
        if (!input || !input->hasQuotaBoundSharedOwner(input))
            return "RHI-ORIGINAL-QUERY-SOURCE-INPUT";
        if (const auto validation = input->validateDescriptor(); !validation.success)
            return validation.diagnostics;
        if (input->scalarFields().count() != object->count()
            || input->scalarFields().type() != object->type())
            return "RHI-ORIGINAL-QUERY-SOURCE-INPUT";
        const auto ticket = object->originalResourceRegistration();
        if (!ticket.matchesObjectIdentity(object.get(), OriginalRegisteredResourceKind::QueryPool)
            || ticket.generation() != budget.registrationGeneration)
            return "RHI-ORIGINAL-QUERY-SOURCE-GENERATION";
        if (const auto* id = ticket.independentCpuQuotaDiagnostic(quota))
            return id;
        if (!object->originalQueryOwnerQuota()
            || core::SharedOwnerProjection::overlapsOwner(quota, object->originalQueryOwnerQuota())
            || core::SharedOwnerProjection::overlapsOwner(quota, input->originalMetadataQuota()))
            return "RHI-ORIGINAL-QUERY-SOURCE-QUOTA-DOMAIN";
        return nullptr;
    }

    const std::shared_ptr<const QueryPool> object_;
    const OriginalCommandQueryPoolSourceBudget budget_;
};
} // namespace arp::rhi
