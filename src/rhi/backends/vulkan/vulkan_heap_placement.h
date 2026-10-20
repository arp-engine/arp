#pragma once

#include "rhi/backends/vulkan/vulkan_native_resources.h"

namespace arp::rhi_vulkan {

struct VulkanRhiPlacedTextureResult {
    const char* diagnostic = nullptr;
    VulkanRhiAllocation allocation;
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload;
};

// HeapPlacementAdmission/1. Native handles, dispatch, allocation ownership and
// the bounded reuse cache remain private to the original Vulkan runtime.
class VulkanRhiHeapPlacement final {
public:
    VulkanRhiHeapPlacement(std::shared_ptr<VulkanRhiRuntime> runtime,
        const VulkanRhiAllocator& allocator,
        bool enabled,
        std::uint64_t budget);
    const char* admissionDiagnostic() const noexcept;
    core::UniquePtr<rhi::GpuMemoryHeap> createHeap(std::uint64_t size,
        std::uint64_t alignment,
        rhi::MemoryUsage usage,
        const char*& diagnostic);
    VulkanRhiPlacedTextureResult createPlaced(
        const rhi::TextureDesc& desc, const rhi::GpuMemoryHeap& heap, std::uint64_t offset);
    rhi::TexturePlacementRequirements query(const rhi::TextureDesc& desc, const char*& diagnostic);

private:
    void captureLoss(VkResult result) const noexcept;
    class State;
    std::shared_ptr<State> state_;
    const char* setupDiagnostic_ = nullptr;
};

} // namespace arp::rhi_vulkan
