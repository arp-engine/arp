#pragma once
#include "core/ownership/quota_bound_shared_identity.h"
#include "core/ownership/shared_allocation_projection.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/command_retention_storage.h"

#include <array>

namespace arp::rhi_webgpu {

// WebOrdinaryTransferManifest/1. Every limit is caller-owned and independent.
struct WebGpuOrdinaryTransferBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    rhi::CommandRetentionBudget retention;
    std::uint32_t maxTransferResources = 0u;
    std::uint64_t maxRetainedResourceBytes = 0u;
};

struct WebGpuOrdinaryTransferCompletionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingSubmissions = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class WebGpuTransferResourceObservation final {
    // Keep the original accounting owner alive after dependent payloads even
    // when an invalid backend mutation has replaced their token field.
    std::shared_ptr<const void> allocation_;

public:
    const char* capture(
        rhi::GpuResourceId id, const WebGpuRhiRegisteredResource& resource) noexcept;
    bool matches() const noexcept;
    bool sameOwnersAndLayout(const WebGpuTransferResourceObservation&) const noexcept;

    std::array<std::shared_ptr<const void>, 3> owners() const noexcept
    {
        return buffer ? std::array<std::shared_ptr<const void>, 3>{allocation_, writeSeal, buffer}
                      : std::array<std::shared_ptr<const void>, 3>{allocation_, texture, nullptr};
    }

    std::size_t ownerCount() const noexcept { return buffer ? 3u : 2u; }

    const auto& allocationOwner() const noexcept { return allocation_; }

    core::SharedAllocationProjection::Record allocationRecord() const noexcept
    {
        return {
            allocation_, buffer ? std::shared_ptr<const void>(buffer) : texture, allocationBytes};
    }

    rhi::GpuResourceId id;
    std::shared_ptr<WebGpuNativeBufferPayload> buffer;
    std::shared_ptr<WebGpuNativeTexturePayload> texture;
    // Labels are deliberately absent; these descriptions contain scalar facts only.
    rhi::BufferDesc bufferDesc;
    rhi::TextureDesc textureDesc;
    std::shared_ptr<const rhi::GpuBufferWriteSealState> writeSeal;
    std::uint64_t allocationBytes = 0u;

private:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUBuffer nativeBuffer_ = nullptr;
    WGPUTexture nativeTexture_ = nullptr;
    WGPUDevice device_ = nullptr;
#endif
};

class WebGpuOrdinaryTransferRetention final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOrdinaryTransferRetention;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOrdinaryTransferRetention(ConstructionKey,
        WebGpuOrdinaryTransferBudget budget,
        core::UniquePtr<rhi::common::CommandRetentionStorage> storage)
        : budget_(budget), storage_(std::move(storage))
    {}

    ~WebGpuOrdinaryTransferRetention() { storage_->abandonUnsubmitted(); }

    static const char* validateBudget(const WebGpuOrdinaryTransferBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-TRANSFER-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-TRANSFER-PROTOTYPE-OPT-IN";
        if (const auto* id =
                rhi::semantics::CommandRetentionBudgetLowering::validateIntent(budget.retention))
            return id;
        if (budget.retention.maxQueryPools || budget.retention.maxTimestampWrites
            || budget.retention.maxPipelineStatisticsWrites)
            return "RHI-WEBGPU-TRANSFER-QUERY-UNSUPPORTED";
        return nullptr;
    }

    static core::UniquePtr<WebGpuOrdinaryTransferRetention> prepare(
        const WebGpuRhiResourceRegistry& registry,
        const rhi::GpuResourceId* ids,
        std::size_t count,
        const WebGpuOrdinaryTransferBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if ((count && !ids) || count > budget.maxTransferResources) {
            diagnostic = "RHI-WEBGPU-TRANSFER-MANIFEST-CAPACITY";
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
            auto result = core::makeUnique<WebGpuOrdinaryTransferRetention>(
                ConstructionKey{}, budget, std::move(storage));
            result->resources_.reserve(budget.maxTransferResources);
            result->resources_.resize(count);
            diagnostic = registry.observeTransferResources(ids, count, result->resources_.data());
            if (diagnostic)
                return {};
            for (const auto& resource : result->resources_) {
                if (resource.allocationBytes
                    > budget.maxRetainedResourceBytes - result->retainedBytes_) {
                    diagnostic = "RHI-WEBGPU-TRANSFER-RESOURCE-BYTE-CAPACITY";
                    return {};
                }
                result->retainedBytes_ += resource.allocationBytes;
                const auto owners = resource.owners();
                diagnostic =
                    result->storage_->retainResources(owners.data(), resource.ownerCount());
                if (diagnostic)
                    return {};
            }
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-TRANSFER-PREPARATION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-TRANSFER-PREPARATION-ALLOCATION";
            return {};
        }
    }

    const WebGpuTransferResourceObservation* find(rhi::GpuResourceId id) const noexcept
    {
        for (const auto& resource : resources_)
            if (resource.id == id)
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

    auto& storage() noexcept { return *storage_; }

    const auto& resources() const noexcept { return resources_; }

    std::uint64_t retainedBytes() const noexcept { return retainedBytes_; }

private:
    const WebGpuOrdinaryTransferBudget budget_;
    core::UniquePtr<rhi::common::CommandRetentionStorage> storage_;
    core::pmr::Vector<WebGpuTransferResourceObservation> resources_{
        &core::defaultCpuMemoryResource()};
    std::uint64_t retainedBytes_ = 0u;
};

} // namespace arp::rhi_webgpu
