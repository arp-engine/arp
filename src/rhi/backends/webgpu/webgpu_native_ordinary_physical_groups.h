#pragma once
#include "core/container/bounded_sequence.h"
#include "rhi/backends/webgpu/webgpu_ordinary_descriptor_observation.h"
#include "rhi/backends/webgpu/webgpu_pipeline_ownership.h"

namespace arp::rhi_webgpu {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
// Shared cold native creation. Callers preflight their complete binding intent
// and reserve all metadata/owners before issuing any native object.
bool initializeWebGpuNativePhysicalDescriptorGroup(WebGpuNativeDescriptorGroupPayload&,
    const WebGpuNativeIssuedPipelineContract&,
    std::uint32_t group,
    const WGPUBindGroupEntry*,
    std::size_t,
    core::pmr::String&);
#endif
struct WebGpuOrdinaryPhysicalGroupBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSets = 0u, maxBindings = 0u, maxResources = 0u;
    std::uint32_t maxGroups = 0u, maxNativeGroupLayouts = 0u, maxNativeGroups = 0u;
    std::uint64_t maxBufferBindingBytes = 0u, maxRetainedAllocationBytes = 0u,
                  maxCpuMetadataBytes = 0u;
};

struct WebGpuOrdinaryObservedSet {
    std::uint32_t logicalSet = 0u;
    std::shared_ptr<const WebGpuOrdinaryDescriptorObservation> observation;
};

// Cold physical objects only. No draw/dispatch/submit entry or execution claim.
class WebGpuOrdinaryPhysicalGroups final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOrdinaryPhysicalGroups;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOrdinaryPhysicalGroups(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline,
        std::uint32_t setCount)
        : pipeline_(std::move(pipeline)), sets_(setCount)
    {}

    static const char* validateBudget(const WebGpuOrdinaryPhysicalGroupBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-GROUP-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-GROUP-OPT-IN";
        if (!budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORDINARY-GROUP-BUDGET";
        return nullptr;
    }

    static core::UniquePtr<WebGpuOrdinaryPhysicalGroups> prepare(
        std::shared_ptr<const WebGpuOrdinaryPipelineObservation>,
        const WebGpuOrdinaryObservedSet*,
        std::size_t,
        const WebGpuOrdinaryPhysicalGroupBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    const char* validate() const noexcept;
    const char* executionDiagnostic() const noexcept;

    std::uint32_t groupCount() const noexcept { return static_cast<std::uint32_t>(groups_.size()); }

    std::uint64_t bufferBindingBytes() const noexcept { return bufferBytes_; }

    std::uint64_t retainedAllocationBytes() const noexcept { return allocationBytes_; }

    std::uint64_t bindingRangeBytes(std::uint32_t group, std::uint32_t binding) const noexcept;
    std::size_t dynamicOffsetCount(std::uint32_t group) const noexcept;

    struct OriginalBufferBinding {
        const WebGpuOrdinaryResourceObservation* resource = nullptr;
        WebGpuNativeDescriptorBindingLowering mapping;
        rhi::DescriptorType type = rhi::DescriptorType::UniformBuffer;
        std::uint64_t offsetBytes = 0u, sizeBytes = 0u;
    };

    // Non-owning result is valid only while this exact original group is retained.
    // Failure never changes output; no physical slot alone is an identity proof.
    static const char* originalBufferBinding(
        const std::shared_ptr<const WebGpuOrdinaryPhysicalGroups>&,
        std::uint32_t group,
        std::uint32_t binding,
        OriginalBufferBinding& output) noexcept;

    // Original cold ownership closure, for command graph composition only.
    // These observations grant no recording or queue execution authority.
    const auto& pipeline() const noexcept { return pipeline_; }

    const auto& observedSets() const noexcept { return sets_.values(); }

private:
    friend class WebGpuOrdinaryScalarComputeRange;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinaryGraphicsTriangleRange;
    friend class WebGpuOrdinaryGraphicsAttachments;
    friend class WebGpuOrdinaryShaderSizeValidationOwner;
    const std::shared_ptr<const WebGpuOrdinaryPipelineObservation> pipeline_;
    core::BoundedSequence<WebGpuOrdinaryObservedSet> sets_;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    struct Group {
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> owner;
        WGPUBindGroupLayout originalLayout = nullptr;
        WGPUBindGroup originalGroup = nullptr;
        core::pmr::Vector<WGPUBindGroupEntry> entries{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::uint32_t> offsets{&core::defaultCpuMemoryResource()};
    };
#else
    struct Group {};
#endif
    core::pmr::Vector<Group> groups_{&core::defaultCpuMemoryResource()};
    std::uint64_t bufferBytes_ = 0u, allocationBytes_ = 0u;
    bool deferredShaderSize_ = false;
};
} // namespace arp::rhi_webgpu
