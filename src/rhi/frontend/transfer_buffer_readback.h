#pragma once

#include "core/math/unsigned_range.h"
#include "rhi/buffer_readback_range.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi {
// Correlation has the same opaque request/frame/generation semantics for both
// resource kinds. This alias carries no texture layout or execution authority.
using BufferReadbackIdentity = TextureReadbackIdentity;

struct BufferReadbackRequestBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxReadbackBytes = 0u, maxCpuMetadataBytes = 0u;
};

// OriginalBufferReadbackRequest/1 freezes facts from an actual registered lease.
// It retains no GPU object, native buffer, staging, state or completion owner.
// The canonical execution graph must subsequently join that exact registration
// and range; correlation alone does not grant mapping or publication.
class BufferReadbackRequestDeclaration final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class BufferReadbackRequestDeclaration;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr std::uint32_t retainedOwnerCount = 1u;

    BufferReadbackRequestDeclaration(ConstructionKey,
        GpuResourceId source,
        std::uint64_t registration,
        BufferResourceLayout layout,
        BufferReadbackIdentity identity,
        std::uint64_t offset,
        std::uint64_t bytes,
        BufferReadbackRequestBudget budget) noexcept
        : source_(source), registration_(registration), layout_(layout), identity_(identity),
          offset_(offset), bytes_(bytes), budget_(budget)
    {}

    static constexpr auto lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        return semantics::BufferReadbackRequestLowering::lowering(backend, version);
    }

    static constexpr const char* validateBudget(const BufferReadbackRequestBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-BUFFER-READBACK-REQUEST-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-BUFFER-READBACK-REQUEST-OPT-IN";
        return budget.maxReadbackBytes && budget.maxCpuMetadataBytes
            ? nullptr
            : "RHI-BUFFER-READBACK-REQUEST-BUDGET";
    }

    static std::shared_ptr<const BufferReadbackRequestDeclaration> prepareOriginal(
        const std::shared_ptr<const OriginalCommandBufferLease>& source,
        const GpuDevice& device,
        BufferReadbackIdentity identity,
        std::uint64_t offset,
        std::uint64_t bytes,
        const BufferReadbackRequestBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if ((diagnostic = lowering(device.backend()).diagnostic))
            return {};
        if ((diagnostic = OriginalCommandBufferLease::validateOriginal(source)))
            return {};
        if ((diagnostic = device.validateOriginalResourceRegistration(*source->originalObject())))
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-BUFFER-READBACK-REQUEST-QUOTA";
            return {};
        }
        if (core::SharedOwnerProjection::sameOwner(quota, source->originalMetadataQuota())
            || core::cpuAllocationOverlapsQuotaOwner(source.get(), quota)
            || core::cpuAllocationOverlapsQuotaOwner(source->originalObject(), quota)
            || device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-BUFFER-READBACK-REQUEST-QUOTA-DOMAIN";
            return {};
        }
        const auto registration =
            source->originalObject()->originalResourceRegistration().generation();
        if ((diagnostic = validateFields(source->resource(),
                 registration,
                 source->layout(),
                 identity,
                 offset,
                 bytes,
                 budget)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            diagnostic = nullptr;
            return shareOriginal(
                core::makeUnique<BufferReadbackRequestDeclaration>(ConstructionKey{},
                    source->resource(),
                    registration,
                    source->layout(),
                    identity,
                    offset,
                    bytes,
                    budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-BUFFER-READBACK-REQUEST-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const BufferReadbackRequestDeclaration>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-BUFFER-READBACK-REQUEST-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-BUFFER-READBACK-REQUEST-QUOTA";
        return validateFields(source_, registration_, layout_, identity_, offset_, bytes_, budget_);
    }

    const char* matchesOriginalSource(
        const std::shared_ptr<const OriginalCommandBufferLease>& source,
        const GpuDevice& device) const noexcept
    {
        if (const auto* id = matchesOriginalSource(source))
            return id;
        if (const auto* id = device.validateOriginalResourceRegistration(*source->originalObject()))
            return id;
        return nullptr;
    }

    // Retained CPU correspondence only; device-bound admission uses the overload
    // above and the original graph's registered-lifetime validation.
    const char* matchesOriginalSource(
        const std::shared_ptr<const OriginalCommandBufferLease>& source) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (const auto* id = OriginalCommandBufferLease::validateOriginal(source))
            return id;
        if (const auto* id = source->originalObject()->originalResourceRegistration().diagnostic())
            return id;
        return source->resource() == source_
                && source->originalObject()->originalResourceRegistration().generation()
                    == registration_
                && layout_.matches(source->originalObject()->desc())
            ? nullptr
            : "RHI-BUFFER-READBACK-REQUEST-SOURCE";
    }

    GpuResourceId source() const noexcept { return source_; }

    std::uint64_t registrationGeneration() const noexcept { return registration_; }

    const auto& originalLayout() const noexcept { return layout_; }

    BufferReadbackIdentity identity() const noexcept { return identity_; }

    std::uint64_t offsetBytes() const noexcept { return offset_; }

    std::uint64_t byteCapacity() const noexcept { return bytes_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateFields(GpuResourceId source,
        std::uint64_t registration,
        const BufferResourceLayout& layout,
        BufferReadbackIdentity identity,
        std::uint64_t offset,
        std::uint64_t bytes,
        const BufferReadbackRequestBudget& budget) noexcept
    {
        if (!source.isValid() || !registration)
            return "RHI-BUFFER-READBACK-REQUEST-SOURCE";
        if (!identity.requestId || !identity.frameSerial || !identity.resourceGeneration)
            return "RHI-BUFFER-READBACK-REQUEST-IDENTITY";
        if (layout.memory != MemoryUsage::GpuToCpu
            || (layout.usage & ResourceUsage::CopyDst) == ResourceUsage::None)
            return "RHI-BUFFER-READBACK-REQUEST-MEMORY";
        if (!bytes || offset % BufferReadbackRangeContract::offsetAlignment
            || bytes % BufferReadbackRangeContract::sizeAlignment)
            return "RHI-BUFFER-READBACK-REQUEST-ALIGNMENT";
        if (bytes > budget.maxReadbackBytes
            || !core::containsUnsignedRange<std::uint64_t>(0u, layout.sizeBytes, offset, bytes))
            return "RHI-BUFFER-READBACK-REQUEST-RANGE";
        return nullptr;
    }

    const GpuResourceId source_;
    const std::uint64_t registration_;
    const BufferResourceLayout layout_;
    const BufferReadbackIdentity identity_;
    const std::uint64_t offset_, bytes_;
    const BufferReadbackRequestBudget budget_;
};
} // namespace arp::rhi
