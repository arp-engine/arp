#pragma once
#include "core/ownership/mutation_journal.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_resource_registry.h"
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/original_texture_upload_device.h"
#include "rhi/frontend/transfer_upload_recording_validation.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <memory>
#include <string_view>

namespace arp::rhi {
class OriginalCommandStreamProgram;
struct OriginalRenderPassPreparationRequest;
} // namespace arp::rhi

namespace arp::rhi_vulkan {

class VulkanRhiNativeResourcePayload;
class VulkanRhiStagingUploadPool;
struct VulkanRhiTimelineSubmitInfo;
struct VulkanRhiRecordingStorageLimits;

/// Private recorder bridge for the optional sanitized texture readback
/// extension. Native Vulkan handles remain below the public RHI boundary.
class VulkanTextureReadbackRecorder {
public:
    virtual ~VulkanTextureReadbackRecorder() = default;
    virtual bool scheduleTextureReadback(
        const rhi::GpuTexture& texture, std::string_view label, std::pmr::string& diagnostics) = 0;
};

std::uint64_t estimateVulkanRhiTextureBytes(const rhi::TextureDesc& desc);

arp::core::UniquePtr<rhi::GpuBuffer> createVulkanRhiBuffer(rhi::GpuResourceId id,
    const rhi::BufferDesc& desc,
    const VulkanRhiAllocation& allocation,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {},
    std::shared_ptr<VulkanRhiResourceRegistry> registry = {});

arp::core::UniquePtr<rhi::GpuTexture> createVulkanRhiTexture(rhi::GpuResourceId id,
    const rhi::TextureDesc& desc,
    const VulkanRhiAllocation& allocation,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {},
    std::shared_ptr<VulkanRhiResourceRegistry> registry = {});

arp::core::UniquePtr<rhi::GpuBufferView> createVulkanRhiBufferView(
    rhi::GpuResourceId id, rhi::GpuResourceId bufferId, const rhi::BufferViewDesc& desc);

arp::core::UniquePtr<rhi::GpuTextureView> createVulkanRhiTextureView(rhi::GpuResourceId id,
    rhi::GpuResourceId textureId,
    const rhi::TextureViewDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {},
    std::shared_ptr<VulkanRhiResourceRegistry> registry = {});

arp::core::UniquePtr<rhi::GpuSampler> createVulkanRhiSampler(rhi::GpuResourceId id,
    const rhi::SamplerDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {},
    std::shared_ptr<VulkanRhiResourceRegistry> registry = {});

rhi::ResourceBarrierResult transitionVulkanRhiBuffer(rhi::GpuBuffer& buffer,
    const rhi::ResourceBarrierDesc& desc,
    core::MutationJournal* journal = nullptr);

rhi::ResourceBarrierResult transitionVulkanRhiTexture(rhi::GpuTexture& texture,
    const rhi::ResourceBarrierDesc& desc,
    core::MutationJournal* journal = nullptr);
rhi::ResourceBarrierResult transitionVulkanRhiTexture(rhi::GpuTexture& texture,
    const rhi::ResourceBarrierDesc& desc,
    core::MutationJournal* journal,
    const VulkanRhiNativeCommandPayload* originalUploadOwner);

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::GpuBuffer& buffer);

bool vulkanRhiBufferRegistryMatches(
    const rhi::GpuBuffer&, const std::shared_ptr<VulkanRhiResourceRegistry>&) noexcept;

bool vulkanRhiTextureRegistryMatches(
    const rhi::GpuTexture&, const std::shared_ptr<VulkanRhiResourceRegistry>&) noexcept;
bool vulkanRhiTextureViewRegistryMatches(
    const rhi::GpuTextureView&, const std::shared_ptr<VulkanRhiResourceRegistry>&) noexcept;

bool vulkanRhiBufferStateCommitted(const rhi::GpuBuffer&) noexcept;
bool vulkanRhiBufferStateAvailable(const rhi::GpuBuffer&, const core::MutationJournal*) noexcept;
bool vulkanRhiBufferCanTransition(const rhi::GpuBuffer&, const core::MutationJournal&) noexcept;
bool vulkanRhiTextureStateCommitted(const rhi::GpuTexture&) noexcept;
bool vulkanRhiTextureStateAvailable(const rhi::GpuTexture&,
    const rhi::TextureSubresourceRange&,
    const core::MutationJournal*) noexcept;
bool vulkanRhiTextureCanTransition(const rhi::GpuTexture&, const core::MutationJournal&) noexcept;

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::GpuTexture& texture);

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::GpuTextureView& textureView);

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::GpuSampler& sampler);

arp::core::UniquePtr<rhi::PipelineLayout> createVulkanRhiPipelineLayout(
    const rhi::PipelineLayoutDesc& desc, rhi::semantics::DirectObjectBudgetLease budgetLease = {});

rhi::PipelineValidationReport validateVulkanRhiRenderPipeline(const rhi::RenderPipelineDesc& desc);

rhi::PipelineValidationReport validateVulkanRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc);

rhi::PipelineValidationReport validateVulkanRhiAuthorizedBindlessComputePipeline(
    const rhi::ComputePipelineDesc& desc);

rhi::PipelineValidationReport validateVulkanRhiAuthorizedBindlessRenderPipeline(
    const rhi::RenderPipelineDesc& desc);

arp::core::UniquePtr<rhi::RenderPipeline> createVulkanRhiRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {},
    std::shared_ptr<const rhi::BindlessAuthorization> authorization = {},
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout = {});

arp::core::UniquePtr<rhi::RenderPipeline> createVulkanRhiAuthorizedBindlessRenderPipeline(
    const rhi::RenderPipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout);

const rhi::OriginalBindlessGraphicsPassContract::PipelineState*
vulkanRhiBindlessGraphicsPassPipelineState(const rhi::RenderPipeline&) noexcept;

// Factory-captured ordinary pipeline facts, independent of canonical bindless
// authorization. No borrowed definition/package/layout is retained here.
struct VulkanRhiRetainedGraphicsPipelineState {
    bool fullscreenColor = false;
    rhi::TextureFormat color = rhi::TextureFormat::Unknown;
    rhi::ShaderStage stages = rhi::ShaderStage::None;
};

const VulkanRhiRetainedGraphicsPipelineState* vulkanRhiRetainedGraphicsPipelineState(
    const rhi::RenderPipeline&) noexcept;

arp::core::UniquePtr<rhi::ComputePipeline> createVulkanRhiComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload = {},
    std::shared_ptr<const rhi::BindlessAuthorization> authorization = {},
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout = {});

arp::core::UniquePtr<rhi::ComputePipeline> createVulkanRhiAuthorizedBindlessComputePipeline(
    const rhi::ComputePipelineDesc& desc,
    std::shared_ptr<VulkanRhiNativeResourcePayload> nativePayload,
    std::shared_ptr<const rhi::BindlessAuthorization> authorization,
    arp::core::UniquePtr<rhi::PipelineLayout> ownedLayout);

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::RenderPipeline& pipeline);

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::ComputePipeline& pipeline);

arp::core::UniquePtr<rhi::DescriptorArena> createVulkanRhiDescriptorArena(
    const rhi::DescriptorArenaDesc& desc,
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    bool requireNativeDescriptorAllocation,
    std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget = {});

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanRhiNativePayload(
    const rhi::DescriptorSet& descriptorSet);

bool vulkanRhiDescriptorSetContextMatches(const rhi::DescriptorSet&,
    const std::shared_ptr<VulkanRhiRuntime>&,
    const std::shared_ptr<VulkanRhiResourceRegistry>&) noexcept;

// Cold storage preparation only; does not authorize native recording.
std::shared_ptr<VulkanRhiNativeResourcePayload> prepareVulkanOriginalImageCellDescriptors(
    const rhi::BindlessSnapshot&);

const char* validateVulkanOriginalDescriptorLossSource(const rhi::DescriptorArena&,
    const std::shared_ptr<VulkanRhiRuntime>&,
    const std::shared_ptr<VulkanRhiResourceRegistry>&) noexcept;
const char* validateVulkanOriginalDescriptorLossSource(const rhi::DescriptorSet&,
    const std::shared_ptr<VulkanRhiRuntime>&,
    const std::shared_ptr<VulkanRhiResourceRegistry>&) noexcept;

arp::core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoder(
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    VulkanRhiStagingUploadPool* stagingUploadPool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    bool bindlessExecutable,
    bool requireNativeCommandEncoding,
    const rhi::CommandEncoderDesc& retention = {});

// Private storage prerequisite only; does not execute/authorize the original
// stream. Uses its original Recording quota and independent pass occurrences.
core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdPassStorage(
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    VulkanRhiStagingUploadPool* stagingUploadPool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> original,
    core::pmr::String& diagnostic);

// Explicit native object preparation opt-in. Graphics recording stays closed.
core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdRenderPasses(
    VulkanRhiNativeResourceAllocator,
    std::shared_ptr<VulkanRhiResourceRegistry>,
    VulkanRhiStagingUploadPool*,
    const rhi::semantics::NativeBackendCapabilityContract&,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram>,
    const rhi::GpuDevice&,
    const rhi::OriginalRenderPassPreparationRequest&,
    core::pmr::String&);

// Same ordinary encoder, with the original device issuer, Inputs9 and its
// existing journal joined to the independently bounded query category.
core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdPassStorage(
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry,
    VulkanRhiStagingUploadPool* stagingUploadPool,
    const rhi::semantics::NativeBackendCapabilityContract& capabilityContract,
    const rhi::GpuDevice& device,
    const VulkanRhiRecordingStorageLimits& originalQueries,
    core::pmr::String& diagnostic);

core::UniquePtr<rhi::CommandEncoder> createVulkanRhiCommandEncoderWithColdTextureUploads(
    VulkanRhiNativeResourceAllocator,
    std::shared_ptr<VulkanRhiResourceRegistry>,
    VulkanRhiStagingUploadPool*,
    const rhi::semantics::NativeBackendCapabilityContract&,
    std::shared_ptr<const rhi::OriginalCommandStreamProgram>,
    rhi::GpuDevice&,
    const rhi::OriginalOrdinaryTextureUploadContract::Budget&,
    const rhi::OriginalOrdinaryTextureUploadContract::Binding*,
    std::size_t,
    core::pmr::String&,
    const rhi::OriginalTextureUploadPreparationRequest* = nullptr,
    const std::shared_ptr<core::CpuMemoryQuota>& = {},
    core::pmr::Vector<std::shared_ptr<const rhi::OriginalTextureUploadPublication>>* = nullptr);

arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createVulkanRhiSecondaryCommandEncoder(
    const rhi::SecondaryCommandEncoderDesc& desc);

arp::core::UniquePtr<rhi::RenderBundleEncoder> createVulkanRhiRenderBundleEncoder(
    const rhi::RenderBundleDesc& desc);

rhi::QueueSubmitResult submitVulkanRhiCommandBuffer(const rhi::CommandBuffer& commandBuffer);

rhi::QueueSubmitResult submitVulkanRhiCommandBuffer(
    const rhi::CommandBuffer& commandBuffer, const VulkanRhiTimelineSubmitInfo& timeline);

std::shared_ptr<VulkanRhiNativeCommandPayload> vulkanRhiCommandPayload(
    const rhi::CommandBuffer& commandBuffer);
std::shared_ptr<VulkanRhiNativeCommandPayload> vulkanRhiCommandPayload(rhi::CommandEncoder&);

arp::core::UniquePtr<rhi::Swapchain> createVulkanRhiSwapchain(const rhi::SwapchainDesc& desc,
    VulkanRhiNativeResourceAllocator nativeResources,
    std::shared_ptr<VulkanRhiResourceRegistry> resourceRegistry);

} // namespace arp::rhi_vulkan
