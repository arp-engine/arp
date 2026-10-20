#pragma once
#include "rhi/backends/metal/metal_backend_objects.h"
#include "rhi/backends/metal/metal_graphics_preparation.h"

namespace arp::rhi_metal {
// One shared physical observer for the proved scalar writer and reader domains.
// It resolves authored set/binding using sealed mapping and actual queried ABI.
class MetalRhiScalarStorageBindingValidator final {
public:
    static const char* capture(const MetalRhiOrdinaryPreparedGraphicsBinding& binding,
        const rhi::GpuBuffer& buffer,
        std::uint32_t setIndex,
        std::uint32_t bindingIndex,
        rhi::DescriptorType type,
        MetalRhiStageBufferRange& range)
    {
        if (type != rhi::DescriptorType::StorageBuffer
            && type != rhi::DescriptorType::ReadOnlyStorageBuffer
            && type != rhi::DescriptorType::UniformBuffer)
            return "RHI-METAL-SCALAR-STORAGE-DESCRIPTOR-DOMAIN";
        range = {};
        const auto& provider = *binding.provider();
        const auto& native = *provider.native();
        const auto& snapshot = *binding.payload()->descriptors;
        if (snapshot.bindings.size() != 1u || binding.payload()->set != setIndex
            || provider.consumption().bindings().size() != 1u
            || !provider.consumption().pushRanges().empty())
            return "RHI-METAL-ORDINARY-WRITE-DESCRIPTOR-DOMAIN";
        const auto& descriptor = snapshot.bindings.front();
        const auto registered = binding.registry()->find(buffer.id());
        const auto original = metalRhiNativePayloadFor(buffer);
        if (!registered || !original || registered->kind != rhi::DescriptorResourceKind::Buffer
            || registered->buffer != original || registered->buffer.owner_before(original)
            || original.owner_before(registered->buffer)
            || registered->bufferSizeBytes != buffer.desc().sizeBytes
            || registered->bufferUsage != buffer.desc().usage
            || (buffer.desc().usage
                   & (type == rhi::DescriptorType::UniformBuffer
                           ? rhi::ResourceUsage::UniformBuffer
                           : rhi::ResourceUsage::StorageBuffer))
                != (type == rhi::DescriptorType::UniformBuffer ? rhi::ResourceUsage::UniformBuffer
                                                               : rhi::ResourceUsage::StorageBuffer))
            return "RHI-METAL-ORDINARY-WRITE-RESOURCE-MISMATCH";
        if (descriptor.binding != bindingIndex || descriptor.type != type
            || descriptor.stages != rhi::ShaderStage::Fragment || descriptor.arrayCount != 1u
            || descriptor.arrayElement || descriptor.bufferOffsetBytes
            || descriptor.dynamicOffsetIndex != UINT32_MAX
            || descriptor.resourceKind != rhi::DescriptorResourceKind::Buffer
            || descriptor.resourceId != buffer.id()
            || descriptor.argumentKind != MetalRhiArgumentKind::Buffer
            || descriptor.buffer != original || descriptor.buffer.owner_before(original)
            || original.owner_before(descriptor.buffer))
            return "RHI-METAL-ORDINARY-WRITE-DESCRIPTOR-RESOURCE-MISMATCH";
        const auto& resource = original->resource;
        if (const auto* id =
                validateMetalRhiTrackedNativeBufferObject(resource, buffer.desc().sizeBytes))
            return id;
        if (!native.nativeRuntimeBacked || !native.renderPipelinePayloadPresent
            || !native.pipelineState || !resource.nativeRuntimeBacked
            || resource.nativeRuntimeDiagnosticOnly || !resource.hazardsExplicitlyTracked
            || !resource.allocationReleaseToken || resource.nativeDevice != native.nativeDevice)
            return "RHI-METAL-ORDINARY-WRITE-NATIVE-IDENTITY-MISMATCH";
        const auto& stage = provider.stages()[1];
        if (!stage || stage->stage != rhi::ShaderStage::Fragment || !binding.payload()->layouts[1]
            || !stage->matches(*binding.payload()->layouts[1]))
            return "RHI-METAL-ORDINARY-WRITE-STAGE-MISMATCH";
        MetalRhiPreparedBindingLocation location;
        const auto mapped =
            stage->resolve(descriptor, setIndex, native.nativeBindingStages, location);
        if (!mapped.success || !location.active || location.array)
            return "RHI-METAL-ORDINARY-WRITE-MAPPING-MISMATCH";
        const MetalRhiBufferBindingFact* fact = nullptr;
        if (type == rhi::DescriptorType::StorageBuffer) {
            if (const auto* id = MetalRhiAtomicBufferAbiValidator::validate(
                    native.graphicsBufferFacts[1], location.slot, buffer.desc().sizeBytes, fact))
                return id;
        } else {
            if (const auto* id = validateMetalRhiBufferOffset(native.graphicsBufferFacts[1].entries,
                    native.graphicsBufferFacts[1].version,
                    location.slot,
                    0u,
                    buffer.desc().sizeBytes,
                    fact))
                return id;
            if (native.graphicsBufferFacts[1].stage != rhi::ShaderStage::Fragment
                || fact->access != MetalRhiBufferPointeeFact::readOnlyAccess
                || fact->elementType != MetalRhiAtomicBufferAbiValidator::uintElementType
                || fact->requiredBytes != sizeof(std::uint32_t))
                return "RHI-METAL-ORDINARY-READ-CONSUMER-ABI-DOMAIN";
        }
        if (const auto* id =
                MetalRhiStageBufferRangeValidator::capture(native.graphicsBufferFacts[1],
                    rhi::ShaderStage::Fragment,
                    location.slot,
                    0u,
                    buffer.desc().sizeBytes,
                    range))
            return id;
        range.nativeIdentity = MetalRhiNativeResourceObservation(resource);
        return nullptr;
    }
};
} // namespace arp::rhi_metal
