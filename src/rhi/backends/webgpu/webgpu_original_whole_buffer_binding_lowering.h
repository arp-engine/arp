#pragma once
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/gpu_device.h"

namespace arp::rhi_webgpu {
// Borrowed original whole-buffer entry. No finite interval substitution and no
// command authority; the candidate retains these exact original owners.
class WebGpuOriginalWholeBufferBindingLowering final {
public:
    template <class Pipeline>
    static const char* validate(const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        const rhi::DescriptorBindingWrite& write,
        std::uint32_t dynamicOffset) noexcept
    {
        if (!pipeline || !pipeline->sharedOwnerMatches(pipeline)
            || !pipeline->originalAcceptedSharedQuotaBound())
            return "RHI-ORIGINAL-WHOLE-WRITE-PIPELINE-OWNER";
        if (const auto* id = device.validateOriginalResourceRegistration(*pipeline))
            return id;
        if (const auto* id = rhi::OriginalCommandBufferLease::validateOriginal(buffer))
            return id;
        const auto* object = buffer->originalObject();
        if (const auto* id = device.validateOriginalResourceRegistration(*object))
            return id;
        if (write.resource != object->id())
            return "RHI-ORIGINAL-WHOLE-WRITE-BUFFER";
        if ((buffer->layout().usage & rhi::ResourceUsage::StorageBuffer)
            != rhi::ResourceUsage::StorageBuffer)
            return "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-USAGE";
        if (const auto* id = pipeline->originalDescriptorBufferBindingDiagnostic(
                {1u, true}, write.type, buffer->layout().sizeBytes, dynamicOffset))
            return id;
        const auto& definition = pipeline->originalLayoutOwner()->desc();
        if (!definition.descriptorTables.empty() || !definition.pushConstantRanges.empty())
            return "RHI-ORIGINAL-WHOLE-WRITE-PIPELINE-PROFILE";
        const auto* slot = rhi::findDescriptorSetSlot(definition, set);
        const auto* layout =
            slot ? rhi::findDescriptorSetLayout(definition, slot->layoutName) : nullptr;
        const auto* binding =
            layout ? rhi::findDescriptorSetBinding(*layout, write.binding) : nullptr;
        if (!binding)
            return "RHI-ORIGINAL-WHOLE-WRITE-BINDING";
        rhi::DescriptorBufferBindingRangeContract::Resolved resolved;
        return rhi::OriginalWholeDescriptorWriteContract::validateBinding(
            *binding, write, buffer->layout().sizeBytes, dynamicOffset, resolved);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class Pipeline>
    static const char* lower(const rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        const rhi::DescriptorBindingWrite& write,
        std::uint32_t dynamicOffset,
        WGPUBindGroupEntry& output,
        std::uint32_t& group,
        std::uint32_t& displacement) noexcept
    {
        output = {};
        group = 0u;
        displacement = 0u;
        if (const auto* id = validate(device, pipeline, buffer, set, write, dynamicOffset))
            return id;
        const auto native = webGpuNativePayloadFor(*pipeline);
        const auto source = webGpuNativePayloadFor(*buffer->originalObject());
        if (!native || !native->pipeline || !native->issuedContract() || !source || !source->buffer)
            return "RHI-ORIGINAL-WHOLE-WRITE-NATIVE-PAYLOAD";
        const auto& issued = *native->issuedContract();
        if (source->ownerDevice != static_cast<WGPUDevice>(issued.nativeDevice())
            || !issued.nativeDevice())
            return "RHI-ORIGINAL-WHOLE-WRITE-NATIVE-DEVICE";
        if (source->sizeBytes != buffer->layout().sizeBytes)
            return "RHI-ORIGINAL-WHOLE-WRITE-NATIVE-EXTENT";
        const WebGpuNativePipelineBindingFact* selected = nullptr;
        for (const auto& fact : issued.facts())
            if (fact.mapping.logicalSet == set && fact.mapping.logicalBinding == write.binding) {
                if (selected)
                    return "RHI-ORIGINAL-WHOLE-WRITE-PHYSICAL-BINDING";
                selected = &fact;
            }
        if (!selected || selected->physicalType != write.type
            || selected->mapping.webGpuGroup >= issued.semantics().groupCount
            || issued.semantics().usesPush || !issued.tables().empty())
            return "RHI-ORIGINAL-WHOLE-WRITE-PHYSICAL-PROFILE";
        const auto& mapping = selected->mapping;
        // Consume the original source/target declaration directly. The whole
        // extent is never transformed into an authored finite range or length.
        const auto input = [&] {
            if constexpr (std::is_same_v<Pipeline, rhi::ComputePipeline>)
                return issued.artifactInput();
            else
                return issued.graphicsInput();
        }();
        if (!input)
            return "RHI-ORIGINAL-WHOLE-WRITE-INPUT";
        using Artifact =
            std::remove_const_t<typename std::decay_t<decltype(input->artifact())>::element_type>;
        using Memory = std::conditional_t<std::is_same_v<Pipeline, rhi::ComputePipeline>,
            shader::TargetBufferMemoryLayout,
            shader::GraphicsTargetBufferMemoryLayout>;
        Memory memory;
        if (const auto* id =
                Artifact::originalTargetBufferMemory(input->artifact(), set, write.binding, memory))
            return id;
        const auto type = rhi::descriptorTypeFromShaderBinding(memory.type);
        if (!type || *type != write.type || memory.target.group != mapping.webGpuGroup
            || memory.target.binding != mapping.webGpuBinding || !memory.target.minimumBindingBytes
            || source->sizeBytes < memory.target.minimumBindingBytes
            || source->sizeBytes < selected->nativeDeclaredMinimumBytes)
            return "RHI-ORIGINAL-WHOLE-WRITE-TARGET-MEMBER";
        auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = mapping.webGpuBinding;
        entry.buffer = source->buffer;
        entry.offset = 0u;
        entry.size = source->sizeBytes;
        output = entry;
        group = mapping.webGpuGroup;
        return nullptr;
    }
#endif
};
} // namespace arp::rhi_webgpu
