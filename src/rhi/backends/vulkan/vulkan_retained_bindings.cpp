#include "core/math/unsigned_range.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_retained_command.h"
#include "rhi/backends/vulkan/vulkan_texture_descriptor.h"
#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/descriptor_whole_buffer_binding.h"
#include "rhi/frontend/resource_binding_layout.h"
#include "rhi/semantics/push_constant_validation.h"

#include <limits>

namespace arp::rhi_vulkan {

bool VulkanRhiRetainedCommand::validateSampledTexture(const VulkanRhiRetainedSampledTexture& input,
    bool allowColor,
    core::pmr::String& diagnostic) const
{
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    if (!input.texture)
        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-REGISTRATION");
    const bool depth = input.texture->desc().format == rhi::TextureFormat::D32Float;
    if ((!depth
            && (!allowColor || input.texture->desc().format != rhi::TextureFormat::Rgba16Float
                || !(colorFormatFeatures_ & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)))
        || (depth && !sampledDepthLoadAvailable_))
        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-CAPABILITY");
    if (!input.view || !input.texture || !vulkanRhiTextureRegistryMatches(*input.texture, registry_)
        || !registry_->textureObjectMatches(*input.texture)
        || !vulkanRhiTextureViewRegistryMatches(*input.view, registry_)
        || input.view->textureId() != input.texture->id()
        || !registry_->textureViewParentIsLive(input.view->id(), input.texture->id()))
        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-REGISTRATION");
    const auto view = vulkanRhiNativePayload(*input.view);
    const auto texture = vulkanRhiNativePayload(*input.texture);
    const auto registeredView = registry_->payload(input.view->id());
    const auto registeredTexture = registry_->payload(input.texture->id());
    const auto parent = view ? view->backendVulkanTextureViewParentPayload() : nullptr;
    const auto device = command_->impl_->device.logicalDevice();
    if (!view || !texture || view != registeredView || texture != registeredTexture
        || !core::SharedOwnerProjection::sameOwner(view, registeredView)
        || !core::SharedOwnerProjection::sameOwner(texture, registeredTexture) || parent != texture
        || !core::SharedOwnerProjection::sameOwner(parent, texture)
        || view->backendVulkanDeviceHandle() != device
        || texture->backendVulkanDeviceHandle() != device || !texture->backendVulkanImageHandle()
        || view->backendVulkanImageHandle() != texture->backendVulkanImageHandle()
        || view->backendVulkanOriginalContentExclusive()
        || input.texture->originalContentExclusive())
        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-PAYLOAD");
    const auto& desc = input.texture->desc();
    const auto& vd = input.view->desc();
    const auto* original = view->backendVulkanOriginalTextureViewDesc();
    if (!original || !rhi::TextureViewResourceLayout::capture(vd).matches(*original)
        || vd.format != desc.format || desc.extent.depth != 1u || desc.sampleCount != 1u
        || desc.cubeCompatible || !desc.extent.width || !desc.extent.height
        || vd.dimension != rhi::TextureViewDimension::Texture2D || vd.arrayLayerCount != 1u
        || vd.aspect != (depth ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color)
        || vd.usage != rhi::TextureUsage::Sampled
        || (desc.usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None
        || texture->backendVulkanImageFormat() != VulkanTextureDescriptor::format(desc.format)
        || texture->backendVulkanImageExtent() != desc.extent)
        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-SHAPE");
    if (!vd.mipLevelCount
        || !core::containsUnsignedRange<std::uint32_t>(
            0u, desc.mipLevelCount, vd.baseMipLevel, vd.mipLevelCount)
        || !core::containsUnsignedRange<std::uint32_t>(
            0u, desc.arrayLayerCount, vd.baseArrayLayer, vd.arrayLayerCount))
        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-RANGE");
    const rhi::TextureSubresourceRange range{
        vd.baseMipLevel, vd.mipLevelCount, vd.baseArrayLayer, vd.arrayLayerCount, vd.aspect};
    if (!vulkanRhiTextureStateAvailable(
            *input.texture, range, command_->impl_->retainedResourceStates.get())
        || input.texture->subresourceState(range) != rhi::ResourceState::ShaderRead)
        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-STATE");
    return true;
}

bool VulkanRhiRetainedCommand::validateBindings(const VulkanRhiRetainedBindings& request,
    const rhi::PipelineLayoutDesc& layout,
    const rhi::ShaderReflectionDesc& reflection,
    rhi::ShaderStage stage,
    core::pmr::String& diagnostic) const
{
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto runtime = command_->impl_->deviceOwner;
    const auto& limits = runtime->nativeLimits();
    if ((!request.sets.empty() && !request.sets.data())
        || (!request.buffers.empty() && !request.buffers.data())
        || (!request.sampledTextures.empty() && !request.sampledTextures.data()))
        return reject("RHI-VULKAN-RETENTION-COMPUTE-METADATA-MISSING");
    const auto reflectedLayout =
        rhi::validatePipelineLayoutAgainstShaderReflection(layout, reflection);
    if (!reflectedLayout.success) {
        diagnostic = reflectedLayout.diagnostics;
        return false;
    }
    if (!layout.descriptorTables.empty() || request.sets.size() != layout.descriptorSets.size()
        || request.sets.size() > rhi::BoundDescriptorSlots::kMaxTrackedSlots)
        return reject("RHI-VULKAN-RETENTION-COMPUTE-SET-SHAPE");
    for (std::size_t i = 0u; i < request.buffers.size(); ++i) {
        if (!request.buffers[i])
            return reject("RHI-VULKAN-RETENTION-COMPUTE-BUFFER-REGISTRATION");
        if (!resolve(*request.buffers[i], diagnostic))
            return false;
        for (std::size_t j = 0u; j < i; ++j)
            if (request.buffers[j]->id() == request.buffers[i]->id())
                return reject("RHI-VULKAN-RETENTION-COMPUTE-BUFFER-DUPLICATE");
    }
    for (std::size_t i = 0u; i < request.sampledTextures.size(); ++i) {
        if (!validateSampledTexture(
                request.sampledTextures[i], stage == rhi::ShaderStage::Fragment, diagnostic))
            return false;
        for (std::size_t j = 0u; j < i; ++j)
            if (request.sampledTextures[j].view->id() == request.sampledTextures[i].view->id())
                return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-DUPLICATE");
    }
    rhi::BoundDescriptorSlots bound;
    std::uint64_t uniforms = 0u, storageBuffers = 0u, sampledImages = 0u;
    if (limits.minUniformBufferOffsetAlignment > UINT32_MAX
        || limits.minStorageBufferOffsetAlignment > UINT32_MAX)
        return reject("RHI-VULKAN-RETENTION-COMPUTE-BINDING-LIMIT");
    const rhi::semantics::DescriptorWholeBufferBinding::Limits bindingLimits{1u,
        static_cast<std::uint32_t>(limits.minUniformBufferOffsetAlignment),
        static_cast<std::uint32_t>(limits.minStorageBufferOffsetAlignment),
        limits.maxUniformBufferRange,
        limits.maxStorageBufferRange};
    for (std::size_t i = 0u; i < request.sets.size(); ++i) {
        const auto& slot = request.sets[i];
        if (!slot.descriptors || slot.index >= limits.maxBoundDescriptorSets
            || !vulkanRhiDescriptorSetContextMatches(*slot.descriptors, runtime, registry_))
            return reject("RHI-VULKAN-RETENTION-COMPUTE-SET-REGISTRATION");
        for (std::size_t j = 0u; j < i; ++j)
            if (request.sets[j].index == slot.index)
                return reject("RHI-VULKAN-RETENTION-COMPUTE-SET-DUPLICATE");
        const auto& set = *slot.descriptors;
        if (set.hasOriginalFiniteContent() || set.hasOriginalWholeContent()
            || set.hasOriginalGeneralContent())
            return reject("RHI-VULKAN-RETENTION-COMPUTE-ORIGINAL-DOMAIN");
        if (const auto* id =
                rhi::DescriptorSlotShapeContract::validateSet(layout, slot.index, set.layout()))
            return reject(id);
        const auto complete = rhi::validateDescriptorResources(
            set, rhi::DescriptorContentRequirement::CompleteLayout);
        if (!complete.success) {
            diagnostic = complete.diagnostics;
            return false;
        }
        const auto nativeSet = vulkanRhiNativePayload(set);
        if (!nativeSet)
            return reject("RHI-VULKAN-RETENTION-COMPUTE-SET-PAYLOAD");
        for (const auto& binding : set.layout().bindings) {
            const bool uniform = binding.type == rhi::DescriptorType::UniformBuffer;
            const bool writable = binding.type == rhi::DescriptorType::StorageBuffer;
            const bool sampledDepth = binding.type == rhi::DescriptorType::SampledTexture;
            if ((!uniform && !writable && binding.type != rhi::DescriptorType::ReadOnlyStorageBuffer
                    && !sampledDepth)
                || binding.dynamicOffset || binding.tableBacked || binding.stages != stage
                || (stage == rhi::ShaderStage::Fragment && writable))
                return reject("RHI-VULKAN-RETENTION-COMPUTE-BINDING-DOMAIN");
            (sampledDepth     ? sampledImages
                    : uniform ? uniforms
                              : storageBuffers) += binding.arrayCount;
            for (std::uint32_t element = 0u; element < binding.arrayCount; ++element) {
                const char* observationDiagnostic = nullptr;
                const auto observation = set.observeSuccessfulBindingWrite(
                    binding.binding, element, observationDiagnostic);
                const auto* write = observation ? &observation->write() : nullptr;
                if (sampledDepth) {
                    if (!write || write->resourceKind != rhi::DescriptorResourceKind::TextureView
                        || write->physicalLowering.enabled
                        || write->resourceState != rhi::ResourceState::ShaderRead
                        || !binding.textureViewDimensionConstrained
                        || binding.textureViewDimension != rhi::TextureViewDimension::Texture2D
                        || binding.sampledTextureFilterable)
                        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-BINDING");
                    const VulkanRhiRetainedSampledDepth* selectedInput = nullptr;
                    for (const auto& input : request.sampledTextures)
                        if (input.view->id() == write->resource)
                            selectedInput = &input;
                    if (!selectedInput)
                        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-MISSING");
                    if (binding.depthTexture
                        != (selectedInput->texture->desc().format == rhi::TextureFormat::D32Float))
                        return reject("RHI-VULKAN-RETENTION-SAMPLED-TEXTURE-DEPTH-TYPE");
                    const auto index =
                        rhi::descriptorElementIndex(set.layout(), binding.binding, element);
                    const auto selected = nativeSet->backendVulkanDescriptorElementResource(index);
                    const auto actual = vulkanRhiNativePayload(*selectedInput->view);
                    if (selected != actual
                        || !core::SharedOwnerProjection::sameOwner(selected, actual)
                        || !nativeSet->backendVulkanDescriptorElementImageView(index))
                        return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-DESCRIPTOR");
                    continue;
                }
                if (!write || write->resourceKind != rhi::DescriptorResourceKind::Buffer
                    || write->physicalLowering.enabled
                    || write->bufferBindingRange.contractVersion != 1u
                    || write->bufferBindingRange.offsetBytes || write->bufferBindingRange.sizeBytes)
                    return reject("RHI-VULKAN-RETENTION-COMPUTE-BUFFER-WRITE");
                const rhi::GpuBuffer* buffer = nullptr;
                for (const auto* candidate : request.buffers)
                    if (candidate->id() == write->resource)
                        buffer = candidate;
                if (!buffer)
                    return reject("RHI-VULKAN-RETENTION-COMPUTE-BUFFER-MISSING");
                const auto usage =
                    uniform ? rhi::ResourceUsage::UniformBuffer : rhi::ResourceUsage::StorageBuffer;
                const auto state =
                    writable ? rhi::ResourceState::ShaderWrite : rhi::ResourceState::ShaderRead;
                if ((buffer->desc().usage & usage) == rhi::ResourceUsage::None
                    || buffer->state() != state)
                    return reject("RHI-VULKAN-RETENTION-COMPUTE-BUFFER-STATE");
                if (writable
                    && (buffer->gpuWritesSealed()
                        || registry_->bufferGpuWritesSealed(buffer->id())))
                    return reject("RHI-BUFFER-GPU-WRITES-SEALED");
                if (const auto* id = rhi::semantics::DescriptorWholeBufferBinding::diagnostic(
                        rhi::semantics::DescriptorWholeBufferBinding::validate(
                            binding.type, buffer->desc().sizeBytes, 0u, bindingLimits)))
                    return reject(id);
                const auto index =
                    rhi::descriptorElementIndex(set.layout(), binding.binding, element);
                const auto selected = nativeSet->backendVulkanDescriptorElementResource(index);
                const auto original = vulkanRhiNativePayload(*buffer);
                if (selected != original
                    || !core::SharedOwnerProjection::sameOwner(selected, original)
                    || nativeSet->backendVulkanDescriptorElementBufferRange(index)
                        != std::pair<std::uint64_t, std::uint64_t>{0u, VK_WHOLE_SIZE})
                    return reject("RHI-VULKAN-RETENTION-COMPUTE-DESCRIPTOR-RESOURCE");
            }
        }
        const auto tracked = bound.bindSet(slot.index, set, nullptr, 0u);
        if (!tracked.success) {
            diagnostic = tracked.diagnostics;
            return false;
        }
    }
    if (uniforms > limits.maxPerStageDescriptorUniformBuffers
        || uniforms > limits.maxDescriptorSetUniformBuffers
        || storageBuffers > limits.maxPerStageDescriptorStorageBuffers
        || storageBuffers > limits.maxDescriptorSetStorageBuffers
        || sampledImages > limits.maxPerStageDescriptorSampledImages
        || sampledImages > limits.maxDescriptorSetSampledImages
        || uniforms + storageBuffers + sampledImages > limits.maxPerStageResources)
        return reject("RHI-VULKAN-RETENTION-COMPUTE-BINDING-LIMIT");
    for (const auto* buffer : request.buffers) {
        bool selected = false;
        for (const auto& slot : request.sets)
            for (const auto& binding : slot.descriptors->layout().bindings)
                for (std::uint32_t element = 0u; element < binding.arrayCount; ++element) {
                    const char* observationDiagnostic = nullptr;
                    const auto observation = slot.descriptors->observeSuccessfulBindingWrite(
                        binding.binding, element, observationDiagnostic);
                    if (!observation)
                        return reject("RHI-VULKAN-RETENTION-COMPUTE-WRITE-CHANGED");
                    selected = selected || observation->write().resource == buffer->id();
                }
        if (!selected)
            return reject("RHI-VULKAN-RETENTION-COMPUTE-BUFFER-UNUSED");
    }
    for (const auto& input : request.sampledTextures) {
        bool selected = false;
        for (const auto& slot : request.sets)
            for (const auto& binding : slot.descriptors->layout().bindings)
                for (std::uint32_t element = 0u; element < binding.arrayCount; ++element) {
                    const char* id = nullptr;
                    const auto observation = slot.descriptors->observeSuccessfulBindingWrite(
                        binding.binding, element, id);
                    if (!observation)
                        return reject("RHI-VULKAN-RETENTION-COMPUTE-WRITE-CHANGED");
                    selected = selected || observation->write().resource == input.view->id();
                }
        if (!selected)
            return reject("RHI-VULKAN-RETENTION-SAMPLED-DEPTH-UNUSED");
    }
    const auto complete = bound.validateComplete(layout, reflection, stage);
    if (!complete.success) {
        diagnostic = complete.diagnostics;
        return false;
    }
    return true;
}

bool VulkanRhiRetainedCommand::retainBindings(const VulkanRhiRetainedBindings& request,
    const std::shared_ptr<VulkanRhiNativeResourcePayload>& nativePipeline,
    core::pmr::String& diagnostic)
{
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    if (request.sets.size() >= std::numeric_limits<std::size_t>::max() - request.buffers.size())
        return reject("RHI-VULKAN-RETENTION-COMPUTE-OWNER-OVERFLOW");
    const auto bufferEnd = 1u + request.sets.size() + request.buffers.size();
    if (request.sampledTextures.size() > (std::numeric_limits<std::size_t>::max() - bufferEnd) / 2u)
        return reject("RHI-VULKAN-RETENTION-COMPUTE-OWNER-OVERFLOW");
    const auto ownerCount = bufferEnd + request.sampledTextures.size() * 2u;
    const auto ownerAt = [&](std::size_t index) noexcept -> std::shared_ptr<const void> {
        if (!index)
            return nativePipeline;
        --index;
        if (index < request.sets.size())
            return vulkanRhiNativePayload(*request.sets[index].descriptors);
        index -= request.sets.size();
        if (index < request.buffers.size())
            return vulkanRhiNativePayload(*request.buffers[index]);
        index -= request.buffers.size();
        const auto& input = request.sampledTextures[index / 2u];
        return index % 2u ? vulkanRhiNativePayload(*input.texture)
                          : vulkanRhiNativePayload(*input.view);
    };
    auto& retention = *command_->impl_->recordingRetention;
    if (const auto* id = retention.preflightResourceProjection(ownerCount, ownerAt))
        return reject(id);
    if (const auto* id = retention.retainResourceProjection(ownerCount, ownerAt)) {
        failed_ = true;
        return reject(id);
    }
    return true;
}

} // namespace arp::rhi_vulkan
