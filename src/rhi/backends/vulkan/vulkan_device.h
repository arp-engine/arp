#pragma once
#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/frontend/memory_upload_staging_budget.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/render_output_observation.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace arp::rhi::common {
class CommandRetentionStorage;
}

namespace arp::rhi_vulkan {

class VulkanRhiRuntime;
class VulkanRhiRetainedCommand;

// Explicit backend-private prototype. Does not publish public L2 capability.
core::UniquePtr<VulkanRhiRetainedCommand> prepareVulkanRhiRetainedCommand(
    rhi::GpuDevice&, core::UniquePtr<rhi::common::CommandRetentionStorage>, core::pmr::String&);

struct VulkanRhiDeviceDesc {
    std::pmr::string label;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract;
    std::uint64_t memoryBudgetBytes = 0;
    std::uint64_t uploadStagingPageSizeBytes = 32u * 1024u * 1024u;
    std::uint32_t uploadStagingMaxPageCount = 8;
    std::shared_ptr<VulkanRhiRuntime> runtime;
    bool enableVmaAllocator = true;
    // Enables the bounded, backend-owned Vulkan transient heap pool.
    bool enableTransientAliasing = true;
    // Historical creation-time runtime check only. False never permits a
    // failed native resource creation to publish a placeholder.
    bool requireNativeResourceAllocation = false;
    bool allowNativeHandleExposureToUpperLayers = false;
    bool allowBlockingFrameWaits = false;
    bool allowPerDrawPipelineCreation = false;
    bool allowPerFrameShaderCompilation = false;
    rhi::semantics::PushConstantLoweringPlan pushConstantLoweringPlan;
    rhi::semantics::DirectObjectBudget directObjectBudget;
    std::optional<rhi::semantics::UploadStagingBudgetV1> uploadStagingBudget;
};

struct VulkanRhiDeviceReport {
    bool success = false;
    std::pmr::string diagnostics;
    rhi::Backend backend = rhi::Backend::Vulkan;
    bool vmaAllocatorIntegrated = false;
    bool vmaHiddenFromPublicApi = true;
    bool memoryBudgetTelemetrySanitized = false;
    bool boundedFrameSynchronization = false;
    bool nullDelegateUsed = false;
    bool backendOwnedResourceObjects = false;
    bool backendOwnedDescriptorObjects = false;
    bool backendOwnedPipelineObjects = false;
    bool backendOwnedCommandObjects = false;
    bool backendOwnedQueueSubmit = false;
    bool backendOwnedSwapchain = false;
    bool nativeResourceAllocationAvailable = false;
    bool nativeHandleExposedToUpperLayer = false;
    bool perDrawPipelineCreationAllowed = false;
    bool perFrameShaderCompilationAllowed = false;
};

struct VulkanRhiNativeResourceStats {
    bool runtimeConnected = false;
    std::uint32_t nativeBufferCreateCount = 0;
    std::uint32_t nativeImageCreateCount = 0;
    std::uint32_t nativeMemoryAllocationCount = 0;
    std::uint32_t nativeDescriptorSetLayoutCreateCount = 0;
    std::uint32_t nativeDescriptorPoolCreateCount = 0;
    std::uint32_t nativeDescriptorSetAllocateCount = 0;
    std::uint32_t nativeDescriptorWriteCount = 0;
    std::uint32_t nativeDescriptorBindRecordCount = 0;
    std::uint32_t nativeShaderModuleCreateCount = 0;
    std::uint32_t nativePipelineLayoutCreateCount = 0;
    std::uint32_t nativeRenderPassCreateCount = 0;
    std::uint32_t nativeRenderPipelineCreateCount = 0;
    std::uint32_t nativeComputePipelineCreateCount = 0;
    std::uint32_t nativeCommandPoolCreateCount = 0;
    std::uint32_t nativeCommandBufferAllocateCount = 0;
    std::uint32_t nativeCommandBufferBeginCount = 0;
    std::uint32_t nativeCommandBufferRecordCount = 0;
    std::uint32_t nativeCommandBufferEndCount = 0;
    std::uint32_t nativeQueueSubmitCount = 0;
    std::uint32_t nativeSwapchainSubmitWaitCount = 0;
    std::uint32_t nativeSwapchainSubmitSignalCount = 0;
    std::uint32_t nativeBufferBarrierRecordCount = 0;
    std::uint32_t nativeImageBarrierRecordCount = 0;
    std::uint32_t nativeBufferCopyRecordCount = 0;
    std::uint32_t nativeUploadStagingPoolPageCount = 0;
    std::uint32_t nativeUploadStagingSliceCount = 0;
    std::uint64_t nativeUploadStagingPeakBytes = 0;
    bool nativeUploadStagingPoolBounded = false;
    bool nativeUploadStagingHandlesHidden = true;
    std::uint32_t nativeBufferToImageCopyRecordCount = 0;
    std::uint32_t nativeImageToBufferCopyRecordCount = 0;
    std::uint32_t nativeImageToImageCopyRecordCount = 0;
    std::uint32_t nativeRenderAttachmentViewCreateCount = 0;
    std::uint32_t nativeFramebufferCreateCount = 0;
    std::uint32_t nativeRenderPassBeginRecordCount = 0;
    std::uint32_t nativeGraphicsPipelineBindRecordCount = 0;
    std::uint32_t nativeIndexedDrawRecordCount = 0;
    std::uint32_t nativeDrawRecordCount = 0;
    std::uint32_t nativeRenderPassEndRecordCount = 0;
    std::uint32_t nativeComputePipelineBindRecordCount = 0;
    std::uint32_t backendDispatchRecordCount = 0;
    std::uint32_t nativeExecutableDispatchRecordCount = 0;
    std::uint32_t nativeSwapchainCreateCount = 0;
    std::uint32_t nativeSwapchainImageCount = 0;
    std::uint32_t nativeSwapchainAcquireCount = 0;
    std::uint32_t nativeSwapchainPresentCount = 0;
    std::uint32_t nativeSwapchainResizeCount = 0;
    std::uint32_t backendSwapchainAcquireCount = 0;
    std::uint32_t backendSwapchainPresentCount = 0;
    bool hiddenWindowRenderOutputHashCaptured = false;
    const char* renderOutputEvidenceDiagnostic =
        arp::rhi::semantics::RenderOutputObservationContract::unavailableDiagnostic;
    std::uint32_t renderOutputHashCaptureCount = 0;
    std::uint64_t deterministicRenderOutputHash = 0;
    bool renderOutputHashIncludesNativePresent = false;
    bool nativeHandlesHiddenFromPublicApi = true;
};

VulkanRhiDeviceReport describeVulkanRhiDevice(const VulkanRhiDeviceDesc& desc);

arp::core::UniquePtr<rhi::GpuDevice> createVulkanRhiDevice(const VulkanRhiDeviceDesc& desc);

VulkanRhiNativeResourceStats inspectVulkanRhiNativeResourceStats(const rhi::GpuDevice& device);

} // namespace arp::rhi_vulkan
