#include "rhi/backends/vulkan/vulkan_device.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/dummy_sync.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/backends/vulkan/vulkan_conformance_clear.h"
#include "rhi/backends/vulkan/vulkan_heap_placement.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_original_compute_pipeline.h"
#include "rhi/backends/vulkan/vulkan_original_device_native_loss.h"
#include "rhi/backends/vulkan/vulkan_original_graphics_pipeline.h"
#include "rhi/backends/vulkan/vulkan_query_pool.h"
#include "rhi/backends/vulkan/vulkan_retained_command.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"
#include "rhi/backends/vulkan/vulkan_swapchain_retirement.h"
#include "rhi/frontend/device_validation.h"
#include "rhi/frontend/original_prepared_command_device.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_extensions.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/fallback_plan.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/native_engine_fallback_bridge.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace arp::rhi_vulkan {

namespace {

class VulkanTextureReadbackExtensionImpl final
    : public rhi::RetiredTextureReadbackExtension<rhi::Backend::Vulkan> {};

class NativeTextureViewPayload final : public VulkanRhiNativeResourcePayload {
public:
    NativeTextureViewPayload(std::shared_ptr<VulkanRhiNativeResourcePayload> texturePayload,
        rhi::TextureViewDesc viewDesc)
        : texturePayload_(std::move(texturePayload)), viewDesc_(std::move(viewDesc))
    {}

    VkDevice backendVulkanDeviceHandle() const override
    {
        return texturePayload_ ? texturePayload_->backendVulkanDeviceHandle() : VK_NULL_HANDLE;
    }

    std::shared_ptr<VulkanRhiNativeResourcePayload>
    backendVulkanTextureViewParentPayload() const override
    {
        return texturePayload_;
    }

    VkImage backendVulkanImageHandle() const override
    {
        return texturePayload_ ? texturePayload_->backendVulkanImageHandle() : VK_NULL_HANDLE;
    }

    bool backendVulkanOriginalContentExclusive() const noexcept override
    {
        return texturePayload_ && texturePayload_->backendVulkanOriginalContentExclusive();
    }

    VkFormat backendVulkanImageFormat() const override
    {
        return texturePayload_ ? texturePayload_->backendVulkanImageFormat() : VK_FORMAT_UNDEFINED;
    }

    rhi::Extent3D backendVulkanImageExtent() const override
    {
        return texturePayload_ ? texturePayload_->backendVulkanImageExtent() : rhi::Extent3D{};
    }

    bool backendVulkanTextureViewDescAvailable() const override { return true; }

    rhi::TextureViewDesc backendVulkanTextureViewDesc() const override { return viewDesc_; }

    const rhi::TextureViewDesc* backendVulkanOriginalTextureViewDesc() const noexcept override
    {
        return &viewDesc_;
    }

private:
    std::shared_ptr<VulkanRhiNativeResourcePayload> texturePayload_;
    rhi::TextureViewDesc viewDesc_;
};

rhi::TextureViewDesc resolvedTextureViewDesc(
    const rhi::GpuTexture& texture, rhi::TextureViewDesc desc)
{
    if (desc.format == rhi::TextureFormat::Unknown) {
        desc.format = texture.desc().format;
    }
    if (desc.usage == rhi::TextureUsage::None) {
        desc.usage = texture.desc().usage;
    }
    return desc;
}

class VulkanCompletionFence final : public rhi::GpuFence {
public:
    void signal() override
    {
        throw std::logic_error("RHI-VULKAN-FENCE-HOST-SIGNAL-UNAUTHENTICATED");
    }

    bool isSignaled() const override { return payload_ && payload_->submittedFenceCompleted(); }

    void wait() override
    {
        if (!payload_)
            throw std::logic_error("RHI-VULKAN-FENCE-NOT-SUBMITTED");
        std::pmr::string diagnostics;
        if (!payload_->waitSubmittedFence(diagnostics))
            throw std::runtime_error(diagnostics.c_str());
    }

    void reset() override
    {
        if (payload_ && !payload_->submittedFenceCompleted())
            throw std::logic_error("RHI-VULKAN-FENCE-RESET-PENDING");
        payload_.reset();
    }

    bool canBind() const noexcept { return !payload_; }

    bool bind(std::shared_ptr<VulkanRhiNativeCommandPayload> payload)
    {
        if (!payload || payload_) {
            return false;
        }
        payload_ = std::move(payload);
        return true;
    }

private:
    std::shared_ptr<VulkanRhiNativeCommandPayload> payload_;
};

class VulkanTimelineSemaphore;

bool makeVulkanTimelineSubmitInfo(rhi::GpuTimelineSemaphore* waitSemaphore,
    std::uint64_t waitValue,
    rhi::GpuTimelineSemaphore* signalSemaphore,
    std::uint64_t signalValue,
    VulkanRhiTimelineSubmitInfo& result,
    std::pmr::string& diagnostics);

class VulkanRhiQueue final : public rhi::GpuQueue {
public:
    explicit VulkanRhiQueue(rhi::GpuQueueRole role) : role_(role) {}

    rhi::GpuQueueRole role() const override { return role_; }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commandBuffer) override
    {
        return submit(commandBuffer, nullptr);
    }

    rhi::QueueSubmitResult submit(
        const rhi::CommandBuffer& commandBuffer, rhi::GpuFence* signalFence) override
    {
        return submit(commandBuffer, nullptr, 0, nullptr, 0, signalFence);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commandBuffer,
        rhi::GpuTimelineSemaphore* waitSemaphore,
        uint64_t waitValue,
        rhi::GpuTimelineSemaphore* signalSemaphore,
        uint64_t signalValue,
        rhi::GpuFence* signalFence) override
    {
        VulkanRhiTimelineSubmitInfo timeline;
        std::pmr::string timelineDiagnostics;
        if (!makeVulkanTimelineSubmitInfo(waitSemaphore,
                waitValue,
                signalSemaphore,
                signalValue,
                timeline,
                timelineDiagnostics)) {
            return {false, timelineDiagnostics, 0};
        }
        auto* completionFence =
            signalFence ? dynamic_cast<VulkanCompletionFence*>(signalFence) : nullptr;
        if (signalFence && !completionFence) {
            return {false, "Vulkan completion fence belongs to another backend", 0};
        }
        auto nativePayload = vulkanRhiCommandPayload(commandBuffer);
        if (completionFence && !nativePayload) {
            return {false, "Vulkan completion fence requires a native command payload", 0};
        }
        if (completionFence && !completionFence->canBind())
            return {false, "RHI-VULKAN-FENCE-ALREADY-BOUND", 0};
        auto result = submitVulkanRhiCommandBuffer(commandBuffer, timeline);
        if (result.hasAcceptedWork()) {
            ++submitCount_;
            if (completionFence && !completionFence->bind(std::move(nativePayload))) {
                if (!result.success)
                    return result; // Preserve the first accepted error.
                return {false,
                    "Vulkan completion fence binding failed",
                    result.submittedCommandCount,
                    true};
            }
        }
        return result;
    }

    std::uint32_t submitCount() const override { return submitCount_; }

private:
    rhi::GpuQueueRole role_;
    std::uint32_t submitCount_ = 0;
};

/// Vulkan-native timeline semaphore wrapping VK_SEMAPHORE_TYPE_TIMELINE.
/// All Vulkan functions are loaded dynamically via the runtime to avoid
/// link-time dependency on vulkan-1.lib.
class VulkanTimelineSemaphore final : public rhi::GpuTimelineSemaphore {
public:
    explicit VulkanTimelineSemaphore(std::shared_ptr<VulkanRhiTimelineSemaphorePayload> payload)
        : payload_(std::move(payload))
    {}

    void signal(uint64_t value) override
    {
        report(payload_->hostSignal(value), "RHI-VULKAN-TIMELINE-HOST-SIGNAL-FAILED");
    }

    void wait(uint64_t value) override { hostWait(value, UINT64_MAX); }

    uint64_t getValue() const override
    {
        std::uint64_t value = 0u;
        report(payload_->queryCounter(value), "RHI-VULKAN-TIMELINE-HOST-QUERY-FAILED");
        return value;
    }

    void hostWait(uint64_t value, uint64_t timeoutNs) override
    {
        report(payload_->hostWait(value, timeoutNs), "RHI-VULKAN-TIMELINE-HOST-WAIT-FAILED");
    }

    const std::shared_ptr<VulkanRhiTimelineSemaphorePayload>& payload() const noexcept
    {
        return payload_;
    }

private:
    static void report(VkResult result, const char* diagnostic)
    {
        if (result != VK_SUCCESS)
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic);
    }

    std::shared_ptr<VulkanRhiTimelineSemaphorePayload> payload_;
};

bool makeVulkanTimelineSubmitInfo(rhi::GpuTimelineSemaphore* waitSemaphore,
    std::uint64_t waitValue,
    rhi::GpuTimelineSemaphore* signalSemaphore,
    std::uint64_t signalValue,
    VulkanRhiTimelineSubmitInfo& result,
    std::pmr::string& diagnostics)
{
    result = {};
    const auto* wait =
        waitSemaphore ? dynamic_cast<const VulkanTimelineSemaphore*>(waitSemaphore) : nullptr;
    const auto* signal =
        signalSemaphore ? dynamic_cast<const VulkanTimelineSemaphore*>(signalSemaphore) : nullptr;
    if ((waitSemaphore && (!wait || !wait->payload() || !wait->payload()->nativeHandle()))
        || (signalSemaphore
            && (!signal || !signal->payload() || !signal->payload()->nativeHandle()))) {
        diagnostics = "RHI-VULKAN-TIMELINE-SYNC-REQUIRES-NATIVE-SEMAPHORE";
        return false;
    }
    if (waitSemaphore) {
        result.waitSemaphore = wait->payload();
        result.waitValue = waitValue;
    }
    if (signalSemaphore) {
        result.signalSemaphore = signal->payload();
        result.signalValue = signalValue;
    }
    return true;
}

} // namespace

class VulkanRhiDevice final : public rhi::GpuDevice,
                              public rhi::OriginalPreparedCommandDeviceExtension {
public:
    core::UniquePtr<VulkanRhiRetainedCommand> prepareRetainedCommand(
        core::UniquePtr<rhi::common::CommandRetentionStorage> storage,
        core::pmr::String& diagnostic)
    {
        return VulkanRhiRetainedCommand::prepare(
            nativeResources_, resourceRegistry_, std::move(storage), diagnostic);
    }

    explicit VulkanRhiDevice(VulkanRhiDeviceDesc desc)
        : desc_(std::move(desc)), allocator_(
                                      {
                                          desc_.label,
                                          desc_.memoryBudgetBytes,
                                          desc_.enableVmaAllocator,
                                          desc_.allowNativeHandleExposureToUpperLayers,
                                      },
                                      desc_.runtime),
          stagingUploadPool_(
              {
                  desc_.label.empty() ? "VulkanRhiUploadStagingPool"
                                      : desc_.label + "UploadStagingPool",
                  desc_.uploadStagingPageSizeBytes,
                  desc_.uploadStagingMaxPageCount,
                  desc_.uploadStagingBudget ? desc_.uploadStagingBudget->alignmentBytes : 256u,
                  desc_.uploadStagingBudget ? desc_.uploadStagingBudget->maxRequestBytes : 0u,
              },
              allocator_),
          nativeResources_(desc_.runtime, &allocator_), queue_(rhi::GpuQueueRole::Graphics),
          bindless_(desc_.runtime.get(), &nativeResources_, directObjectBudget_)
    {
        if (desc_.runtime && desc_.runtime->queueFamilyCount() >= 2) {
            hasAsyncCompute_ = true;
        }
    }

    ~VulkanRhiDevice() override
    {
        closeOriginalDeviceLifetimeForShutdown();
        drainVulkanRhiSubmittedCommandsForShutdown();
        if (desc_.runtime && !desc_.runtime->syntheticRuntime()) {
            const char* diagnostic = "";
            if (retireVulkanRhiSwapchainLifetimes(desc_.runtime, diagnostic))
                rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic);
        }
    }

    rhi::Backend backend() const override { return rhi::Backend::Vulkan; }

    void* queryInterface(std::string_view extensionId) noexcept override
    {
        if (extensionId == rhi::OriginalPreparedCommandDeviceExtension::id)
            return static_cast<rhi::OriginalPreparedCommandDeviceExtension*>(this);
        if (extensionId == rhi::kExtensionBindlessCross) {
            return static_cast<rhi::BindlessCrossExtension*>(&bindless_);
        }
        if (extensionId == rhi::kExtensionTextureReadback) {
            return &textureReadbackExtension_;
        }
        return nullptr;
    }

    const rhi::GpuDevice& originalDevice() const noexcept override { return *this; }

    std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation> createGraphics(
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::RenderPipelineDesc& authored,
        rhi::OriginalSynchronousGraphicsCreationContract::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const char*& diagnostic) override
    {
        return createOriginalGraphicsCreation(
            input, layout, authored, budget, ownerQuota, nativeQuota, diagnostic);
    }

    core::UniquePtr<rhi::CommandEncoder> prepare(
        std::shared_ptr<const rhi::OriginalCommandStreamProgram> program,
        const rhi::OriginalRenderPassPreparationRequest& request,
        core::pmr::String& diagnostic) override
    {
        if (const auto* id = validateRequest(request)) {
            diagnostic = id;
            return {};
        }
        return createVulkanRhiCommandEncoderWithColdRenderPasses(nativeResources_,
            resourceRegistry_,
            &stagingUploadPool_,
            desc_.capabilityContract,
            std::move(program),
            *this,
            request,
            diagnostic);
    }

    rhi::DeviceCapabilities capabilities() const override
    {
        auto caps =
            rhi::makeSharedDeviceCapabilities(rhi::Backend::Vulkan, "RHI Vulkan", desc_.label);
        const bool nativeAvailable =
            desc_.runtime && desc_.runtime->coreQueueCapabilityDiagnostic() == nullptr;
        if (nativeAvailable) {
            caps.core = desc_.runtime->coreQueueCapabilities();
            caps.explicitFeatures.explicitBarriers = true;
            caps.swapchain = rhi::SwapchainCapabilities{};
            const auto& memory = desc_.runtime->memoryProperties();
            for (std::uint32_t i = 0u; i < memory.memoryTypeCount; ++i) {
                caps.memory.deviceLocal = caps.memory.deviceLocal
                    || (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                        != 0u;
                caps.memory.hostVisible = caps.memory.hostVisible
                    || (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                        != 0u;
            }
        } else if (desc_.runtime) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, desc_.runtime->coreQueueCapabilityDiagnostic());
        }
        caps.explicitFeatures.pushConstants = nativeAvailable
            && rhi::semantics::hasNativeFeature(
                desc_.capabilityContract, rhi::semantics::NativeBackendFeature::PushConstants);
        caps.explicitFeatures.secondaryCommandBuffers = nativeAvailable
            && rhi::semantics::hasNativeFeature(desc_.capabilityContract,
                rhi::semantics::NativeBackendFeature::SecondaryCommandBuffers);
        caps.memory.transientAliasing = false;
        // RuntimeFeatureCapabilities now driven by EngineFallbackPlan,
        // not raw native feature queries.
        rhi::EngineFeatureRequirementSet requirements;
        requirements.logicalFrameSlots = 2;
        requirements.features = {
            {rhi::EngineFeature::PushConstants, {}},
            {rhi::EngineFeature::SecondaryCommandBuffer, {}},
            {rhi::EngineFeature::DescriptorTable, {}},
            {rhi::EngineFeature::TransientAliasing, {}},
            {rhi::EngineFeature::TimelineSync, {}},
            {rhi::EngineFeature::ComputeDispatch, {}},
            {rhi::EngineFeature::StorageTextureBinding, {}},
            {rhi::EngineFeature::NativeSync, {}},
            {rhi::EngineFeature::SubpassInputAttachment, {}},
            {rhi::EngineFeature::SubpassDepthInputAttachment, {}},
            {rhi::EngineFeature::Rg16FloatStorageTexture, {}},
            {rhi::EngineFeature::Rgba16FloatStorageTexture, {}},
            {rhi::EngineFeature::StorageBufferBinding, {}},
            {rhi::EngineFeature::SceneGeometryRayQuery, {}},
            {rhi::EngineFeature::IndirectDraw, {}},
            {rhi::EngineFeature::MultiDrawIndirectCount, {}},
        };
        auto plan =
            rhi::semantics::buildNativeEngineFallbackPlan(desc_.capabilityContract, requirements);
        plan.applyTo(caps);
        caps.features.timelineSync = caps.features.timelineSync && desc_.runtime
            && desc_.runtime->timelineSemaphoreEnabled();
        if (desc_.runtime && !desc_.runtime->syntheticRuntime()
            && desc_.runtime->physicalDevice() != VK_NULL_HANDLE) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(desc_.runtime->physicalDevice(), &properties);
            VkFormatProperties depthProperties{};
            vkGetPhysicalDeviceFormatProperties(
                desc_.runtime->physicalDevice(), VK_FORMAT_D32_SFLOAT, &depthProperties);
            constexpr VkFormatFeatureFlags kSampledDepthFeatures =
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            auto& compute = caps.computeResources;
            compute.sampledDepthTexture =
                (depthProperties.optimalTilingFeatures & kSampledDepthFeatures)
                == kSampledDepthFeatures;
            compute.maxComputeWorkgroupInvocations =
                properties.limits.maxComputeWorkGroupInvocations;
            compute.maxComputeWorkgroupSizeX = properties.limits.maxComputeWorkGroupSize[0];
            compute.maxComputeWorkgroupSizeY = properties.limits.maxComputeWorkGroupSize[1];
            compute.maxComputeWorkgroupSizeZ = properties.limits.maxComputeWorkGroupSize[2];
            compute.maxComputeWorkgroupCountX = properties.limits.maxComputeWorkGroupCount[0];
            compute.maxComputeWorkgroupCountY = properties.limits.maxComputeWorkGroupCount[1];
            compute.maxComputeWorkgroupCountZ = properties.limits.maxComputeWorkGroupCount[2];
            compute.maxUniformBufferBindingsPerStage =
                properties.limits.maxPerStageDescriptorUniformBuffers;
            compute.maxUniformBufferBindingSizeBytes = properties.limits.maxUniformBufferRange;
            compute.maxSampledTextureBindingsPerStage =
                properties.limits.maxPerStageDescriptorSampledImages;
            compute.maxTexture2DDimension = properties.limits.maxImageDimension2D;
            compute.maxComputeWorkgroupMemoryBytes = properties.limits.maxComputeSharedMemorySize;
            compute.maxStorageBufferBindingsPerStage =
                properties.limits.maxPerStageDescriptorStorageBuffers;
            compute.maxStorageBufferBindingSizeBytes = properties.limits.maxStorageBufferRange;
            const bool complete = compute.sampledDepthTexture && caps.features.computeDispatch
                && caps.features.storageBufferBinding
                && compute.maxComputeWorkgroupInvocations != 0u
                && compute.maxComputeWorkgroupSizeX != 0u && compute.maxComputeWorkgroupSizeY != 0u
                && compute.maxComputeWorkgroupMemoryBytes != 0u
                && compute.maxStorageBufferBindingsPerStage != 0u
                && compute.maxStorageBufferBindingSizeBytes != 0u;
            compute.mode =
                complete ? rhi::CapabilityMode::Native : rhi::CapabilityMode::DiagnosticFailure;
        }
        caps.features.timestampQueries = desc_.runtime && desc_.runtime->timestampQueriesEnabled();
        // The whole RHI BC capability is optional in Vulkan. Publish it only
        // after querying every BC format exposed by this RHI (BC1, BC3, and
        // both BC7 color spaces) for sampled + transfer-destination usage.
        // Synthetic runtimes stay fail-closed and use decoded assets.
        caps.features.textureCompressionBc = false;
        if (desc_.runtime && !desc_.runtime->syntheticRuntime()
            && desc_.runtime->physicalDevice() != VK_NULL_HANDLE) {
            constexpr VkFormatFeatureFlags kUploadAndSample =
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            const auto supportsSampledUpload = [&](VkFormat format) {
                VkFormatProperties properties{};
                vkGetPhysicalDeviceFormatProperties(
                    desc_.runtime->physicalDevice(), format, &properties);
                return (properties.optimalTilingFeatures & kUploadAndSample) == kUploadAndSample;
            };
            caps.features.textureCompressionBc =
                supportsSampledUpload(VK_FORMAT_BC1_RGBA_UNORM_BLOCK)
                && supportsSampledUpload(VK_FORMAT_BC3_UNORM_BLOCK)
                && supportsSampledUpload(VK_FORMAT_BC7_UNORM_BLOCK)
                && supportsSampledUpload(VK_FORMAT_BC7_SRGB_BLOCK);
            caps.features.r32FloatSampledUpload = supportsSampledUpload(VK_FORMAT_R32_SFLOAT);
        }

        // ExplicitMemoryControl says that Vulkan can express aliasing, not
        // that this backend currently has the required bounded heap pool.
        // Do not expose a native capability until its lowering avoids
        // per-frame VkDeviceMemory allocation and has a deterministic budget
        // and reuse policy. This is an explicit L2 capability degradation;
        // FrameGraph then selects its non-alias transient path, matching the
        // existing Metal/WebGPU lowering contract.
        if (heapPlacement_.admissionDiagnostic()) {
            caps.memory.transientAliasing = false;
            caps.features.transientAliasing = false;
        }

        // Validate lowering model against contract.
        // A representative render pass workload exercises the full
        // command/descriptor/resource lowering chain at device creation.
        {
            rhi::semantics::CommandWorkloadDesc workload;
            workload.label = "vulkan-backend-validation";
            workload.commandCount = 1;
            workload.usesRenderPass = true;
            workload.usesPushConstants = caps.features.pushConstants;
            workload.usesBarriers = true;
            workload.colorAttachmentCount = 1;
            workload.usesDepthStencilAttachment = true;
            workload.usesTransferCopies = true;
            workload.usesUploadStaging = true;
            workload.usesSubresourceBarriers = false;
            workload.usesStageAccessBarriers = false;

            auto decision = rhi::semantics::lowerCommandWorkload(desc_.capabilityContract,
                workload,
                rhi::semantics::CommandRecordingIntent::PrimaryCommandBuffer);

            if (!decision.success) {
                rhi::RhiErrorHandler::get().report(
                    rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            }
        }

        // Track A P2: persistent lowering plan (built once at device creation).
        {
            auto pcPlan = desc_.pushConstantLoweringPlan;
            rhi::semantics::applyPushConstantLoweringPlan(caps, pcPlan);
        }

        caps.features.computeDispatch = caps.features.computeDispatch && caps.core.compute;
        caps.features.sceneGeometryRayQuery =
            caps.features.sceneGeometryRayQuery && caps.features.computeDispatch;
        if (!nativeAvailable)
            rhi::revokeUnavailableNativeDeviceCapabilities(caps);
        rhi::synchronizeExecutionStatuses(caps);
        rhi::synchronizeQueryExecutionStatuses(caps);

        return caps;
    }

    rhi::MemoryStats memoryStats() const override
    {
        auto stats = allocator_.memoryStats();
        stats.backendFallbacksHiddenFromUpperLayer = true;
        return stats;
    }

    rhi::semantics::PhysicalBudgetLedgerReport physicalBudgetLedger() const override
    {
        rhi::semantics::PhysicalBudgetLedgerReport report;
        report.backend = backend();

        const auto memory = memoryStats();
        if (memory.budgetCapacityBytes != 0u) {
            report.category(rhi::semantics::PhysicalBudgetCategory::ResourceAllocation) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::Bytes,
                    memory.budgetCapacityBytes,
                    memory.allocatedBytes,
                    memory.peakAllocatedBytes,
                    memory.budgetRejectionCount,
                    memory.liveResourceCount);
        }

        const auto pageSize = desc_.uploadStagingPageSizeBytes;
        const auto maxPageCount = static_cast<std::uint64_t>(desc_.uploadStagingMaxPageCount);
        if (pageSize != 0u && maxPageCount != 0u
            && pageSize <= std::numeric_limits<std::uint64_t>::max() / maxPageCount) {
            const auto staging = stagingUploadPool_.stats();
            if (staging.boundedCapacity) {
                report.category(rhi::semantics::PhysicalBudgetCategory::UploadStaging) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::Bytes,
                        pageSize * maxPageCount,
                        staging.allocatedBytes,
                        staging.peakAllocatedBytes,
                        staging.failedAllocationCount,
                        staging.liveSliceCount,
                        staging.retiredSliceCount,
                        rhi::semantics::PhysicalBudgetChargeRelation::OverlapsResourceAllocation);
            }
        }
        if (descriptorCache_) {
            const auto descriptors = descriptorCache_->budgetStats();
            if (descriptors.configured) {
                report.category(rhi::semantics::PhysicalBudgetCategory::Descriptor) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::Descriptors,
                        descriptors.capacity,
                        descriptors.current,
                        descriptors.peak,
                        descriptors.rejectionCount,
                        descriptorCache_->entryCount());
            }
        }
        if (pipelineCache_) {
            const auto pipelines = pipelineCache_->budgetStats();
            if (pipelines.configured) {
                report.category(rhi::semantics::PhysicalBudgetCategory::PipelineCache) =
                    rhi::semantics::observedPhysicalBudgetObservation(
                        rhi::semantics::PhysicalBudgetUnit::PipelineEntries,
                        pipelines.capacity,
                        pipelines.current,
                        pipelines.peak,
                        pipelines.rejectionCount,
                        pipelineCache_->entryCount());
            }
        }
        if (directObjectBudget_) {
            const auto direct = directObjectBudget_->stats();
            report.category(rhi::semantics::PhysicalBudgetCategory::DirectPipelineObjects) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::PipelineEntries,
                    direct.pipelineObjectCapacity,
                    direct.currentPipelineObjects,
                    direct.peakPipelineObjects,
                    direct.pipelineRejectionCount,
                    direct.currentPipelineObjects);
            report.category(rhi::semantics::PhysicalBudgetCategory::DirectDescriptorArenas) =
                rhi::semantics::observedPhysicalBudgetObservation(
                    rhi::semantics::PhysicalBudgetUnit::Descriptors,
                    direct.descriptorCapacity,
                    direct.currentDescriptors,
                    direct.peakDescriptors,
                    direct.descriptorRejectionCount,
                    direct.currentDescriptorReservations);
        }
        return report;
    }

    rhi::GpuTimingSample latestGpuTiming() const override
    {
        return latestVulkanRhiGpuTimingSample();
    }

    rhi::semantics::UploadStagingReadiness uploadStagingReadiness() const override
    {
        return {
            rhi::semantics::UploadStagingReadinessStatus::Available,
            stagingUploadPool_.completedSubmissionEpoch(),
            nullptr,
        };
    }

    rhi::GpuQueue& queue() override { return queue_; }

    void waitIdle() override
    {
        const auto runtime = nativeResources_.runtime();
        waitIdleAndRetireVulkanRhiSubmittedCommands(runtime);
    }

    rhi::semantics::ShutdownDrainReport drainForShutdown(
        const rhi::semantics::ShutdownDrainRequest& request) override
    {
        const auto started = std::chrono::steady_clock::now();
        if (!request.validAt(started)) {
            return rhi::semantics::rejectedShutdownDrainReport(
                backend(), started, request, started);
        }
        rhi::semantics::ShutdownDrainReport report;
        report.backend = backend();
        const auto runtime = nativeResources_.runtime();
        if (!runtime || runtime->syntheticRuntime() || !runtime->logicalDevice()) {
            report.diagnosticId = "ARP-RHI-VULKAN-SHUTDOWN-DRAIN-NATIVE-RUNTIME-UNAVAILABLE";
        } else if (runtime->surface() && !runtime->presentFenceEnabled()) {
            report.diagnosticId = "ARP-RHI-VULKAN-SHUTDOWN-DRAIN-PRESENT-UNAVAILABLE";
        } else {
            std::pmr::string diagnostics;
            const bool commandsComplete =
                drainVulkanRhiSubmittedCommandsUntil(request.deadline, diagnostics);
            const bool presentsComplete = commandsComplete
                && (!runtime->surface()
                    || runtime->drainPresentFencesUntil(request.deadline, diagnostics));
            if (commandsComplete && presentsComplete
                && pendingVulkanRhiSwapchainLifetimes(runtime) != 0u) {
                report.diagnosticId = "RHI-VULKAN-SHUTDOWN-SWAPCHAIN-RETIREMENT-PENDING";
            } else if (commandsComplete && presentsComplete) {
                report.outcome = rhi::semantics::ShutdownDrainOutcome::Native;
                report.state = rhi::semantics::ShutdownDrainState::Completed;
            } else {
                report.diagnosticId = diagnostics;
                if (diagnostics == "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED") {
                    report.state = rhi::semantics::ShutdownDrainState::DeadlineExpired;
                }
            }
        }
        rhi::semantics::finalizeShutdownDrainTiming(
            report, started, request, std::chrono::steady_clock::now());
        return report;
    }

    rhi::GpuQueue* computeQueue() override
    {
        // Fail-closed: real Vulkan async compute is NOT yet implemented.
        // VulkanRhiQueue carries no native VkQueue handle of its own — both
        // queue_ (graphics) and computeQueue_ (compute) submit through the
        // same submitVulkanRhiCommandBuffer path, so they do NOT run on
        // separate GPU queues. Likewise GpuTimelineSemaphore wait/signal are
        // host-side operations (hostWait before vkQueueSubmit, signal after
        // it returns) rather than GPU-side sync wired into VkSubmitInfo.
        // Exposing computeQueue_ here would make the RenderGraph executor
        // believe it is performing real multi-queue execution and emit
        // asyncComputeSplitExecuted / asyncComputeTimelineUsed telemetry that
        // does not reflect reality (no real queue overlap, no GPU completion
        // ordering). Return nullptr so the executor takes the honest
        // single-queue degradation path with a diagnostic. The async split
        // DAG logic itself remains exercised on the Null backend (Prototype).
        // hasAsyncCompute_ (runtime queue-family detection) is retained for a
        // future real implementation that binds a compute-only VkQueue and
        // pushes timeline semaphores into VkSubmitInfo.
        (void)hasAsyncCompute_;
        return nullptr;
    }

    arp::core::UniquePtr<rhi::GpuBuffer> createBuffer(const rhi::BufferDesc& desc) override
    {
        auto nativeResource = nativeResources_.createBuffer(desc);
        if (!nativeResource.success || !nativeResource.payload) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-VULKAN-NATIVE-CREATION-FAILED");
            return nullptr;
        }
        auto allocation = nativeResource.allocation;
        if (!allocation.success) {
            return nullptr;
        }
        const auto id = nextResourceId();
        if (std::getenv("ARP_FORWARD_PLUS_BUFFER_TRACE") != nullptr && nativeResource.payload) {
            std::cerr << "[vulkan-buffer-create] id=" << id.value << " label=" << desc.label.c_str()
                      << " vkBuffer=" << nativeResource.payload->backendVulkanBufferHandle()
                      << " mapped=" << nativeResource.payload->backendMappedData()
                      << " size=" << desc.sizeBytes << '\n';
        }
        auto buffer =
            createVulkanRhiBuffer(id, desc, allocation, nativeResource.payload, resourceRegistry_);
        // Publish the weak native registration only after the owned wrapper
        // exists. Wrapper-allocation failure must not leave a weak control block
        // charged to its CPU quota until an unrelated creation prunes it.
        if (buffer && nativeResource.payload) {
            resourceRegistry_->registerResource(id, nativeResource.payload);
            resourceRegistry_->registerBufferObject(id, *buffer);
        }
        return completeOriginalResourceCreation(std::move(buffer));
    }

    rhi::CommandValidationResult writeBuffer(rhi::GpuBuffer& buffer,
        const void* data,
        std::uint64_t sizeBytes,
        std::uint64_t offsetBytes = 0) override
    {
        if (buffer.hostWritesSealed())
            return {false, "RHI-BUFFER-HOST-WRITES-SEALED", 0};
        if (!data || sizeBytes == 0u || offsetBytes > buffer.desc().sizeBytes
            || sizeBytes > buffer.desc().sizeBytes - offsetBytes) {
            return {false, "Vulkan RHI buffer write desc is invalid", 0};
        }
        if (auto runtime = nativeResources_.runtime(); runtime && runtime->syntheticRuntime()) {
            return {true, "", 1};
        }
        const auto payload = resourceRegistry_->payload(buffer.id());
        if (!payload
            || !core::SharedOwnerProjection::sameOwner(payload, vulkanRhiNativePayload(buffer)))
            return {false, "RHI-VULKAN-BUFFER-WRITE-ORIGINAL-OWNER", 0};
        auto* mapped = payload ? payload->backendMappedData() : nullptr;
        if (!mapped) {
            return {false, "Vulkan RHI buffer write requires mapped native upload memory", 0};
        }
        std::memcpy(static_cast<unsigned char*>(mapped) + offsetBytes, data, sizeBytes);
        std::pmr::string flushDiagnostics;
        if (!payload->backendFlushMappedWrite(offsetBytes, sizeBytes, flushDiagnostics)) {
            payload->originalHostInitializedObject_ = nullptr;
            payload->originalFrozenHostInitialization_.store(false, std::memory_order_release);
            payload->originalHostInitialization_.reset();
            payload->originalHostInitializedBytes_ = 0u;
            return {false, flushDiagnostics, 0};
        }
        if (!offsetBytes && sizeBytes == buffer.desc().sizeBytes) {
            payload->originalHostInitializedObject_ = &buffer;
            payload->originalFrozenHostInitialization_.store(false, std::memory_order_release);
            payload->originalHostInitialization_.reset();
            if (const auto initial = buffer.observeOriginalCommittedState())
                payload->originalHostInitialization_.emplace(*initial);
            payload->originalHostInitializedBytes_ = sizeBytes;
        }
        if (std::getenv("ARP_FORWARD_PLUS_BUFFER_TRACE") != nullptr && offsetBytes == 0u
            && sizeBytes >= 16u) {
            const auto* words = static_cast<const std::uint32_t*>(data);
            std::cerr << "[vulkan-buffer-trace] id=" << buffer.id().value
                      << " label=" << buffer.desc().label.c_str() << " vkBuffer="
                      << (payload ? payload->backendVulkanBufferHandle() : VK_NULL_HANDLE)
                      << " mapped=" << mapped << " size=" << sizeBytes << " words=" << words[0]
                      << ',' << words[1] << ',' << words[2] << ',' << words[3] << '\n';
        }
        return {true, "", 1};
    }

    arp::core::UniquePtr<rhi::GpuBufferView> createBufferView(
        const rhi::GpuBuffer& buffer, const rhi::BufferViewDesc& desc) override
    {
        if (!resourceRegistry_->payload(buffer.id()))
            return nullptr;
        return createVulkanRhiBufferView(nextResourceId(), buffer.id(), desc);
    }

    arp::core::UniquePtr<rhi::GpuTexture> createTexture(const rhi::TextureDesc& desc) override
    {
        return createTexturePayload(desc, 0u, nullptr);
    }

    core::UniquePtr<rhi::GpuTexture> createOriginalContentTexturePayload(
        const rhi::TextureDesc& desc,
        const rhi::OriginalTextureContentCreationContract::Decision& decision,
        const rhi::OriginalTextureContentCreationBudget& budget,
        const char*& diagnostic) override
    {
        auto runtime = nativeResources_.runtime();
        if (!rhi::OriginalTextureContentCreationContract::hasNativeAllocationBudget(
                decision.contractVersion)
            || !decision.exclusiveCreationAvailable
            || budget.contractVersion != decision.contractVersion
            || !budget.maxNativeAllocationBytes
            || desc.initialState != rhi::ResourceState::Undefined) {
            diagnostic = "RHI-VULKAN-CONTENT-CREATION-PROFILE";
            return {};
        }
        if (!runtime || !runtime->valid() || runtime->syntheticRuntime()) {
            diagnostic = "RHI-VULKAN-CONTENT-CREATION-AUTHENTIC-RUNTIME";
            return {};
        }
        return createTexturePayload(desc, budget.maxNativeAllocationBytes, &diagnostic);
    }

    std::uint64_t queryOriginalContentTextureAllocationPayload(
        const rhi::TextureDesc& desc, const char*& diagnostic) override
    {
        return nativeResources_.queryTextureAllocationBytes(desc, diagnostic);
    }

    core::UniquePtr<rhi::GpuTexture> createTexturePayload(const rhi::TextureDesc& desc,
        std::uint64_t maxNativeAllocationBytes,
        const char** diagnostic)
    {
        if (diagnostic)
            *diagnostic = "RHI-VULKAN-CONTENT-CREATION-CAPABILITY";
        if (desc.format == rhi::TextureFormat::R32Float
            && (static_cast<std::uint32_t>(desc.usage)
                   & static_cast<std::uint32_t>(rhi::TextureUsage::Sampled))
                != 0u
            && (static_cast<std::uint32_t>(desc.usage)
                   & static_cast<std::uint32_t>(rhi::TextureUsage::CopyDst))
                != 0u
            && !capabilities().features.r32FloatSampledUpload) {
            return nullptr;
        }
        if (rhi::isBlockCompressed(desc.format) && !capabilities().features.textureCompressionBc) {
            std::cerr << "[arp-vulkan] block-compressed texture rejected label="
                      << desc.label.c_str()
                      << " reason=textureCompressionBc capability unavailable\n";
            return nullptr;
        }
        auto nativeResource = nativeResources_.createTexture(desc, maxNativeAllocationBytes);
        if (maxNativeAllocationBytes && !nativeResource.success) {
            if (diagnostic)
                *diagnostic =
                    nativeResource.diagnostics == "RHI-VULKAN-CONTENT-CREATION-NATIVE-CAPACITY"
                    ? "RHI-VULKAN-CONTENT-CREATION-NATIVE-CAPACITY"
                    : nativeResource.diagnostics == "RHI-VULKAN-CONTENT-CREATION-DEVICE-CAPACITY"
                    ? "RHI-VULKAN-CONTENT-CREATION-DEVICE-CAPACITY"
                    : "RHI-VULKAN-CONTENT-CREATION-NATIVE-ALLOCATION";
            return {};
        }
        if (!nativeResource.success || !nativeResource.payload) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-VULKAN-NATIVE-CREATION-FAILED");
            return nullptr;
        }
        auto allocation = nativeResource.allocation;
        if (!allocation.success) {
            return nullptr;
        }
        const auto id = nextResourceId();
        resourceRegistry_->registerTexture(id, nativeResource.payload);
        // VkImage creation has no implicit layout transition.  Keeping a
        // caller-declared ShaderRead/ColorAttachment state here would make the
        // L2 tracker disagree with Vulkan's actual UNDEFINED layout and skip
        // the first required barrier.  Native textures therefore always enter
        // the command stream as Undefined; their first use must explicitly
        // lower a ResourceBarrierDesc.
        auto trackedDesc = desc;
        trackedDesc.initialState = rhi::ResourceState::Undefined;
        try {
            auto result = completeOriginalResourceCreation(createVulkanRhiTexture(
                id, trackedDesc, allocation, nativeResource.payload, resourceRegistry_));
            if (diagnostic)
                *diagnostic = result ? nullptr : "RHI-VULKAN-CONTENT-CREATION-REGISTRATION";
            return result;
        } catch (...) {
            resourceRegistry_->unregisterResource(id);
            throw;
        }
    }

    rhi::ResourceBarrierResult prepareTextureForPoolReuse(rhi::GpuTexture& texture) override
    {
        if (texture.state() == rhi::ResourceState::Undefined) {
            return {true,
                "",
                texture.id(),
                rhi::ResourceState::Undefined,
                rhi::ResourceState::Undefined,
                true};
        }
        return transitionVulkanRhiTexture(texture,
            rhi::ResourceBarrierDesc{texture.id(),
                rhi::defaultTextureSubresourceRange(texture.desc()),
                texture.state(),
                rhi::ResourceState::Undefined});
    }

    rhi::ResourceBarrierResult prepareBufferForPoolReuse(rhi::GpuBuffer& buffer) override
    {
        if (buffer.state() == rhi::ResourceState::Undefined) {
            return {true,
                "",
                buffer.id(),
                rhi::ResourceState::Undefined,
                rhi::ResourceState::Undefined};
        }
        return transitionVulkanRhiBuffer(buffer,
            rhi::ResourceBarrierDesc{
                buffer.id(), {}, buffer.state(), rhi::ResourceState::Undefined});
    }

    arp::core::UniquePtr<rhi::GpuMemoryHeap> createMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, rhi::MemoryUsage usage) override
    {
        lastHeapDiagnostic_ = nullptr;
        auto heap = createOriginalMemoryHeap([&] {
            return heapPlacement_.createHeap(sizeBytes, alignmentBytes, usage, lastHeapDiagnostic_);
        });
        if (!heap && !lastHeapDiagnostic_)
            lastHeapDiagnostic_ = lastOriginalResourceRegistrationDiagnostic().data();
        reportHeapDiagnostic(lastHeapDiagnostic_);
        return heap;
    }

    std::string_view lastMemoryHeapCreationDiagnostic() const override
    {
        return lastHeapDiagnostic_ ? lastHeapDiagnostic_ : "";
    }

    arp::core::UniquePtr<rhi::GpuTexture> createPlacedTexture(const rhi::TextureDesc& desc,
        const rhi::GpuMemoryHeap& heap,
        std::uint64_t offsetBytes) override
    {
        if (const auto* diagnostic = originalMemoryHeapPlacementDiagnostic(heap)) {
            reportHeapDiagnostic(diagnostic);
            return nullptr;
        }
        auto result = heapPlacement_.createPlaced(desc, heap, offsetBytes);
        if (!result.payload) {
            reportHeapDiagnostic(result.diagnostic);
            return nullptr;
        }
        try {
            const auto id = nextResourceId();
            auto texture = createVulkanRhiTexture(
                id, desc, result.allocation, result.payload, resourceRegistry_);
            // The wrapper removes its own weak registration on every later
            // failure. Before it exists, no registry entry may retain a failed
            // payload's control block under the caller's CPU quota.
            if (texture) {
                resourceRegistry_->registerTexture(id, result.payload);
                resourceRegistry_->registerTextureObject(id, *texture);
            }
            return completeOriginalResourceCreation(std::move(texture));
        } catch (const std::bad_alloc&) {
            reportHeapDiagnostic("RHI-VULKAN-HEAP-HOST-ALLOCATION-FAILED");
            return nullptr;
        }
    }

    rhi::TexturePlacementRequirements queryTexturePlacementRequirements(
        const rhi::TextureDesc& desc) override
    {
        const char* diagnostic = nullptr;
        const auto requirements = heapPlacement_.query(desc, diagnostic);
        reportHeapDiagnostic(diagnostic);
        return requirements;
    }

    arp::core::UniquePtr<rhi::GpuTextureView> createTextureView(
        const rhi::GpuTexture& texture, const rhi::TextureViewDesc& desc) override
    {
        if (!resourceRegistry_->payload(texture.id()))
            return nullptr;
        auto resolved = resolvedTextureViewDesc(texture, desc);
        auto viewNativePayload = arp::core::makeShared<NativeTextureViewPayload>(
            resourceRegistry_->payload(texture.id()), resolved);
        auto view = createVulkanRhiTextureView(
            nextResourceId(), texture.id(), resolved, viewNativePayload, resourceRegistry_);
        if (view) {
            resourceRegistry_->registerResource(view->id(), viewNativePayload, texture.id());
        }
        return completeOriginalResourceCreation(std::move(view), &texture);
    }

    arp::core::UniquePtr<rhi::GpuSampler> createSampler(const rhi::SamplerDesc& desc) override
    {
        auto nativeSampler = nativeResources_.createSampler(desc);
        if (!nativeSampler.success || !nativeSampler.payload) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-VULKAN-NATIVE-CREATION-FAILED");
            return nullptr;
        }
        auto sampler = createVulkanRhiSampler(
            nextResourceId(), desc, nativeSampler.payload, resourceRegistry_);
        if (sampler && nativeSampler.payload) {
            resourceRegistry_->registerResource(sampler->id(), nativeSampler.payload);
        }
        return completeOriginalResourceCreation(std::move(sampler));
    }

    arp::core::UniquePtr<rhi::PipelineLayout> createPipelineLayout(
        const rhi::PipelineLayoutDesc& desc) override
    {
        auto reservation =
            reserveDirectPipeline(rhi::semantics::DirectObjectBudgetKind::PipelineLayout);
        if (!reservation.success)
            return nullptr;
        return createVulkanRhiPipelineLayout(desc, std::move(reservation.lease));
    }

    rhi::PipelineValidationReport validateRenderPipeline(
        const rhi::RenderPipelineDesc& desc) override
    {
        return validateVulkanRhiRenderPipeline(desc);
    }

    rhi::PipelineValidationReport validateComputePipeline(
        const rhi::ComputePipelineDesc& desc) override
    {
        return validateVulkanRhiComputePipeline(desc);
    }

    arp::core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        const rhi::RenderPipelineDesc& desc) override
    {
        // Capability lowering gate: run the L2 lowering decision (which
        // includes validateRenderPipelineState plus per-backend capability
        // checks such as the Vulkan depthBiasClamp feature, VUID-00754)
        // before any native allocation. A descriptor the device cannot lower
        // must fail closed instead of reaching the native pipeline builder
        // where the unsupported state would be silently dropped.
        const auto decision =
            rhi::semantics::lowerRenderPipelineState(desc_.capabilityContract, desc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        const auto validation = validateVulkanRhiRenderPipeline(desc);
        if (!validation.success) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error,
                validation.diagnostics.empty() ? "Vulkan RHI render pipeline validation failed"
                                               : validation.diagnostics.c_str());
            return nullptr;
        }
        auto reservation =
            reserveDirectPipeline(rhi::semantics::DirectObjectBudgetKind::RenderPipeline);
        if (!reservation.success)
            return nullptr;
        auto nativePipeline = nativeResources_.createRenderPipeline(desc);
        if (!nativePipeline.success || !nativePipeline.payload) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error,
                nativePipeline.diagnostics.empty()
                    ? "Vulkan RHI native render pipeline creation failed"
                    : nativePipeline.diagnostics.c_str());
            return nullptr;
        }
        if (directObjectBudget_)
            nativePipeline.payload->directObjectBudgetReleaseToken =
                core::makeShared<rhi::semantics::DirectObjectBudgetLease>(
                    std::move(reservation.lease));
        return createVulkanRhiRenderPipeline(desc, nativePipeline.payload);
    }

    std::shared_ptr<const VulkanOriginalArtifactGraphicsCreation> createOriginalGraphicsCreation(
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::RenderPipelineDesc& authored,
        VulkanOriginalArtifactGraphicsCreation::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const char*& diagnostic)
    {
        diagnostic = rhi::OriginalSynchronousGraphicsCreationContract::validateBudget(budget);
        if (diagnostic)
            return {};
        if (!nativeQuota || nativeQuota->limitBytes() != budget.maxCpuNativeBytes) {
            diagnostic = "RHI-SYNC-GRAPHICS-CREATION-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(nativeQuota);
            const auto lowering =
                rhi::semantics::lowerRenderPipelineState(desc_.capabilityContract, authored);
            if (!lowering.success) {
                diagnostic = "RHI-SYNC-GRAPHICS-CREATION-STATE-LOWERING";
                return {};
            }
            auto reservation =
                reserveDirectPipeline(rhi::semantics::DirectObjectBudgetKind::RenderPipeline);
            if (!reservation.success) {
                diagnostic = "RHI-SYNC-GRAPHICS-CREATION-DIRECT-BUDGET";
                return {};
            }
            return VulkanOriginalArtifactGraphicsCreation::create(*this,
                nativeResources_,
                input,
                layout,
                authored,
                budget,
                ownerQuota,
                nativeQuota,
                std::move(reservation.lease),
                diagnostic);
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-SYNC-GRAPHICS-CREATION-ALLOCATION";
            return {};
        }
    }

    std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>
    createOriginalSynchronousGraphicsCreation(
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::RenderPipelineDesc& authored,
        const rhi::OriginalSynchronousGraphicsCreationContract::Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const char*& diagnostic) override
    {
        return createOriginalGraphicsCreation(
            input, layout, authored, budget, ownerQuota, nativeQuota, diagnostic);
    }

    std::shared_ptr<const rhi::OriginalComputePipelineCreation>
    createOriginalSynchronousComputeCreation(
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>& input,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const rhi::ComputePipelineDesc& authored,
        const rhi::OriginalSynchronousComputeCreationContract::Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        const char*& diagnostic) override
    {
        diagnostic = rhi::OriginalSynchronousComputeCreationContract::validateBudget(budget);
        if (diagnostic)
            return {};
        if (!nativeQuota || nativeQuota->limitBytes() != budget.maxCpuNativeBytes) {
            diagnostic = "RHI-SYNC-COMPUTE-CREATION-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(nativeQuota);
            const auto lowering =
                rhi::semantics::lowerComputePipelineState(desc_.capabilityContract, authored);
            if (!lowering.success) {
                diagnostic = "RHI-SYNC-COMPUTE-CREATION-STATE-LOWERING";
                return {};
            }
            auto reservation =
                reserveDirectPipeline(rhi::semantics::DirectObjectBudgetKind::ComputePipeline);
            if (!reservation.success) {
                diagnostic = "RHI-SYNC-COMPUTE-CREATION-DIRECT-BUDGET";
                return {};
            }
            return VulkanOriginalArtifactComputeCreation::create(*this,
                nativeResources_,
                input,
                layout,
                authored,
                budget,
                ownerQuota,
                nativeQuota,
                std::move(reservation.lease),
                diagnostic);
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-SYNC-COMPUTE-CREATION-ALLOCATION";
            return {};
        }
    }

    core::UniquePtr<rhi::ComputePipeline> createAcceptedOriginalComputePipeline(
        const rhi::ComputePipelineDesc& definition,
        const std::shared_ptr<const rhi::OriginalComputePipelineCreation>& creation,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        return VulkanAcceptedOriginalComputePipelineFactory::create(
            definition, creation, quota, desc_.runtime.get(), diagnostic);
    }

    core::UniquePtr<rhi::RenderPipeline> createAcceptedOriginalGraphicsPipeline(
        const rhi::RenderPipelineDesc& definition,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>& creation,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        return VulkanAcceptedOriginalGraphicsPipelineFactory::create(
            definition, creation, quota, desc_.runtime.get(), diagnostic);
    }

    arp::core::UniquePtr<rhi::ComputePipeline> createComputePipeline(
        const rhi::ComputePipelineDesc& desc) override
    {
        // Capability lowering gate: run the L2 compute lowering decision
        // (which includes validateComputePipelineState) before any native
        // allocation. A descriptor the device cannot lower must fail closed
        // instead of reaching the native compute pipeline builder.
        const auto decision =
            rhi::semantics::lowerComputePipelineState(desc_.capabilityContract, desc);
        if (!decision.success) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, decision.diagnostics.c_str());
            return nullptr;
        }
        const auto validation = validateVulkanRhiComputePipeline(desc);
        if (!validation.success) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error,
                validation.diagnostics.empty() ? "Vulkan RHI compute pipeline validation failed"
                                               : validation.diagnostics.c_str());
            return nullptr;
        }
        auto reservation =
            reserveDirectPipeline(rhi::semantics::DirectObjectBudgetKind::ComputePipeline);
        if (!reservation.success)
            return nullptr;
        auto nativePipeline = nativeResources_.createComputePipeline(desc);
        if (!nativePipeline.success || !nativePipeline.payload) {
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error,
                nativePipeline.diagnostics.empty()
                    ? "Vulkan RHI native compute pipeline creation failed"
                    : nativePipeline.diagnostics.c_str());
            return nullptr;
        }
        if (directObjectBudget_)
            nativePipeline.payload->directObjectBudgetReleaseToken =
                core::makeShared<rhi::semantics::DirectObjectBudgetLease>(
                    std::move(reservation.lease));
        return createVulkanRhiComputePipeline(desc, nativePipeline.payload);
    }

    arp::core::UniquePtr<rhi::DescriptorArena> createDescriptorArena(
        const rhi::DescriptorArenaDesc& desc) override
    {
        return createVulkanRhiDescriptorArena(
            desc, nativeResources_, resourceRegistry_, true, directObjectBudget_);
    }

    core::UniquePtr<rhi::OriginalConformanceClearExecution> createConformanceClearExecution(
        core::UniquePtr<rhi::ConformanceClearWorkload> workload,
        const rhi::ConformanceClearExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        diagnostic = rhi::ConformanceClearExecutionContract::validate(request);
        if (diagnostic)
            return {};
        if ((diagnostic = originalDeviceNativeLossDiagnostic()))
            return {};
        if (!quota || quota->limitBytes() != request.observation.maxCpuMetadataBytes) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto execution = core::makeUnique<VulkanConformanceClearExecution>(
                VulkanConformanceClearExecution::Key{},
                *this,
                nativeResources_,
                desc_.capabilityContract,
                request,
                quota);
            diagnostic = execution->initialize(std::move(workload));
            if (diagnostic)
                return {};
            return execution;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION";
            return {};
        }
    }

    core::UniquePtr<rhi::OriginalConformanceSampledExecution> createConformanceSampledExecution(
        core::UniquePtr<rhi::ConformanceSampledWorkload> workload,
        const rhi::ConformanceSampledExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        diagnostic = rhi::ConformanceSampledExecutionContract::validate(request);
        if (diagnostic || (diagnostic = originalDeviceNativeLossDiagnostic()))
            return {};
        if (!quota || quota->limitBytes() != request.execution.observation.maxCpuMetadataBytes) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto execution = core::makeUnique<VulkanConformanceClearExecution>(
                VulkanConformanceClearExecution::Key{},
                *this,
                nativeResources_,
                desc_.capabilityContract,
                request.execution,
                quota);
            diagnostic =
                execution->initialize(std::move(workload), request.maxNativeInputBufferBytes);
            if (diagnostic)
                return {};
            return execution;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION";
            return {};
        }
    }

    core::UniquePtr<rhi::OriginalConformanceStorageMrtExecution>
    createConformanceStorageMrtExecution(
        core::UniquePtr<rhi::ConformanceStorageMrtWorkload> workload,
        const rhi::ConformanceStorageMrtExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        diagnostic = rhi::ConformanceStorageMrtExecutionContract::validate(request);
        if (diagnostic || (diagnostic = originalDeviceNativeLossDiagnostic()))
            return {};
        if (!quota || quota->limitBytes() != request.execution.observation.maxCpuMetadataBytes) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto execution = core::makeUnique<VulkanConformanceClearExecution>(
                VulkanConformanceClearExecution::Key{},
                *this,
                nativeResources_,
                desc_.capabilityContract,
                request.execution,
                quota);
            diagnostic = execution->initialize(std::move(workload),
                request.maxNativeInputBufferBytes,
                request.maxNativeTextureBytes);
            if (diagnostic)
                return {};
            return execution;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CONFORMANCE-ORIGINAL-CPU-ALLOCATION";
            return {};
        }
    }

    arp::core::UniquePtr<rhi::CommandEncoder> createCommandEncoder(
        const rhi::CommandEncoderDesc& commandDesc) override
    {
        return createVulkanRhiCommandEncoder(nativeResources_,
            resourceRegistry_,
            &stagingUploadPool_,
            desc_.capabilityContract,
            bindless_.capabilities().execution == rhi::BindlessExecution::Native
                && bindless_.capabilities().executablePayload
                && bindless_.capabilities().faultReadbackContractVersion == 1u,
            // A Vulkan command encoder is an executable backend object. The
            // resource-allocation fallback must never turn it into a
            // semantic-only recorder.
            true,
            commandDesc);
    }

    arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createSecondaryCommandEncoder(
        const rhi::SecondaryCommandEncoderDesc& commandDesc) override
    {
        return createVulkanRhiSecondaryCommandEncoder(commandDesc);
    }

    arp::core::UniquePtr<rhi::RenderBundleEncoder> createRenderBundleEncoder(
        const rhi::RenderBundleDesc& bundleDesc) override
    {
        return createVulkanRhiRenderBundleEncoder(bundleDesc);
    }

    arp::core::UniquePtr<rhi::Swapchain> createSwapchain(const rhi::SwapchainDesc& desc) override
    {
        if (!desc_.runtime || !desc_.runtime->valid()) {
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-VULKAN-SWAPCHAIN-NATIVE-UNAVAILABLE");
            return nullptr;
        }
        return createVulkanRhiSwapchain(desc, nativeResources_, resourceRegistry_);
    }

    arp::core::UniquePtr<rhi::GpuFence> createFence() override
    {
        return nativeResources_.runtime() ? arp::core::makeUnique<VulkanCompletionFence>()
                                          : nullptr;
    }

    arp::core::UniquePtr<rhi::GpuSemaphore> createSemaphore() override
    {
        // The public binary semaphore API has no queue-bound Vulkan lowering
        // yet.  Swapchain binary semaphores remain private to the swapchain;
        // do not expose a dummy primitive as NativeSync.
        return nullptr;
    }

    arp::core::UniquePtr<rhi::GpuTimelineSemaphore> createTimelineSemaphore(
        uint64_t initialValue = 0) override
    {
        if (auto runtime = nativeResources_.runtime(); runtime && runtime->logicalDevice()
            && runtime->timelineSemaphoreEnabled()
            && rhi::semantics::hasExecutableNativeFeature(desc_.capabilityContract,
                rhi::semantics::NativeBackendFeature::TimelineSynchronization)) {
            core::pmr::String diagnostic;
            auto payload =
                VulkanRhiTimelineSemaphorePayload::create(runtime, initialValue, diagnostic);
            if (payload)
                return core::makeUnique<VulkanTimelineSemaphore>(std::move(payload));
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic.c_str());
        }
        return nullptr;
    }

    arp::core::UniquePtr<rhi::QueryPool> createQueryPool(const rhi::QueryPoolDesc& desc) override
    {
        return createVulkanRhiQueryPool(desc_.runtime, desc);
    }

    std::shared_ptr<rhi::QueryPool> createOriginalQueryPoolCandidate(
        std::shared_ptr<const rhi::QueryPoolInputSnapshot> input,
        const rhi::OriginalQueryPoolCreationBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic) override
    {
        return VulkanRhiQueryPool::prepareOriginal(
            desc_.runtime, std::move(input), budget, std::move(quota), diagnostic);
    }

    const char* validateOriginalQueryPoolCandidate(
        const std::shared_ptr<const rhi::QueryPool>& object) const noexcept override
    {
        if (!desc_.runtime)
            return "RHI-VULKAN-ORIGINAL-QUERY-RUNTIME";
        return VulkanRhiQueryPool::validateOriginal(
            std::dynamic_pointer_cast<const VulkanRhiQueryPool>(object), *desc_.runtime);
    }

    rhi::PipelineCache& pipelineCache() override
    {
        if (!pipelineCache_) {
            pipelineCache_ = arp::core::makeUnique<rhi::PipelineCache>(*this);
        }
        return *pipelineCache_;
    }

    rhi::DescriptorCache& descriptorCache() override
    {
        if (!descriptorCache_) {
            descriptorCache_ = arp::core::makeUnique<rhi::DescriptorCache>(*this);
        }
        return *descriptorCache_;
    }

    VulkanRhiNativeResourceStats nativeResourceStats() const
    {
        auto stats = nativeResources_.stats();
        const auto staging = stagingUploadPool_.stats();
        stats.nativeUploadStagingPoolPageCount = staging.allocatedPageCount;
        stats.nativeUploadStagingSliceCount = staging.liveSliceCount + staging.retiredSliceCount;
        stats.nativeUploadStagingPeakBytes = staging.peakAllocatedBytes;
        stats.nativeUploadStagingPoolBounded = staging.boundedCapacity;
        stats.nativeUploadStagingHandlesHidden = staging.nativeHandlesHiddenFromUpperLayer;
        return stats;
    }

private:
    std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> bindOriginalDeviceNativeResultLoss(
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeResultLossBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic) override
    {
        return VulkanOriginalDeviceNativeResultLoss::prepare(
            nativeResources_.runtime(), receiver, budget, std::move(quota), diagnostic);
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuBuffer& buffer) const noexcept override
    {
        const auto runtime = nativeResources_.runtime();
        const auto payload = vulkanRhiNativePayload(buffer);
        return runtime && !runtime->syntheticRuntime() && payload
                && payload->backendVulkanBufferHandle()
                && payload->backendVulkanDeviceHandle() == runtime->logicalDevice()
                && core::SharedOwnerProjection::sameOwner(
                    payload, resourceRegistry_->payload(buffer.id()))
            ? nullptr
            : "RHI-VULKAN-RESULT-LOSS-RESOURCE-SOURCE";
    }

    const char* validateOriginalNativeLossPipeline(
        const rhi::ComputePipeline& pipeline) const noexcept override
    {
        const auto runtime = nativeResources_.runtime();
        const auto payload = vulkanRhiNativePayload(pipeline);
        return runtime && runtime->valid() && !runtime->syntheticRuntime()
                && !runtime->coreQueueCapabilityDiagnostic()
                && runtime->coreQueueCapabilities().compute && payload
                && payload->backendVulkanDeviceHandle() == runtime->logicalDevice()
                && payload->backendVulkanComputePipelineHandle()
                && payload->backendVulkanComputePipelineLayoutHandle()
            ? nullptr
            : "RHI-VULKAN-RESULT-LOSS-COMPUTE-PIPELINE-SOURCE";
    }

    const char* validateOriginalNativeLossPipeline(
        const rhi::RenderPipeline& pipeline) const noexcept override
    {
        // The common factory has authenticated the accepted wrapper seal;
        // registration and creation ownership are published after this check.
        const auto runtime = nativeResources_.runtime();
        const auto payload = vulkanRhiNativePayload(pipeline);
        return runtime && runtime->valid() && !runtime->syntheticRuntime()
                && !runtime->coreQueueCapabilityDiagnostic() && payload
                && payload->backendVulkanDeviceHandle() == runtime->logicalDevice()
                && payload->backendVulkanRenderPipelineHandle()
                && payload->backendVulkanRenderPipelineLayoutHandle()
            ? nullptr
            : "RHI-VULKAN-RESULT-LOSS-GRAPHICS-PIPELINE-SOURCE";
    }

    const char* validateOriginalNativeLossDescriptor(
        const rhi::DescriptorArena& arena) const noexcept override
    {
        return validateVulkanOriginalDescriptorLossSource(
            arena, nativeResources_.runtime(), resourceRegistry_);
    }

    const char* validateOriginalNativeLossDescriptor(
        const rhi::DescriptorSet& set) const noexcept override
    {
        return validateVulkanOriginalDescriptorLossSource(
            set, nativeResources_.runtime(), resourceRegistry_);
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuSampler& sampler) const noexcept override
    {
        const auto runtime = nativeResources_.runtime();
        const auto payload = vulkanRhiNativePayload(sampler);
        return runtime && runtime->valid() && !runtime->syntheticRuntime() && payload
                && payload->backendVulkanSamplerHandle()
                && payload->backendVulkanDeviceHandle() == runtime->logicalDevice()
                && core::SharedOwnerProjection::sameOwner(
                    payload, resourceRegistry_->payload(sampler.id()))
            ? nullptr
            : "RHI-VULKAN-RESULT-LOSS-SAMPLER-SOURCE";
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuTexture& texture) const noexcept override
    {
        const auto runtime = nativeResources_.runtime();
        const auto payload = vulkanRhiNativePayload(texture);
        return runtime && !runtime->syntheticRuntime() && payload
                && payload->backendVulkanImageHandle()
                && payload->backendVulkanDeviceHandle() == runtime->logicalDevice()
                && core::SharedOwnerProjection::sameOwner(
                    payload, resourceRegistry_->payload(texture.id()))
            ? nullptr
            : "RHI-VULKAN-RESULT-LOSS-RESOURCE-SOURCE";
    }

    const char* validateOriginalNativeLossResource(
        const rhi::GpuTextureView& view) const noexcept override
    {
        const auto runtime = nativeResources_.runtime();
        const auto payload = vulkanRhiNativePayload(view);
        const auto parent = payload ? payload->backendVulkanTextureViewParentPayload() : nullptr;
        return runtime && !runtime->syntheticRuntime() && payload && parent
                && parent->backendVulkanImageHandle()
                && payload->backendVulkanDeviceHandle() == runtime->logicalDevice()
                && parent->backendVulkanDeviceHandle() == runtime->logicalDevice()
                && core::SharedOwnerProjection::sameOwner(
                    payload, resourceRegistry_->payload(view.id()))
            ? nullptr
            : "RHI-VULKAN-RESULT-LOSS-VIEW-SOURCE";
    }

    rhi::GpuResourceId nextResourceId() { return rhi::allocateGpuResourceId(); }

    rhi::semantics::DirectObjectBudgetReservation reserveDirectPipeline(
        rhi::semantics::DirectObjectBudgetKind kind) const
    {
        auto reservation = directObjectBudget_
            ? directObjectBudget_->reserve(kind)
            : rhi::semantics::DirectObjectBudgetReservation{true, {}};
        if (!reservation.success)
            rhi::RhiErrorHandler::get().report(
                rhi::RhiSeverity::Error, "RHI-DIRECT-PIPELINE-BUDGET");
        return reservation;
    }

    VulkanRhiDeviceDesc desc_;
    std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget_ =
        desc_.directObjectBudget.configured()
        ? core::makeShared<rhi::semantics::DirectObjectBudgetTracker>(desc_.directObjectBudget)
        : nullptr;
    VulkanRhiQueue queue_;
    VulkanRhiQueue computeQueue_{rhi::GpuQueueRole::Compute};
    bool hasAsyncCompute_ = false;
    VulkanRhiAllocator allocator_;
    VulkanRhiStagingUploadPool stagingUploadPool_;
    VulkanRhiNativeResourceAllocator nativeResources_;
    VulkanRhiHeapPlacement heapPlacement_{desc_.runtime,
        allocator_,
        desc_.enableTransientAliasing&& rhi::semantics::hasExecutableNativeFeature(
            desc_.capabilityContract, rhi::semantics::NativeBackendFeature::ExplicitMemoryControl),
        desc_.memoryBudgetBytes};
    const char* lastHeapDiagnostic_ = nullptr;

    static void reportHeapDiagnostic(const char* diagnostic)
    {
        if (diagnostic)
            rhi::RhiErrorHandler::get().report(rhi::RhiSeverity::Error, diagnostic);
    }

    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry_ =
        arp::core::makeShared<VulkanRhiResourceRegistry>();
    VulkanTextureReadbackExtensionImpl textureReadbackExtension_;
    VulkanBindlessCrossExtension bindless_;
    arp::core::UniquePtr<rhi::PipelineCache> pipelineCache_;
    arp::core::UniquePtr<rhi::DescriptorCache> descriptorCache_;
};

VulkanRhiDeviceReport describeVulkanRhiDevice(const VulkanRhiDeviceDesc& desc)
{
    VulkanRhiDeviceReport report;
    report.backend = rhi::Backend::Vulkan;
    report.vmaAllocatorIntegrated = desc.enableVmaAllocator;
    report.vmaHiddenFromPublicApi = true;
    report.memoryBudgetTelemetrySanitized = desc.memoryBudgetBytes > 0;
    report.boundedFrameSynchronization = !desc.allowBlockingFrameWaits;
    report.nullDelegateUsed = false;
    report.backendOwnedResourceObjects = true;
    report.backendOwnedDescriptorObjects = true;
    report.backendOwnedPipelineObjects = true;
    report.backendOwnedCommandObjects = true;
    report.backendOwnedQueueSubmit = true;
    report.backendOwnedSwapchain = true;
    report.nativeResourceAllocationAvailable = desc.runtime != nullptr && desc.runtime->valid();
    report.nativeHandleExposedToUpperLayer = desc.allowNativeHandleExposureToUpperLayers;
    report.perDrawPipelineCreationAllowed = desc.allowPerDrawPipelineCreation;
    report.perFrameShaderCompilationAllowed = desc.allowPerFrameShaderCompilation;

    if (desc.directObjectBudget.configured() && !desc.directObjectBudget.valid()) {
        report.diagnostics = "RHI-DIRECT-OBJECT-BUDGET-INVALID";
        return report;
    }
    if (desc.uploadStagingBudget) {
        const auto validation =
            rhi::semantics::validateUploadStagingBudget(*desc.uploadStagingBudget);
        if (!validation.success) {
            report.diagnostics = validation.diagnosticId;
            return report;
        }
    }

    if (desc.runtime) {
        const auto runtimeInspection = inspectVulkanRhiRuntime(*desc.runtime);
        if (!runtimeInspection.success) {
            report.diagnostics = runtimeInspection.diagnostics;
            return report;
        }
    }
    if (!desc.enableVmaAllocator) {
        report.diagnostics = "Vulkan RHI device requires backend-owned VMA allocator integration";
        return report;
    }
    if (desc.allowBlockingFrameWaits) {
        report.diagnostics = "Vulkan RHI device must use bounded frame synchronization";
        return report;
    }

    const auto invariants = rhi::validateSharedDeviceInvariants(rhi::Backend::Vulkan,
        "Vulkan",
        desc.capabilityContract.backend,
        desc.memoryBudgetBytes,
        desc.allowNativeHandleExposureToUpperLayers,
        desc.allowPerDrawPipelineCreation,
        desc.allowPerFrameShaderCompilation);
    if (!invariants.success) {
        report.diagnostics = invariants.diagnostics;
        return report;
    }
    report.nativeHandleExposedToUpperLayer = invariants.nativeHandleExposedToUpperLayer;
    report.perDrawPipelineCreationAllowed = invariants.perDrawPipelineCreationAllowed;
    report.perFrameShaderCompilationAllowed = invariants.perFrameShaderCompilationAllowed;

    if (!desc.uploadStagingBudget
        && (desc.uploadStagingPageSizeBytes == 0 || desc.uploadStagingMaxPageCount == 0
            || desc.uploadStagingPageSizeBytes
                > std::numeric_limits<std::uint64_t>::max() / desc.uploadStagingMaxPageCount)) {
        report.diagnostics = "Vulkan RHI device requires a bounded upload staging pool budget";
        return report;
    }
    if (desc.requireNativeResourceAllocation && !report.nativeResourceAllocationAvailable) {
        report.diagnostics = "Vulkan RHI device requires a connected RHI-owned Vulkan runtime";
        return report;
    }

    report.success = true;
    return report;
}

arp::core::UniquePtr<rhi::GpuDevice> createVulkanRhiDevice(const VulkanRhiDeviceDesc& desc)
{
    if (!describeVulkanRhiDevice(desc).success) {
        return nullptr;
    }
    auto deviceDesc = desc;
    if (deviceDesc.uploadStagingBudget) {
        deviceDesc.uploadStagingPageSizeBytes = deviceDesc.uploadStagingBudget->pageSizeBytes;
        deviceDesc.uploadStagingMaxPageCount = deviceDesc.uploadStagingBudget->maxPageCount;
    }
    deviceDesc.pushConstantLoweringPlan = rhi::semantics::buildPushConstantLoweringPlan(
        rhi::Backend::Vulkan, desc.capabilityContract);
    return arp::core::makeUnique<VulkanRhiDevice>(std::move(deviceDesc));
}

std::shared_ptr<const VulkanOriginalArtifactGraphicsCreation> createVulkanOriginalGraphicsCreation(
    rhi::GpuDevice& device,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>& input,
    const std::shared_ptr<rhi::PipelineLayout>& layout,
    const rhi::RenderPipelineDesc& authored,
    VulkanOriginalArtifactGraphicsCreation::Budget budget,
    const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
    const char*& diagnostic)
{
    auto* native = dynamic_cast<VulkanRhiDevice*>(&device);
    if (!native) {
        diagnostic = "RHI-SYNC-GRAPHICS-CREATION-DEVICE";
        return {};
    }
    return native->createOriginalGraphicsCreation(
        input, layout, authored, budget, ownerQuota, nativeQuota, diagnostic);
}

core::UniquePtr<VulkanRhiRetainedCommand> prepareVulkanRhiRetainedCommand(rhi::GpuDevice& device,
    core::UniquePtr<rhi::common::CommandRetentionStorage> storage,
    core::pmr::String& diagnostic)
{
    auto* native = dynamic_cast<VulkanRhiDevice*>(&device);
    if (!native) {
        diagnostic = "RHI-VULKAN-RETENTION-DEVICE-REQUIRED";
        return {};
    }
    return native->prepareRetainedCommand(std::move(storage), diagnostic);
}

VulkanRhiNativeResourceStats inspectVulkanRhiNativeResourceStats(const rhi::GpuDevice& device)
{
    const auto* vulkanDevice = dynamic_cast<const VulkanRhiDevice*>(&device);
    if (!vulkanDevice) {
        return {};
    }
    return vulkanDevice->nativeResourceStats();
}

} // namespace arp::rhi_vulkan
