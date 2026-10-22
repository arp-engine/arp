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

bool VulkanRhiRetainedCommand::dispatch(
    const VulkanRhiRetainedComputeDispatch& request, core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    const auto* pipeline = request.pipeline;
    const auto runtime = command_->impl_->deviceOwner;
    if (!runtime->coreQueueCapabilities().compute)
        return reject("RHI-VULKAN-RETENTION-COMPUTE-QUEUE");
    if (!pipeline || !pipeline->layout() || !pipeline->validationReport().success
        || pipeline->nativeHandleExposedToUpperLayer() || pipeline->bindlessAuthorization()
        || !pipeline->shaderReflection().bindlessContractVersion.empty())
        return reject("RHI-VULKAN-RETENTION-COMPUTE-PIPELINE");
    const auto nativePipeline = vulkanRhiNativePayload(*pipeline);
    if (!nativePipeline || nativePipeline->backendVulkanDeviceHandle() != runtime->logicalDevice()
        || !nativePipeline->backendVulkanComputePipelineHandle()
        || !nativePipeline->backendVulkanComputePipelineLayoutHandle())
        return reject("RHI-VULKAN-RETENTION-COMPUTE-PIPELINE");

    const auto& limits = runtime->nativeLimits();
    const auto& reflection = pipeline->shaderReflection();
    const std::uint32_t size[]{reflection.computeWorkgroupSizeX,
        reflection.computeWorkgroupSizeY,
        reflection.computeWorkgroupSizeZ};
    const std::uint32_t groups[]{request.groupsX, request.groupsY, request.groupsZ};
    std::uint64_t invocations = 1u;
    for (unsigned axis = 0u; axis < 3u; ++axis) {
        if (!size[axis] || size[axis] > limits.maxComputeWorkGroupSize[axis] || !groups[axis]
            || groups[axis] > limits.maxComputeWorkGroupCount[axis]
            || !core::canMultiplyUnsigned(invocations, std::uint64_t{size[axis]}))
            return reject("RHI-VULKAN-RETENTION-COMPUTE-DISPATCH-LIMIT");
        invocations *= size[axis];
    }
    if (invocations > limits.maxComputeWorkGroupInvocations)
        return reject("RHI-VULKAN-RETENTION-COMPUTE-DISPATCH-LIMIT");
    const auto& layout = pipeline->layout()->desc();
    if (reflection.pushConstantByteCount || !layout.pushConstantRanges.empty()) {
        if (const auto* id = rhi::semantics::pushConstantCommandDiagnostic(
                rhi::ShaderStage::Compute, 0u, request.pushConstantBytes, request.pushConstants))
            return reject(id);
        if (const auto* id =
                rhi::semantics::pushConstantBindingDiagnostic(layout.pushConstantRanges,
                    rhi::ShaderStage::Compute,
                    rhi::ShaderStage::Compute,
                    0u,
                    request.pushConstantBytes,
                    &reflection))
            return reject(id);
        if (request.pushConstantBytes > limits.maxPushConstantsSize
            || nativePipeline->backendVulkanPushConstantStageFlags() != VK_SHADER_STAGE_COMPUTE_BIT)
            return reject("RHI-VULKAN-RETENTION-COMPUTE-PUSH-LIMIT");
    } else if (request.pushConstants || request.pushConstantBytes) {
        return reject("RHI-VULKAN-RETENTION-COMPUTE-PUSH-UNDECLARED");
    }

    const VulkanRhiRetainedBindings bindings{request.sets, request.buffers, request.sampledDepth};
    if (!validateBindings(bindings, layout, reflection, rhi::ShaderStage::Compute, diagnostic))
        return false;
    if (!runtime->getDeviceProcAddr("vkCmdBindPipeline")
        || !runtime->getDeviceProcAddr("vkCmdDispatch")
        || (!request.sets.empty() && !runtime->getDeviceProcAddr("vkCmdBindDescriptorSets"))
        || (request.pushConstantBytes && !runtime->getDeviceProcAddr("vkCmdPushConstants")))
        return reject("RHI-VULKAN-RETENTION-COMPUTE-ENTRYPOINT");

    if (!retainBindings(bindings, nativePipeline, diagnostic))
        return false;
    if (!command_->recordComputePipelineBind(*nativePipeline, diagnostic)) {
        failed_ = true;
        return false;
    }
    for (const auto& slot : request.sets)
        if (!command_->recordDescriptorSetBind(
                *vulkanRhiNativePayload(*slot.descriptors), slot.index, nullptr, 0u, diagnostic)) {
            failed_ = true;
            return false;
        }
    if ((request.pushConstantBytes
            && !command_->recordComputePushConstants(
                0u, request.pushConstantBytes, request.pushConstants, diagnostic))
        || !command_->recordDispatch(
            request.groupsX, request.groupsY, request.groupsZ, diagnostic)) {
        failed_ = true;
        return false;
    }
    diagnostic.clear();
    return true;
}

} // namespace arp::rhi_vulkan
