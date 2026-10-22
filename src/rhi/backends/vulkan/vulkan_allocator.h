#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

namespace arp::rhi_vulkan {

class VulkanRhiRuntime;
struct VulkanRhiAllocationLedger;

enum class VulkanRhiMemoryPolicy { DeviceLocal, HostUpload, HostReadback, LazilyReusedTransient };

struct VulkanRhiAllocatorDesc {
    std::pmr::string label;
    std::uint64_t memoryBudgetBytes = 0;
    bool enableVmaAllocator = true;
    bool exposeNativeAllocationsToUpperLayer = false;
};

struct VulkanRhiAllocatorReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool vmaAllocatorIntegrated = false;
    bool vmaHiddenFromPublicApi = true;
    bool memoryBudgetTelemetrySanitized = false;
    bool nativeAllocationExposedToUpperLayer = false;
};

struct VulkanRhiAllocationDesc {
    std::pmr::string label;
    std::uint64_t sizeBytes = 0;
    rhi::MemoryUsage memory = rhi::MemoryUsage::GpuOnly;
};

struct VulkanRhiAllocation {
    bool success = false;
    std::pmr::string diagnostics;
    VulkanRhiMemoryPolicy policy = VulkanRhiMemoryPolicy::DeviceLocal;
    std::uint64_t sizeBytes = 0;
    bool nativeAllocationExposedToUpperLayer = false;
    bool vmaAllocationVisibleToUpperLayer = false;
    std::shared_ptr<void> releaseToken;
    std::weak_ptr<rhi::OriginalAllocationLifetime> originalLifetime;
};

class VulkanRhiMappedBufferWriteFlusher {
public:
    virtual ~VulkanRhiMappedBufferWriteFlusher() = default;
    virtual bool flushMappedWrite(
        std::uint64_t offsetBytes, std::uint64_t sizeBytes, std::pmr::string& diagnostics) = 0;
};

/// Backend-owned readback invalidation hook.  Host-visible Vulkan memory is
/// not required to be coherent; callers must invalidate the mapped range
/// before inspecting GPU-written bytes.
class VulkanRhiMappedBufferReadback {
public:
    virtual ~VulkanRhiMappedBufferReadback() = default;
    virtual bool invalidateMappedRead(
        std::uint64_t offsetBytes, std::uint64_t sizeBytes, std::pmr::string& diagnostics) = 0;
};

struct VulkanRhiVmaBufferAllocation {
    bool success = false;
    std::pmr::string diagnostics;
    VulkanRhiAllocation allocation;
    VkBuffer buffer = VK_NULL_HANDLE;
    void* mappedData = nullptr;
    std::shared_ptr<void> payload;
    std::shared_ptr<VulkanRhiMappedBufferWriteFlusher> writeFlusher;
    std::shared_ptr<VulkanRhiMappedBufferReadback> readback;
};

struct VulkanRhiVmaImageAllocation {
    bool success = false;
    std::pmr::string diagnostics;
    VulkanRhiAllocation allocation;
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    rhi::Extent3D extent;
    std::shared_ptr<void> payload;
};

VulkanRhiAllocatorReport describeVulkanRhiAllocator(const VulkanRhiAllocatorDesc& desc);

class VulkanRhiAllocator {
public:
    explicit VulkanRhiAllocator(
        VulkanRhiAllocatorDesc desc, std::shared_ptr<VulkanRhiRuntime> runtime = {});

    VulkanRhiAllocation allocate(const VulkanRhiAllocationDesc& desc);
    VulkanRhiVmaBufferAllocation createBuffer(
        const rhi::BufferDesc& desc, VkBufferUsageFlags usage);
    VulkanRhiVmaImageAllocation createImage(const rhi::TextureDesc& desc,
        VkImageCreateInfo imageInfo,
        std::uint64_t maxNativeAllocationBytes = 0u);
    rhi::MemoryStats memoryStats() const;

private:
    VulkanRhiAllocation recordAllocation(const VulkanRhiAllocationDesc& desc);

    VulkanRhiAllocatorDesc desc_;
    std::shared_ptr<VulkanRhiRuntime> runtime_;
    struct VmaState;
    std::shared_ptr<VmaState> vma_;
    std::shared_ptr<VulkanRhiAllocationLedger> ledger_;
};

} // namespace arp::rhi_vulkan
