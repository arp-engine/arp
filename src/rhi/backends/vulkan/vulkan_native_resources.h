#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vulkan/vulkan.h>

namespace arp::rhi {
class BindlessShaderPackage;
struct BindlessGraphicsPushRequest;
class OriginalCompletedDepthTextureSource;
} // namespace arp::rhi

namespace arp::rhi_vulkan {

class VulkanRhiNativeCommandPayload;

class VulkanRhiSwapchainFrameSyncState final {
public:
    explicit VulkanRhiSwapchainFrameSyncState(const std::shared_ptr<VulkanRhiRuntime>& runtime)
        : runtime_(runtime)
    {}

    VulkanRhiSwapchainFrameSyncState(const VulkanRhiSwapchainFrameSyncState&) = delete;
    VulkanRhiSwapchainFrameSyncState& operator=(const VulkanRhiSwapchainFrameSyncState&) = delete;

    bool belongsTo(const std::shared_ptr<VulkanRhiRuntime>& runtime) const noexcept
    {
        return runtime && runtime_.lock() == runtime;
    }

    bool renderSubmitted() const noexcept { return renderSubmitted_; }

    bool graphicsCompleted() const noexcept { return graphicsCompleted_; }

    bool nativePresentAccepted() const noexcept { return nativePresentAccepted_; }

    std::shared_ptr<VulkanRhiNativeCommandPayload> submittedCommand() const noexcept
    {
        return submittedCommand_.lock();
    }

private:
    friend class VulkanRhiNativeCommandPayload;
    friend class VulkanRhiSwapchain;
    bool nativePresentAccepted_ = false;
    std::weak_ptr<VulkanRhiRuntime> runtime_;
    bool renderSubmitted_ = false;
    bool graphicsCompleted_ = false;
    // The command payload owns this state while it records/submits.  The
    // swapchain only observes it so it can wait before reusing a frame slot;
    // making this a shared_ptr would form a command -> state -> command cycle
    // and retain all command-owned Vulkan objects until vkDestroyDevice.
    std::weak_ptr<VulkanRhiNativeCommandPayload> submittedCommand_;
};

class VulkanRhiNativeResourcePayload {
    friend class VulkanRhiDevice;
    const rhi::GpuBuffer* originalHostInitializedObject_ = nullptr;
    std::uint64_t originalHostInitializedBytes_ = 0u;
    std::optional<rhi::GpuBuffer::CommittedStateObservation> originalHostInitialization_;
    mutable std::atomic<bool> originalFrozenHostInitialization_{false};

public:
    // Full mapped write + flush is the only issuer. Before the first sealed
    // validation, the exact committed revision excludes intervening GPU work.
    // Monotonic host/shader/transfer seals preserve immutable contents across
    // read-only journal cancellation. Writable profiles use the separate
    // defined-byte proof below and never infer immutability or output values.
    bool originalHostInitializationMatches(const rhi::GpuBuffer& object) const noexcept
    {
        return object.gpuWritesSealed() && object.gpuTransferWritesSealed()
            && originalHostDefinedBytesMatch(object);
    }

    // A full flushed host initialization remains defined under bounded GPU
    // writes. It proves no shader output values, coverage or immutability.
    bool originalHostDefinedBytesMatch(const rhi::GpuBuffer& object) const noexcept
    {
        if (originalHostInitializedObject_ != &object
            || originalHostInitializedBytes_ != object.desc().sizeBytes
            || !originalHostInitialization_ || !object.hostWritesSealed()
            || object.originalCommittedStateDiagnostic()
            || object.originalResourceRegistration().generation()
                != originalHostInitialization_->resourceGeneration())
            return false;
        if (originalFrozenHostInitialization_.load(std::memory_order_acquire))
            return true;
        if (object.validateOriginalCommittedState(*originalHostInitialization_))
            return false;
        originalFrozenHostInitialization_.store(true, std::memory_order_release);
        return true;
    }

    // The base outlives derived native allocation owners during destruction.
    // Retained commands/views keep this budget charge alongside the payload.
    std::shared_ptr<void> allocationReleaseToken;
    // Shared with arena generations or owned by one pipeline bundle. The
    // base dies after native destruction, including command-retained payloads.
    std::shared_ptr<void> directObjectBudgetReleaseToken;
    virtual ~VulkanRhiNativeResourcePayload() = default;

    virtual VkDevice backendVulkanDeviceHandle() const { return VK_NULL_HANDLE; }

    virtual VkBuffer backendVulkanBufferHandle() const { return VK_NULL_HANDLE; }

    virtual VkImage backendVulkanImageHandle() const { return VK_NULL_HANDLE; }

    virtual bool backendVulkanOriginalContentExclusive() const noexcept { return false; }

    virtual VkFormat backendVulkanImageFormat() const { return VK_FORMAT_UNDEFINED; }

    virtual rhi::Extent3D backendVulkanImageExtent() const { return {}; }

    virtual VkPipeline backendVulkanRenderPipelineHandle() const { return VK_NULL_HANDLE; }

    virtual VkPipeline backendVulkanComputePipelineHandle() const { return VK_NULL_HANDLE; }

    virtual VkPipelineLayout backendVulkanComputePipelineLayoutHandle() const
    {
        return VK_NULL_HANDLE;
    }

    virtual VkPipelineLayout backendVulkanRenderPipelineLayoutHandle() const
    {
        return VK_NULL_HANDLE;
    }

    /// Native push-constant recording must use the complete stage mask of the
    /// bound Vulkan pipeline layout range.  The L2 encoder may request only
    /// the consuming graphics stage (for example Fragment), while Vulkan's
    /// vkCmdPushConstants contract requires every stage in the overlapping
    /// layout range to be included.
    virtual VkShaderStageFlags backendVulkanPushConstantStageFlags() const { return 0; }

    virtual VkDescriptorSet backendVulkanDescriptorSetHandle() const { return VK_NULL_HANDLE; }

    virtual VkDescriptorSetLayout backendVulkanDescriptorSetLayoutHandle() const
    {
        return VK_NULL_HANDLE;
    }

    virtual VkSampler backendVulkanSamplerHandle() const { return VK_NULL_HANDLE; }

    // Backend-private descriptor/view correspondence, independent of public
    // successful-write metadata. Default absence cannot validate a closure.
    virtual std::shared_ptr<VulkanRhiNativeResourcePayload>
    backendVulkanTextureViewParentPayload() const
    {
        return {};
    }

    virtual std::shared_ptr<VulkanRhiNativeResourcePayload> backendVulkanDescriptorElementResource(
        std::uint32_t) const
    {
        return {};
    }

    virtual VkImageView backendVulkanDescriptorElementImageView(std::uint32_t) const
    {
        return VK_NULL_HANDLE;
    }

    virtual std::pair<std::uint64_t, std::uint64_t> backendVulkanDescriptorElementBufferRange(
        std::uint32_t) const noexcept
    {
        return {UINT64_MAX, 0};
    }

    virtual std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>
    backendVulkanDescriptorElementDepthContent(std::uint32_t) const
    {
        return {};
    }

    virtual bool backendVulkanTextureViewDescAvailable() const { return false; }

    virtual rhi::TextureViewDesc backendVulkanTextureViewDesc() const { return {}; }

    // Immutable borrow under the retained payload owner. Hot correspondence
    // validation must not copy descriptor labels or allocate for long names.
    virtual const rhi::TextureViewDesc* backendVulkanOriginalTextureViewDesc() const noexcept
    {
        return nullptr;
    }

    virtual bool backendVulkanSwapchainFrameSyncAvailable() const { return false; }

    virtual const char* backendVulkanSwapchainImageDiagnostic() const noexcept
    {
        return "RHI-CONFORMANCE-CLEAR-ORIGINAL-IMAGE";
    }

    virtual rhi::ResourceState backendVulkanSwapchainAcquiredState() const
    {
        return rhi::ResourceState::Undefined;
    }

    virtual VkSemaphore backendVulkanAcquireSemaphore() const { return VK_NULL_HANDLE; }

    virtual VkSemaphore backendVulkanRenderFinishedSemaphore() const { return VK_NULL_HANDLE; }

    virtual std::uint64_t backendVulkanSwapchainFrameSerial() const { return 0; }

    virtual std::shared_ptr<VulkanRhiSwapchainFrameSyncState>
    backendVulkanSwapchainFrameSyncState() const
    {
        return {};
    }

    virtual void* backendMappedData() const { return nullptr; }

    virtual bool backendFlushMappedWrite(
        std::uint64_t offsetBytes, std::uint64_t sizeBytes, std::pmr::string& diagnostics) const
    {
        (void)offsetBytes;
        (void)sizeBytes;
        diagnostics = "Vulkan RHI native payload has no mapped write flush contract";
        return false;
    }

    virtual bool backendInvalidateMappedRead(
        std::uint64_t offsetBytes, std::uint64_t sizeBytes, std::pmr::string& diagnostics)
    {
        (void)offsetBytes;
        (void)sizeBytes;
        diagnostics = "Vulkan RHI native payload has no mapped read invalidation contract";
        return false;
    }
};

struct VulkanRhiNativeResourceResult {
    bool success = false;
    std::pmr::string diagnostics;
    VulkanRhiAllocation allocation;
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
    std::shared_ptr<VulkanRhiMappedBufferReadback> readback;
};

class VulkanRhiNativeResourceAllocator {
public:
    explicit VulkanRhiNativeResourceAllocator(
        std::shared_ptr<VulkanRhiRuntime> runtime = {}, VulkanRhiAllocator* allocator = nullptr);

    bool connected() const;
    VulkanRhiNativeResourceStats stats() const;
    std::shared_ptr<VulkanRhiRuntime> runtime() const;
    VulkanRhiAllocator* allocator() const;
    std::shared_ptr<VulkanRhiNativeResourceStats> sharedStats() const;

    VulkanRhiNativeResourceResult createBuffer(const rhi::BufferDesc& desc);
    std::uint64_t queryBufferAllocationBytes(
        const rhi::BufferDesc& desc, const char*& diagnostic) const;
    VulkanRhiNativeResourceResult createTexture(
        const rhi::TextureDesc& desc, std::uint64_t maxNativeAllocationBytes = 0u);
    std::uint64_t queryTextureAllocationBytes(
        const rhi::TextureDesc& desc, const char*& diagnostic) const;
    VulkanRhiNativeResourceResult createDescriptorSet(const rhi::DescriptorLayoutDesc& layout);
    VkResult createOriginalDescriptorImageView(
        const VkImageViewCreateInfo&, VkImageView&) const noexcept;
    VulkanRhiNativeResourceResult createRenderPipeline(const rhi::RenderPipelineDesc& desc);
    VulkanRhiNativeResourceResult createComputePipeline(const rhi::ComputePipelineDesc& desc);
    VulkanRhiNativeResourceResult createComputePipelineFromBytes(
        const rhi::ComputePipelineDesc&, const std::uint32_t*, std::size_t);
    // Backend-private preparation, not a BindlessCross execution ticket. The
    // future device-owned factory must additionally own snapshots and budgets.
    VulkanRhiNativeResourceResult createBindlessComputePayload(
        const rhi::BindlessShaderPackage&, std::string_view entry);
    VulkanRhiNativeResourceResult createBindlessRenderPayload(
        std::shared_ptr<const rhi::BindlessShaderPackage>,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        const rhi::BindlessGraphicsPushRequest* push = nullptr);
    VulkanRhiNativeResourceResult createSampler(const rhi::SamplerDesc& desc);

private:
    friend class VulkanConformanceClearExecution;
    friend class VulkanOriginalArtifactGraphicsCreation;
    VulkanRhiNativeResourceResult createRenderPipelineFromBytes(const rhi::RenderPipelineDesc&,
        const std::uint32_t* vertexWords,
        std::size_t vertexCount,
        const std::uint32_t* fragmentWords,
        std::size_t fragmentCount);
    std::shared_ptr<VulkanRhiRuntime> runtime_;
    VulkanRhiAllocator* allocator_ = nullptr;
    std::shared_ptr<VulkanRhiNativeResourceStats> stats_;
};

} // namespace arp::rhi_vulkan
