#pragma once

#include "core/lifetime_epoch.h"
#include "core/shared_owner_projection.h"
#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/frontend/descriptor_validation.h"
#include "rhi/gpu_commands.h"

#include <limits>

namespace arp::rhi_metal {
class MetalRhiResourceRegistry;

class MetalRhiOrdinaryDescriptorObservation final {
public:
    MetalRhiOrdinaryDescriptorObservation(
        const rhi::DescriptorSet& descriptor, std::shared_ptr<MetalRhiResourceRegistry> registry)
        : descriptor_(&descriptor, descriptor.lifetimeTicket()),
          revision_(descriptor.contentRevision()), registry_(std::move(registry))
    {}

    const rhi::DescriptorSet* descriptor() const noexcept { return descriptor_.get(); }

    const auto& registry() const noexcept { return registry_; }

    rhi::ValidationResult validate(const MetalRhiNativeDescriptorGroupPayload&) const;

private:
    core::LifetimeObserver<const rhi::DescriptorSet> descriptor_;
    std::uint64_t revision_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
};

struct MetalRhiOrdinaryBindingBudget {
    std::uint32_t contractVersion = 0u;
    bool optIn = false;
    std::uint64_t maxCpuMetadataBytes = 0u;
    std::uint32_t maxArgumentBuffers = 0u;
    std::uint64_t maxArgumentBufferBytes = 0u;

    const char* validate(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (contractVersion != 1u || !optIn || !maxCpuMetadataBytes
            || static_cast<bool>(maxArgumentBuffers) != static_cast<bool>(maxArgumentBufferBytes))
            return "RHI-METAL-ORDINARY-PREPARED-BUDGET-INVALID";
        if (!quota || quota->limitBytes() != maxCpuMetadataBytes)
            return "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return nullptr;
    }
};

class MetalRhiOrdinaryPreparedComputeBinding;

// Prepared before recording, with storage charged to the caller's explicit quota.
class MetalRhiOrdinaryPushBlock final {
public:
    MetalRhiOrdinaryPushBlock(const rhi::PushConstantsDesc&,
        MetalRhiOrdinaryBindingBudget,
        std::shared_ptr<core::CpuMemoryQuota>);
    MetalRhiOrdinaryPushBlock(const MetalRhiOrdinaryPushBlock&) = delete;
    MetalRhiOrdinaryPushBlock& operator=(const MetalRhiOrdinaryPushBlock&) = delete;

    const auto& bytes() const noexcept { return bytes_; }

    const auto& quota() const noexcept { return quota_; }

    rhi::ShaderStage stages() const noexcept { return stages_; }

    rhi::ValidationResult validate() const;

private:
    core::pmr::Vector<std::uint8_t> bytes_;
    rhi::ShaderStage stages_;
    MetalRhiOrdinaryBindingBudget budget_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
};

class MetalRhiOrdinaryPreparedComputePush;
std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush> prepareMetalRhiOrdinaryComputePush(
    const rhi::ComputePipeline&,
    const rhi::PushConstantsDesc&,
    const MetalRhiOrdinaryBindingBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    core::pmr::String&);

class MetalRhiOrdinaryPreparedComputePush final {
    class ConstructionKey {
        friend std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush>
        prepareMetalRhiOrdinaryComputePush(const rhi::ComputePipeline&,
            const rhi::PushConstantsDesc&,
            const MetalRhiOrdinaryBindingBudget&,
            std::shared_ptr<core::CpuMemoryQuota>,
            core::pmr::String&);
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    friend std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush>
    prepareMetalRhiOrdinaryComputePush(const rhi::ComputePipeline&,
        const rhi::PushConstantsDesc&,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);

public:
    MetalRhiOrdinaryPreparedComputePush(ConstructionKey,
        std::shared_ptr<MetalRhiNativePipelinePayload> pipeline,
        std::uint32_t slot,
        std::uint64_t alignment,
        const rhi::PushConstantsDesc& desc,
        MetalRhiOrdinaryBindingBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    MetalRhiOrdinaryPreparedComputePush(const MetalRhiOrdinaryPreparedComputePush&) = delete;
    MetalRhiOrdinaryPreparedComputePush& operator=(
        const MetalRhiOrdinaryPreparedComputePush&) = delete;

    const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline() const noexcept
    {
        return pipeline_;
    }

    const std::shared_ptr<core::CpuMemoryQuota>& quota() const noexcept { return block_.quota(); }

    const core::pmr::Vector<std::uint8_t>& bytes() const noexcept { return block_.bytes(); }

    std::uint32_t slot() const noexcept { return slot_; }

    rhi::ValidationResult validate() const;

private:
    std::shared_ptr<MetalRhiNativePipelinePayload> pipeline_;
    std::shared_ptr<void> function_;
    MetalRhiOrdinaryPushBlock block_;
    std::uint32_t slot_;
    std::uint64_t alignment_;
};

std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding> prepareMetalRhiOrdinaryComputeBinding(
    const rhi::ComputePipeline&,
    const rhi::DescriptorSet&,
    std::uint32_t,
    std::shared_ptr<MetalRhiResourceRegistry>,
    const MetalRhiOrdinaryBindingBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    core::pmr::String&,
    const std::uint32_t* = nullptr,
    std::uint32_t = 0u);

// Backend-owned, preparation-only immutable storage; no public execution ticket.
class MetalRhiOrdinaryPreparedComputeBinding final {
    class ConstructionKey {
        friend std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding>
        prepareMetalRhiOrdinaryComputeBinding(const rhi::ComputePipeline&,
            const rhi::DescriptorSet&,
            std::uint32_t,
            std::shared_ptr<MetalRhiResourceRegistry>,
            const MetalRhiOrdinaryBindingBudget&,
            std::shared_ptr<core::CpuMemoryQuota>,
            core::pmr::String&,
            const std::uint32_t*,
            std::uint32_t);
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    friend std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding>
    prepareMetalRhiOrdinaryComputeBinding(const rhi::ComputePipeline&,
        const rhi::DescriptorSet&,
        std::uint32_t,
        std::shared_ptr<MetalRhiResourceRegistry>,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&,
        const std::uint32_t*,
        std::uint32_t);

public:
    MetalRhiOrdinaryPreparedComputeBinding(ConstructionKey,
        std::shared_ptr<const MetalRhiNativePreparedComputeBindings> function,
        const rhi::ComputePipeline& pipeline,
        const rhi::DescriptorSet& descriptor,
        std::shared_ptr<MetalRhiResourceRegistry> registry,
        MetalRhiOrdinaryBindingBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    MetalRhiOrdinaryPreparedComputeBinding(const MetalRhiOrdinaryPreparedComputeBinding&) = delete;

    const MetalRhiNativePreparedComputeBindings& function() const noexcept { return *function_; }

    const rhi::DescriptorConsumptionContract& consumption() const noexcept { return consumption_; }

    const rhi::DescriptorSet* descriptor() const noexcept { return observation_.descriptor(); }

    const std::shared_ptr<MetalRhiResourceRegistry>& registry() const noexcept
    {
        return observation_.registry();
    }

    const std::shared_ptr<core::CpuMemoryQuota>& quota() const noexcept { return quota_; }

    const MetalRhiOrdinaryBindingBudget& budget() const noexcept { return budget_; }

    std::uint32_t requiredPushBytes() const noexcept { return pushBytes_; }

    rhi::ValidationResult validateObservation() const;

    // This excludes the outer prepared wrapper. The recorder projects it as
    // owner0, then every child; transitive ownership cannot bypass owner counts.
    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    std::shared_ptr<const void> childOwnerAt(std::size_t index) const noexcept
    {
        return owners_.at(index);
    }

private:
    std::shared_ptr<const MetalRhiNativePreparedComputeBindings> function_;
    rhi::DescriptorConsumptionContract consumption_;
    MetalRhiOrdinaryDescriptorObservation observation_;
    MetalRhiOrdinaryBindingBudget budget_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::uint32_t pushBytes_;
    core::SharedOwnerProjection owners_;
};

} // namespace arp::rhi_metal
