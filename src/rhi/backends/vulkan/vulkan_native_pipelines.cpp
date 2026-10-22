#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_initialized_graphics.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_bindless.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_execution_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_push_consumption_lowering.h"
#include "rhi/semantics/bindless_scene_raster.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace arp::rhi_vulkan {

namespace {
// Cold preparation freezes the real native payloads. Owners retain the original
// pipeline, immutable descriptor snapshot, authorization and pool reservations.
template <class Base, class Owners, rhi::BindlessBindingDomain Domain>
class VulkanPreparedBinding final : public Base {
public:
    VulkanPreparedBinding(std::shared_ptr<const Owners> owners, VkDevice device)
        : Base(std::move(owners)), device_(device),
          pipeline_(vulkanRhiNativePayload(*this->owners()->desc().pipeline)),
          descriptors_(this->owners()->desc().snapshot->descriptorSet()
                  ? descriptorPayload(*this->owners()->desc().snapshot)
                  : nullptr),
          pipelineHandle_(pipeline_ ? nativePipeline(*pipeline_) : VK_NULL_HANDLE),
          layoutHandle_(pipeline_ ? nativeLayout(*pipeline_) : VK_NULL_HANDLE),
          setHandle_(
              descriptors_ ? descriptors_->backendVulkanDescriptorSetHandle() : VK_NULL_HANDLE)
    {}

    rhi::ValidationResult validatePayload() const override
    {
        auto valid = this->validateOwners();
        if (!valid.success)
            return valid;
        const auto& desc = this->owners()->desc();
        const auto livePipeline = vulkanRhiNativePayload(*desc.pipeline);
        const auto liveSet =
            desc.snapshot->descriptorSet() ? descriptorPayload(*desc.snapshot) : nullptr;
        if (desc.plan.execution != rhi::BindlessExecution::Native
            || (!desc.plan.executable
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(desc.plan, 6u)
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                    desc.plan))
            || desc.pipeline->validationReport().backend != rhi::Backend::Vulkan
            || !desc.pipeline->validationReport().success || !device_ || !pipeline_ || !descriptors_
            || !core::SharedOwnerProjection::sameOwner(pipeline_, livePipeline)
            || !core::SharedOwnerProjection::sameOwner(descriptors_, liveSet)
            || livePipeline.get() != pipeline_.get() || liveSet.get() != descriptors_.get()
            || pipeline_->backendVulkanDeviceHandle() != device_
            || descriptors_->backendVulkanDeviceHandle() != device_ || !pipelineHandle_
            || pipelineHandle_ != nativePipeline(*pipeline_) || !layoutHandle_
            || layoutHandle_ != nativeLayout(*pipeline_) || !setHandle_
            || setHandle_ != descriptors_->backendVulkanDescriptorSetHandle())
            return {false,
                rhi::bindlessBindingDiagnostic(
                    Domain, rhi::BindlessBindingDiagnostic::PayloadMismatch)};
        return rhi::validateDescriptorSetBinding(
            desc.pipeline->layout(), desc.snapshot->setIndex(), *desc.snapshot->descriptorSet());
    }

    std::uint32_t argumentBufferCount() const noexcept override { return 0u; }

    std::uint64_t argumentBufferBytes() const noexcept override { return 0u; }

private:
    static auto descriptorPayload(const rhi::BindlessSnapshot& snapshot)
    {
        return rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                   snapshot.plan())
            ? prepareVulkanOriginalImageCellDescriptors(snapshot)
            : vulkanRhiNativePayload(*snapshot.descriptorSet());
    }

    static VkPipeline nativePipeline(const VulkanRhiNativeResourcePayload& payload) noexcept
    {
        if constexpr (Domain == rhi::BindlessBindingDomain::Compute)
            return payload.backendVulkanComputePipelineHandle();
        else
            return payload.backendVulkanRenderPipelineHandle();
    }

    static VkPipelineLayout nativeLayout(const VulkanRhiNativeResourcePayload& payload) noexcept
    {
        if constexpr (Domain == rhi::BindlessBindingDomain::Compute)
            return payload.backendVulkanComputePipelineLayoutHandle();
        else
            return payload.backendVulkanRenderPipelineLayoutHandle();
    }

    VkDevice device_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> pipeline_, descriptors_;
    VkPipeline pipelineHandle_;
    VkPipelineLayout layoutHandle_;
    VkDescriptorSet setHandle_;
};

using VulkanPreparedComputeBinding = VulkanPreparedBinding<rhi::BindlessPreparedComputeBinding,
    rhi::BindlessComputeBindingOwners,
    rhi::BindlessBindingDomain::Compute>;
using VulkanPreparedGraphicsBinding = VulkanPreparedBinding<rhi::BindlessPreparedGraphicsBinding,
    rhi::BindlessGraphicsBindingOwners,
    rhi::BindlessBindingDomain::Graphics>;

template <class Binding, class Owners, rhi::BindlessBindingDomain Domain>
std::shared_ptr<const Binding> prepareVulkanBinding(const rhi::BindlessCrossExtension& issuer,
    const VulkanRhiRuntime* runtime,
    std::shared_ptr<const Owners> owners,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!runtime || !runtime->valid() || runtime->syntheticRuntime() || !owners
        || !owners->sharedOwnerMatches(owners)) {
        diagnostic =
            rhi::bindlessBindingDiagnostic(Domain, rhi::BindlessBindingDiagnostic::PayloadMismatch);
        return {};
    }
    const auto valid = owners->validate();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    if (!owners->authorization()->issuedBy(&issuer, owners->authorization())) {
        diagnostic = rhi::bindlessBindingDiagnostic(
            Domain, rhi::BindlessBindingDiagnostic::AuthorizationInvalid);
        return {};
    }
    auto binding = core::makeShared<Binding>(std::move(owners), runtime->logicalDevice());
    const auto payload = binding->validatePayload();
    if (!payload.success) {
        diagnostic = payload.diagnostics;
        return {};
    }
    return binding;
}
} // namespace

bool isVulkanPreparedComputeBinding(const rhi::BindlessPreparedComputeBinding& binding) noexcept
{
    return dynamic_cast<const VulkanPreparedComputeBinding*>(&binding) != nullptr;
}

bool isVulkanPreparedGraphicsBinding(const rhi::BindlessPreparedGraphicsBinding& binding) noexcept
{
    return dynamic_cast<const VulkanPreparedGraphicsBinding*>(&binding) != nullptr;
}

std::shared_ptr<const rhi::BindlessPreparedComputeBinding>
VulkanBindlessCrossExtension::prepareComputeBinding(
    std::shared_ptr<const rhi::BindlessComputeBindingOwners> owners,
    const rhi::BindlessComputeBindingBudget&,
    core::pmr::String& diagnostic) const
{
    return prepareVulkanBinding<VulkanPreparedComputeBinding,
        rhi::BindlessComputeBindingOwners,
        rhi::BindlessBindingDomain::Compute>(*this, runtime_, std::move(owners), diagnostic);
}

std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>
VulkanBindlessCrossExtension::prepareGraphicsBinding(
    std::shared_ptr<const rhi::BindlessGraphicsBindingOwners> owners,
    const rhi::BindlessGraphicsBindingBudget&,
    core::pmr::String& diagnostic) const
{
    return prepareVulkanBinding<VulkanPreparedGraphicsBinding,
        rhi::BindlessGraphicsBindingOwners,
        rhi::BindlessBindingDomain::Graphics>(*this, runtime_, std::move(owners), diagnostic);
}

VulkanBindlessCrossExtension::VulkanBindlessCrossExtension(
    const VulkanRhiRuntime* runtime, VulkanRhiNativeResourceAllocator* nativeResources)
    : runtime_(runtime), nativeResources_(nativeResources)
{
    caps_.deviceIdentity = rhi::allocateGpuResourceId().value;
    if (!runtime || !runtime->valid() || runtime->syntheticRuntime() || !nativeResources_
        || !nativeResources_->connected())
        return;
    const auto& indexing = runtime->descriptorIndexing();
    if (!indexing.enablementVerified())
        return;
    caps_.indirectFirstInstance = runtime->indirectFirstInstanceEnabled();
    const auto& limits = indexing.limits;
    caps_.limitsQueried = true;
    caps_.maxBindGroups = limits.maxBoundDescriptorSets;
    const std::uint64_t descriptorBindings = std::uint64_t(limits.maxDescriptorSetSampledImages)
        + limits.maxDescriptorSetSamplers + limits.maxDescriptorSetStorageImages
        + limits.maxDescriptorSetUniformBuffers + limits.maxDescriptorSetStorageBuffers;
    caps_.maxBindingsPerGroup = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(descriptorBindings, std::numeric_limits<std::uint32_t>::max()));
    caps_.maxUniformBufferBindingSize = limits.maxUniformBufferRange;
    caps_.maxStorageBufferBindingSize = limits.maxStorageBufferRange;
    caps_.uniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
    caps_.storageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
    caps_.workloadResourceLimits = {1u,
        indexing.maintenance4Queried ? indexing.maxBufferSize : 0u,
        limits.maxUniformBufferRange,
        limits.maxStorageBufferRange,
        0u,
        0u,
        // ResourceDemand/1 shares one dimension for 2D and cube faces.
        std::min(limits.maxImageDimension2D, limits.maxImageDimensionCube),
        limits.maxImageDimension3D,
        limits.maxImageArrayLayers};
    // Preserve representability; unavailable alignment must refuse preflight.
    if (limits.minUniformBufferOffsetAlignment <= UINT32_MAX)
        caps_.workloadResourceLimits.uniformOffsetAlignment =
            static_cast<std::uint32_t>(limits.minUniformBufferOffsetAlignment);
    if (limits.minStorageBufferOffsetAlignment <= UINT32_MAX)
        caps_.workloadResourceLimits.storageOffsetAlignment =
            static_cast<std::uint32_t>(limits.minStorageBufferOffsetAlignment);
    for (auto& stage : caps_.stageLimits) {
        stage.sampledTextures = limits.maxPerStageDescriptorSampledImages;
        stage.samplers = limits.maxPerStageDescriptorSamplers;
        stage.storageTextures = limits.maxPerStageDescriptorStorageImages;
        stage.uniformBuffers = limits.maxPerStageDescriptorUniformBuffers;
        stage.storageBuffers = limits.maxPerStageDescriptorStorageBuffers;
    }

    const auto& enabled = indexing.enabled;
    const auto publish = [&](std::size_t stage,
                             std::size_t resource,
                             rhi::BindlessOperation operations,
                             bool writable) {
        const bool writeStageEnabled = stage == 2u
            || (stage == 1u && enabled.fragmentStoresAndAtomics)
            || (stage == 0u && enabled.vertexStoresAndAtomics);
        if (writable && !writeStageEnabled)
            return;
        caps_.support[stage][resource] = {
            operations,
            enabled.nonUniform[resource == 0u || resource == 1u ? 0u
                    : resource == 2u                            ? 1u
                    : resource == 3u                            ? 2u
                                                                : 3u],
        };
    };
    using Op = rhi::BindlessOperation;
    // Explicit LOD sampling has no derivative requirement in Compute. The
    // selected package emits OpImageSampleExplicitLod with separate native
    // image/sampler descriptors; non-uniform support remains driver-enabled.
    const auto sampledComputeOps = Op::Load | Op::Query | Op::SampleLevel;
    const auto sampledVertexOps = Op::Load | Op::Query;
    const auto storageTextureOps = Op::Load | Op::Query | Op::Store;
    const auto readOnlyBufferOps = Op::Load | Op::Query;
    const auto writableBufferOps = Op::Load | Op::Query | Op::Store | Op::Atomic;
    const std::size_t computeStage = 2u;
    publish(computeStage, 0u, sampledComputeOps, false);
    publish(computeStage, 1u, Op::None, false);
    publish(computeStage, 2u, storageTextureOps, true);
    publish(computeStage, 3u, Op::Load, false);
    publish(computeStage, 4u, readOnlyBufferOps, false);
    publish(computeStage, 5u, writableBufferOps, true);
    const std::size_t vertexStage = 0u;
    publish(vertexStage, 0u, sampledVertexOps, false);
    publish(vertexStage, 1u, Op::None, false);
    publish(vertexStage, 2u, storageTextureOps, true);
    publish(vertexStage, 3u, Op::Load, false);
    publish(vertexStage, 4u, readOnlyBufferOps, false);
    publish(vertexStage, 5u, writableBufferOps, true);
    if (enabled.fragmentStoresAndAtomics) {
        const std::size_t fragmentStage = 1u;
        const auto sampledFragmentOps = sampledComputeOps | Op::SampleLevel | Op::SampleGrad;
        publish(fragmentStage, 0u, sampledFragmentOps, true);
        publish(fragmentStage, 1u, Op::None, true);
        // The original Graphics /9 issuer consumes the same registered buffer
        // closure and journal as Vertex/Compute. Fault reporting itself writes
        // in Fragment, so all three buffer classes require the enabled stage
        // side-effect feature as well as their actual indexing feature above.
        publish(fragmentStage, 3u, Op::Load, true);
        publish(fragmentStage, 4u, readOnlyBufferOps, true);
        publish(fragmentStage, 5u, writableBufferOps, true);
    }
    caps_.execution = rhi::BindlessExecution::Native;
    caps_.executablePayload = true;
    caps_.faultReadbackContractVersion = 1u;
    caps_.preparedComputeBindingContractVersion = 1u;
    caps_.ordinaryDescriptorDomains[3u] = true;
    caps_.ordinaryDescriptorDomains[4u] = true;
    caps_.ordinaryDescriptorDomains[5u] = true;
    // Cold Native depth preparation requires actual queried sampled/attachment
    // support. It cannot grant a native command, state claim or completion.
    const auto instanceProc = runtime->getInstanceProcAddr();
    const auto queryFormat = instanceProc
        ? reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
              instanceProc(runtime->instance(), "vkGetPhysicalDeviceFormatProperties"))
        : nullptr;
    if (queryFormat) {
        VkFormatProperties format{};
        queryFormat(runtime->physicalDevice(), VK_FORMAT_R8G8B8A8_UNORM, &format);
        const auto colorRequired =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((format.optimalTilingFeatures & colorRequired) == colorRequired
            && limits.maxDescriptorSetSampledImages && limits.maxDescriptorSetSamplers) {
            caps_.ordinaryDescriptorDomains[0u] = true;
            caps_.ordinaryDescriptorDomains[1u] = true;
            // Public color8 consumes the existing native lifecycle8 command.
            // Require real format filtering and per-stage descriptor budgets;
            // table operations/non-uniform enablement are checked by the plan.
            if (limits.maxPerStageDescriptorSampledImages && limits.maxPerStageDescriptorSamplers
                && limits.maxPerStageDescriptorStorageBuffers && limits.maxStorageBufferRange)
                caps_.originalNativeColorComputeExecutionContractVersion = 8u;
        }
        queryFormat(runtime->physicalDevice(), VK_FORMAT_D32_SFLOAT, &format);
        const auto required =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if ((format.optimalTilingFeatures & required) == required) {
            caps_.originalNativeDepthRootPreparationContractVersion = 6u;
            caps_.originalNativeDepthComputeExecutionContractVersion = 6u;
        }
        queryFormat(runtime->physicalDevice(), VK_FORMAT_R16G16B16A16_SFLOAT, &format);
        const auto storageRequired = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
            | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if ((format.optimalTilingFeatures & storageRequired) == storageRequired
            && limits.maxPerStageDescriptorStorageImages
            && limits.maxPerStageDescriptorSampledImages
            && limits.maxPerStageDescriptorStorageBuffers && limits.maxStorageBufferRange) {
            caps_.originalStoragePreparationContractVersion = 7u;
            caps_.originalStorageRootPreparationContractVersion = 8u;
            if (caps_.originalNativeDepthRootPreparationContractVersion == 6u) {
                caps_.originalMixedImageSnapshotPreparationContractVersion = 9u;
                caps_.originalMixedImageRootPreparationContractVersion = 10u;
            }
            // Execution9 uses full native clear/store and the original queue
            // fence. Publish only with queried attachment extent/capacity and
            // every cold-pass/native execution entrypoint on this real device.
            bool storageCommands = limits.maxColorAttachments && limits.maxFramebufferWidth
                && limits.maxFramebufferHeight;
            for (const char* entry : {"vkCreateRenderPass",
                     "vkCreateImageView",
                     "vkCreateFramebuffer",
                     "vkDestroyRenderPass",
                     "vkDestroyImageView",
                     "vkDestroyFramebuffer",
                     "vkCmdBeginRenderPass",
                     "vkCmdEndRenderPass",
                     "vkCmdPipelineBarrier",
                     "vkCmdDispatch",
                     "vkCmdBindPipeline",
                     "vkCmdBindDescriptorSets",
                     "vkCmdFillBuffer",
                     "vkCmdCopyBuffer",
                     "vkEndCommandBuffer",
                     "vkQueueSubmit",
                     "vkGetFenceStatus"})
                storageCommands = storageCommands && runtime->getDeviceProcAddr(entry);
            if (storageCommands) {
                caps_.originalNativeStorageComputeExecutionContractVersion = 9u;
                if (caps_.originalMixedImageRootPreparationContractVersion == 10u)
                    caps_.originalNativeMixedComputeExecutionContractVersion = 11u;
            }
        }
    }
    caps_.preparedGraphicsBindingContractVersion = 1u;
    if (enabled.fragmentStoresAndAtomics) {
        caps_.originalGraphicsExecutionContractVersion = 1u;
        if (caps_.originalNativeDepthRootPreparationContractVersion == 6u
            && caps_.ordinaryDescriptorDomains[0u] && caps_.ordinaryDescriptorDomains[1u]
            && limits.maxPerStageDescriptorSampledImages && limits.maxPerStageDescriptorSamplers
            && limits.maxColorAttachments && limits.maxFramebufferWidth
            && limits.maxFramebufferHeight)
            caps_.originalGraphicsDepthExecutionContractVersion = 2u;
        if (limits.maxPushConstantsSize && runtime_->getDeviceProcAddr("vkCmdPushConstants"))
            caps_.originalGraphicsPushContractVersion = 1u;
    }
}

std::uint64_t VulkanBindlessCrossExtension::queryOriginalSnapshotBufferAllocation(
    const rhi::BufferDesc& desc, const char*& diagnostic) const
{
    diagnostic = "RHI-VULKAN-BINDLESS-SNAPSHOT-ALLOCATION-RUNTIME";
    if (!nativeResources_ || !runtime_ || !runtime_->valid() || runtime_->syntheticRuntime())
        return 0u;
    return nativeResources_->queryBufferAllocationBytes(desc, diagnostic);
}

rhi::ValidationResult VulkanBindlessCrossExtension::validateSnapshotCreation(
    const rhi::BindlessLoweringPlan& plan) const
{
    if (plan.originalExecutionContractVersion) {
        const auto execution = rhi::semantics::OriginalBindlessExecutionLowering::lowering(
            rhi::Backend::Vulkan, plan.originalExecutionContractVersion);
        if (!execution.executionAvailable || execution.diagnostic
            || !rhi::semantics::OriginalBindlessExecutionLowering::capabilityAvailable(
                caps_, plan.originalExecutionContractVersion))
            return {false,
                execution.diagnostic ? execution.diagnostic
                                     : "RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE"};
    }
    if (plan.execution != rhi::BindlessExecution::Native
        || (!plan.executable
            && !(rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 6u)
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, 6u))
            && !(rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 7u)
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, 7u))
            && !((rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageSnapshotPlan(
                      plan)
                     || rhi::semantics::OriginalBindlessPreparationLowering::
                         hasMixedImageResourcePlan(plan))
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, plan.originalPreparationContractVersion))
            && !(rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(plan)
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, plan.originalPreparationContractVersion == 10u ? 10u : 8u)))
        || plan.deviceIdentity != caps_.deviceIdentity || !caps_.limitsQueried
        || !caps_.executablePayload || caps_.faultReadbackContractVersion != 1u || !runtime_
        || !runtime_->valid() || runtime_->syntheticRuntime() || !nativeResources_
        || !nativeResources_->connected() || !runtime_->descriptorIndexing().enablementVerified()) {
        return {false, "RHI-VULKAN-BINDLESS-SNAPSHOT-EXECUTION-UNAVAILABLE"};
    }
    return {true, {}};
}

rhi::ValidationResult VulkanBindlessCrossExtension::validateAuthorization(
    const rhi::BindlessShaderPackage& package, const rhi::BindlessSnapshot& snapshot) const
{
    if (snapshot.plan().originalPreparationContractVersion == 7u)
        return {false, "RHI-BINDLESS-ORIGINAL-STORAGE-PREPARATION-SNAPSHOT-ONLY"};
    if (rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageSnapshotPlan(
            snapshot.plan()))
        return {false, "RHI-BINDLESS-ORIGINAL-MIXED-IMAGE-SNAPSHOT-ONLY"};
    const auto& artifact = package.artifact();
    if ((!snapshot.plan().executable
            && !(rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(snapshot.plan(), 6u)
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, 6u))
            && !(rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                     snapshot.plan())
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, snapshot.plan().originalPreparationContractVersion == 10u ? 10u : 8u)))
        || snapshot.plan().execution != rhi::BindlessExecution::Native
        || artifact.execution != rhi::BindlessExecution::Native
        || artifact.faultReportingContractVersion != 1u || artifact.faultLatch.contractVersion != 1u
        || !snapshot.faultLatchBuffer() || !snapshot.faultReadbackBuffer()
        || !snapshot.descriptorSet()) {
        return {false, "RHI-VULKAN-BINDLESS-AUTHORIZATION-PAYLOAD-INCOMPLETE"};
    }
    const auto latch = rhi_vulkan::vulkanRhiNativePayload(*snapshot.faultLatchBuffer());
    const auto readback = rhi_vulkan::vulkanRhiNativePayload(*snapshot.faultReadbackBuffer());
    const auto descriptorSet =
        rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
            snapshot.plan())
        ? prepareVulkanOriginalImageCellDescriptors(snapshot)
        : rhi_vulkan::vulkanRhiNativePayload(*snapshot.descriptorSet());
    if (!latch || !latch->backendVulkanBufferHandle() || !readback
        || !readback->backendVulkanBufferHandle() || !readback->backendMappedData()
        || !descriptorSet || !descriptorSet->backendVulkanDescriptorSetHandle()) {
        return {false, "RHI-VULKAN-BINDLESS-AUTHORIZATION-NATIVE-PAYLOAD-MISSING"};
    }
    return {true, {}};
}

core::UniquePtr<rhi::ComputePipeline> VulkanBindlessCrossExtension::createComputePipeline(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    std::string_view entry,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (!authorization || !authorization->issuedBy(this, authorization)
        || authorization->plan().execution != rhi::BindlessExecution::Native
        || (!authorization->plan().executable
            && !(rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                     authorization->plan(), 6u)
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                    caps_, 6u))
            && !(rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                     authorization->plan())
                && rhi::semantics::OriginalBindlessPreparationLowering::capabilityAvailable(caps_,
                    authorization->plan().originalPreparationContractVersion == 10u ? 10u : 8u)))
        || authorization->plan().deviceIdentity != caps_.deviceIdentity
        || authorization->plan().shaderPackageIdentity
            != authorization->package().artifact().shaderPackageIdentity) {
        diagnostic = "RHI-VULKAN-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return {};
    }
    if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime() || !nativeResources_
        || !nativeResources_->connected()) {
        diagnostic = "RHI-VULKAN-BINDLESS-NATIVE-RUNTIME-REQUIRED";
        return {};
    }

    const auto& package = authorization->package();
    const auto& artifact = package.artifact();
    if (artifact.execution != rhi::BindlessExecution::Native
        || artifact.shaderPackageIdentity != authorization->plan().shaderPackageIdentity) {
        diagnostic = "RHI-VULKAN-BINDLESS-PIPELINE-PACKAGE-MISMATCH";
        return {};
    }
    rhi::ShaderPackageDesc interface;
    interface.label = artifact.physicalLayout.name;
    interface.generatedReflectionHydrated = true;
    if (!rhi::appendBindlessShaderEntry(
            package, entry, rhi::ShaderStage::Compute, interface, diagnostic))
        return {};

    const auto layoutDesc = rhi::bindlessPipelineLayout(artifact);
    auto layout = createVulkanRhiPipelineLayout(layoutDesc);
    if (!layout) {
        diagnostic = "RHI-VULKAN-BINDLESS-PIPELINE-LAYOUT-INVALID";
        return {};
    }
    const auto reflection =
        rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, interface.reflection);
    if (!reflection.success) {
        diagnostic = reflection.diagnostics;
        return {};
    }

    rhi::ComputePipelineDesc desc;
    desc.label = interface.label;
    desc.layout = layout.get();
    desc.shaderPackage = &interface;
    desc.computeShader = interface.entrypoints.front();
    auto native = nativeResources_->createBindlessComputePayload(package, entry);
    if (!native.success || !native.payload) {
        diagnostic = native.diagnostics.empty() ? "RHI-VULKAN-BINDLESS-COMPUTE-PAYLOAD-MISSING"
                                                : native.diagnostics;
        return {};
    }
    auto pipeline = createVulkanRhiAuthorizedBindlessComputePipeline(
        desc, std::move(native.payload), std::move(authorization), std::move(layout));
    if (!pipeline)
        diagnostic = "RHI-VULKAN-BINDLESS-PIPELINE-CREATION-FAILED";
    return pipeline;
}

core::UniquePtr<rhi::RenderPipeline> VulkanBindlessCrossExtension::createRenderPipeline(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    core::pmr::String& diagnostic) const
{
    return createRenderPipelineImpl(
        std::move(authorization), state, vertexEntry, fragmentEntry, nullptr, diagnostic);
}

core::UniquePtr<rhi::RenderPipeline> VulkanBindlessCrossExtension::createRenderPipeline(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::BindlessGraphicsPushRequest& request,
    core::pmr::String& diagnostic) const
{
    if (const auto* id = request.diagnostic()) {
        diagnostic = id;
        return {};
    }
    if (caps_.originalGraphicsPushContractVersion != request.contractVersion) {
        diagnostic = "RHI-BINDLESS-GRAPHICS-PUSH-PIPELINE-UNAVAILABLE";
        return {};
    }
    return createRenderPipelineImpl(
        std::move(authorization), state, vertexEntry, fragmentEntry, &request, diagnostic);
}

core::UniquePtr<rhi::RenderPipeline> VulkanBindlessCrossExtension::createRenderPipelineImpl(
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    const rhi::RenderPipelineDesc& state,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::BindlessGraphicsPushRequest* push,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (!authorization || !authorization->issuedBy(this, authorization)
        || !authorization->current(authorization)
        || authorization->plan().execution != rhi::BindlessExecution::Native
        || !authorization->plan().executable
        || authorization->plan().deviceIdentity != caps_.deviceIdentity
        || authorization->plan().shaderPackageIdentity
            != authorization->package().artifact().shaderPackageIdentity) {
        diagnostic = "RHI-VULKAN-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
        return {};
    }
    if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime() || !nativeResources_
        || !nativeResources_->connected()) {
        diagnostic = "RHI-VULKAN-BINDLESS-NATIVE-RUNTIME-REQUIRED";
        return {};
    }
    const auto authored = rhi::validateBindlessAuthoredRenderState(state, !fragmentEntry.empty());
    if (!authored.success) {
        diagnostic = authored.diagnostics;
        return {};
    }

    const auto scene = rhi::selectBindlessInitializedGraphics(
        authorization->package(), vertexEntry, fragmentEntry);
    if (scene.diagnostic) {
        diagnostic = scene.diagnostic;
        return {};
    }
    if (scene.recognized)
        if (const auto* id =
                rhi::semantics::BindlessSceneRasterContract::inspect(rhi::Backend::Vulkan, state)) {
            diagnostic = id;
            return {};
        }

    const auto& package = authorization->package();
    const auto& artifact = package.artifact();
    if (artifact.execution != rhi::BindlessExecution::Native
        || artifact.shaderPackageIdentity != authorization->plan().shaderPackageIdentity) {
        diagnostic = "RHI-VULKAN-BINDLESS-PIPELINE-PACKAGE-MISMATCH";
        return {};
    }
    rhi::ShaderPackageDesc interface;
    interface.label = artifact.physicalLayout.name;
    interface.generatedReflectionHydrated = true;
    auto layoutDesc = rhi::bindlessPipelineLayout(artifact);
    if (push) {
        rhi::BindlessGraphicsPipelineEntries entries;
        const auto composed =
            rhi::BindlessGraphicsPipelineEntries::compose(authorization->originalPackageOwner(),
                vertexEntry,
                fragmentEntry,
                interface,
                layoutDesc,
                entries);
        if (!composed.success) {
            diagnostic = composed.diagnostics;
            return {};
        }
        if (const auto* id =
                rhi::semantics::OriginalBindlessPushConsumptionLowering::validateNativeSize(
                    interface.reflection.pushConstantByteCount,
                    push->maxBytes,
                    runtime_->descriptorIndexing().limits.maxPushConstantsSize)) {
            diagnostic = id;
            return {};
        }
    } else {
        if (!rhi::appendBindlessShaderEntry(
                package, vertexEntry, rhi::ShaderStage::Vertex, interface, diagnostic)
            || (!fragmentEntry.empty()
                && !rhi::appendBindlessShaderEntry(
                    package, fragmentEntry, rhi::ShaderStage::Fragment, interface, diagnostic))) {
            return {};
        }
    }
    auto layout = createVulkanRhiPipelineLayout(layoutDesc);
    if (!layout) {
        diagnostic = "RHI-VULKAN-BINDLESS-PIPELINE-LAYOUT-INVALID";
        return {};
    }
    const auto reflection =
        rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, interface.reflection);
    if (!reflection.success) {
        diagnostic = reflection.diagnostics;
        return {};
    }

    auto desc = state;
    desc.label = interface.label;
    desc.layout = layout.get();
    desc.shaderPackage = &interface;
    desc.vertexShader = interface.entrypoints[0];
    if (!fragmentEntry.empty())
        desc.fragmentShader = interface.entrypoints[1];
    auto native = nativeResources_->createBindlessRenderPayload(
        authorization->originalPackageOwner(), state, vertexEntry, fragmentEntry, push);
    if (!native.success || !native.payload) {
        diagnostic = native.diagnostics.empty() ? "RHI-VULKAN-BINDLESS-RENDER-PAYLOAD-MISSING"
                                                : native.diagnostics;
        return {};
    }
    const auto pipelineValidation = validateVulkanRhiAuthorizedBindlessRenderPipeline(desc);
    if (!pipelineValidation.success) {
        diagnostic = pipelineValidation.diagnostics;
        return {};
    }
    auto pipeline = createVulkanRhiAuthorizedBindlessRenderPipeline(
        desc, std::move(native.payload), std::move(authorization), std::move(layout));
    if (!pipeline)
        diagnostic = "RHI-VULKAN-BINDLESS-RENDER-PIPELINE-CREATION-FAILED";
    return pipeline;
}

namespace {

template <typename Fn> Fn functionFrom(PFN_vkVoidFunction value)
{
    Fn result = nullptr;
    static_assert(sizeof(result) == sizeof(value), "unexpected Vulkan function pointer size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::pmr::string vkFailure(const char* operation, VkResult result)
{
    std::ostringstream message;
    message << operation << " failed: " << static_cast<int>(result);
    return arp::core::toPmrString(message.str());
}

std::pmr::vector<std::uint32_t> readSpirvWords(const std::pmr::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const auto byteSize = file.tellg();
    if (byteSize <= 0 || static_cast<std::uint64_t>(byteSize) % sizeof(std::uint32_t) != 0) {
        return {};
    }
    std::pmr::vector<std::uint32_t> words(
        static_cast<std::size_t>(byteSize) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(byteSize));
    return words;
}

VkFormat formatFor(rhi::TextureFormat format)
{
    switch (format) {
    case rhi::TextureFormat::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case rhi::TextureFormat::Rgba8UnormSrgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case rhi::TextureFormat::Bgra8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case rhi::TextureFormat::R8Unorm:
        return VK_FORMAT_R8_UNORM;
    case rhi::TextureFormat::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case rhi::TextureFormat::Rg16Float:
        return VK_FORMAT_R16G16_SFLOAT;
    case rhi::TextureFormat::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case rhi::TextureFormat::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    case rhi::TextureFormat::Depth24Stencil8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case rhi::TextureFormat::Unknown:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

VkSampleCountFlagBits sampleCountFor(std::uint32_t sampleCount)
{
    switch (sampleCount) {
    case 1:
        return VK_SAMPLE_COUNT_1_BIT;
    case 2:
        return VK_SAMPLE_COUNT_2_BIT;
    case 4:
        return VK_SAMPLE_COUNT_4_BIT;
    case 8:
        return VK_SAMPLE_COUNT_8_BIT;
    }
    return static_cast<VkSampleCountFlagBits>(0);
}

VkPrimitiveTopology topologyFor(rhi::PrimitiveTopology topology)
{
    switch (topology) {
    case rhi::PrimitiveTopology::TriangleList:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case rhi::PrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case rhi::PrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case rhi::PrimitiveTopology::PointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case rhi::PrimitiveTopology::PatchList:
        break;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags cullModeFor(rhi::CullMode cullMode)
{
    switch (cullMode) {
    case rhi::CullMode::None:
        return VK_CULL_MODE_NONE;
    case rhi::CullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    case rhi::CullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkFrontFace frontFaceFor(rhi::FrontFace frontFace)
{
    return frontFace == rhi::FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE
                                                  : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkCompareOp compareOpFor(rhi::CompareOp compare)
{
    switch (compare) {
    case rhi::CompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case rhi::CompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case rhi::CompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case rhi::CompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case rhi::CompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case rhi::CompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case rhi::CompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case rhi::CompareOp::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkStencilOp stencilOpFor(rhi::StencilOp operation)
{
    switch (operation) {
    case rhi::StencilOp::Keep:
        return VK_STENCIL_OP_KEEP;
    case rhi::StencilOp::Zero:
        return VK_STENCIL_OP_ZERO;
    case rhi::StencilOp::Replace:
        return VK_STENCIL_OP_REPLACE;
    case rhi::StencilOp::IncrementClamp:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case rhi::StencilOp::DecrementClamp:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case rhi::StencilOp::Invert:
        return VK_STENCIL_OP_INVERT;
    }
    return VK_STENCIL_OP_KEEP;
}

VkStencilOpState stencilFaceFor(
    const rhi::StencilFaceState& face, const rhi::DepthStencilState& state)
{
    VkStencilOpState native{};
    native.failOp = stencilOpFor(face.failOp);
    native.passOp = stencilOpFor(face.passOp);
    native.depthFailOp = stencilOpFor(face.depthFailOp);
    native.compareOp = compareOpFor(face.compare);
    native.compareMask = state.stencilReadMask;
    native.writeMask = state.stencilWriteMask;
    native.reference = 0;
    return native;
}

VkFormat vertexFormatFor(rhi::VertexFormat format)
{
    switch (format) {
    case rhi::VertexFormat::Float32x1:
        return VK_FORMAT_R32_SFLOAT;
    case rhi::VertexFormat::Float32x2:
        return VK_FORMAT_R32G32_SFLOAT;
    case rhi::VertexFormat::Float32x3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case rhi::VertexFormat::Float32x4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkBlendFactor blendFactorFor(rhi::BlendFactor factor)
{
    switch (factor) {
    case rhi::BlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case rhi::BlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case rhi::BlendFactor::SrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case rhi::BlendFactor::OneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case rhi::BlendFactor::DstAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case rhi::BlendFactor::OneMinusDstAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp blendOpFor(rhi::BlendOp operation)
{
    switch (operation) {
    case rhi::BlendOp::Add:
        return VK_BLEND_OP_ADD;
    case rhi::BlendOp::Subtract:
        return VK_BLEND_OP_SUBTRACT;
    case rhi::BlendOp::ReverseSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case rhi::BlendOp::Min:
        return VK_BLEND_OP_MIN;
    case rhi::BlendOp::Max:
        return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkColorComponentFlags colorWriteMaskFor(rhi::ColorWriteMask mask)
{
    VkColorComponentFlags flags = 0;
    if ((mask & rhi::ColorWriteMask::Red) != rhi::ColorWriteMask::None) {
        flags |= VK_COLOR_COMPONENT_R_BIT;
    }
    if ((mask & rhi::ColorWriteMask::Green) != rhi::ColorWriteMask::None) {
        flags |= VK_COLOR_COMPONENT_G_BIT;
    }
    if ((mask & rhi::ColorWriteMask::Blue) != rhi::ColorWriteMask::None) {
        flags |= VK_COLOR_COMPONENT_B_BIT;
    }
    if ((mask & rhi::ColorWriteMask::Alpha) != rhi::ColorWriteMask::None) {
        flags |= VK_COLOR_COMPONENT_A_BIT;
    }
    return flags;
}

VkDescriptorType descriptorTypeFor(rhi::DescriptorType type, bool dynamicOffset = false)
{
    switch (type) {
    case rhi::DescriptorType::SampledTexture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case rhi::DescriptorType::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case rhi::DescriptorType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case rhi::DescriptorType::UniformBuffer:
        return dynamicOffset ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                             : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case rhi::DescriptorType::StorageBuffer:
    case rhi::DescriptorType::ReadOnlyStorageBuffer:
        return dynamicOffset ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                             : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case rhi::DescriptorType::InputAttachment:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    }
    return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

VkShaderStageFlags shaderStagesFor(rhi::ShaderStage stages)
{
    VkShaderStageFlags flags = 0;
    if ((stages & rhi::ShaderStage::Vertex) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if ((stages & rhi::ShaderStage::Fragment) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if ((stages & rhi::ShaderStage::Compute) != rhi::ShaderStage::None) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

const rhi::DescriptorLayoutDesc* findDescriptorLayout(
    const rhi::PipelineLayoutDesc& desc, const std::pmr::string& name)
{
    for (const auto& layout : desc.descriptorLayouts) {
        if (layout.name == name) {
            return &layout;
        }
    }
    return nullptr;
}

// Owns the native objects produced while lowering an L2 PipelineLayoutDesc.
// A pipeline creation can fail after this lowering has succeeded (for example
// while loading shader code or creating the VkPipeline), so this intermediate
// must have the same deterministic destruction rule as the final payload.
// Ownership is transferred exactly once into NativePipelinePayload on success.
class NativePipelineLayoutBuild final {
public:
    explicit NativePipelineLayoutBuild(const VulkanRhiRuntime& device) : device_(&device) {}

    ~NativePipelineLayoutBuild() { reset(); }

    NativePipelineLayoutBuild(const NativePipelineLayoutBuild&) = delete;
    NativePipelineLayoutBuild& operator=(const NativePipelineLayoutBuild&) = delete;

    NativePipelineLayoutBuild(NativePipelineLayoutBuild&& other) noexcept
        : success(other.success), diagnostics(std::move(other.diagnostics)),
          layout(std::exchange(other.layout, VK_NULL_HANDLE)),
          descriptorSetLayouts(std::move(other.descriptorSetLayouts)),
          device_(std::exchange(other.device_, nullptr))
    {}

    NativePipelineLayoutBuild& operator=(NativePipelineLayoutBuild&&) = delete;

    VkPipelineLayout releaseLayout() { return std::exchange(layout, VK_NULL_HANDLE); }

    std::pmr::vector<VkDescriptorSetLayout> releaseDescriptorSetLayouts()
    {
        // Do not rely on moved-from vector contents for native ownership:
        // swap makes the source observably empty before this guard destructs.
        std::pmr::vector<VkDescriptorSetLayout> released(descriptorSetLayouts.get_allocator());
        released.swap(descriptorSetLayouts);
        return released;
    }

    bool success = false;
    std::pmr::string diagnostics;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::pmr::vector<VkDescriptorSetLayout> descriptorSetLayouts;

private:
    void reset()
    {
        if (!device_) {
            return;
        }
        auto* destroyPipelineLayout = functionFrom<PFN_vkDestroyPipelineLayout>(
            device_->getDeviceProcAddr("vkDestroyPipelineLayout"));
        auto* destroyDescriptorSetLayout = functionFrom<PFN_vkDestroyDescriptorSetLayout>(
            device_->getDeviceProcAddr("vkDestroyDescriptorSetLayout"));
        if (destroyPipelineLayout && layout) {
            destroyPipelineLayout(device_->logicalDevice(), layout, nullptr);
        }
        if (destroyDescriptorSetLayout) {
            for (const auto descriptorSetLayout : descriptorSetLayouts) {
                destroyDescriptorSetLayout(device_->logicalDevice(), descriptorSetLayout, nullptr);
            }
        }
        layout = VK_NULL_HANDLE;
        descriptorSetLayouts.clear();
    }

    const VulkanRhiRuntime* device_ = nullptr;
};

NativePipelineLayoutBuild createPipelineLayout(const VulkanRhiRuntime& device,
    const rhi::PipelineLayoutDesc& desc,
    VulkanRhiNativeResourceStats& stats)
{
    NativePipelineLayoutBuild result(device);
    auto* createDescriptorSetLayout = functionFrom<PFN_vkCreateDescriptorSetLayout>(
        device.getDeviceProcAddr("vkCreateDescriptorSetLayout"));
    auto* destroyDescriptorSetLayout = functionFrom<PFN_vkDestroyDescriptorSetLayout>(
        device.getDeviceProcAddr("vkDestroyDescriptorSetLayout"));
    auto* createPipelineLayout = functionFrom<PFN_vkCreatePipelineLayout>(
        device.getDeviceProcAddr("vkCreatePipelineLayout"));
    if (!createDescriptorSetLayout || !destroyDescriptorSetLayout || !createPipelineLayout) {
        result.diagnostics = "Vulkan RHI native pipeline layout missing Vulkan entrypoints";
        return result;
    }

    std::uint32_t maxDescriptorSetIndex = 0;
    for (const auto& setSlot : desc.descriptorSets) {
        maxDescriptorSetIndex = std::max(maxDescriptorSetIndex, setSlot.set);
    }
    const auto descriptorSetLayoutCount =
        desc.descriptorSets.empty() ? 0u : maxDescriptorSetIndex + 1u;
    for (std::uint32_t setIndex = 0; setIndex < descriptorSetLayoutCount; ++setIndex) {
        const auto foundSetSlot = std::find_if(desc.descriptorSets.begin(),
            desc.descriptorSets.end(),
            [setIndex](const rhi::DescriptorSetSlotDesc& setSlot) {
                return setSlot.set == setIndex;
            });
        const auto* layout = foundSetSlot == desc.descriptorSets.end()
            ? nullptr
            : findDescriptorLayout(desc, foundSetSlot->layoutName);
        if (foundSetSlot != desc.descriptorSets.end() && !layout) {
            result.diagnostics =
                "Vulkan RHI native pipeline layout references missing descriptor layout";
            return result;
        }
        std::pmr::vector<VkDescriptorSetLayoutBinding> bindings;
        if (layout) {
            bindings.reserve(layout->bindings.size());
            for (const auto& binding : layout->bindings) {
                bindings.push_back(VkDescriptorSetLayoutBinding{
                    binding.binding,
                    descriptorTypeFor(binding.type, binding.dynamicOffset),
                    binding.arrayCount,
                    shaderStagesFor(binding.stages),
                    nullptr,
                });
                if (std::getenv("ARP_FORWARD_PLUS_LAYOUT_TRACE") != nullptr) {
                    std::cerr << "[vulkan-layout-trace] pipeline-set=" << setIndex
                              << " name=" << layout->name.c_str() << " binding=" << binding.binding
                              << " type=" << static_cast<unsigned>(bindings.back().descriptorType)
                              << " count=" << binding.arrayCount
                              << " stages=" << bindings.back().stageFlags << '\n';
                }
            }
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout nativeLayout = VK_NULL_HANDLE;
        const auto created =
            createDescriptorSetLayout(device.logicalDevice(), &layoutInfo, nullptr, &nativeLayout);
        if (created != VK_SUCCESS) {
            result.diagnostics = vkFailure("vkCreateDescriptorSetLayout", created);
            return result;
        }
        ++stats.nativeDescriptorSetLayoutCreateCount;
        result.descriptorSetLayouts.push_back(nativeLayout);
    }

    std::pmr::vector<VkPushConstantRange> pushRanges;
    pushRanges.reserve(desc.pushConstantRanges.size());
    for (const auto& range : desc.pushConstantRanges) {
        pushRanges.push_back(VkPushConstantRange{
            shaderStagesFor(range.stages),
            range.offsetBytes,
            range.sizeBytes,
        });
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<std::uint32_t>(result.descriptorSetLayouts.size());
    layoutInfo.pSetLayouts = result.descriptorSetLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<std::uint32_t>(pushRanges.size());
    layoutInfo.pPushConstantRanges = pushRanges.data();

    const auto created =
        createPipelineLayout(device.logicalDevice(), &layoutInfo, nullptr, &result.layout);
    if (created != VK_SUCCESS) {
        result.diagnostics = vkFailure("vkCreatePipelineLayout", created);
        return result;
    }

    if (std::getenv("ARP_FORWARD_PLUS_LAYOUT_TRACE") != nullptr) {
        std::cerr << "[vulkan-layout-trace] pipeline-layout=" << result.layout
                  << " set-count=" << result.descriptorSetLayouts.size();
        for (std::size_t i = 0; i < result.descriptorSetLayouts.size(); ++i) {
            std::cerr << " set" << i << "=" << result.descriptorSetLayouts[i];
        }
        std::cerr << '\n';
    }

    ++stats.nativePipelineLayoutCreateCount;
    result.success = true;
    return result;
}

VkShaderModule createShaderModule(const VulkanRhiRuntime& device,
    const std::uint32_t* words,
    std::size_t wordCount,
    VulkanRhiNativeResourceStats& stats,
    std::pmr::string& diagnostics)
{
    if (!words || wordCount < 5 || words[0] != 0x07230203u) {
        diagnostics = "Vulkan RHI native pipeline requires readable SPIR-V artifact";
        return VK_NULL_HANDLE;
    }
    auto* createShaderModule =
        functionFrom<PFN_vkCreateShaderModule>(device.getDeviceProcAddr("vkCreateShaderModule"));
    if (!createShaderModule) {
        diagnostics = "Vulkan RHI native pipeline missing vkCreateShaderModule";
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = wordCount * sizeof(std::uint32_t);
    moduleInfo.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    const auto created = createShaderModule(device.logicalDevice(), &moduleInfo, nullptr, &module);
    if (created != VK_SUCCESS) {
        diagnostics = vkFailure("vkCreateShaderModule", created);
        return VK_NULL_HANDLE;
    }
    ++stats.nativeShaderModuleCreateCount;
    return module;
}

class NativePipelinePayload final : public VulkanRhiNativeResourcePayload {
public:
    NativePipelinePayload(std::shared_ptr<VulkanRhiRuntime> device,
        VkPipeline pipeline,
        VkPipelineLayout layout,
        VkRenderPass renderPass,
        VkShaderStageFlags pushConstantStages,
        std::pmr::vector<VkDescriptorSetLayout> descriptorSetLayouts)
        : device_(std::move(device)), pipeline_(pipeline), layout_(layout), renderPass_(renderPass),
          pushConstantStages_(pushConstantStages),
          descriptorSetLayouts_(std::move(descriptorSetLayouts))
    {}

    ~NativePipelinePayload() override
    {
        auto* destroyPipeline =
            functionFrom<PFN_vkDestroyPipeline>(device_->getDeviceProcAddr("vkDestroyPipeline"));
        auto* destroyPipelineLayout = functionFrom<PFN_vkDestroyPipelineLayout>(
            device_->getDeviceProcAddr("vkDestroyPipelineLayout"));
        auto* destroyRenderPass = functionFrom<PFN_vkDestroyRenderPass>(
            device_->getDeviceProcAddr("vkDestroyRenderPass"));
        auto* destroyDescriptorSetLayout = functionFrom<PFN_vkDestroyDescriptorSetLayout>(
            device_->getDeviceProcAddr("vkDestroyDescriptorSetLayout"));
        if (destroyPipeline && pipeline_) {
            destroyPipeline(device_->logicalDevice(), pipeline_, nullptr);
        }
        if (destroyPipelineLayout && layout_) {
            destroyPipelineLayout(device_->logicalDevice(), layout_, nullptr);
        }
        if (destroyRenderPass && renderPass_) {
            destroyRenderPass(device_->logicalDevice(), renderPass_, nullptr);
        }
        if (destroyDescriptorSetLayout) {
            for (auto descriptorSetLayout : descriptorSetLayouts_) {
                destroyDescriptorSetLayout(device_->logicalDevice(), descriptorSetLayout, nullptr);
            }
        }
    }

    VkDevice backendVulkanDeviceHandle() const override { return device_->logicalDevice(); }

    VkPipeline backendVulkanRenderPipelineHandle() const override { return pipeline_; }

    VkPipeline backendVulkanComputePipelineHandle() const override { return pipeline_; }

    VkPipelineLayout backendVulkanRenderPipelineLayoutHandle() const override { return layout_; }

    VkPipelineLayout backendVulkanComputePipelineLayoutHandle() const override { return layout_; }

    VkShaderStageFlags backendVulkanPushConstantStageFlags() const override
    {
        return pushConstantStages_;
    }

private:
    std::shared_ptr<VulkanRhiRuntime> device_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkShaderStageFlags pushConstantStages_ = 0;
    std::pmr::vector<VkDescriptorSetLayout> descriptorSetLayouts_;
};

void destroyShaderModule(const VulkanRhiRuntime& device, VkShaderModule module)
{
    auto* destroyShaderModule =
        functionFrom<PFN_vkDestroyShaderModule>(device.getDeviceProcAddr("vkDestroyShaderModule"));
    if (destroyShaderModule && module) {
        destroyShaderModule(device.logicalDevice(), module, nullptr);
    }
}

VulkanRhiNativeResourceResult createComputePayload(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const rhi::PipelineLayoutDesc& layout,
    const core::pmr::String& entry,
    const std::uint32_t* words,
    std::size_t wordCount,
    VulkanRhiNativeResourceStats& stats)
{
    VulkanRhiNativeResourceResult result;
    auto* createComputePipelines = functionFrom<PFN_vkCreateComputePipelines>(
        runtime->getDeviceProcAddr("vkCreateComputePipelines"));
    if (!createComputePipelines) {
        result.diagnostics = "Vulkan RHI native compute pipeline missing vkCreateComputePipelines";
        return result;
    }

    auto layoutBuild = createPipelineLayout(*runtime, layout, stats);
    if (!layoutBuild.success) {
        result.diagnostics = layoutBuild.diagnostics;
        return result;
    }

    std::pmr::string diagnostics;
    auto computeModule = createShaderModule(*runtime, words, wordCount, stats, diagnostics);
    if (!computeModule) {
        result.diagnostics = diagnostics;
        return result;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = computeModule;
    stage.pName = entry.c_str();

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = layoutBuild.layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto pipelineCreated = createComputePipelines(
        runtime->logicalDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    destroyShaderModule(*runtime, computeModule);
    if (pipelineCreated != VK_SUCCESS) {
        result.diagnostics = vkFailure("vkCreateComputePipelines", pipelineCreated);
        return result;
    }

    ++stats.nativeComputePipelineCreateCount;
    const auto nativeLayout = layoutBuild.releaseLayout();
    auto nativeDescriptorSetLayouts = layoutBuild.releaseDescriptorSetLayouts();
    const auto pushConstantStages = layout.pushConstantRanges.empty()
        ? 0u
        : shaderStagesFor(layout.pushConstantRanges.front().stages);
    result.payload = arp::core::makeShared<NativePipelinePayload>(runtime,
        pipeline,
        nativeLayout,
        VK_NULL_HANDLE,
        pushConstantStages,
        std::move(nativeDescriptorSetLayouts));
    result.success = true;
    return result;
}

} // namespace

const char* inspectVulkanBindlessOrdinaryBinding(const rhi::DescriptorBindingDesc& binding) noexcept
{
    using namespace rhi;
    if (binding.arrayCount != 1u || binding.dynamicOffset || binding.tableBacked)
        return "RHI-VULKAN-BINDLESS-ORDINARY-RESOURCE-UNSUPPORTED";
    switch (binding.type) {
    case DescriptorType::UniformBuffer:
    case DescriptorType::ReadOnlyStorageBuffer:
    case DescriptorType::StorageBuffer:
        return nullptr;
    case DescriptorType::Sampler:
        if (!semantics::OriginalBindlessOrdinaryColorDomain::acceptsStages(binding.stages))
            return "RHI-VULKAN-BINDLESS-ORDINARY-SAMPLER-STAGE-UNSUPPORTED";
        return binding.comparisonSampler ? "RHI-VULKAN-BINDLESS-ORDINARY-COMPARISON-UNSUPPORTED"
                                         : nullptr;
    case DescriptorType::SampledTexture:
        if (!semantics::OriginalBindlessOrdinaryColorDomain::acceptsStages(binding.stages))
            return "RHI-VULKAN-BINDLESS-ORDINARY-IMAGE-STAGE-UNSUPPORTED";
        if (semantics::OriginalBindlessOrdinaryColorDomain::acceptsBinding(binding))
            return nullptr;
        return "RHI-VULKAN-BINDLESS-ORDINARY-IMAGE-UNSUPPORTED";
    default:
        return "RHI-VULKAN-BINDLESS-ORDINARY-RESOURCE-UNSUPPORTED";
    }
}

rhi::ValidationResult validateVulkanBindlessDescriptorRequirements(
    const VulkanDescriptorIndexingInspection& device, const rhi::BindlessShaderArtifact& artifact)
{
    if (!device.enablementVerified())
        return {false, device.diagnosticId};
    if (artifact.execution != rhi::BindlessExecution::Native)
        return {false, "RHI-VULKAN-BINDLESS-PACKAGE-MODE"};
    if (artifact.otherBindGroups
        || (!artifact.additionalDomains.empty() && artifact.pipelineInterfaceContractVersion != 1u))
        return {false, "RHI-VULKAN-BINDLESS-DOMAINS-INCOMPLETE"};
    for (const auto& domain : artifact.additionalDomains) {
        if (domain.set >= device.limits.maxBoundDescriptorSets)
            return {false, "RHI-VULKAN-BINDLESS-SET-LIMIT"};
        for (const auto& binding : domain.layout.bindings) {
            // A cold pipeline may retain an explicitly declared ordinary
            // depth/comparison interface. The Root's native closure uses the
            // separate validator above and still rejects this resource domain.
            const bool depthDeclaration = binding.type == rhi::DescriptorType::SampledTexture
                ? binding.depthTexture
                : binding.type == rhi::DescriptorType::Sampler && binding.comparisonSampler;
            const auto* id = depthDeclaration
                ? rhi::semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                      rhi::Backend::Vulkan, binding)
                : inspectVulkanBindlessOrdinaryBinding(binding);
            if (id)
                return {false, id};
        }
    }
    for (const auto& reserved : artifact.reservedPerStage)
        if (reserved.sampledTextures || reserved.samplers || reserved.storageTextures
            || reserved.uniformBuffers || reserved.storageBuffers)
            return {false, "RHI-VULKAN-BINDLESS-DOMAINS-INCOMPLETE"};
    const auto& limits = device.limits;
    if (artifact.set >= limits.maxBoundDescriptorSets)
        return {false, "RHI-VULKAN-BINDLESS-SET-LIMIT"};
    if (std::uint64_t(artifact.snapshotMetadata.wordCount) * 4u > limits.maxStorageBufferRange)
        return {false, "RHI-VULKAN-BINDLESS-BUFFER-RANGE-LIMIT"};
    using Counts = std::array<std::uint64_t, 5>;
    const Counts perStageLimits{{limits.maxPerStageDescriptorSampledImages,
        limits.maxPerStageDescriptorSamplers,
        limits.maxPerStageDescriptorStorageImages,
        limits.maxPerStageDescriptorUniformBuffers,
        limits.maxPerStageDescriptorStorageBuffers}};
    const Counts totalLimits{{limits.maxDescriptorSetSampledImages,
        limits.maxDescriptorSetSamplers,
        limits.maxDescriptorSetStorageImages,
        limits.maxDescriptorSetUniformBuffers,
        limits.maxDescriptorSetStorageBuffers}};
    Counts total{};
    std::array<Counts, 3> stages{};
    const auto inspect = [&](const rhi::DescriptorLayoutDesc& layout) -> rhi::ValidationResult {
        for (const auto& binding : layout.bindings) {
            const auto type = rhi::bindlessResourceClass(binding.type);
            const auto mask = static_cast<std::uint32_t>(binding.stages);
            if (type >= rhi::kBindlessResourceClasses || !binding.arrayCount || !mask
                || (mask & ~7u) || binding.dynamicOffset)
                return {false, "RHI-VULKAN-BINDLESS-BINDING-ABI"};
            const auto countClass = type == 5u ? 4u : type;
            total[countClass] += binding.arrayCount;
            for (std::size_t stage = 0; stage < stages.size(); ++stage)
                if (mask & (1u << stage))
                    stages[stage][countClass] += binding.arrayCount;
            // Writable ordinary descriptors also require stage-side-effect features.
            if (binding.type == rhi::DescriptorType::StorageBuffer
                || binding.type == rhi::DescriptorType::StorageTexture) {
                if ((mask & 1u) && !device.enabled.vertexStoresAndAtomics)
                    return {false, "RHI-VULKAN-BINDLESS-STAGE-WRITES-NOT-ENABLED"};
                if ((mask & 2u) && !device.enabled.fragmentStoresAndAtomics)
                    return {false, "RHI-VULKAN-BINDLESS-STAGE-WRITES-NOT-ENABLED"};
            }
        }
        return {true, {}};
    };
    auto inspected = inspect(artifact.physicalLayout);
    if (!inspected.success)
        return inspected;
    for (const auto& domain : artifact.additionalDomains) {
        inspected = inspect(domain.layout);
        if (!inspected.success)
            return inspected;
    }
    for (std::size_t type = 0; type < total.size(); ++type) {
        if (total[type] > totalLimits[type])
            return {false, "RHI-VULKAN-BINDLESS-PIPELINE-DESCRIPTOR-LIMIT"};
        for (const auto& stage : stages)
            if (stage[type] > perStageLimits[type])
                return {false, "RHI-VULKAN-BINDLESS-STAGE-DESCRIPTOR-LIMIT"};
    }
    for (const auto& stage : stages)
        // Separate sampler descriptors are excluded from maxPerStageResources.
        if (stage[0] + stage[2] + stage[3] + stage[4] > limits.maxPerStageResources)
            return {false, "RHI-VULKAN-BINDLESS-STAGE-RESOURCE-LIMIT"};
    for (const auto& table : artifact.tables) {
        const auto found = std::find_if(artifact.physicalLayout.bindings.begin(),
            artifact.physicalLayout.bindings.end(),
            [&](const auto& b) {
                return b.binding == table.firstBinding;
            });
        if (found == artifact.physicalLayout.bindings.end())
            return {false, "RHI-VULKAN-BINDLESS-BINDING-ABI"};
        const auto resource = found->type == rhi::DescriptorType::SampledTexture
                || found->type == rhi::DescriptorType::Sampler
            ? VulkanIndexedResource::SampledImage
            : found->type == rhi::DescriptorType::StorageTexture
            ? VulkanIndexedResource::StorageImage
            : found->type == rhi::DescriptorType::UniformBuffer
            ? VulkanIndexedResource::UniformBuffer
            : VulkanIndexedResource::StorageBuffer;
        const auto feature = static_cast<std::size_t>(resource);
        if (table.nonUniform ? !device.enabled.nonUniform[feature]
                             : !device.enabled.dynamicallyUniform[feature])
            return {false, "RHI-VULKAN-BINDLESS-INDEXING-NOT-ENABLED"};
        if (found->type == rhi::DescriptorType::UniformBuffer
            && table.bufferLayout.minBindingSizeBytes > limits.maxUniformBufferRange)
            return {false, "RHI-VULKAN-BINDLESS-BUFFER-RANGE-LIMIT"};
        if ((found->type == rhi::DescriptorType::StorageBuffer
                || found->type == rhi::DescriptorType::ReadOnlyStorageBuffer)
            && table.bufferLayout.minBindingSizeBytes > limits.maxStorageBufferRange)
            return {false, "RHI-VULKAN-BINDLESS-BUFFER-RANGE-LIMIT"};
    }
    return {true, {}};
}

rhi::ValidationResult validateVulkanBindlessComputeRequirements(
    const VulkanDescriptorIndexingInspection& device,
    const rhi::BindlessShaderArtifact& artifact,
    const std::array<std::uint32_t, 3>& workgroup)
{
    const auto descriptors = validateVulkanBindlessDescriptorRequirements(device, artifact);
    if (!descriptors.success)
        return descriptors;
    const auto& limits = device.limits;
    for (std::size_t i = 0; i < 3; ++i)
        if (!workgroup[i] || workgroup[i] > limits.maxComputeWorkGroupSize[i])
            return {false, "RHI-VULKAN-BINDLESS-WORKGROUP-LIMIT"};
    const auto xy = std::uint64_t(workgroup[0]) * workgroup[1];
    if (xy > limits.maxComputeWorkGroupInvocations
        || xy * workgroup[2] > limits.maxComputeWorkGroupInvocations)
        return {false, "RHI-VULKAN-BINDLESS-WORKGROUP-LIMIT"};
    return {true, {}};
}

namespace {
rhi::ValidationResult validateBindlessStorageFormats(
    const VulkanRhiRuntime& runtime, const rhi::BindlessShaderArtifact& artifact)
{
    for (const auto& binding : artifact.physicalLayout.bindings) {
        if (binding.type != rhi::DescriptorType::StorageTexture)
            continue;
        auto* getFormat =
            functionFrom<PFN_vkGetPhysicalDeviceFormatProperties>(runtime.getInstanceProcAddr()(
                runtime.instance(), "vkGetPhysicalDeviceFormatProperties"));
        const auto format = formatFor(binding.storageTextureFormat);
        VkFormatProperties properties{};
        if (getFormat && format != VK_FORMAT_UNDEFINED)
            getFormat(runtime.physicalDevice(), format, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
            return {false, "RHI-VULKAN-BINDLESS-STORAGE-FORMAT-UNSUPPORTED"};
    }
    return {true, {}};
}

core::pmr::Vector<std::uint32_t> moduleWords(
    const rhi::BindlessShaderPackage& package, const rhi::BindlessPackageModule& module)
{
    const auto& bytes = package.files()[module.spirvFile].bytes;
    core::pmr::Vector<std::uint32_t> words(
        bytes.size() / sizeof(std::uint32_t), &core::defaultCpuMemoryResource());
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}
} // namespace

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createBindlessComputePayload(
    const rhi::BindlessShaderPackage& package, core::StringView entry)
{
    VulkanRhiNativeResourceResult result;
    if (!connected() || runtime_->syntheticRuntime()) {
        result.diagnostics = "RHI-VULKAN-BINDLESS-NATIVE-RUNTIME-REQUIRED";
        return result;
    }
    const auto selected =
        std::find_if(package.modules().begin(), package.modules().end(), [&](const auto& module) {
            return module.entry == entry;
        });
    if (selected == package.modules().end()
        || selected->reflection.reflection.entryPoints[0].stage != shader::ShaderStage::Compute) {
        result.diagnostics = "RHI-VULKAN-BINDLESS-COMPUTE-ENTRY";
        return result;
    }
    const auto& compiledEntry = selected->reflection.reflection.entryPoints[0];
    const auto validation =
        validateVulkanBindlessComputeRequirements(runtime_->descriptorIndexing(),
            package.artifact(),
            {{compiledEntry.computeWorkgroupSizeX,
                compiledEntry.computeWorkgroupSizeY,
                compiledEntry.computeWorkgroupSizeZ}});
    if (!validation.success) {
        result.diagnostics = validation.diagnostics;
        return result;
    }
    const auto formats = validateBindlessStorageFormats(*runtime_, package.artifact());
    if (!formats.success) {
        result.diagnostics = formats.diagnostics;
        return result;
    }
    const auto layout = rhi::bindlessPipelineLayout(package.artifact());
    // Copy into aligned words during preparation, never during recording.
    const auto words = moduleWords(package, *selected);
    return createComputePayload(
        runtime_, layout, selected->entry, words.data(), words.size(), *stats_);
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createBindlessRenderPayload(
    std::shared_ptr<const rhi::BindlessShaderPackage> owner,
    const rhi::RenderPipelineDesc& authored,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::BindlessGraphicsPushRequest* push)
{
    VulkanRhiNativeResourceResult result;
    if (!owner || !owner->sharedOwnerMatches(owner)) {
        result.diagnostics = "RHI-BINDLESS-GRAPHICS-INTERFACE-ORIGINAL-OWNER";
        return result;
    }
    const auto& package = *owner;
    if (!connected() || runtime_->syntheticRuntime()) {
        result.diagnostics = "RHI-VULKAN-BINDLESS-NATIVE-RUNTIME-REQUIRED";
        return result;
    }
    const auto authoredValidation =
        rhi::validateBindlessAuthoredRenderState(authored, !fragmentEntry.empty());
    if (!authoredValidation.success) {
        result.diagnostics = authoredValidation.diagnostics;
        return result;
    }
    const auto requirements = validateVulkanBindlessDescriptorRequirements(
        runtime_->descriptorIndexing(), package.artifact());
    if (!requirements.success) {
        result.diagnostics = requirements.diagnostics;
        return result;
    }
    const auto formats = validateBindlessStorageFormats(*runtime_, package.artifact());
    if (!formats.success) {
        result.diagnostics = formats.diagnostics;
        return result;
    }
    rhi::ShaderPackageDesc interface;
    interface.label = package.artifact().physicalLayout.name;
    interface.generatedReflectionHydrated = true;
    auto layoutDesc = rhi::bindlessPipelineLayout(package.artifact());
    if (push) {
        if (const auto* id = push->diagnostic()) {
            result.diagnostics = id;
            return result;
        }
        rhi::BindlessGraphicsPipelineEntries entries;
        const auto composed = rhi::BindlessGraphicsPipelineEntries::compose(
            owner, vertexEntry, fragmentEntry, interface, layoutDesc, entries);
        if (!composed.success) {
            result.diagnostics = composed.diagnostics;
            return result;
        }
        if (const auto* id =
                rhi::semantics::OriginalBindlessPushConsumptionLowering::validateNativeSize(
                    interface.reflection.pushConstantByteCount,
                    push->maxBytes,
                    runtime_->descriptorIndexing().limits.maxPushConstantsSize)) {
            result.diagnostics = id;
            return result;
        }
    } else {
        if (!rhi::appendBindlessShaderEntry(
                package, vertexEntry, rhi::ShaderStage::Vertex, interface, result.diagnostics)
            || (!fragmentEntry.empty()
                && !rhi::appendBindlessShaderEntry(package,
                    fragmentEntry,
                    rhi::ShaderStage::Fragment,
                    interface,
                    result.diagnostics)))
            return result;
    }
    auto layout = createVulkanRhiPipelineLayout(layoutDesc);
    if (!layout) {
        result.diagnostics = "RHI-BINDLESS-PIPELINE-LAYOUT-INVALID";
        return result;
    }
    const auto reflected =
        rhi::validatePipelineLayoutAgainstShaderReflection(layoutDesc, interface.reflection);
    if (!reflected.success) {
        result.diagnostics = reflected.diagnostics;
        return result;
    }
    auto desc = authored;
    desc.layout = layout.get();
    desc.shaderPackage = &interface;
    desc.vertexShader = interface.entrypoints[0];
    if (!fragmentEntry.empty())
        desc.fragmentShader = interface.entrypoints[1];
    const auto lowered = rhi::semantics::lowerRenderPipelineState(
        rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Vulkan), desc);
    if (!lowered.success) {
        result.diagnostics = lowered.diagnostics;
        return result;
    }
    auto* getFormat =
        functionFrom<PFN_vkGetPhysicalDeviceFormatProperties>(runtime_->getInstanceProcAddr()(
            runtime_->instance(), "vkGetPhysicalDeviceFormatProperties"));
    for (std::uint32_t i = 0; i < rhi::effectiveColorTargetCount(desc); ++i) {
        const auto format = formatFor(rhi::effectiveColorTargetAt(desc, i).format);
        VkFormatProperties properties{};
        if (getFormat && format != VK_FORMAT_UNDEFINED)
            getFormat(runtime_->physicalDevice(), format, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            result.diagnostics = "RHI-VULKAN-BINDLESS-COLOR-FORMAT-UNSUPPORTED";
            return result;
        }
    }
    if (const auto depthFormat = rhi::effectiveDepthStencilFormat(desc);
        depthFormat != rhi::TextureFormat::Unknown) {
        const auto format = formatFor(depthFormat);
        VkFormatProperties properties{};
        if (getFormat && format != VK_FORMAT_UNDEFINED)
            getFormat(runtime_->physicalDevice(), format, &properties);
        if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            result.diagnostics = "RHI-VULKAN-BINDLESS-DEPTH-FORMAT-UNSUPPORTED";
            return result;
        }
    }
    const auto vertex =
        std::find_if(package.modules().begin(), package.modules().end(), [&](const auto& m) {
            return m.entry == vertexEntry;
        });
    const auto vertexWords = moduleWords(package, *vertex);
    if (fragmentEntry.empty())
        return createRenderPipelineFromBytes(
            desc, vertexWords.data(), vertexWords.size(), nullptr, 0);
    const auto fragment =
        std::find_if(package.modules().begin(), package.modules().end(), [&](const auto& m) {
            return m.entry == fragmentEntry;
        });
    const auto fragmentWords = moduleWords(package, *fragment);
    return createRenderPipelineFromBytes(
        desc, vertexWords.data(), vertexWords.size(), fragmentWords.data(), fragmentWords.size());
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createRenderPipeline(
    const rhi::RenderPipelineDesc& desc)
{
    if (!connected() || !desc.layout || !desc.shaderPackage || runtime_->syntheticRuntime())
        return createRenderPipelineFromBytes(desc, nullptr, 0, nullptr, 0);
    const auto words = readSpirvWords(desc.shaderPackage->artifacts.spirvPath);
    return createRenderPipelineFromBytes(
        desc, words.data(), words.size(), words.data(), words.size());
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createRenderPipelineFromBytes(
    const rhi::RenderPipelineDesc& desc,
    const std::uint32_t* vertexWords,
    std::size_t vertexCount,
    const std::uint32_t* fragmentWords,
    std::size_t fragmentCount)
{
    VulkanRhiNativeResourceResult result;
    if (!connected()) {
        result.diagnostics = "Vulkan RHI native render pipeline requires a native device";
        return result;
    }
    if (!desc.layout || !desc.shaderPackage) {
        result.diagnostics = "Vulkan RHI native render pipeline requires layout and shader package";
        return result;
    }
    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        result.diagnostics = "RHI-VULKAN-PATCH-LIST-TESSELLATION-ABI-NOT-LOWERED";
        return result;
    }
    const auto stateValidation = rhi::validateRenderPipelineState(desc);
    if (!stateValidation.success) {
        result.diagnostics = stateValidation.diagnostics;
        return result;
    }
    if (runtime_->syntheticRuntime()) {
        stats_->nativeShaderModuleCreateCount += desc.fragmentShader.entryPoint.empty() ? 1u : 2u;
        ++stats_->nativePipelineLayoutCreateCount;
        ++stats_->nativeRenderPassCreateCount;
        ++stats_->nativeRenderPipelineCreateCount;
        result.payload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
        result.success = true;
        return result;
    }

    const bool hasFragment =
        fragmentWords && fragmentCount && !desc.fragmentShader.entryPoint.empty();
    if (!hasFragment
        && (!stateValidation.depthOnlyStateValidated || fragmentWords || fragmentCount
            || !desc.fragmentShader.entryPoint.empty())) {
        result.diagnostics = "RHI-VULKAN-DEPTH-ONLY-FRAGMENT-ABI";
        return result;
    }

    const auto rasterizationSamples = sampleCountFor(desc.multisample.sampleCount);
    if (rasterizationSamples == static_cast<VkSampleCountFlagBits>(0)) {
        result.diagnostics = "Vulkan RHI native render pipeline MSAA sample count is unsupported";
        return result;
    }

    auto* createRenderPass =
        functionFrom<PFN_vkCreateRenderPass>(runtime_->getDeviceProcAddr("vkCreateRenderPass"));
    auto* createGraphicsPipelines = functionFrom<PFN_vkCreateGraphicsPipelines>(
        runtime_->getDeviceProcAddr("vkCreateGraphicsPipelines"));
    auto* destroyRenderPass =
        functionFrom<PFN_vkDestroyRenderPass>(runtime_->getDeviceProcAddr("vkDestroyRenderPass"));
    if (!createRenderPass || !createGraphicsPipelines || !destroyRenderPass) {
        result.diagnostics = "Vulkan RHI native render pipeline missing Vulkan entrypoints";
        return result;
    }

    auto layoutBuild = createPipelineLayout(*runtime_, desc.layout->desc(), *stats_);
    if (!layoutBuild.success) {
        result.diagnostics = layoutBuild.diagnostics;
        return result;
    }

    std::pmr::string diagnostics;
    auto vertexModule =
        createShaderModule(*runtime_, vertexWords, vertexCount, *stats_, diagnostics);
    auto fragmentModule = hasFragment
        ? createShaderModule(*runtime_, fragmentWords, fragmentCount, *stats_, diagnostics)
        : VK_NULL_HANDLE;
    if (!vertexModule || (hasFragment && !fragmentModule)) {
        destroyShaderModule(*runtime_, vertexModule);
        destroyShaderModule(*runtime_, fragmentModule);
        result.diagnostics = diagnostics;
        return result;
    }

    const bool depthOnlyRenderPipeline = stateValidation.depthOnlyStateValidated;
    const auto colorTargetCount =
        depthOnlyRenderPipeline ? 0u : rhi::effectiveColorTargetCount(desc);
    const auto& compatibility = desc.renderPassCompatibility;
    const bool hasCompatibility = compatibility.enabled();
    const auto renderPassColorCount = hasCompatibility
        ? static_cast<std::uint32_t>(compatibility.colorAttachmentFormats.size())
        : colorTargetCount;
    std::pmr::vector<VkAttachmentDescription> attachments;
    attachments.resize(renderPassColorCount
        + (rhi::isDepthStencilFormat(hasCompatibility ? compatibility.depthStencilFormat
                                                      : rhi::effectiveDepthStencilFormat(desc))
                ? 1u
                : 0u));
    std::pmr::vector<VkAttachmentReference> colorAttachmentRefs;
    const auto depthFormat = hasCompatibility ? compatibility.depthStencilFormat
                                              : rhi::effectiveDepthStencilFormat(desc);
    const bool hasDepthStencilAttachment = rhi::isDepthStencilFormat(depthFormat);
    std::uint32_t attachmentCount = renderPassColorCount;
    for (std::uint32_t index = 0u; index < renderPassColorCount; ++index) {
        const auto format = hasCompatibility ? compatibility.colorAttachmentFormats[index]
                                             : rhi::effectiveColorTargetAt(desc, index).format;
        auto& attachment = attachments[index];
        attachment.format = formatFor(format);
        if (attachment.format == VK_FORMAT_UNDEFINED) {
            destroyShaderModule(*runtime_, vertexModule);
            destroyShaderModule(*runtime_, fragmentModule);
            result.diagnostics =
                "Vulkan RHI pipeline color attachment format has no native lowering";
            return result;
        }
        attachment.samples = rasterizationSamples;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkAttachmentReference depthAttachmentRef{};
    if (hasDepthStencilAttachment) {
        const auto depthAttachmentIndex = attachmentCount;
        attachments[depthAttachmentIndex].format = formatFor(depthFormat);
        if (attachments[depthAttachmentIndex].format == VK_FORMAT_UNDEFINED) {
            destroyShaderModule(*runtime_, vertexModule);
            destroyShaderModule(*runtime_, fragmentModule);
            result.diagnostics = "Vulkan RHI pipeline depth target format has no native lowering";
            return result;
        }
        attachments[depthAttachmentIndex].samples = rasterizationSamples;
        attachments[depthAttachmentIndex].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[depthAttachmentIndex].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[depthAttachmentIndex].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[depthAttachmentIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[depthAttachmentIndex].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[depthAttachmentIndex].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachmentRef.attachment = depthAttachmentIndex;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ++attachmentCount;
    }

    std::pmr::vector<VkSubpassDescription> subpasses{&core::defaultCpuMemoryResource()};
    std::array<std::pmr::vector<VkAttachmentReference>,
        rhi::RenderPassCompatibilityDesc::kMaxSubpasses>
        subpassColorRefs{};
    std::array<std::pmr::vector<VkAttachmentReference>,
        rhi::RenderPassCompatibilityDesc::kMaxSubpasses>
        subpassInputRefs{};
    std::array<VkAttachmentReference, rhi::RenderPassCompatibilityDesc::kMaxSubpasses>
        subpassDepthRefs{};
    std::pmr::vector<VkSubpassDependency> dependencies{&core::defaultCpuMemoryResource()};

    if (hasCompatibility) {
        subpasses.resize(compatibility.subpasses.size());
        for (std::uint32_t sp = 0u; sp < static_cast<std::uint32_t>(compatibility.subpasses.size());
            ++sp) {
            const auto& source = compatibility.subpasses[sp];
            auto& colorRefs = subpassColorRefs[sp];
            auto& inputRefs = subpassInputRefs[sp];
            for (const auto index : source.colorAttachmentIndices) {
                if (index >= renderPassColorCount) {
                    destroyShaderModule(*runtime_, vertexModule);
                    destroyShaderModule(*runtime_, fragmentModule);
                    result.diagnostics =
                        "Vulkan RHI pipeline compatible color subpass index is out of range";
                    return result;
                }
                colorRefs.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
            }
            for (const auto index : source.inputAttachmentIndices) {
                if (index >= renderPassColorCount) {
                    destroyShaderModule(*runtime_, vertexModule);
                    destroyShaderModule(*runtime_, fragmentModule);
                    result.diagnostics =
                        "Vulkan RHI pipeline compatible input subpass index is out of range";
                    return result;
                }
                inputRefs.push_back({index, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            }
            auto& native = subpasses[sp];
            native.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            native.colorAttachmentCount = static_cast<std::uint32_t>(colorRefs.size());
            native.pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
            native.inputAttachmentCount = static_cast<std::uint32_t>(inputRefs.size());
            native.pInputAttachments = inputRefs.empty() ? nullptr : inputRefs.data();
            if (source.useDepthStencilAttachment) {
                if (!hasDepthStencilAttachment) {
                    destroyShaderModule(*runtime_, vertexModule);
                    destroyShaderModule(*runtime_, fragmentModule);
                    result.diagnostics =
                        "Vulkan RHI compatible subpass requires a depth attachment";
                    return result;
                }
                subpassDepthRefs[sp] = depthAttachmentRef;
                native.pDepthStencilAttachment = &subpassDepthRefs[sp];
            }
            if (source.readDepthInputAttachment) {
                destroyShaderModule(*runtime_, vertexModule);
                destroyShaderModule(*runtime_, fragmentModule);
                result.diagnostics =
                    "Vulkan RHI pipeline compatible depth input attachment lowering is unsupported";
                return result;
            }
        }
        dependencies.reserve(compatibility.subpassDependencies.size());
        for (const auto& source : compatibility.subpassDependencies) {
            VkSubpassDependency dependency{};
            dependency.srcSubpass = source.srcSubpass;
            dependency.dstSubpass = source.dstSubpass;
            dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependency.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT
                | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependencies.push_back(dependency);
        }
        const auto& selected = compatibility.subpasses[desc.subpassIndex];
        colorAttachmentRefs.resize(selected.colorAttachmentIndices.size());
        for (std::uint32_t index = 0u; index < colorTargetCount; ++index) {
            colorAttachmentRefs[index] = {
                selected.colorAttachmentIndices[index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        }
    } else {
        subpasses.resize(1u);
        auto& colorRefs = subpassColorRefs[0];
        colorRefs.resize(colorTargetCount);
        for (std::uint32_t index = 0u; index < colorTargetCount; ++index) {
            colorRefs[index] = {index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        }
        subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[0].colorAttachmentCount = colorTargetCount;
        subpasses[0].pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
        if (hasDepthStencilAttachment) {
            subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;
        }
        colorAttachmentRefs = colorRefs;
    }

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachmentCount;
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = static_cast<std::uint32_t>(subpasses.size());
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.empty() ? nullptr : dependencies.data();

    VkRenderPass renderPass = VK_NULL_HANDLE;
    const auto renderPassCreated =
        createRenderPass(runtime_->logicalDevice(), &renderPassInfo, nullptr, &renderPass);
    if (renderPassCreated != VK_SUCCESS) {
        destroyShaderModule(*runtime_, vertexModule);
        destroyShaderModule(*runtime_, fragmentModule);
        result.diagnostics = vkFailure("vkCreateRenderPass", renderPassCreated);
        return result;
    }
    ++stats_->nativeRenderPassCreateCount;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = desc.vertexShader.entryPoint.c_str();
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = desc.fragmentShader.entryPoint.c_str();

    std::pmr::vector<VkVertexInputBindingDescription> vertexBindings;
    std::pmr::vector<VkVertexInputAttributeDescription> vertexAttributes;
    vertexBindings.reserve(desc.vertexBuffers.size());
    for (const auto& vertexBuffer : desc.vertexBuffers) {
        const VkVertexInputRate rate = (vertexBuffer.inputRate == rhi::VertexInputRate::Instance)
            ? VK_VERTEX_INPUT_RATE_INSTANCE
            : VK_VERTEX_INPUT_RATE_VERTEX;
        vertexBindings.push_back({
            vertexBuffer.slot,
            vertexBuffer.strideBytes,
            rate,
        });
        for (const auto& attribute : vertexBuffer.attributes) {
            vertexAttributes.push_back({
                attribute.shaderLocation,
                vertexBuffer.slot,
                vertexFormatFor(attribute.format),
                attribute.offsetBytes,
            });
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertexBindings.size());
    vertexInput.pVertexBindingDescriptions =
        vertexBindings.empty() ? nullptr : vertexBindings.data();
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions =
        vertexAttributes.empty() ? nullptr : vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topologyFor(desc.topology);

    VkViewport viewport{};
    viewport.x = desc.hasViewport ? desc.viewport.x : 0.0f;
    viewport.y = desc.hasViewport ? desc.viewport.y : 0.0f;
    viewport.width = desc.hasViewport ? desc.viewport.width : 1.0f;
    viewport.height = desc.hasViewport ? desc.viewport.height : 1.0f;
    viewport.minDepth = desc.hasViewport ? desc.viewport.minDepth : 0.0f;
    viewport.maxDepth = desc.hasViewport ? desc.viewport.maxDepth : 1.0f;

    VkRect2D scissor{};
    scissor.offset = {
        desc.hasScissor ? desc.scissor.x : 0,
        desc.hasScissor ? desc.scissor.y : 0,
    };
    scissor.extent = {
        desc.hasScissor ? desc.scissor.width : 1u,
        desc.hasScissor ? desc.scissor.height : 1u,
    };

    // Dynamic viewport/scissor: when the pipeline opts in via
    // desc.dynamicViewportScissor, Vulkan must emit
    // VkPipelineDynamicStateCreateInfo with VK_DYNAMIC_STATE_VIEWPORT +
    // VK_DYNAMIC_STATE_SCISSOR. The viewport/scissor values above are then
    // ignored at draw time; the actual values come from
    // vkCmdSetViewport / vkCmdSetScissor issued by the render pass encoder.
    // The VkPipelineViewportStateCreateInfo still requires viewportCount=1
    // and scissorCount=1 (Vulkan spec: counts are static, only the data
    // pointers may be null when the state is dynamic), but pViewports /
    // pScissors are left null so Vulkan does not read the dummy values.
    // L2 validation (validateRenderPipelineState in gpu_pipeline.h) rejects
    // mixing dynamicViewportScissor with hasViewport/hasScissor, so reaching
    // here with dynamicViewportScissor=true implies hasViewport=hasScissor=false.
    VkPipelineDynamicStateCreateInfo dynamicState{};
    VkDynamicState dynamicStates[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    if (desc.dynamicViewportScissor) {
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;
    }

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = desc.dynamicViewportScissor ? nullptr : &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = desc.dynamicViewportScissor ? nullptr : &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = cullModeFor(desc.cullMode);
    rasterization.frontFace = frontFaceFor(desc.frontFace);
    rasterization.depthBiasEnable = desc.depthStencil.depthBiasEnabled ? VK_TRUE : VK_FALSE;
    rasterization.depthBiasConstantFactor = desc.depthStencil.depthBiasConstantFactor;
    rasterization.depthBiasClamp = desc.depthStencil.depthBiasClamp;
    rasterization.depthBiasSlopeFactor = desc.depthStencil.depthBiasSlopeFactor;
    rasterization.lineWidth = 1.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = compareOpFor(desc.depthStencil.depthCompare);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = desc.depthStencil.stencilEnabled ? VK_TRUE : VK_FALSE;
    depthStencil.front = stencilFaceFor(desc.depthStencil.front, desc.depthStencil);
    depthStencil.back = stencilFaceFor(desc.depthStencil.back, desc.depthStencil);

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    VkSampleMask sampleMask =
        static_cast<VkSampleMask>(desc.multisample.sampleMask & 0xffffffffull);
    multisample.rasterizationSamples = rasterizationSamples;
    multisample.pSampleMask = &sampleMask;
    multisample.alphaToCoverageEnable =
        desc.multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE;

    const auto colorBlendTargetCount = rhi::effectiveColorTargetCount(desc);
    std::pmr::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    colorBlendAttachments.resize(colorBlendTargetCount);
    for (std::uint32_t index = 0; index < colorBlendTargetCount; ++index) {
        const auto target = rhi::effectiveColorTargetAt(desc, index);
        auto& attachment = colorBlendAttachments[index];
        attachment.blendEnable = target.blendEnabled ? VK_TRUE : VK_FALSE;
        attachment.srcColorBlendFactor = blendFactorFor(target.colorBlend.sourceFactor);
        attachment.dstColorBlendFactor = blendFactorFor(target.colorBlend.destinationFactor);
        attachment.colorBlendOp = blendOpFor(target.colorBlend.operation);
        attachment.srcAlphaBlendFactor = blendFactorFor(target.alphaBlend.sourceFactor);
        attachment.dstAlphaBlendFactor = blendFactorFor(target.alphaBlend.destinationFactor);
        attachment.alphaBlendOp = blendOpFor(target.alphaBlend.operation);
        attachment.colorWriteMask = colorWriteMaskFor(target.writeMask);
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = depthOnlyRenderPipeline ? 0u : colorBlendTargetCount;
    colorBlend.pAttachments = depthOnlyRenderPipeline ? nullptr : colorBlendAttachments.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = hasFragment ? 2u : 1u;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    // Only wire pDynamicState when the pipeline opts into dynamic
    // viewport/scissor. Leaving it null for static pipelines preserves the
    // existing behavior of all 20 WoCC pipelines (they set hasViewport /
    // hasScissor, not dynamicViewportScissor).
    pipelineInfo.pDynamicState = desc.dynamicViewportScissor ? &dynamicState : nullptr;
    pipelineInfo.layout = layoutBuild.layout;
    pipelineInfo.renderPass = renderPass;
    // Subpass index: 0 = first/only subpass (default). Non-zero targets a
    // later subpass in a multi-subpass render pass; the throwaway renderPass
    // above is single-subpass, so non-zero subpassIndex requires a compatible
    // multi-subpass render pass (Prototype: renderer does not yet author
    // non-zero subpassIndex pipelines).
    pipelineInfo.subpass = desc.subpassIndex;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const auto pipelineCreated = createGraphicsPipelines(
        runtime_->logicalDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    destroyShaderModule(*runtime_, vertexModule);
    destroyShaderModule(*runtime_, fragmentModule);
    if (pipelineCreated != VK_SUCCESS) {
        destroyRenderPass(runtime_->logicalDevice(), renderPass, nullptr);
        result.diagnostics = vkFailure("vkCreateGraphicsPipelines", pipelineCreated);
        return result;
    }

    ++stats_->nativeRenderPipelineCreateCount;
    const auto nativeLayout = layoutBuild.releaseLayout();
    auto nativeDescriptorSetLayouts = layoutBuild.releaseDescriptorSetLayouts();
    const auto pushConstantStages = desc.layout->desc().pushConstantRanges.empty()
        ? 0u
        : shaderStagesFor(desc.layout->desc().pushConstantRanges.front().stages);
    result.payload = arp::core::makeShared<NativePipelinePayload>(runtime_,
        pipeline,
        nativeLayout,
        renderPass,
        pushConstantStages,
        std::move(nativeDescriptorSetLayouts));
    result.success = true;
    return result;
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createComputePipeline(
    const rhi::ComputePipelineDesc& desc)
{
    VulkanRhiNativeResourceResult result;
    if (!connected()) {
        result.diagnostics = "Vulkan RHI native compute pipeline requires a native device";
        return result;
    }
    if (!desc.layout || !desc.shaderPackage) {
        result.diagnostics =
            "Vulkan RHI native compute pipeline requires layout and shader package";
        return result;
    }
    if (runtime_->syntheticRuntime()) {
        ++stats_->nativeShaderModuleCreateCount;
        ++stats_->nativePipelineLayoutCreateCount;
        ++stats_->nativeComputePipelineCreateCount;
        result.payload = arp::core::makeShared<VulkanRhiNativeResourcePayload>();
        result.success = true;
        return result;
    }
    const auto words = readSpirvWords(desc.shaderPackage->artifacts.spirvPath);
    return createComputePipelineFromBytes(desc, words.data(), words.size());
}

VulkanRhiNativeResourceResult VulkanRhiNativeResourceAllocator::createComputePipelineFromBytes(
    const rhi::ComputePipelineDesc& desc, const std::uint32_t* words, std::size_t count)
{
    VulkanRhiNativeResourceResult result;
    if (!connected() || runtime_->syntheticRuntime() || !desc.layout || !desc.shaderPackage
        || !words || !count) {
        result.diagnostics = "RHI-VULKAN-ORIGINAL-COMPUTE-BYTES-INVALID";
        return result;
    }
    return createComputePayload(
        runtime_, desc.layout->desc(), desc.computeShader.entryPoint, words, count, *stats_);
}

} // namespace arp::rhi_vulkan
