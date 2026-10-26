#pragma once
#include "rhi/backends/metal/metal_original_buffer_limits.h"
#include "rhi/semantics/native_backend_capabilities.h"

namespace arp::rhi_metal {
class MetalRhiDevice;

// Scalar admission only. Callers cannot turn fabricated facts into a receipt.
class MetalOriginalComputeBufferLimits final {
public:
    static const char* inspect(const rhi::ShaderReflectionDesc& reflection,
        const MetalRhiNativePipelinePayload& native,
        const rhi::ComputeResourceCapabilities& queried,
        rhi::OriginalDescriptorBufferBindingContract::Limits& output) noexcept
    {
        output = {};
        if (native.computeBufferFactsVersion != 1u || !queried.maxUniformBufferBindingSizeBytes
            || !queried.maxStorageBufferBindingSizeBytes)
            return "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-FACTS";
        rhi::OriginalDescriptorBufferBindingContract::Limits limits{1u,
            0u,
            0u,
            queried.maxUniformBufferBindingSizeBytes,
            queried.maxStorageBufferBindingSizeBytes};
        for (const auto& binding : reflection.descriptorBindings) {
            const bool uniform = binding.type == rhi::DescriptorType::UniformBuffer;
            if (!uniform && binding.type != rhi::DescriptorType::StorageBuffer
                && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer)
                continue;
            using Shared = MetalOriginalBufferLimitInspection;
            const auto* mapped = Shared::mapping(binding, native);
            if (!mapped
                || native.nativeBindingStages.stages(mapped->type, mapped->metalSlot)
                    != rhi::ShaderStage::Compute)
                return "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-MAPPING";
            const auto* fact = Shared::fact(native.computeBufferFacts, mapped->metalSlot);
            const auto ceiling =
                uniform ? limits.maxUniformBindingBytes : limits.maxStorageBindingBytes;
            if (!Shared::validFact(fact, ceiling))
                return "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-FACTS";
            auto& alignment =
                uniform ? limits.uniformOffsetAlignment : limits.storageOffsetAlignment;
            if (!Shared::accumulate(alignment, fact->alignment))
                return "RHI-METAL-ACCEPTED-COMPUTE-BUFFER-ALIGNMENT-OVERFLOW";
        }
        if (const auto* id = rhi::OriginalDescriptorBufferBindingContract::validateLimits(limits))
            return id;
        output = limits;
        return nullptr;
    }
};

class MetalAcceptedOriginalComputePipelineFactory final {
    friend class MetalRhiDevice;
    static const char* validate(const rhi::GpuDevice&, const rhi::ComputePipeline&) noexcept;
    static core::UniquePtr<rhi::ComputePipeline> create(const rhi::GpuDevice&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const rhi::OriginalComputePipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);
};
} // namespace arp::rhi_metal
