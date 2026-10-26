#pragma once

#include "core/container/array_view.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_resource_registry.h"
#include "rhi/frontend/command_recording_budget.h"
#include "rhi/frontend/command_retention_storage.h"

namespace arp::rhi_vulkan {

struct VulkanRhiRetainedDescriptorSet {
    std::uint32_t index = 0u;
    const rhi::DescriptorSet* descriptors = nullptr;
};

struct VulkanRhiRetainedSampledTexture {
    const rhi::GpuTextureView* view = nullptr;
    const rhi::GpuTexture* texture = nullptr;
};

using VulkanRhiRetainedSampledDepth = VulkanRhiRetainedSampledTexture;

struct VulkanRhiRetainedBindings {
    core::ArrayView<VulkanRhiRetainedDescriptorSet> sets;
    core::ArrayView<const rhi::GpuBuffer*> buffers;
    core::ArrayView<VulkanRhiRetainedSampledTexture> sampledTextures;
};

// Explicit full-target raster intent; this private slice admits one non-indexed
// triangle, no vertex streams, and read-only Fragment bindings only.
struct VulkanRhiRetainedGraphicsDraw {
    const rhi::RenderPipeline* pipeline = nullptr;
    VulkanRhiRetainedBindings bindings;
    rhi::ViewportState viewport;
    rhi::ScissorState scissor;
    std::uint32_t vertexCount = 0u, instanceCount = 0u;
    std::uint32_t firstVertex = 0u, firstInstance = 0u;
};

// Borrowed metadata is consumed synchronously on the serialized recording thread.
struct VulkanRhiRetainedComputeDispatch {
    const rhi::ComputePipeline* pipeline = nullptr;
    core::ArrayView<VulkanRhiRetainedDescriptorSet> sets;
    core::ArrayView<const rhi::GpuBuffer*> buffers;
    std::uint32_t groupsX = 0u, groupsY = 0u, groupsZ = 0u;
    const void* pushConstants = nullptr;
    std::uint32_t pushConstantBytes = 0u;
    core::ArrayView<VulkanRhiRetainedSampledDepth> sampledDepth;
};

// Explicit backend-private CommandRetentionBudget/1 prototype. No ordinary
// recorder/native handle escapes this owner; public L2 admission stays closed.
class VulkanRhiRetainedCommand final {
    class ConstructionKey {
        friend class VulkanRhiRetainedCommand;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    VulkanRhiRetainedCommand(const VulkanRhiRetainedCommand&) = delete;
    VulkanRhiRetainedCommand& operator=(const VulkanRhiRetainedCommand&) = delete;
    VulkanRhiRetainedCommand(VulkanRhiRetainedCommand&&) = delete;
    VulkanRhiRetainedCommand& operator=(VulkanRhiRetainedCommand&&) = delete;

    VulkanRhiRetainedCommand(ConstructionKey,
        std::shared_ptr<VulkanRhiNativeCommandPayload> command,
        std::shared_ptr<VulkanRhiResourceRegistry> registry);

    static core::UniquePtr<VulkanRhiRetainedCommand> prepare(
        const VulkanRhiNativeResourceAllocator&,
        std::shared_ptr<VulkanRhiResourceRegistry>,
        core::UniquePtr<rhi::common::CommandRetentionStorage>,
        core::pmr::String& diagnostic);

    // Cold, one-shot opt-in before pass preparation or any resource admission.
    bool enableRecordingBudget(const rhi::CommandRecordingBudget&, core::pmr::String& diagnostic);
    const rhi::CommandRecordingUsage* recordingUsage() const noexcept;

    bool copy(const rhi::GpuBuffer& source,
        const rhi::GpuBuffer& destination,
        const rhi::BufferCopyDesc&,
        core::pmr::String& diagnostic);
    bool dependency(const rhi::GpuBuffer&,
        const rhi::ResourceMemoryBarrierDesc&,
        core::pmr::String& diagnostic);
    bool dispatch(const VulkanRhiRetainedComputeDispatch&, core::pmr::String& diagnostic);
    bool transition(
        rhi::GpuBuffer&, const rhi::ResourceBarrierDesc&, core::pmr::String& diagnostic);
    bool transition(
        rhi::GpuTexture&, const rhi::ResourceBarrierDesc&, core::pmr::String& diagnostic);
    bool copy(const rhi::GpuTexture& source,
        const rhi::GpuBuffer& destination,
        const rhi::TextureToBufferCopyDesc&,
        core::pmr::String& diagnostic);
    // Cold preparation only; consumes one existing fixed native pass slot.
    bool prepareClearStorePass(const rhi::RenderPassDesc&,
        const rhi::GpuTexture&,
        const rhi::TextureSubresourceRange&,
        core::pmr::String& diagnostic);
    bool clearPreparedPass(const rhi::GpuTexture&, core::pmr::String& diagnostic);
    bool drawPreparedPass(const rhi::GpuTexture&,
        const VulkanRhiRetainedGraphicsDraw&,
        core::pmr::String& diagnostic);
    bool finish(core::pmr::String& diagnostic);
    bool submit(const VulkanRhiTimelineSubmitInfo&, core::pmr::String& diagnostic);
    bool completed();
    bool waitUntil(std::chrono::steady_clock::time_point, core::pmr::String& diagnostic);
    const rhi::common::CommandRetentionStorage* retentionStorage() const noexcept;

private:
    bool preflightRecordingCharge(const rhi::CommandRecordingUsage&, core::pmr::String&) const;
    bool commitRecordingCharge(const rhi::CommandRecordingUsage&, core::pmr::String&);
    bool chargeBindings(
        rhi::CommandRecordingUsage&, const VulkanRhiRetainedBindings&, core::pmr::String&) const;
    bool validateRecording(core::pmr::String&) const;
    bool validateSampledTexture(
        const VulkanRhiRetainedSampledTexture&, bool allowColor, core::pmr::String&) const;
    bool validateBindings(const VulkanRhiRetainedBindings&,
        const rhi::PipelineLayoutDesc&,
        const rhi::ShaderReflectionDesc&,
        rhi::ShaderStage,
        core::pmr::String&) const;
    bool retainBindings(const VulkanRhiRetainedBindings&,
        const std::shared_ptr<VulkanRhiNativeResourcePayload>& pipeline,
        core::pmr::String&);
    bool validatePreparedPass(const rhi::GpuTexture&, core::pmr::String&) const;
    bool beginPreparedPass(const rhi::GpuTexture&, core::pmr::String&);
    void endPreparedPass() noexcept;
    std::shared_ptr<VulkanRhiNativeResourcePayload> resolve(
        const rhi::GpuBuffer&, core::pmr::String&) const;
    std::shared_ptr<VulkanRhiNativeResourcePayload> resolve(
        const rhi::GpuTexture&, core::pmr::String&) const;
    std::shared_ptr<VulkanRhiNativeCommandPayload> command_;
    std::shared_ptr<VulkanRhiResourceRegistry> registry_;
    bool failed_ = false;
    bool sampledDepthLoadAvailable_ = false;
    VkFormatFeatureFlags depthFormatFeatures_ = 0u, colorFormatFeatures_ = 0u;
    rhi::GpuResourceId preparedPassTarget_;
    rhi::TextureSubresourceRange preparedPassRange_;
    rhi::Extent3D preparedPassExtent_;
    rhi::Color preparedPassColor_;
    float preparedPassDepth_ = 1.0f;
    bool preparedPassRecorded_ = false;
    std::optional<rhi::CommandRecordingMeter> recordingMeter_;
};

} // namespace arp::rhi_vulkan
