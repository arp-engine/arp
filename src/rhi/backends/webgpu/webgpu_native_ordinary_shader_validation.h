#pragma once
#include "core/runtime/completion_retention_pool.h"
#include "rhi/backends/webgpu/native_validation.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_physical_groups.h"

namespace arp::rhi_webgpu {
struct WebGpuOrdinaryShaderValidationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingProbes = 0u, maxPendingCallbacks = 0u, maxGroups = 0u,
                  maxVertexStreams = 0u;
    std::uint32_t maxCommandEncoders = 0u, maxComputePasses = 0u, maxCommandBuffers = 0u;
    std::uint32_t maxBundleEncoders = 0u, maxBundles = 0u, maxColorTargets = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class WebGpuOrdinaryShaderSizeValidationOwner;

class WebGpuOrdinaryShaderSizeValidation final : public core::QuotaBoundSharedObjectIdentity {
public:
    enum class Status { Pending, Validated, Rejected, DeviceLost };

    class ConstructionKey {
        friend class WebGpuOrdinaryShaderSizeValidationOwner;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    WebGpuOrdinaryShaderSizeValidation(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryPhysicalGroups> groups,
        core::UniquePtr<WebGpuOrdinaryResourceProjection> vertices,
        std::shared_ptr<const std::atomic<bool>> deviceLost)
        : groups_(std::move(groups)), vertices_(std::move(vertices)),
          deviceLost_(std::move(deviceLost))
    {}

    Status status() const noexcept
    {
        return deviceLost_->load(std::memory_order_acquire) ? Status::DeviceLost : status_;
    }

    const char* diagnostic() const noexcept;

    // Static size validation alone never opens recording/queue execution.
    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = diagnostic())
            return id;
        return "RHI-WEBGPU-ORDINARY-GROUP-RECORDING-UNPREPARED";
    }

    const auto& originalGroups() const noexcept { return groups_; }

private:
    friend class WebGpuOrdinaryScalarComputeRange;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinaryGraphicsTriangleRange;
    friend class WebGpuOrdinaryGraphicsAttachments;
    friend class WebGpuOrdinaryShaderSizeValidationOwner;
    const std::shared_ptr<const WebGpuOrdinaryPhysicalGroups> groups_;
    const core::UniquePtr<WebGpuOrdinaryResourceProjection> vertices_;
    const std::shared_ptr<const std::atomic<bool>> deviceLost_;
    core::pmr::Vector<rhi::GpuResourceId> vertexIds_;
    Status status_ = Status::Pending;
    const char* failure_ = nullptr;
};

class WebGpuOrdinaryShaderSizeValidationOwner final : public core::QuotaBoundSharedObjectIdentity {
    struct Root {
        std::shared_ptr<WebGpuOrdinaryShaderSizeValidation> result;
        std::uint32_t remainingCallbacks = scopeCount;
        bool callbackStatusSuccess = true, noErrors = true;
    };

    using Pool = core::runtime::CompletionRetentionPool<Root>;

public:
    static constexpr std::uint32_t scopeCount =
        WebGpuErrorScopeContract::count; // Validation, OutOfMemory, Internal filters.

    class ConstructionKey {
        friend class WebGpuOrdinaryShaderSizeValidationOwner;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    WebGpuOrdinaryShaderSizeValidationOwner(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline,
        WebGpuOrdinaryShaderValidationBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : pipeline_(std::move(pipeline)), budget_(budget), quota_(std::move(quota)),
          retained_(budget.maxPendingProbes),
          deviceLost_(core::makeShared<std::atomic<bool>>(false))
    {}

    ~WebGpuOrdinaryShaderSizeValidationOwner();
    static const char* validateBudget(const WebGpuOrdinaryShaderValidationBudget& budget) noexcept;

    static const char* callbackDiagnostic(
        bool statusSuccess, bool noError, const char* creationFailure) noexcept
    {
        if (creationFailure)
            return creationFailure;
        if (!statusSuccess)
            return "RHI-WEBGPU-ORDINARY-VALIDATION-CALLBACK-STATUS";
        return noError ? nullptr : "RHI-WEBGPU-ORDINARY-VALIDATION-NATIVE-ERROR";
    }

    static core::UniquePtr<WebGpuOrdinaryShaderSizeValidationOwner> prepare(
        std::shared_ptr<const WebGpuOrdinaryPipelineObservation>,
        const WebGpuOrdinaryShaderValidationBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    static std::shared_ptr<WebGpuOrdinaryShaderSizeValidation> probe(
        const std::shared_ptr<WebGpuOrdinaryShaderSizeValidationOwner>&,
        std::shared_ptr<const WebGpuOrdinaryPhysicalGroups>,
        core::UniquePtr<WebGpuOrdinaryResourceProjection>,
        const rhi::GpuResourceId* vertexIds,
        std::size_t vertexCount,
        const char*& diagnostic);

    std::size_t pending() const noexcept { return retained_.pending(); }

    bool closed() const noexcept { return retained_.closed(); }

private:
    struct Callback {
        std::shared_ptr<WebGpuOrdinaryShaderSizeValidationOwner> owner;
        Pool::Ticket ticket;
    };

    static void lost(void* userdata) noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void complete(
        WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*) noexcept;
#endif
    const std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline_;
    const WebGpuOrdinaryShaderValidationBudget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    Pool retained_;
    const std::shared_ptr<std::atomic<bool>> deviceLost_;
    bool lossObserved_ = false;
};
} // namespace arp::rhi_webgpu
