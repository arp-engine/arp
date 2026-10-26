#pragma once

#include "rhi/frontend/completed_depth_read_input.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_commands.h"

namespace arp::rhi {
class GpuDevice;
class OriginalCommandResourceSources;

// Public L2 declarations consumed by the one canonical Source implementation.
struct OriginalCommandDescriptorSetElement {
    DescriptorBindingWrite write;
    std::size_t sourceIndex = SIZE_MAX;
    std::uint64_t registrationGeneration = 0u;
    std::uint64_t bufferAccessOffset = 0u, bufferAccessBytes = 0u;
    PipelineStage stages = PipelineStage::None;
    ResourceAccess access = ResourceAccess::None;
    std::uint32_t dynamicOffset = 0u;
};

struct OriginalCommandDescriptorSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxLogicalElements = 0u, maxWrittenElements = 0u, maxExclusiveRetainedOwners = 0u;
    std::uint64_t sourceCpuBytes = 0u;
    std::size_t finitePipelineSourceIndex = SIZE_MAX;
    std::uint32_t finiteLogicalSet = 0u, finiteFootprintContractVersion = 0u,
                  maxFrozenDynamicOffsets = 0u;
    bool finiteFullBoundViewOptIn = false;
    OriginalCompletedDepthReadInputContract::Budget completedDepthInputs;
};

// Cold assembly of the existing canonical inventory. Observations retain only
// original L2 owners; they cannot authorize descriptor writes or commands.
class ARP_API OriginalCommandSourceAssembly final {
public:
    struct TextureViewObservation {
        std::shared_ptr<const OriginalCommandTextureViewLease> view;
        std::shared_ptr<const OriginalCommandTextureLease> parent;
    };

    static const char* validate(
        const std::shared_ptr<const OriginalCommandResourceSources>&, const GpuDevice&) noexcept;
    static bool sealed(const OriginalCommandResourceSources&) noexcept;
    static std::size_t size(const OriginalCommandResourceSources&) noexcept;
    static const std::shared_ptr<core::CpuMemoryQuota>& quota(
        const OriginalCommandResourceSources&) noexcept;
    static const char* cpuOwnerCount(const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&,
        std::uint32_t roots,
        std::uint32_t& owners) noexcept;

    static const char* inspectBuffer(const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&,
        std::size_t,
        std::shared_ptr<const OriginalCommandBufferLease>&) noexcept;
    static const char* inspectTextureView(
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&,
        std::size_t,
        TextureViewObservation&) noexcept;
    static const char* inspectComputePipeline(
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&,
        std::size_t,
        std::shared_ptr<const ComputePipeline>&) noexcept;
    static const char* inspectGraphicsPipeline(
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&,
        std::size_t,
        std::shared_ptr<const RenderPipeline>&) noexcept;
    static const char* descriptorForPipeline(
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        const GpuDevice&,
        std::size_t,
        bool& present) noexcept;

    static std::uint32_t finiteDescriptorRetainedOwners() noexcept;
    // Source preparation and append use the same inventory and its authored
    // quota/capacity. Failure publishes no Source or index; the caller retains
    // responsibility for its existing candidate transaction.
    static const char* appendDescriptorSet(core::UniquePtr<DescriptorSet>&,
        const std::shared_ptr<const DescriptorArena>&,
        const std::shared_ptr<OriginalCommandResourceSources>&,
        const OriginalCommandDescriptorSetElement*,
        std::size_t,
        const OriginalCommandDescriptorSourceBudget&,
        const GpuDevice&,
        std::size_t& index);
};
} // namespace arp::rhi
