#pragma once

#include "core/quota_bound_shared_identity.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"

namespace arp::rhi_webgpu {

// WebOrdinaryResourceProjection/1. No physical descriptor execution authority.
struct WebGpuOrdinaryResourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    rhi::CommandRetentionBudget retention;
    std::uint32_t maxResources = 0u;
    std::uint32_t maxTextureViews = 0u;
    std::uint32_t maxSamplers = 0u;
    std::uint64_t maxRetainedAllocationBytes = 0u;
};

class WebGpuOrdinaryResourceObservation final {
public:
    const char* capture(rhi::GpuResourceId,
        const WebGpuRhiRegisteredResource&,
        const WebGpuRhiRegisteredResource* parent) noexcept;
    bool matches() const noexcept;

    bool sameOwners(const WebGpuOrdinaryResourceObservation& other) const noexcept
    {
        if (id_ != other.id_ || backing_.id != other.backing_.id
            || ownerCount() != other.ownerCount())
            return false;
        const auto a = owners(), b = other.owners();
        for (std::size_t index = 0u; index < ownerCount(); ++index)
            if (!core::SharedOwnerProjection::sameOwner(a[index], b[index]))
                return false;
        return true;
    }

    std::array<std::shared_ptr<const void>, 3> owners() const noexcept
    {
        if (sampler_)
            return {sampler_, nullptr, nullptr};
        const auto backingOwners = backing_.owners();
        return {backingOwners[0], backingOwners[1], backingOwners[2] ? backingOwners[2] : view_};
    }

    std::size_t ownerCount() const noexcept { return sampler_ ? 1u : 3u; }

    rhi::GpuResourceId id() const noexcept { return id_; }

    const auto& backing() const noexcept { return backing_; }

    const auto& view() const noexcept { return view_; }

    const auto& sampler() const noexcept { return sampler_; }

    const auto& viewDesc() const noexcept { return viewDesc_; }

    bool sharesAllocationWith(const WebGpuOrdinaryResourceObservation& other) const noexcept
    {
        const auto& a = backing_.allocationOwner();
        const auto& b = other.backing_.allocationOwner();
        return a && b && (a == b || core::SharedOwnerProjection::sameControlBlock(a, b));
    }

    bool sameAllocationBacking(const WebGpuOrdinaryResourceObservation& other) const noexcept
    {
        return core::SharedOwnerProjection::sameOwner(
                   backing_.allocationOwner(), other.backing_.allocationOwner())
            && core::SharedOwnerProjection::sameOwner(backing_.buffer, other.backing_.buffer)
            && core::SharedOwnerProjection::sameOwner(backing_.texture, other.backing_.texture)
            && backing_.allocationBytes == other.backing_.allocationBytes;
    }

    void* nativeDevice() const noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return device_;
#else
        return nullptr;
#endif
    }

private:
    WebGpuTransferResourceObservation backing_;
    rhi::GpuResourceId id_;
    std::shared_ptr<WebGpuNativeTextureViewPayload> view_;
    std::shared_ptr<WebGpuNativeSamplerPayload> sampler_;
    rhi::TextureViewDesc viewDesc_;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUTextureView nativeView_ = nullptr;
    WGPUSampler nativeSampler_ = nullptr;
    WGPUDevice device_ = nullptr;
#endif
};

class WebGpuOrdinaryResourceProjection final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOrdinaryResourceProjection;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOrdinaryResourceProjection(
        ConstructionKey, core::UniquePtr<rhi::common::CommandRetentionStorage> storage)
        : storage_(std::move(storage))
    {}

    ~WebGpuOrdinaryResourceProjection() { storage_->abandonUnsubmitted(); }

    static const char* validateBudget(const WebGpuOrdinaryResourceBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-RESOURCE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-RESOURCE-OPT-IN";
        if (const auto* id =
                rhi::semantics::CommandRetentionBudgetLowering::validateIntent(budget.retention))
            return id;
        if (budget.retention.maxQueryPools || budget.retention.maxTimestampWrites
            || budget.retention.maxPipelineStatisticsWrites)
            return "RHI-WEBGPU-ORDINARY-RESOURCE-QUERY-UNSUPPORTED";
        return nullptr;
    }

    static core::UniquePtr<WebGpuOrdinaryResourceProjection> prepare(
        const WebGpuRhiResourceRegistry& registry,
        const rhi::GpuResourceId* ids,
        std::size_t count,
        const WebGpuOrdinaryResourceBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if ((count && !ids) || count > budget.maxResources) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-MANIFEST-CAPACITY";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.retention.maxCpuMetadataBytes) {
            diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto storage =
                rhi::common::CommandRetentionStorage::prepare(budget.retention, quota, diagnostic);
            if (!storage)
                return {};
            auto result = core::makeUnique<WebGpuOrdinaryResourceProjection>(
                ConstructionKey{}, std::move(storage));
            result->resources_.reserve(budget.maxResources);
            result->resources_.resize(count);
            diagnostic = registry.observeOrdinaryResources(ids, count, result->resources_.data());
            if (diagnostic)
                return {};
            std::size_t views = 0u, samplers = 0u;
            for (std::size_t index = 0u; index < count; ++index) {
                const auto& resource = result->resources_[index];
                if (resource.nativeDevice() != result->resources_.front().nativeDevice()) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-DEVICE-MISMATCH";
                    return {};
                }
                if ((resource.view() && ++views > budget.maxTextureViews)
                    || (resource.sampler() && ++samplers > budget.maxSamplers)) {
                    diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-NATIVE-OBJECT-CAPACITY";
                    return {};
                }
                bool sharedAllocation = false;
                const auto& backing = resource.backing();
                for (std::size_t previous = 0u; backing.allocationOwner() && previous < index;
                    ++previous) {
                    const auto& earlier = result->resources_[previous];
                    if (!resource.sharesAllocationWith(earlier))
                        continue;
                    if (!resource.sameAllocationBacking(earlier)) {
                        diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-ALLOCATION-ALIAS";
                        return {};
                    }
                    sharedAllocation = true;
                }
                if (!sharedAllocation) {
                    if (backing.allocationBytes
                        > budget.maxRetainedAllocationBytes - result->retainedBytes_) {
                        diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-ALLOCATION-BYTE-CAPACITY";
                        return {};
                    }
                    result->retainedBytes_ += backing.allocationBytes;
                }
                const auto owners = resource.owners();
                diagnostic =
                    result->storage_->retainResources(owners.data(), resource.ownerCount());
                if (diagnostic)
                    return {};
            }
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-PREPARATION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-ORDINARY-RESOURCE-PREPARATION-ALLOCATION";
            return {};
        }
    }

    const WebGpuOrdinaryResourceObservation* find(rhi::GpuResourceId id) const noexcept
    {
        for (const auto& resource : resources_)
            if (resource.id() == id)
                return &resource;
        return nullptr;
    }

    bool matches() const noexcept
    {
        for (const auto& resource : resources_)
            if (!resource.matches())
                return false;
        return true;
    }

    const auto& resources() const noexcept { return resources_; }

    const auto& storage() const noexcept { return *storage_; }

    std::uint64_t retainedBytes() const noexcept { return retainedBytes_; }

private:
    core::UniquePtr<rhi::common::CommandRetentionStorage> storage_;
    core::pmr::Vector<WebGpuOrdinaryResourceObservation> resources_{
        &core::defaultCpuMemoryResource()};
    std::uint64_t retainedBytes_ = 0u;
};

} // namespace arp::rhi_webgpu
