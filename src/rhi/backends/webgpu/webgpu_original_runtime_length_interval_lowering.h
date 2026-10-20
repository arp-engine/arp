#pragma once
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/gpu_device.h"

namespace arp::rhi_webgpu {
// Borrowed backend-private native entry construction. The caller keeps the same
// original owners alive through immediate emission; this result issues no proof.
class WebGpuOriginalRuntimeLengthIntervalLowering final {
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Pipeline>
    static const char* lower(
        const rhi::OriginalDescriptorBufferBindingContract::IntervalDiagnosticRequest& request,
        const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const rhi::DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        WGPUBindGroupEntry& output,
        std::uint32_t& group,
        std::uint32_t& displacement) noexcept
    {
        return lowerImpl(request,
            device,
            pipeline,
            buffer,
            set,
            binding,
            range,
            dynamicOffset,
            output,
            group,
            displacement,
            true);
    }

    template <class Pipeline>
    static const char* lowerBindingDiagnostic(
        const rhi::OriginalDescriptorBufferBindingContract::IntervalDiagnosticRequest& request,
        const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const rhi::DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        WGPUBindGroupEntry& output,
        std::uint32_t& group,
        std::uint32_t& displacement) noexcept
    {
        return lowerImpl(request,
            device,
            pipeline,
            buffer,
            set,
            binding,
            range,
            dynamicOffset,
            output,
            group,
            displacement,
            false);
    }

    template <class Pipeline>
    static const char* lowerTargetMemoryBindingDiagnostic(
        const rhi::OriginalTargetMemoryIntervalContract::Request& request,
        const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const rhi::DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        WGPUBindGroupEntry& output,
        std::uint32_t& group,
        std::uint32_t& displacement) noexcept
    {
        return lowerImpl<true>(request,
            device,
            pipeline,
            buffer,
            set,
            binding,
            range,
            dynamicOffset,
            output,
            group,
            displacement,
            false);
    }

private:
    template <bool targetMemory = false, class Request, class Pipeline>
    static const char* lowerImpl(const Request& request,
        const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const rhi::DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        WGPUBindGroupEntry& output,
        std::uint32_t& group,
        std::uint32_t& displacement,
        bool requireOperation) noexcept
    {
        output = {};
        group = 0u;
        displacement = 0u;
        using Contract = rhi::OriginalDescriptorBufferBindingContract;
        using Observation = std::conditional_t<targetMemory,
            rhi::OriginalTargetMemoryIntervalContract::Observation,
            Contract::IntervalDiagnosticObservation>;
        Observation observation;
        const auto* id = [&]() -> const char* {
            if constexpr (targetMemory)
                return device.diagnoseOriginalTargetMemoryBufferInterval(
                    request, pipeline, buffer, set, binding, range, dynamicOffset, observation);
            else {
                static_assert(
                    !Contract::intervalDiagnosticLowering(rhi::Backend::WebGPU).executionAvailable);
                if (const auto* failure = Contract::validateIntervalDiagnosticRequest(request))
                    return failure;
                const auto decision = Contract::intervalDiagnosticLowering(device.backend());
                if (!decision.captureAvailable)
                    return decision.diagnostic;
                return requireOperation
                    ? device.diagnoseOriginalRuntimeLengthInterval(request,
                          pipeline,
                          buffer,
                          set,
                          binding,
                          range,
                          dynamicOffset,
                          observation)
                    : device.diagnoseOriginalRuntimeProfileBufferInterval(request,
                          pipeline,
                          buffer,
                          set,
                          binding,
                          range,
                          dynamicOffset,
                          observation);
            }
        }();
        if (id)
            return id;
        const auto native = webGpuNativePayloadFor(*pipeline);
        const auto source = webGpuNativePayloadFor(*buffer->originalObject());
        if (!native || !native->pipeline || !native->issuedContract() || !source || !source->buffer)
            return "RHI-ORIGINAL-RUNTIME-INTERVAL-NATIVE-PAYLOAD";
        const auto nativeDevice = static_cast<WGPUDevice>(native->issuedContract()->nativeDevice());
        if (!nativeDevice || source->ownerDevice != nativeDevice)
            return "RHI-ORIGINAL-RUNTIME-INTERVAL-NATIVE-DEVICE";
        if (source->sizeBytes != buffer->layout().sizeBytes)
            return "RHI-ORIGINAL-RUNTIME-INTERVAL-NATIVE-EXTENT";
        auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = observation.binding;
        entry.buffer = source->buffer;
        entry.offset = range.offsetBytes;
        entry.size = range.sizeBytes;
        output = entry;
        group = observation.group;
        displacement = dynamicOffset;
        return nullptr;
    }
#endif
};
} // namespace arp::rhi_webgpu
