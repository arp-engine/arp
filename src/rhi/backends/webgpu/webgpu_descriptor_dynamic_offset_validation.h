#pragma once

#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/descriptor_whole_buffer_binding.h"

namespace arp::rhi_webgpu {

// Backend metadata validation only. No native command or original proof issuer.
class WebGpuDescriptorDynamicOffsetValidation final {
public:
    static const char* validateBinding(const WebGpuNativeDescriptorGroupPayload& group,
        std::uint32_t binding,
        std::uint32_t offset,
        const WebGpuNativeDeviceLimits& limits) noexcept
    {
        const WebGpuNativeDescriptorBindingPayload* selected = nullptr;
        for (const auto& candidate : group.bindings) {
            if (candidate.binding.binding != binding || candidate.arrayElement != 0u)
                continue;
            if (selected)
                return "RHI-WEBGPU-DESCRIPTOR-DYNAMIC-BINDING-DUPLICATE";
            selected = &candidate;
        }
        if (!selected || !selected->binding.dynamicOffset)
            return "RHI-WEBGPU-DESCRIPTOR-DYNAMIC-BINDING-MISSING";
        if (const auto* id = rhi::DescriptorBufferBindingRangeContract::writeDiagnostic(
                selected->write.bufferBindingRange, true))
            return id;
        if (!selected->buffer)
            return "RHI-WEBGPU-DESCRIPTOR-DYNAMIC-BUFFER-MISSING";
        // Dynamic layout entries are buffer-only; physical InputAttachment
        // lowering must never manufacture dynamic-buffer intent here.
        using Policy = rhi::semantics::DescriptorWholeBufferBinding;
        return Policy::diagnostic(Policy::validate(selected->binding.type,
            selected->buffer->sizeBytes,
            offset,
            {1u,
                limits.uniformBufferOffsetAlignment,
                limits.storageBufferOffsetAlignment,
                limits.maxUniformBufferBindingSize,
                limits.maxStorageBufferBindingSize}));
    }
};
} // namespace arp::rhi_webgpu
