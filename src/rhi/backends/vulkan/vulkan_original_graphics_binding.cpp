#include "rhi/backends/vulkan/vulkan_original_graphics_binding.h"

#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_original_compute_pipeline.h"

namespace arp::rhi_vulkan {

const char* VulkanOriginalGraphicsBindingInspection::inspect(Contract::Request request,
    const rhi::GpuDevice& device,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
    std::size_t pipelineSourceIndex,
    std::uint32_t logicalSet,
    const rhi::OriginalCommandDescriptorSetElement& element,
    Observation& output) noexcept
{
    output = {};
    const auto decision = Contract::lowering(device.backend(), request);
    if (!decision.inspectionAvailable)
        return decision.diagnostic;
    return request.contractVersion == 2u ? inspectOriginal<true>(device,
                                               sources,
                                               pipelineSourceIndex,
                                               logicalSet,
                                               element,
                                               output,
                                               request.sampledColor)
                                         : inspectOriginal<false>(device,
                                               sources,
                                               pipelineSourceIndex,
                                               logicalSet,
                                               element,
                                               output,
                                               request.sampledColor);
}

template <bool compute>
const char* VulkanOriginalGraphicsBindingInspection::inspectOriginal(const rhi::GpuDevice& device,
    const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
    std::size_t pipelineSourceIndex,
    std::uint32_t logicalSet,
    const rhi::OriginalCommandDescriptorSetElement& element,
    Observation& output,
    rhi::semantics::OriginalOrdinarySampledColorContract::Intent sampledColor) noexcept
{
    std::shared_ptr<const Contract::PipelineSource<compute>> source;
    if (const auto* id =
            Contract::inspectPipeline<compute>(device, sources, pipelineSourceIndex, source))
        return id;
    const auto& pipeline = source->originalPipeline();
    const auto receipt = [&] {
        if constexpr (compute)
            return std::dynamic_pointer_cast<const VulkanOriginalArtifactComputeCreation>(
                pipeline->originalComputeCreation());
        else
            return std::dynamic_pointer_cast<const VulkanOriginalArtifactGraphicsCreation>(
                pipeline->originalGraphicsCreation());
    }();
    if (!receipt)
        return "RHI-GRAPHICS-BINDING-INSPECTION-ISSUER";
    if (const auto* id = receipt->validate())
        return id;
    const auto& runtime = receipt->runtime_;
    if (!runtime || !runtime->valid() || runtime->syntheticRuntime()
        || runtime->coreQueueCapabilityDiagnostic())
        return "RHI-GRAPHICS-BINDING-INSPECTION-RUNTIME";
    Contract::BindingInputs inputs;
    if (const auto* id =
            Contract::inspectBinding<compute>(device, sources, source, logicalSet, element, inputs))
        return id;
    const auto* binding = inputs.binding;
    const auto& owner = inputs.resource;
    const auto native = vulkanRhiNativePayload(*pipeline);
    if (!native || native->backendVulkanDeviceHandle() != runtime->logicalDevice()
        || !(compute ? native->backendVulkanComputePipelineHandle()
                     : native->backendVulkanRenderPipelineHandle())
        || !(compute ? native->backendVulkanComputePipelineLayoutHandle()
                     : native->backendVulkanRenderPipelineLayoutHandle()))
        return "RHI-GRAPHICS-BINDING-INSPECTION-NATIVE-PIPELINE";
    Observation observed;
    observed.device = runtime->logicalDevice();
    observed.pipeline = compute ? native->backendVulkanComputePipelineHandle()
                                : native->backendVulkanRenderPipelineHandle();
    observed.layout = compute ? native->backendVulkanComputePipelineLayoutHandle()
                              : native->backendVulkanRenderPipelineLayoutHandle();
    observed.set = logicalSet;
    observed.binding = binding->binding;
    const auto* id = std::visit(
        [&](const auto& resource) -> const char* {
            using T = std::remove_const_t<typename std::decay_t<decltype(resource)>::element_type>;
            const auto payload = vulkanRhiNativePayload(*resource->originalObject());
            if (!payload || payload->backendVulkanDeviceHandle() != observed.device)
                return "RHI-GRAPHICS-BINDING-INSPECTION-NATIVE-RESOURCE";
            if constexpr (std::is_same_v<T, rhi::OriginalCommandBufferLease>) {
                if (binding->type == rhi::DescriptorType::StorageBuffer
                    && resource->originalObject()->gpuWritesSealed())
                    return "RHI-BUFFER-GPU-WRITES-SEALED";
                rhi::DescriptorBufferBindingRangeContract::Resolved range;
                if (const auto* error = rhi::DescriptorBufferBindingRangeContract::resolve(
                        element.write.bufferBindingRange, resource->layout().sizeBytes, 0u, range))
                    return error;
                const auto& limits = runtime->descriptorIndexing().limits;
                const bool uniform = binding->type == rhi::DescriptorType::UniformBuffer;
                const auto alignment = uniform ? limits.minUniformBufferOffsetAlignment
                                               : limits.minStorageBufferOffsetAlignment;
                const auto maximum =
                    uniform ? limits.maxUniformBufferRange : limits.maxStorageBufferRange;
                if (!alignment || !maximum || !range.sizeBytes || range.offsetBytes % alignment
                    || range.sizeBytes > maximum || !payload->backendVulkanBufferHandle())
                    return "RHI-GRAPHICS-BINDING-INSPECTION-BUFFER-LIMITS";
                // The existing artifact loader owns these SPIR-V declaration
                // proofs. No WGSL minimum, guessed C++ size or reparsing is used.
                const auto entries = [&] {
                    const auto& artifact = receipt->originalInput()->artifact();
                    if constexpr (compute)
                        return std::array{
                            artifact, std::shared_ptr<const shader::SingleEntryShaderArtifact>{}};
                    else
                        return std::array{artifact->vertex(), artifact->fragment()};
                }();
                const auto stages = compute
                    ? std::array{rhi::ShaderStage::Compute, rhi::ShaderStage::None}
                    : std::array{rhi::ShaderStage::Vertex, rhi::ShaderStage::Fragment};
                Contract::BufferExtent extent;
                if (const auto* error = Contract::inspectBufferExtent(
                        entries, stages, logicalSet, *binding, range.sizeBytes, extent))
                    return error;
                const auto& first = extent.declaration;
                observed.runtimeElementCount = extent.runtimeElementCount;
                if (first.hasRuntimeArray) {
                    observed.runtimeStrideBytes = first.runtimeArrayStrideBytes;
                }
                observed.staticOccupiedBytes = first.staticOccupiedExtentBytes;
                observed.type =
                    uniform ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                observed.buffer = VkDescriptorBufferInfo{
                    payload->backendVulkanBufferHandle(), range.offsetBytes, range.sizeBytes};
            } else if constexpr (std::is_same_v<T, rhi::OriginalCommandTextureViewSource>) {
                const auto& parent = resource->originalParentLease();
                if (sources->originalLeaseIndex(parent) == SIZE_MAX)
                    return "RHI-GRAPHICS-BINDING-INSPECTION-VIEW-PARENT-SOURCE";
                if (const auto* error =
                        device.validateOriginalResourceRegistration(*parent->originalObject()))
                    return error;
                const auto parentPayload = vulkanRhiNativePayload(*parent->originalObject());
                const auto* view = payload->backendVulkanOriginalTextureViewDesc();
                if (!parentPayload || !view
                    || !resource->originalViewLease()->layout().matches(*view)
                    || !core::SharedOwnerProjection::sameOwner(
                        parentPayload, payload->backendVulkanTextureViewParentPayload())
                    || parentPayload->backendVulkanDeviceHandle() != observed.device
                    || !payload->backendVulkanImageHandle()
                    || payload->backendVulkanImageHandle()
                        != parentPayload->backendVulkanImageHandle()
                    || payload->backendVulkanImageFormat() == VK_FORMAT_UNDEFINED
                    || view->dimension != rhi::TextureViewDimension::Texture2D
                    || (view->aspect != rhi::TextureAspect::Color
                        && view->aspect != rhi::TextureAspect::Depth))
                    return "RHI-GRAPHICS-BINDING-INSPECTION-VIEW-PARENT-IDENTITY";
                if (const auto* error = Contract::validateTexture(parent->originalObject()->desc(),
                        *view,
                        binding->depthTexture,
                        sampledColor))
                    return error;
                VkFormatProperties properties{};
                vkGetPhysicalDeviceFormatProperties(
                    runtime->physicalDevice(), payload->backendVulkanImageFormat(), &properties);
                const auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
                    | (binding->sampledTextureFilterable
                            ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
                            : 0u);
                if ((properties.optimalTilingFeatures & required) != required)
                    return "RHI-GRAPHICS-BINDING-INSPECTION-FORMAT-FEATURES";
                observed.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                // Match ResourceState::ShaderRead in the ordinary barrier and
                // existing descriptor lowering, including sampled depth.
                observed.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                observed.imageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                observed.imageView.image = payload->backendVulkanImageHandle();
                observed.imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
                observed.imageView.format = payload->backendVulkanImageFormat();
                observed.imageView.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY};
                observed.imageView.subresourceRange = VkImageSubresourceRange{
                    view->aspect == rhi::TextureAspect::Depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                              : VK_IMAGE_ASPECT_COLOR_BIT,
                    view->baseMipLevel,
                    view->mipLevelCount,
                    view->baseArrayLayer,
                    view->arrayLayerCount};
            } else {
                if (!payload->backendVulkanSamplerHandle())
                    return "RHI-GRAPHICS-BINDING-INSPECTION-NATIVE-SAMPLER";
                observed.type = VK_DESCRIPTOR_TYPE_SAMPLER;
                observed.sampler = payload->backendVulkanSamplerHandle();
            }
            return nullptr;
        },
        *owner);
    if (id)
        return id;
    observed.available = true;
    output = observed;
    return nullptr;
}

} // namespace arp::rhi_vulkan
