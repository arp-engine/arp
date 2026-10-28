#include "rhi/backends/vulkan/vulkan_allocator.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/buffer_placement.h"
#include "rhi/frontend/detail/allocator_backing_ledger.h"
#include "rhi/frontend/memory_budget_ledger.h"

#ifndef ARP_RHI_NO_VMA
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#endif

#include <limits>
#include <sstream>
#include <utility>

namespace arp::rhi_vulkan {

struct VulkanRhiAllocationLedger {
    arp::core::MemoryBudgetLedger budget;

    VulkanRhiAllocationLedger() = default;

    explicit VulkanRhiAllocationLedger(arp::core::MemoryBudgetLedgerDesc desc) : budget(desc) {}

    rhi::MemoryStats memoryStats() const noexcept
    {
        const auto s = budget.stats();
        rhi::MemoryStats stats;
        stats.allocatedBytes = s.allocatedBytes;
        stats.budgetCapacityBytes = s.memoryBudgetBytes;
        stats.peakAllocatedBytes = s.peakAllocatedBytes;
        stats.budgetRejectionCount = s.budgetRejectionCount;
        stats.liveResourceCount = s.liveResourceCount;
        stats.backendFallbacksHiddenFromUpperLayer = s.backendFallbacksHiddenFromUpperLayer;
        return stats;
    }
};

namespace {

VulkanRhiMemoryPolicy policyFor(rhi::MemoryUsage usage)
{
    switch (usage) {
    case rhi::MemoryUsage::GpuOnly:
        return VulkanRhiMemoryPolicy::DeviceLocal;
    case rhi::MemoryUsage::CpuToGpu:
        return VulkanRhiMemoryPolicy::HostUpload;
    case rhi::MemoryUsage::GpuToCpu:
        return VulkanRhiMemoryPolicy::HostReadback;
    case rhi::MemoryUsage::Transient:
        return VulkanRhiMemoryPolicy::LazilyReusedTransient;
    }
    return VulkanRhiMemoryPolicy::DeviceLocal;
}

std::pmr::string vkFailure(const char* operation, VkResult result)
{
    std::ostringstream message;
    message << operation << " failed: " << static_cast<int>(result);
    return arp::core::toPmrString(message.str());
}

class VulkanRhiAllocationReleaseToken final : public rhi::OriginalAllocationLifetime {
public:
    VulkanRhiAllocationReleaseToken(
        std::shared_ptr<VulkanRhiAllocationLedger> ledger, std::uint64_t sizeBytes)
        : ledger_(std::move(ledger)), sizeBytes_(sizeBytes)
    {}

    ~VulkanRhiAllocationReleaseToken()
    {
        if (!ledger_) {
            return;
        }
        // Delegates to the shared budget ledger. The clamp-at-zero semantics
        // (no underflow on mismatched release) are implemented once in
        // core::MemoryBudgetLedger::releaseAllocation, previously inlined here
        // and duplicated conceptually across Metal/WebGPU (which had no release
        // path at all).
        ledger_->budget.releaseAllocation(sizeBytes_);
    }

private:
    std::shared_ptr<VulkanRhiAllocationLedger> ledger_;
    std::uint64_t sizeBytes_ = 0;
};

std::uint64_t estimatedTextureBytes(const rhi::TextureDesc& desc)
{
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        return 0;
    }
    // Block-compressed formats (BC1/BC3/BC7) size by 4x4 block, not per-texel.
    // The shared helper handles both; perPixel==0 for BC formats used to
    // short-circuit here, which would starve the VMA ledger.
    return rhi::bytesForTextureMip(
        desc.format, desc.extent.width, desc.extent.height, desc.extent.depth);
}

#ifndef ARP_RHI_NO_VMA
VmaAllocationCreateInfo allocationCreateInfoFor(rhi::MemoryUsage usage)
{
    VmaAllocationCreateInfo info{};
    switch (usage) {
    case rhi::MemoryUsage::GpuOnly:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case rhi::MemoryUsage::CpuToGpu:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case rhi::MemoryUsage::GpuToCpu:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        info.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case rhi::MemoryUsage::Transient:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    }
    return info;
}
#endif

} // namespace

struct VulkanRhiAllocator::VmaState {
    explicit VmaState(std::shared_ptr<VulkanRhiRuntime> runtime) : runtimeOwner(std::move(runtime))
    {}

    // Allocation payloads can outlive the allocator facade. Keep the original
    // native device/instance alive through vmaDestroyAllocator itself.
    const std::shared_ptr<VulkanRhiRuntime> runtimeOwner;
    rhi::detail::AllocatorBackingLedger backing;
#ifndef ARP_RHI_NO_VMA
    static void VKAPI_PTR allocated(
        VmaAllocator, std::uint32_t, VkDeviceMemory, VkDeviceSize bytes, void* user) noexcept
    {
        static_cast<VmaState*>(user)->backing.allocated(bytes);
    }

    static void VKAPI_PTR released(
        VmaAllocator, std::uint32_t, VkDeviceMemory, VkDeviceSize bytes, void* user) noexcept
    {
        static_cast<VmaState*>(user)->backing.released(bytes);
    }

    ~VmaState()
    {
        if (allocator) {
            vmaDestroyAllocator(allocator);
        }
    }

    VmaVulkanFunctions functions{};
    VmaAllocator allocator = VK_NULL_HANDLE;
#endif
    std::pmr::string diagnostics;
};

VulkanRhiAllocatorReport describeVulkanRhiAllocator(const VulkanRhiAllocatorDesc& desc)
{
    VulkanRhiAllocatorReport report;
    report.vmaAllocatorIntegrated = desc.enableVmaAllocator;
    report.vmaHiddenFromPublicApi = true;
    report.memoryBudgetTelemetrySanitized = desc.memoryBudgetBytes > 0;
    report.nativeAllocationExposedToUpperLayer = desc.exposeNativeAllocationsToUpperLayer;

    if (!desc.enableVmaAllocator) {
        report.diagnostics = "Vulkan RHI allocator requires backend-owned VMA integration";
        return report;
    }
    if (desc.exposeNativeAllocationsToUpperLayer) {
        report.diagnostics = "Vulkan RHI allocator must not expose native allocations upward";
        return report;
    }
    if (desc.memoryBudgetBytes == 0) {
        report.diagnostics = "Vulkan RHI allocator requires sanitized memory budget telemetry";
        return report;
    }

    report.success = true;
    return report;
}

VulkanRhiAllocator::VulkanRhiAllocator(
    VulkanRhiAllocatorDesc desc, std::shared_ptr<VulkanRhiRuntime> runtime)
    : desc_(std::move(desc)), runtime_(std::move(runtime)),
      ledger_(arp::core::makeShared<VulkanRhiAllocationLedger>(
          arp::core::MemoryBudgetLedgerDesc{desc_.memoryBudgetBytes,
              /*backendFallbacksHiddenFromUpperLayer=*/true}))
{
    if (!runtime_ || runtime_->syntheticRuntime()) {
        return;
    }

    vma_ = arp::core::makeShared<VmaState>(runtime_);
#ifdef ARP_RHI_NO_VMA
    vma_->diagnostics = "Vulkan RHI allocator was built without VMA headers";
#else
    if (!runtime_->valid()) {
        vma_->diagnostics = "Vulkan RHI VMA allocator requires a valid runtime";
        return;
    }
    if (!runtime_->getInstanceProcAddr()) {
        vma_->diagnostics = "Vulkan RHI VMA allocator requires vkGetInstanceProcAddr";
        return;
    }

    vma_->functions.vkGetInstanceProcAddr = runtime_->getInstanceProcAddr();
    vma_->functions.vkGetDeviceProcAddr = runtime_->getDeviceProcAddr();

    VmaAllocatorCreateInfo createInfo{};
    const VmaDeviceMemoryCallbacks callbacks{&VmaState::allocated, &VmaState::released, vma_.get()};
    createInfo.pDeviceMemoryCallbacks = &callbacks;
    createInfo.instance = runtime_->instance();
    createInfo.physicalDevice = runtime_->physicalDevice();
    createInfo.device = runtime_->logicalDevice();
    createInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    createInfo.pVulkanFunctions = &vma_->functions;

    const auto created = vmaCreateAllocator(&createInfo, &vma_->allocator);
    if (created != VK_SUCCESS) {
        vma_->diagnostics = vkFailure("vmaCreateAllocator", created);
        vma_->allocator = VK_NULL_HANDLE;
    }
#endif
}

VulkanRhiAllocation VulkanRhiAllocator::allocate(const VulkanRhiAllocationDesc& allocationDesc)
{
    auto allocation = recordAllocation(allocationDesc);
    // Metadata allocations grant no native allocation-owner attachment.
    allocation.originalLifetime.reset();
    return allocation;
}

VulkanRhiAllocation VulkanRhiAllocator::recordAllocation(
    const VulkanRhiAllocationDesc& allocationDesc)
{
    VulkanRhiAllocation allocation;
    allocation.policy = policyFor(allocationDesc.memory);
    allocation.sizeBytes = allocationDesc.sizeBytes;

    const auto report = describeVulkanRhiAllocator(desc_);
    if (!report.success) {
        allocation.diagnostics = report.diagnostics;
        return allocation;
    }
    if (allocationDesc.sizeBytes == 0) {
        allocation.diagnostics = "Vulkan RHI allocation size must be greater than zero";
        return allocation;
    }
    if (!ledger_->budget.canAllocate(allocationDesc.sizeBytes)) {
        ledger_->budget.recordBudgetRejection();
        allocation.diagnostics = "Vulkan RHI allocation exceeds sanitized memory budget";
        return allocation;
    }

    ledger_->budget.recordAllocation(allocationDesc.sizeBytes);
    try {
        std::shared_ptr<rhi::OriginalAllocationLifetime> token =
            arp::core::makeShared<VulkanRhiAllocationReleaseToken>(
                ledger_, allocationDesc.sizeBytes);
        allocation.originalLifetime = token;
        allocation.releaseToken = std::move(token);
    } catch (...) {
        ledger_->budget.releaseAllocation(allocationDesc.sizeBytes);
        throw;
    }
    allocation.success = true;
    return allocation;
}

VulkanRhiVmaBufferAllocation VulkanRhiAllocator::createBuffer(
    const rhi::BufferDesc& desc, VkBufferUsageFlags usage)
{
    VulkanRhiVmaBufferAllocation result;
    constexpr auto hostDeviceFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool hostDeviceAvailable = false;
    if (runtime_ && runtime_->valid() && !runtime_->syntheticRuntime()) {
        const auto& memory = runtime_->memoryProperties();
        for (std::uint32_t i = 0u; i < memory.memoryTypeCount; ++i)
            hostDeviceAvailable = hostDeviceAvailable
                || (memory.memoryTypes[i].propertyFlags & hostDeviceFlags) == hostDeviceFlags;
    }
    if (const auto* id = rhi::HostVisibleDeviceLocalBufferContract::validate(
            desc, rhi::Backend::Vulkan, hostDeviceAvailable)) {
        result.diagnostics = id;
        return result;
    }
    const auto report = describeVulkanRhiAllocator(desc_);
    if (!report.success) {
        result.diagnostics = report.diagnostics;
        return result;
    }
    if (desc.sizeBytes == 0 || usage == 0) {
        result.diagnostics = "Vulkan RHI VMA buffer allocation desc is invalid";
        return result;
    }
    if (runtime_ && runtime_->syntheticRuntime()) {
        const auto allocation = allocate({desc.label, desc.sizeBytes, desc.memory});
        result.allocation = allocation;
        result.success = allocation.success;
        result.diagnostics = allocation.diagnostics;
        return result;
    }
    if (!runtime_ || !runtime_->valid()) {
        result.diagnostics = "Vulkan RHI VMA buffer allocation requires a valid runtime";
        return result;
    }
#ifdef ARP_RHI_NO_VMA
    result.diagnostics = "Vulkan RHI allocator was built without VMA headers";
    return result;
#else
    if (!vma_ || !vma_->allocator) {
        result.diagnostics = vma_ && !vma_->diagnostics.empty()
            ? vma_->diagnostics
            : "Vulkan RHI VMA allocator is not initialized";
        return result;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.sizeBytes;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto allocationInfo = allocationCreateInfoFor(desc.memory);
    if (desc.hostVisibleDeviceLocal.requested()) {
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocationInfo.requiredFlags |= hostDeviceFlags;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo vmaInfo{};
    const auto created = vmaCreateBuffer(
        vma_->allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, &vmaInfo);
    if (created != VK_SUCCESS) {
        result.diagnostics = desc.hostVisibleDeviceLocal.requested()
            ? core::toPmrString("RHI-HOST-DEVICE-BUFFER-ALLOCATION")
            : vkFailure("vmaCreateBuffer", created);
        return result;
    }

    // Actual buffer-compatible type and mapped payload must satisfy the same
    // declaration. A global type query alone cannot prove this allocation.
    if (desc.hostVisibleDeviceLocal.requested()
        && (!vmaInfo.pMappedData || !vmaInfo.size || vmaInfo.size < desc.sizeBytes
            || vmaInfo.memoryType >= runtime_->memoryProperties().memoryTypeCount
            || (runtime_->memoryProperties().memoryTypes[vmaInfo.memoryType].propertyFlags
                   & hostDeviceFlags)
                != hostDeviceFlags)) {
        vmaDestroyBuffer(vma_->allocator, buffer, allocation);
        result.diagnostics = "RHI-HOST-DEVICE-BUFFER-NATIVE-PLACEMENT";
        return result;
    }

    const auto accountedBytes =
        vmaInfo.size > 0 ? static_cast<std::uint64_t>(vmaInfo.size) : desc.sizeBytes;
    VulkanRhiAllocation recorded;
    try {
        recorded = recordAllocation({desc.label, accountedBytes, desc.memory});
    } catch (...) {
        vmaDestroyBuffer(vma_->allocator, buffer, allocation);
        throw;
    }
    if (!recorded.success) {
        vmaDestroyBuffer(vma_->allocator, buffer, allocation);
        result.diagnostics = recorded.diagnostics;
        return result;
    }

    class VmaBufferPayload final : public VulkanRhiMappedBufferWriteFlusher,
                                   public VulkanRhiMappedBufferReadback {
    public:
        VmaBufferPayload(std::shared_ptr<VmaState> state,
            VkBuffer buffer,
            VmaAllocation allocation,
            std::shared_ptr<rhi::OriginalAllocationLifetime> charge,
            std::uint64_t logicalBytes)
            : state_(std::move(state)), buffer_(buffer), allocation_(allocation),
              charge_(std::move(charge)), logicalBytes_(logicalBytes)
        {}

        ~VmaBufferPayload()
        {
            if (state_ && state_->allocator && buffer_ && allocation_) {
                vmaDestroyBuffer(state_->allocator, buffer_, allocation_);
            }
        }

        bool flushMappedWrite(const std::uint64_t offsetBytes,
            const std::uint64_t sizeBytes,
            std::pmr::string& diagnostics) override
        {
            if (!state_ || !state_->allocator || !allocation_) {
                diagnostics = "Vulkan RHI mapped buffer flush requires a live VMA allocation";
                return false;
            }
            const auto flushed =
                vmaFlushAllocation(state_->allocator, allocation_, offsetBytes, sizeBytes);
            if (flushed != VK_SUCCESS) {
                diagnostics = vkFailure("vmaFlushAllocation", flushed);
                return false;
            }
            return true;
        }

        bool invalidateMappedRead(const std::uint64_t offsetBytes,
            const std::uint64_t sizeBytes,
            std::pmr::string& diagnostics) override
        {
            if (!state_ || !state_->allocator || !allocation_) {
                diagnostics = "Vulkan RHI mapped buffer read requires a live VMA allocation";
                return false;
            }
            const auto invalidated =
                vmaInvalidateAllocation(state_->allocator, allocation_, offsetBytes, sizeBytes);
            if (invalidated != VK_SUCCESS) {
                diagnostics = vkFailure("vmaInvalidateAllocation", invalidated);
                return false;
            }
            return true;
        }

        const char* flushMappedWriteBounded(
            std::uint64_t offsetBytes, std::uint64_t sizeBytes) noexcept override
        {
            if (!state_ || !state_->allocator || !allocation_)
                return "RHI-VULKAN-MAPPED-WRITE-ALLOCATION";
            if (!sizeBytes || offsetBytes > logicalBytes_
                || sizeBytes > logicalBytes_ - offsetBytes)
                return "RHI-VULKAN-MAPPED-WRITE-RANGE";
            const auto flushed =
                vmaFlushAllocation(state_->allocator, allocation_, offsetBytes, sizeBytes);
            state_->runtimeOwner->captureOriginalDeviceLoss(flushed);
            return flushed == VK_SUCCESS          ? nullptr
                : flushed == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-MAPPED-WRITE-DEVICE-LOST"
                                                  : "RHI-VULKAN-MAPPED-WRITE-FLUSH";
        }

    private:
        std::shared_ptr<VmaState> state_;
        VkBuffer buffer_ = VK_NULL_HANDLE;
        VmaAllocation allocation_ = VK_NULL_HANDLE;
        std::shared_ptr<rhi::OriginalAllocationLifetime> charge_;
        std::uint64_t logicalBytes_ = 0u;
    };

    std::shared_ptr<VmaBufferPayload> payload;
    try {
        payload = arp::core::makeShared<VmaBufferPayload>(
            vma_, buffer, allocation, recorded.originalLifetime.lock(), desc.sizeBytes);
    } catch (...) {
        vmaDestroyBuffer(vma_->allocator, buffer, allocation);
        throw;
    }
    result.buffer = buffer;
    result.mappedData = vmaInfo.pMappedData;
    result.allocation = recorded;
    result.payload = payload;
    result.readback = payload;
    result.writeFlusher = payload;
    result.success = true;
    return result;
#endif
}

VulkanRhiVmaImageAllocation VulkanRhiAllocator::createImage(const rhi::TextureDesc& desc,
    VkImageCreateInfo imageInfo,
    std::uint64_t maxNativeAllocationBytes)
{
    VulkanRhiVmaImageAllocation result;
    result.format = imageInfo.format;
    result.extent = desc.extent;

    const auto report = describeVulkanRhiAllocator(desc_);
    if (!report.success) {
        result.diagnostics = report.diagnostics;
        return result;
    }
    const auto estimatedBytes = estimatedTextureBytes(desc);
    if (estimatedBytes == 0 || imageInfo.format == VK_FORMAT_UNDEFINED || imageInfo.usage == 0) {
        result.diagnostics = "Vulkan RHI VMA image allocation desc is invalid";
        return result;
    }
    if (runtime_ && runtime_->syntheticRuntime()) {
        if (maxNativeAllocationBytes) {
            result.diagnostics = "RHI-VULKAN-CONTENT-CREATION-AUTHENTIC-RUNTIME";
            return result;
        }
        const auto allocation = allocate({desc.label, estimatedBytes, desc.memory});
        result.allocation = allocation;
        result.success = allocation.success;
        result.diagnostics = allocation.diagnostics;
        return result;
    }
    if (!runtime_ || !runtime_->valid()) {
        result.diagnostics = "Vulkan RHI VMA image allocation requires a valid runtime";
        return result;
    }
#ifdef ARP_RHI_NO_VMA
    result.diagnostics = "Vulkan RHI allocator was built without VMA headers";
    return result;
#else
    if (!vma_ || !vma_->allocator) {
        result.diagnostics = vma_ && !vma_->diagnostics.empty()
            ? vma_->diagnostics
            : "Vulkan RHI VMA allocator is not initialized";
        return result;
    }

    auto allocationInfo = allocationCreateInfoFor(desc.memory);
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo vmaInfo{};
    VkResult created;
    if (maxNativeAllocationBytes) {
        if (desc.memory != rhi::MemoryUsage::GpuOnly) {
            result.diagnostics = "RHI-VULKAN-CONTENT-CREATION-MEMORY-PROFILE";
            return result;
        }
        // vmaAllocateMemoryForImage has no image-create usage input, so AUTO
        // heuristics are unavailable. This selected GpuOnly profile explicitly
        // requires DEVICE_LOCAL; it never falls back to host-only memory.
        allocationInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        // Query this exact unbound image, with the same flags/shape as the
        // eventual allocation. No resident memory is allocated before admission.
        created = vkCreateImage(runtime_->logicalDevice(), &imageInfo, nullptr, &image);
        if (created == VK_SUCCESS) {
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(runtime_->logicalDevice(), image, &requirements);
            if (!requirements.size || requirements.size > maxNativeAllocationBytes) {
                vkDestroyImage(runtime_->logicalDevice(), image, nullptr);
                result.diagnostics = "RHI-VULKAN-CONTENT-CREATION-NATIVE-CAPACITY";
                return result;
            }
            if (!ledger_->budget.canAllocate(requirements.size)) {
                vkDestroyImage(runtime_->logicalDevice(), image, nullptr);
                ledger_->budget.recordBudgetRejection();
                result.diagnostics = "RHI-VULKAN-CONTENT-CREATION-DEVICE-CAPACITY";
                return result;
            }
            created = vmaAllocateMemoryForImage(
                vma_->allocator, image, &allocationInfo, &allocation, &vmaInfo);
            if (created == VK_SUCCESS) {
                if (!vmaInfo.size || vmaInfo.size > maxNativeAllocationBytes) {
                    vmaDestroyImage(vma_->allocator, image, allocation);
                    result.diagnostics = "RHI-VULKAN-CONTENT-CREATION-NATIVE-CAPACITY";
                    return result;
                }
                created = vmaBindImageMemory(vma_->allocator, allocation, image);
            }
            if (created != VK_SUCCESS)
                vmaDestroyImage(vma_->allocator, image, allocation);
        }
    } else
        created = vmaCreateImage(
            vma_->allocator, &imageInfo, &allocationInfo, &image, &allocation, &vmaInfo);
    if (created != VK_SUCCESS) {
        runtime_->captureOriginalDeviceLoss(created);
        result.diagnostics = vkFailure(
            maxNativeAllocationBytes ? "bounded native image allocation" : "vmaCreateImage",
            created);
        return result;
    }

    const auto accountedBytes =
        vmaInfo.size > 0 ? static_cast<std::uint64_t>(vmaInfo.size) : estimatedBytes;
    VulkanRhiAllocation recorded;
    try {
        recorded = recordAllocation({desc.label, accountedBytes, desc.memory});
    } catch (...) {
        vmaDestroyImage(vma_->allocator, image, allocation);
        throw;
    }
    if (!recorded.success) {
        vmaDestroyImage(vma_->allocator, image, allocation);
        result.diagnostics = recorded.diagnostics;
        return result;
    }

    class VmaImagePayload final {
    public:
        VmaImagePayload(std::shared_ptr<VmaState> state,
            VkImage image,
            VmaAllocation allocation,
            std::shared_ptr<rhi::OriginalAllocationLifetime> charge)
            : state_(std::move(state)), image_(image), allocation_(allocation),
              charge_(std::move(charge))
        {}

        ~VmaImagePayload()
        {
            if (state_ && state_->allocator && image_ && allocation_) {
                vmaDestroyImage(state_->allocator, image_, allocation_);
            }
        }

    private:
        std::shared_ptr<VmaState> state_;
        VkImage image_ = VK_NULL_HANDLE;
        VmaAllocation allocation_ = VK_NULL_HANDLE;
        std::shared_ptr<rhi::OriginalAllocationLifetime> charge_;
    };

    result.image = image;
    result.allocation = recorded;
    try {
        result.payload = arp::core::makeShared<VmaImagePayload>(
            vma_, image, allocation, recorded.originalLifetime.lock());
    } catch (...) {
        vmaDestroyImage(vma_->allocator, image, allocation);
        throw;
    }
    result.success = true;
    return result;
#endif
}

rhi::MemoryStats VulkanRhiAllocator::memoryStats() const
{
    return ledger_ ? ledger_->memoryStats() : rhi::MemoryStats{};
}

rhi::AllocatorBackingObservation VulkanRhiAllocator::observeBacking(std::uint32_t version) const
{
    auto result = rhi::AllocatorBackingContract::unavailable(rhi::Backend::Vulkan, version);
    if (version != 1u)
        return result;
#ifndef ARP_RHI_NO_VMA
    if (vma_ && vma_->allocator && runtime_ && runtime_->valid() && !runtime_->syntheticRuntime()) {
        VmaTotalStatistics statistics{};
        vmaCalculateStatistics(vma_->allocator, &statistics);
        const auto& total = statistics.total.statistics;
        return vma_->backing.observe(
            total.blockBytes, total.blockCount, total.allocationBytes, total.allocationCount);
    }
#endif
    return result;
}

} // namespace arp::rhi_vulkan
