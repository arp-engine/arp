#include "rhi/frontend/descriptor_original_buffer_range.h"

#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_extensions.h"

namespace arp::rhi {
const char* OriginalBufferRangeDescriptorContract::validateSource(GpuDevice& device,
    const DescriptorBindingWrite& write,
    const std::shared_ptr<const OriginalCommandBufferLease>& source,
    std::uint64_t minimumBytes) noexcept
{
    if (const auto* id = OriginalCommandBufferLease::validateOriginal(source))
        return id;
    if (const auto* id = device.validateOriginalResourceRegistration(*source->originalObject()))
        return id;
    const bool uniform = write.type == DescriptorType::UniformBuffer;
    if ((!uniform && write.type != DescriptorType::ReadOnlyStorageBuffer
            && write.type != DescriptorType::StorageBuffer)
        || write.resourceKind != DescriptorResourceKind::Buffer || write.physicalLowering.enabled
        || write.resource != source->resource())
        return "RHI-BUFFER-RANGE-DESCRIPTOR-WRITE";
    const auto usage = uniform ? ResourceUsage::UniformBuffer : ResourceUsage::StorageBuffer;
    if ((source->layout().usage & usage) != usage)
        return "RHI-BUFFER-RANGE-DESCRIPTOR-USAGE";
    if (write.type == DescriptorType::StorageBuffer && source->originalObject()->gpuWritesSealed())
        return "RHI-BUFFER-GPU-WRITES-SEALED";
    const auto* extension =
        static_cast<const BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
    if (!extension)
        return "RHI-BUFFER-RANGE-DESCRIPTOR-LIMITS";
    const auto& caps = extension->capabilities();
    if (!caps.limitsQueried || caps.uniformBufferOffsetAlignment > UINT32_MAX
        || caps.storageBufferOffsetAlignment > UINT32_MAX)
        return "RHI-BUFFER-RANGE-DESCRIPTOR-LIMITS";
    const OriginalDescriptorBufferBindingContract::Limits limits{1u,
        static_cast<std::uint32_t>(caps.uniformBufferOffsetAlignment),
        static_cast<std::uint32_t>(caps.storageBufferOffsetAlignment),
        caps.maxUniformBufferBindingSize,
        caps.maxStorageBufferBindingSize};
    DescriptorBufferBindingRangeContract::Resolved range;
    return OriginalDescriptorBufferBindingContract::validateFiniteInterval(write.type,
        write.bufferBindingRange,
        source->layout().sizeBytes,
        0u,
        minimumBytes,
        limits,
        range);
}

ValidationResult DescriptorSet::writeOriginalBufferRange(GpuDevice& device,
    const DescriptorBindingWrite& write,
    const std::shared_ptr<const OriginalCommandBufferLease>& source,
    std::uint64_t minimumBytes,
    const OriginalBufferRangeDescriptorRequest& request)
{
    if (!isCurrentAllocation())
        return {false, "RHI-DESCRIPTOR-ALLOCATION-STALE"};
    if (const auto* id = contentWriteDiagnostic())
        return {false, id};
    if (const auto* id = ordinaryWriteAdmissionDiagnostic())
        return {false, id};
    if (const auto* id = OriginalBufferRangeDescriptorContract::validateIntent(request))
        return {false, id};
    const auto decision = OriginalBufferRangeDescriptorContract::lowering(device.backend());
    if (!decision.publicationAvailable || decision.diagnostic)
        return {false, decision.diagnostic};
    if (const auto* id = device.validateOriginalResourceRegistration(*this))
        return {false, id};
    if (const auto* id = OriginalBufferRangeDescriptorContract::validateSource(
            device, write, source, minimumBytes))
        return {false, id};
    const auto* binding = findDescriptorSetBinding(layout(), write.binding);
    if (!binding || binding->dynamicOffset || binding->type != write.type
        || write.arrayElement >= binding->arrayCount)
        return {false, "RHI-BUFFER-RANGE-DESCRIPTOR-BINDING"};
    auto result = writeOriginalBufferRangeImpl(write, source);
    if (result.success)
        commitContentWrite();
    return result;
}
} // namespace arp::rhi
