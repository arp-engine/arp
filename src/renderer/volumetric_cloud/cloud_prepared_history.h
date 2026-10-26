#pragma once

#include "renderer/volumetric_cloud/cloud_reconstruction.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"
#include "rhi/frontend/resource_leases.h"

namespace arp::renderer::cloud {

struct CloudPreparedHistorySlot {
    std::shared_ptr<const rhi::OriginalCommandBufferLease> raw, resolved;
};

// Only renderer ownership/selection. Original preparation still admits every
// resource, descriptor, access, budget and backend disposition before execution.
// The device and observer must outlive pending graphs and their completion.
class ARP_API CloudPreparedHistory final : public render_graph::PassSubmissionObserver {
public:
    ~CloudPreparedHistory() override { release(); }

    CloudResult create(rhi::GpuDevice& device,
        std::uint64_t cameraId,
        const CloudRenderConfig& config,
        const CloudExecutionPlan& plan,
        const assets::cloud::CloudCookDesc& asset,
        const CloudPreparedHistorySlot* slots,
        std::size_t slotCount,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const CloudReconstructionConfig* reconstruction = nullptr,
        const rhi::OriginalCommandBufferLease::CapacityOwners& capacityOwners = {},
        const std::shared_ptr<core::AtomicCapacityBudget>& immutableGenerations = {},
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota = {});

    template <std::size_t N>
    CloudResult create(rhi::GpuDevice& device,
        std::uint64_t cameraId,
        const CloudRenderConfig& config,
        const CloudExecutionPlan& plan,
        const assets::cloud::CloudCookDesc& asset,
        const std::array<CloudPreparedHistorySlot, N>& slots,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const CloudReconstructionConfig* reconstruction = nullptr,
        const rhi::OriginalCommandBufferLease::CapacityOwners& capacityOwners = {},
        const std::shared_ptr<core::AtomicCapacityBudget>& immutableGenerations = {},
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota = {})
    {
        return create(device,
            cameraId,
            config,
            plan,
            asset,
            slots.data(),
            slots.size(),
            quota,
            reconstruction,
            capacityOwners,
            immutableGenerations,
            sourceQuota);
    }

    // Cold generation creation, including a fully uploaded, immutable UBO.
    // The caller publishes these exact leases in the original inventory.
    // Optional capacity custody follows both immutable uniforms. An authored
    // generation budget must match framesInFlight and uses capacity owner[1];
    // its unit remains charged until both original uniform leases retire.
    // All exported leases must share the canonical inventory's source quota.
    // Omitting sourceQuota retains the existing single-quota create contract.
    CloudResult prepareFrame(const CloudGraphContext& context);
    CloudResult cancelPendingFrame() noexcept;
    CloudResult releaseIdle() noexcept;
    // Explicit teardown only: retain failed graphs, drain here, then destroy
    // their original owners. No frame admission or resize operation waits.
    void release() noexcept;
    CloudResult resetHistory() noexcept;
    bool frameReady() const noexcept;
    bool idle() const noexcept;

    const CloudFrameState& pendingFrame() const noexcept { return pendingFrame_; }

    const auto& constants() const noexcept { return pendingConstants_; }

    const auto& reconstructionConstants() const noexcept { return pendingReconstructionConstants_; }

    const auto& reconstructionPlan() const noexcept { return reconstructionPlan_; }

    std::uint64_t densityBytes() const noexcept { return densityBytes_; }

    std::shared_ptr<const rhi::OriginalCommandBufferLease> raw() const noexcept
    {
        return pending_ ? slots_[pendingSlot_].resources.raw : nullptr;
    }

    std::shared_ptr<const rhi::OriginalCommandBufferLease> resolved() const noexcept
    {
        return pending_ ? slots_[pendingSlot_].resources.resolved : nullptr;
    }

    std::shared_ptr<const rhi::OriginalCommandBufferLease> previous() const noexcept
    {
        if (!pending_)
            return nullptr;
        if (pendingFrame_.historyUsed)
            return slots_[previousSlot_].resources.resolved;
        // The /3 binding is always full sized. This cold-initialized spare is
        // never consumed as history while previousCamera.w is zero.
        return reconstruction_.contractVersion
            ? slots_[(pendingSlot_ + 1u) % slots_.size()].resources.resolved
            : raw();
    }

    render_graph::PassSubmissionToken token() const noexcept { return {cameraId_, generation_}; }

    // Called before graph mutation; rejects stale or unrelated frame resources.
    const char* validateGraph(const CloudPreparedGraphContext& context) const;
    const char* validateSubmission(render_graph::PassSubmissionToken token) const noexcept override;
    void onSubmitted(render_graph::PassSubmissionToken token,
        const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override;

private:
    struct Slot {
        CloudPreparedHistorySlot resources;
        CloudFrameState frame;
        std::shared_ptr<const rhi::GpuFence> completion;
    };

    rhi::GpuDevice* device_ = nullptr;
    CloudRenderConfig config_;
    CloudExecutionPlan plan_;
    CloudReconstructionConfig reconstruction_;
    CloudReconstructionResources reconstructionPlan_;
    assets::cloud::CloudCookDesc asset_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::shared_ptr<core::CpuMemoryQuota> sourceQuota_;
    rhi::OriginalCommandBufferLease::CapacityOwners capacityOwners_;
    std::shared_ptr<core::AtomicCapacityBudget> immutableGenerations_;
    core::pmr::Vector<Slot> slots_;
    CloudFrameState pendingFrame_;
    std::shared_ptr<const rhi::OriginalCommandBufferLease> pendingConstants_;
    std::shared_ptr<const rhi::OriginalCommandBufferLease> pendingReconstructionConstants_;
    std::uint64_t cameraId_ = 0u, generation_ = 0u, densityBytes_ = 0u;
    std::uint32_t pendingSlot_ = 0u, previousSlot_ = 0u, samplingFrame_ = 0u;
    bool pending_ = false, historyValid_ = false;
    mutable bool executionValidated_ = false;
};

} // namespace arp::renderer::cloud
