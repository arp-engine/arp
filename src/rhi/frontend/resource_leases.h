#pragma once

#include "core/atomic_capacity.h"
#include "core/cold_owned_object_slot.h"
#include "core/quota_bound_shared_identity.h"
#include "core/shared_owner_projection.h"
#include "rhi/frontend/command_model.h"
#include "rhi/frontend/resource_binding_layout.h"
#include "rhi/frontend/resource_texture_resource_layout.h"

#include <array>

// region original_command_resource_lease
namespace arp::rhi {
struct OriginalCommandResourceLeaseBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// Typed CPU object lifetime prerequisite for GenericOriginalCommandAdmission/1.
// Capturing an ID/layout is not proof the caller object is engine-created or
// registered. Native admission must bind this exact wrapper to the original
// logical device registry; no inferred device/generation/state facts here.
template <class T>
class OriginalCommandResourceLease final : public core::QuotaBoundSharedObjectIdentity {
    static_assert(std::is_same_v<T, GpuBuffer> || std::is_same_v<T, GpuTexture>
        || std::is_same_v<T, GpuTextureView> || std::is_same_v<T, GpuSampler>);

    class ConstructionKey {
        friend class OriginalCommandResourceLease;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    // Optional resource accounting custody, separate from Source metadata quota
    // and original wrapper authority. Shared reservations are projected once
    // by the canonical inventory, even when several original children retain it.
    using CapacityOwners = std::array<std::shared_ptr<const core::AtomicCapacityLease>, 2u>;
    static constexpr std::size_t capacityOwnerSlots = 4u;
    using Layout = std::conditional_t<std::is_same_v<T, GpuBuffer>,
        BufferResourceLayout,
        std::conditional_t<std::is_same_v<T, GpuTexture>,
            TextureResourceLayout,
            std::conditional_t<std::is_same_v<T, GpuTextureView>,
                TextureViewResourceLayout,
                SamplerResourceLayout>>>;
    static constexpr CommandReferenceDomain domain = std::is_same_v<T, GpuBuffer>
        ? CommandReferenceDomain::Buffer
        : std::is_same_v<T, GpuTexture>     ? CommandReferenceDomain::Texture
        : std::is_same_v<T, GpuTextureView> ? CommandReferenceDomain::TextureView
                                            : CommandReferenceDomain::Sampler;
    static constexpr std::uint32_t retainedOwnerCount = 2u; // Lease plus adopted wrapper.

    OriginalCommandResourceLease(ConstructionKey,
        GpuResourceId id,
        Layout layout,
        std::uint64_t metadataBytes,
        GpuResourceId parent,
        CapacityOwners capacityOwners = {})
        : id_(id), layout_(layout), metadataBytes_(metadataBytes), parent_(parent),
          capacityOwners_(std::move(capacityOwners))
    {}

    static const char* validateBudget(const OriginalCommandResourceLeaseBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-SOURCE-LEASE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-SOURCE-LEASE-OPT-IN";
        if (!b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-SOURCE-LEASE-BUDGET";
        if (b.maxRetainedOwners < retainedOwnerCount)
            return "RHI-ORIGINAL-SOURCE-LEASE-OWNER-CAPACITY";
        return nullptr;
    }

    static std::shared_ptr<OriginalCommandResourceLease> prepareColdOriginal(
        const core::UniquePtr<T>& source,
        const OriginalCommandResourceLeaseBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic,
        const CapacityOwners& capacityOwners = {})
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-ORIGINAL-SOURCE-LEASE-QUOTA";
            return {};
        }
        if (!source) {
            diagnostic = "RHI-ORIGINAL-SOURCE-LEASE-OBJECT-MISSING";
            return {};
        }
        for (const auto& owner : capacityOwners)
            if (owner && !owner->charged()) {
                diagnostic = "RHI-ORIGINAL-SOURCE-LEASE-CAPACITY-RELEASED";
                return {};
            }
        const auto id = source->id();
        if (!id.isValid()) {
            diagnostic = "RHI-ORIGINAL-SOURCE-LEASE-IDENTITY";
            return {};
        }
        const auto layout = Layout::capture(source->desc());
        const auto parent = parentOf(*source);
        if constexpr (std::is_same_v<T, GpuTextureView>)
            if (!parent.isValid()) {
                diagnostic = "RHI-ORIGINAL-SOURCE-LEASE-PARENT-IDENTITY";
                return {};
            }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto lease = shareOriginal(core::makeUnique<OriginalCommandResourceLease>(
                ConstructionKey{}, id, layout, budget.maxCpuMetadataBytes, parent, capacityOwners));
            if ((diagnostic = lease->validateObject(*source)))
                return {};
            if (!lease->source_.bindCold(source)) {
                diagnostic = "RHI-ORIGINAL-SOURCE-LEASE-COLD-CANDIDATE";
                return {};
            }
            return lease;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-SOURCE-LEASE-ALLOCATION";
            return {};
        }
    }

    static std::shared_ptr<const OriginalCommandResourceLease> prepareOriginal(
        core::UniquePtr<T>& source,
        const OriginalCommandResourceLeaseBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        auto lease = prepareColdOriginal(source, budget, std::move(quota), diagnostic);
        if (!lease || (diagnostic = adoptOriginal(lease, source)))
            return {};
        return lease;
    }

    static const char* validateColdOriginal(
        const std::shared_ptr<const OriginalCommandResourceLease>& self,
        const T& candidate) noexcept
    {
        if (const auto* id = validateOwner(self))
            return id;
        if (!self->source_.matchesCold(candidate))
            return "RHI-ORIGINAL-SOURCE-LEASE-COLD-CANDIDATE";
        return self->validateObject(candidate);
    }

    static const char* adoptOriginal(const std::shared_ptr<OriginalCommandResourceLease>& self,
        core::UniquePtr<T>& source) noexcept
    {
        if (!source)
            return "RHI-ORIGINAL-SOURCE-LEASE-OBJECT-MISSING";
        if (const auto* id = validateColdOriginal(self, *source))
            return id;
        return self->source_.adoptOriginal(source,
                   [&](const T& candidate) noexcept {
                       return !self->validateObject(candidate);
                   })
            ? nullptr
            : "RHI-ORIGINAL-SOURCE-LEASE-COLD-CANDIDATE";
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandResourceLease>& self) noexcept
    {
        if (const auto* id = validateOwner(self))
            return id;
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateQuota())
            return id;
        const auto* object = source_.originalObject();
        return object ? validateObject(*object) : "RHI-ORIGINAL-SOURCE-LEASE-OBJECT-MISSING";
    }

    const T* originalObject() const noexcept { return source_.originalObject(); }

    GpuResourceId resource() const noexcept { return id_; }

    const Layout& layout() const noexcept { return layout_; }

    GpuResourceId parentResource() const noexcept { return parent_; }

    std::shared_ptr<const core::AtomicCapacityLease> originalCapacityLease(
        std::size_t index) const noexcept
    {
        return index < capacityOwners_.size() ? capacityOwners_[index] : nullptr;
    }

    std::shared_ptr<const core::AtomicCapacityBudget> originalCapacityBudget(
        std::size_t index) const noexcept
    {
        const auto lease = originalCapacityLease(index);
        return lease ? lease->originalBudgetOwner() : nullptr;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateOwner(
        const std::shared_ptr<const OriginalCommandResourceLease>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-SOURCE-LEASE-OWNER";
        return self->validateQuota();
    }

    const char* validateQuota() const noexcept
    {
        for (const auto& owner : capacityOwners_)
            if (owner && !owner->charged())
                return "RHI-ORIGINAL-SOURCE-LEASE-CAPACITY-RELEASED";
        return originalMetadataQuota() && originalMetadataQuota()->limitBytes() == metadataBytes_
            ? nullptr
            : "RHI-ORIGINAL-SOURCE-LEASE-QUOTA";
    }

    const char* validateObject(const T& object) const noexcept
    {
        return object.id() == id_ && layout_.matches(object.desc()) && parentOf(object) == parent_
            ? nullptr
            : "RHI-ORIGINAL-SOURCE-LEASE-OBJECT-MISMATCH";
    }

    static GpuResourceId parentOf(const T& object) noexcept
    {
        if constexpr (std::is_same_v<T, GpuTextureView>)
            return object.textureId();
        else
            return {};
    }

    const GpuResourceId id_;
    const Layout layout_;
    const std::uint64_t metadataBytes_;
    const GpuResourceId parent_;
    const CapacityOwners capacityOwners_;
    core::ColdOwnedObjectSlot<T> source_;
};

using OriginalCommandBufferLease = OriginalCommandResourceLease<GpuBuffer>;
using OriginalCommandTextureLease = OriginalCommandResourceLease<GpuTexture>;
using OriginalCommandTextureViewLease = OriginalCommandResourceLease<GpuTextureView>;
using OriginalCommandSamplerLease = OriginalCommandResourceLease<GpuSampler>;

// endregion original_command_resource_lease

// region original_command_texture_view_source

// Canonical CPU view-parent ownership binder. A view lease alone cannot enter
// the source table; its exact original parent must also be retained there.
// Declared IDs/full layouts remain independent of registry/state/native proof.
class OriginalCommandTextureViewSource final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class OriginalCommandTextureViewSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr auto domain = CommandReferenceDomain::TextureView;
    static constexpr std::uint32_t retainedOwnerCount =
        3u; // Exclusive contribution to a complete source table.
    static constexpr std::uint32_t completeRetainedOwnerCount =
        retainedOwnerCount + OriginalCommandTextureLease::retainedOwnerCount;

    OriginalCommandTextureViewSource(ConstructionKey,
        std::shared_ptr<const OriginalCommandTextureViewLease> view,
        std::shared_ptr<const OriginalCommandTextureLease> parent)
        : view_(std::move(view)), parent_(std::move(parent))
    {}

    static std::shared_ptr<const OriginalCommandTextureViewSource> prepareOriginal(
        std::shared_ptr<const OriginalCommandTextureViewLease> view,
        std::shared_ptr<const OriginalCommandTextureLease> parent,
        const OriginalCommandResourceLeaseBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = OriginalCommandTextureViewLease::validateBudget(budget);
        if (diagnostic)
            return {};
        if (budget.maxRetainedOwners < completeRetainedOwnerCount) {
            diagnostic = "RHI-ORIGINAL-VIEW-SOURCE-OWNER-CAPACITY";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-ORIGINAL-VIEW-SOURCE-QUOTA";
            return {};
        }
        if ((diagnostic = validateSources(view, parent, quota)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<OriginalCommandTextureViewSource>(
                ConstructionKey{}, std::move(view), std::move(parent)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-VIEW-SOURCE-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandTextureViewSource>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-VIEW-SOURCE-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        return validateSources(view_, parent_, originalMetadataQuota());
    }

    const GpuTextureView* originalObject() const noexcept { return view_->originalObject(); }

    GpuResourceId resource() const noexcept { return view_->resource(); }

    const auto& originalViewLease() const noexcept { return view_; }

    const auto& originalParentLease() const noexcept { return parent_; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateSources(
        const std::shared_ptr<const OriginalCommandTextureViewLease>& view,
        const std::shared_ptr<const OriginalCommandTextureLease>& parent,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        if (const auto* id = OriginalCommandTextureViewLease::validateOriginal(view))
            return id;
        if (const auto* id = OriginalCommandTextureLease::validateOriginal(parent))
            return id;
        if (!quota || !core::SharedOwnerProjection::sameOwner(quota, view->originalMetadataQuota())
            || !core::SharedOwnerProjection::sameOwner(quota, parent->originalMetadataQuota()))
            return "RHI-ORIGINAL-VIEW-SOURCE-QUOTA";
        return view->parentResource() == parent->resource() ? nullptr
                                                            : "RHI-ORIGINAL-VIEW-SOURCE-PARENT";
    }

    const std::shared_ptr<const OriginalCommandTextureViewLease> view_;
    const std::shared_ptr<const OriginalCommandTextureLease> parent_;
};
} // namespace arp::rhi

// endregion original_command_texture_view_source
