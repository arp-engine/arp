#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_retained_command.h"

namespace arp::rhi_vulkan {

bool VulkanRhiRetainedCommand::drawPreparedPass(const rhi::GpuTexture& target,
    const VulkanRhiRetainedGraphicsDraw& request,
    core::pmr::String& diagnostic)
{
    if (!validatePreparedPass(target, diagnostic))
        return false;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto runtime = command_->impl_->deviceOwner;
    if (!runtime->coreQueueCapabilities().graphics)
        return reject("RHI-VULKAN-RETENTION-GRAPHICS-QUEUE");
    const auto* p = request.pipeline;
    const auto* state = p ? vulkanRhiRetainedGraphicsPipelineState(*p) : nullptr;
    const auto stages = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment;
    if (!p || !p->layout() || !p->validationReport().success || p->bindlessAuthorization()
        || p->nativeHandleExposedToUpperLayer()
        || !p->shaderReflection().bindlessContractVersion.empty() || !state
        || !state->fullscreenColor || state->stages != stages
        || state->color != rhi::TextureFormat::Rgba16Float || state->color != target.desc().format)
        return reject("RHI-VULKAN-RETENTION-GRAPHICS-PIPELINE");
    const auto pipeline = vulkanRhiNativePayload(*p);
    if (!pipeline || pipeline->backendVulkanDeviceHandle() != runtime->logicalDevice()
        || !pipeline->backendVulkanRenderPipelineHandle()
        || !pipeline->backendVulkanRenderPipelineLayoutHandle())
        return reject("RHI-VULKAN-RETENTION-GRAPHICS-PIPELINE");
    if (p->shaderReflection().pushConstantByteCount
        || !p->layout()->desc().pushConstantRanges.empty())
        return reject("RHI-VULKAN-RETENTION-GRAPHICS-PUSH-UNSUPPORTED");
    const auto& v = request.viewport;
    const auto& s = request.scissor;
    const auto& limits = runtime->nativeLimits();
    if (v.x != 0.0f || v.y != 0.0f || v.width != float(preparedPassExtent_.width)
        || v.height != float(preparedPassExtent_.height) || v.minDepth != 0.0f || v.maxDepth != 1.0f
        || s.x != 0 || s.y != 0 || s.width != preparedPassExtent_.width
        || s.height != preparedPassExtent_.height || s.width > limits.maxViewportDimensions[0]
        || s.height > limits.maxViewportDimensions[1] || v.x < limits.viewportBoundsRange[0]
        || v.y < limits.viewportBoundsRange[0] || v.x + v.width > limits.viewportBoundsRange[1]
        || v.y + v.height > limits.viewportBoundsRange[1])
        return reject("RHI-VULKAN-RETENTION-GRAPHICS-RASTER");
    if (request.vertexCount != 3u || request.instanceCount != 1u || request.firstVertex
        || request.firstInstance)
        return reject("RHI-VULKAN-RETENTION-GRAPHICS-DRAW");
    if (!validateBindings(request.bindings,
            p->layout()->desc(),
            p->shaderReflection(),
            rhi::ShaderStage::Fragment,
            diagnostic))
        return false;
    const auto attachment = vulkanRhiNativePayload(target)->backendVulkanImageHandle();
    for (const auto& input : request.bindings.sampledTextures)
        if (input.texture->id() == target.id()
            || vulkanRhiNativePayload(*input.texture)->backendVulkanImageHandle() == attachment)
            return reject("RHI-VULKAN-RETENTION-GRAPHICS-ATTACHMENT-ALIAS");
    for (const auto* name :
        {"vkCmdBindPipeline", "vkCmdSetViewport", "vkCmdSetScissor", "vkCmdDraw"})
        if (!runtime->getDeviceProcAddr(name))
            return reject("RHI-VULKAN-RETENTION-GRAPHICS-ENTRYPOINT");
    if (!request.bindings.sets.empty() && !runtime->getDeviceProcAddr("vkCmdBindDescriptorSets"))
        return reject("RHI-VULKAN-RETENTION-GRAPHICS-ENTRYPOINT");
    rhi::CommandRecordingUsage charge;
    for (const auto kind : {rhi::CommandUsageKind::BeginRenderPass,
             rhi::CommandUsageKind::BindGraphicsPipeline,
             rhi::CommandUsageKind::SetViewport,
             rhi::CommandUsageKind::SetScissor,
             rhi::CommandUsageKind::Draw,
             rhi::CommandUsageKind::EndRenderPass})
        charge.append(kind);
    charge.append(rhi::CommandUsageKind::BindRenderDescriptor, request.bindings.sets.size());
    charge.resourceReferences = 1u; // Attachment visit in addition to bindings.
    if (!chargeBindings(charge, request.bindings, diagnostic)
        || !preflightRecordingCharge(charge, diagnostic))
        return false;
    if (!retainBindings(request.bindings, pipeline, diagnostic)
        || !commitRecordingCharge(charge, diagnostic))
        return false;
    if (!beginPreparedPass(target, diagnostic)) {
        failed_ = true;
        return false;
    }
    bool success = command_->recordGraphicsPipelineBind(*pipeline, diagnostic)
        && command_->recordSetViewport(v, diagnostic) && command_->recordSetScissor(s, diagnostic);
    for (const auto& slot : request.bindings.sets) {
        if (!success)
            break;
        success = command_->recordDescriptorSetBind(
            *vulkanRhiNativePayload(*slot.descriptors), slot.index, nullptr, 0u, diagnostic);
    }
    if (success)
        success = command_->recordDraw(request.vertexCount,
            request.instanceCount,
            request.firstVertex,
            request.firstInstance,
            diagnostic);
    endPreparedPass();
    if (!success) {
        failed_ = true;
        return false;
    }
    diagnostic.clear();
    return true;
}

} // namespace arp::rhi_vulkan
