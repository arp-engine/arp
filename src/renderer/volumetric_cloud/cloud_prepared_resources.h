#pragma once

#include "renderer/volumetric_cloud/cloud_prepared_history.h"
#include "renderer/volumetric_cloud/cloud_prepared_pipelines.h"

namespace arp::renderer::cloud {

struct CloudPreparedResourceCreateContext {
    rhi::GpuDevice& device;
    const assets::cloud::CloudAsset& asset;
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t cameraId = 0u;
    CloudPreparedPipelineSelection selection;
    CloudPreparedResourceRequest resources;
    // Explicit intent. The backend must accept the full host initialization;
    // unmapped memory refuses without changing the requested memory class.
    std::optional<rhi::MemoryUsage> sampleMemory;
    std::uint64_t maxCpuOwnerBytes = 0u, maxCpuInitializationBytes = 0u;
    std::shared_ptr<core::CpuMemoryQuota> ownerQuota, initializationQuota, sourceQuota,
        historyQuota;
    // Logical reservation for the entire planned generation, including its
    // output/constant allowance. Shared by the original leases until their
    // last owner dies. This is not queried driver allocation accounting.
    std::shared_ptr<core::AtomicCapacityBudget> retainedLogicalBytes;
    std::shared_ptr<const rhi::ComputeDispatchValidationDomain> dispatchDomain;
};

struct CloudPreparedFrameBuffers {
    std::shared_ptr<const rhi::OriginalCommandBufferLease> constants, density, raw, previous,
        resolved, reconstructionConstants;
    std::uint64_t historyGeneration = 0u;
};

// Persistent density/sample owners and the existing real-reader history
// controller. The caller composes pendingBuffers into its one canonical Source
// inventory before freezing descriptors and preparing the ordinary graph.
// Outputs remain ordinary graph attachments; no second inventory/executor or
// resource-state/content journal is created here. Cold initialization is IO-free.
class ARP_API CloudPreparedResources final {
public:
    CloudPreparedResources();
    ~CloudPreparedResources();
    CloudPreparedResources(const CloudPreparedResources&) = delete;
    CloudPreparedResources& operator=(const CloudPreparedResources&) = delete;

    // Pure preflight, including metadata, memory intent and quota identities.
    // Does not reserve capacity or allocate a replacement generation.
    static CloudResult validateCreate(const CloudPreparedResourceCreateContext&,
        const rhi::DeviceCapabilities&,
        CloudPreparedResourcePlan&) noexcept;
    CloudResult create(const CloudPreparedResourceCreateContext&);
    CloudResult prepareFrame(const CloudGraphContext&);
    CloudResult pendingBuffers(CloudPreparedFrameBuffers&) const noexcept;
    CloudResult releaseIdle() noexcept;
    void release() noexcept;

    bool created() const noexcept { return state_ != nullptr; }

    CloudPreparedHistory* history() noexcept;
    // Immutable original slot owners for composition into the caller's single
    // canonical prefix. The borrowed view ends when this owner releases.
    core::ArrayView<CloudPreparedHistorySlot> historySlots() const noexcept;

    const CloudPreparedResourcePlan& plan() const noexcept { return plan_; }

    const CloudPreparedPipelineSelection& selection() const noexcept { return selection_; }

private:
    struct State;
    core::UniquePtr<State> state_;
    CloudPreparedResourcePlan plan_;
    CloudPreparedPipelineSelection selection_;
};

} // namespace arp::renderer::cloud
