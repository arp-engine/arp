#include "rhi/backends/vulkan/vulkan_heap_placement.h"

#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_texture_descriptor.h"
#include "rhi/frontend/heap_placement_contract.h"

#include <algorithm>
#include <new>

namespace arp::rhi_vulkan {
namespace {

class VulkanHeapAllocation final {
public:
    VulkanHeapAllocation(std::shared_ptr<VulkanRhiRuntime> runtime,
        PFN_vkFreeMemory freeMemory,
        std::uint64_t size,
        std::uint32_t type)
        : runtime_(std::move(runtime)), freeMemory_(freeMemory), size_(size), type_(type)
    {}

    ~VulkanHeapAllocation()
    {
        if (memory_)
            freeMemory_(runtime_->logicalDevice(), memory_, nullptr);
    }

    VkDevice device() const { return runtime_->logicalDevice(); }

    VkDeviceMemory memory() const { return memory_; }

    std::uint64_t size() const { return size_; }

    std::uint32_t type() const { return type_; }

    void adopt(VkDeviceMemory memory) noexcept { memory_ = memory; }

    std::shared_ptr<void> accounting;

private:
    std::shared_ptr<VulkanRhiRuntime> runtime_;
    PFN_vkFreeMemory freeMemory_;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    std::uint64_t size_;
    std::uint32_t type_;
};

// One logical placement lease per heap acquisition. The physical cache never
// owns this object, so it cannot keep an obsolete wrapper's receipt attached.
class VulkanHeapLease final : public rhi::OriginalAllocationLifetime {
public:
    std::shared_ptr<VulkanHeapAllocation> allocation;
};

class PlacedImagePayload final : public VulkanRhiNativeResourcePayload {
public:
    PlacedImagePayload(std::shared_ptr<VulkanRhiRuntime> runtime,
        PFN_vkDestroyImage destroyImage,
        VkFormat format,
        rhi::Extent3D extent,
        std::shared_ptr<VulkanHeapLease> lease = {})
        : runtime_(std::move(runtime)), destroyImage_(destroyImage), format_(format),
          extent_(extent), lease_(std::move(lease))
    {}

    ~PlacedImagePayload() override
    {
        // The logical lease and physical backing outlive native image destruction.
        if (image_)
            destroyImage_(runtime_->logicalDevice(), image_, nullptr);
    }

    void adopt(VkImage image) noexcept { image_ = image; }

    VkDevice backendVulkanDeviceHandle() const override { return runtime_->logicalDevice(); }

    VkImage backendVulkanImageHandle() const override { return image_; }

    VkFormat backendVulkanImageFormat() const override { return format_; }

    rhi::Extent3D backendVulkanImageExtent() const override { return extent_; }

private:
    std::shared_ptr<VulkanRhiRuntime> runtime_;
    PFN_vkDestroyImage destroyImage_;
    VkImage image_ = VK_NULL_HANDLE;
    VkFormat format_;
    rhi::Extent3D extent_;
    std::shared_ptr<VulkanHeapLease> lease_;
};

// A heap lease retains the cache/ledger as well as its already-created memory.
// A second facade on the original runtime may consume the lease; no borrowed
// allocator member or creator facade participates in native destruction.
class VulkanMemoryHeap final : public rhi::GpuMemoryHeap {
public:
    VulkanMemoryHeap(std::shared_ptr<void> pool, std::uint64_t size, std::uint64_t alignment)
        : lease(core::makeShared<VulkanHeapLease>()), id_(rhi::allocateGpuResourceId()),
          pool_(std::move(pool)), size_(size), alignment_(alignment)
    {
        bindOriginalAllocationLifetime(*lease);
    }

    rhi::GpuResourceId id() const override { return id_; }

    std::uint64_t sizeBytes() const override { return size_; }

    std::uint64_t alignmentBytes() const override { return alignment_; }

    bool supportsAliasing() const override { return true; }

    std::shared_ptr<VulkanHeapLease> lease;

private:
    rhi::GpuResourceId id_;
    std::shared_ptr<void> pool_;
    std::uint64_t size_;
    std::uint64_t alignment_;
};

} // namespace

class VulkanRhiHeapPlacement::State final {
public:
    State(std::shared_ptr<VulkanRhiRuntime> runtime,
        const VulkanRhiAllocator& allocator,
        bool enabled,
        std::uint64_t budget)
        : runtime_(std::move(runtime)), allocator_(allocator), enabled_(enabled)
    {
        if (const auto* id = admissionDiagnostic()) {
            setupDiagnostic_ = id;
            return;
        }
        allocateMemory =
            reinterpret_cast<PFN_vkAllocateMemory>(runtime_->getDeviceProcAddr("vkAllocateMemory"));
        freeMemory =
            reinterpret_cast<PFN_vkFreeMemory>(runtime_->getDeviceProcAddr("vkFreeMemory"));
        createImage =
            reinterpret_cast<PFN_vkCreateImage>(runtime_->getDeviceProcAddr("vkCreateImage"));
        destroyImage =
            reinterpret_cast<PFN_vkDestroyImage>(runtime_->getDeviceProcAddr("vkDestroyImage"));
        requirements2 = reinterpret_cast<PFN_vkGetImageMemoryRequirements2>(
            runtime_->getDeviceProcAddr("vkGetImageMemoryRequirements2"));
        bindImage = reinterpret_cast<PFN_vkBindImageMemory>(
            runtime_->getDeviceProcAddr("vkBindImageMemory"));
        imageProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(
            runtime_->getInstanceProcAddr()(
                runtime_->instance(), "vkGetPhysicalDeviceImageFormatProperties"));
        if (!allocateMemory || !freeMemory || !createImage || !destroyImage || !requirements2
            || !bindImage || !imageProperties) {
            setupDiagnostic_ = "RHI-VULKAN-HEAP-ENTRYPOINT-UNAVAILABLE";
            return;
        }
        const auto& memory = runtime_->memoryProperties();
        if (!memory.memoryHeapCount || memory.memoryHeapCount > VK_MAX_MEMORY_HEAPS) {
            setupDiagnostic_ = "RHI-VULKAN-HEAP-NATIVE-LIMITS";
            return;
        }
        for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
            const auto& type = memory.memoryTypes[index];
            if ((type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                && type.heapIndex < memory.memoryHeapCount
                && memory.memoryHeaps[type.heapIndex].size) {
                memoryType_ = index;
                nativeHeapSize_ = memory.memoryHeaps[type.heapIndex].size;
                break;
            }
        }
        // Every admitted heap has at least one byte. The explicit byte budget
        // and actual native allocation limit bound the maximum entry count.
        capacityLimit_ =
            std::min<std::uint64_t>(budget, runtime_->nativeLimits().maxMemoryAllocationCount);
        if (!capacityLimit_ || !nativeHeapSize_) {
            setupDiagnostic_ = "RHI-VULKAN-HEAP-NATIVE-LIMITS";
            return;
        }
    }

    const char* admissionDiagnostic() const noexcept
    {
        if (!enabled_)
            return "RHI-VULKAN-HEAP-CAPABILITY-UNAVAILABLE";
        if (!runtime_ || !runtime_->valid() || runtime_->syntheticRuntime()
            || runtime_->coreQueueCapabilityDiagnostic())
            return "RHI-VULKAN-HEAP-RUNTIME-UNAVAILABLE";
        return setupDiagnostic_;
    }

    std::shared_ptr<VulkanHeapAllocation> acquire(
        std::uint64_t size, rhi::MemoryUsage usage, const char*& diagnostic, VkResult& nativeResult)
    {
        if (size > nativeHeapSize_) {
            diagnostic = "RHI-VULKAN-HEAP-NATIVE-LIMITS";
            return {};
        }
        // Existing retirement proves completion with the original fences;
        // accepted/issued/presented work never makes an entry reusable.
        retireCompletedVulkanRhiSubmittedCommands();
        for (const auto& allocation : allocations_) {
            if (allocation.use_count() == 1 && allocation->size() >= size)
                return allocation;
        }
        allocations_.erase(std::remove_if(allocations_.begin(),
                               allocations_.end(),
                               [](const auto& allocation) {
                                   return allocation.use_count() == 1;
                               }),
            allocations_.end());
        if (allocations_.size() >= capacityLimit_) {
            diagnostic = "RHI-VULKAN-HEAP-NATIVE-LIMITS";
            return {};
        }
        // This is a cold cache miss. Reserve only the demanded entry before
        // any native allocation, never the driver's theoretical maximum.
        // Warm reuse above performs no container growth.
        if (allocations_.size() == allocations_.capacity())
            allocations_.reserve(allocations_.size() + 1u);
        auto owner =
            core::makeShared<VulkanHeapAllocation>(runtime_, freeMemory, size, memoryType_);
        auto accounting = allocator_.allocate({"VulkanTransientAliasHeap", size, usage});
        if (!accounting.success) {
            diagnostic = "RHI-VULKAN-HEAP-BUDGET-REJECTED";
            return {};
        }
        owner->accounting = std::move(accounting.releaseToken);
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = size;
        info.memoryTypeIndex = memoryType_;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        nativeResult = allocateMemory(runtime_->logicalDevice(), &info, nullptr, &memory);
        if (nativeResult != VK_SUCCESS || !memory) {
            diagnostic = "RHI-VULKAN-HEAP-NATIVE-ALLOCATION-FAILED";
            return {};
        }
        owner->adopt(memory);
        allocations_.push_back(owner); // Capacity and owner already exist.
        return owner;
    }

    const char* preflight(const rhi::TextureDesc& desc,
        VkImageCreateInfo& info,
        VkImageFormatProperties& properties,
        VkResult& nativeResult) const
    {
        if (const auto* id = admissionDiagnostic())
            return id;
        std::uint64_t logicalBytes = 0;
        if (const auto* id = rhi::HeapPlacementContract::texture(desc, logicalBytes))
            return id;
        // No enabled compression feature capture is available in this runtime
        // contract. Native format support alone cannot authorize BC creation.
        if (rhi::isBlockCompressed(desc.format))
            return "RHI-VULKAN-HEAP-CAPABILITY-UNAVAILABLE";
        info = VulkanTextureDescriptor::imageInfo(
            desc, VK_IMAGE_CREATE_ALIAS_BIT, static_cast<VkSampleCountFlagBits>(desc.sampleCount));
        if (info.format == VK_FORMAT_UNDEFINED || !info.usage)
            return "RHI-VULKAN-HEAP-IMAGE-FORMAT-UNSUPPORTED";
        nativeResult = imageProperties(runtime_->physicalDevice(),
            info.format,
            info.imageType,
            info.tiling,
            info.usage,
            info.flags,
            &properties);
        if (nativeResult != VK_SUCCESS)
            return "RHI-VULKAN-HEAP-IMAGE-FORMAT-UNSUPPORTED";
        if (desc.extent.width > properties.maxExtent.width
            || desc.extent.height > properties.maxExtent.height
            || desc.extent.depth > properties.maxExtent.depth
            || desc.mipLevelCount > properties.maxMipLevels
            || desc.arrayLayerCount > properties.maxArrayLayers
            || !(properties.sampleCounts & info.samples) || !properties.maxResourceSize)
            return "RHI-VULKAN-HEAP-NATIVE-LIMITS";
        return nullptr;
    }

    const char* requirements(VkImage image,
        const VkImageFormatProperties& properties,
        VkMemoryRequirements& output) const
    {
        VkMemoryDedicatedRequirements dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
        VkMemoryRequirements2 requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
        requirements.pNext = &dedicated;
        VkImageMemoryRequirementsInfo2 info{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
        info.image = image;
        requirements2(runtime_->logicalDevice(), &info, &requirements);
        output = requirements.memoryRequirements;
        if (dedicated.requiresDedicatedAllocation)
            return "RHI-VULKAN-HEAP-DEDICATED-REQUIRED";
        if (!output.size || !core::isPowerOfTwoUnsigned(output.alignment) || !output.memoryTypeBits)
            return "RHI-HEAP-REQUIREMENTS-INVALID";
        if (output.size > properties.maxResourceSize)
            return "RHI-VULKAN-HEAP-NATIVE-LIMITS";
        if (!(output.memoryTypeBits & (std::uint32_t{1} << memoryType_)))
            return "RHI-VULKAN-HEAP-MEMORY-TYPE-MISMATCH";
        return nullptr;
    }

    std::shared_ptr<VulkanRhiRuntime> runtime_;
    VulkanRhiAllocator allocator_;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements2 requirements2 = nullptr;
    PFN_vkBindImageMemory bindImage = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties imageProperties = nullptr;

private:
    bool enabled_;
    const char* setupDiagnostic_ = nullptr;
    std::uint32_t memoryType_ = 0;
    std::uint64_t nativeHeapSize_ = 0;
    std::uint64_t capacityLimit_ = 0;
    core::pmr::Vector<std::shared_ptr<VulkanHeapAllocation>> allocations_{
        &core::defaultCpuMemoryResource()};
};

VulkanRhiHeapPlacement::VulkanRhiHeapPlacement(std::shared_ptr<VulkanRhiRuntime> runtime,
    const VulkanRhiAllocator& allocator,
    bool enabled,
    std::uint64_t budget)
{
    try {
        state_ = core::makeShared<State>(std::move(runtime), allocator, enabled, budget);
    } catch (const std::bad_alloc&) {
        setupDiagnostic_ = "RHI-VULKAN-HEAP-HOST-ALLOCATION-FAILED";
    }
}

const char* VulkanRhiHeapPlacement::admissionDiagnostic() const noexcept
{
    return state_ ? state_->admissionDiagnostic() : setupDiagnostic_;
}

void VulkanRhiHeapPlacement::captureLoss(VkResult result) const noexcept
{
    if (state_ && state_->runtime_)
        state_->runtime_->captureOriginalDeviceLoss(result);
}

core::UniquePtr<rhi::GpuMemoryHeap> VulkanRhiHeapPlacement::createHeap(
    std::uint64_t size, std::uint64_t alignment, rhi::MemoryUsage usage, const char*& diagnostic)
{
    diagnostic = rhi::HeapPlacementContract::heap(size, alignment, usage);
    if (!diagnostic)
        diagnostic = admissionDiagnostic();
    if (diagnostic)
        return {};
    VkResult nativeResult = VK_SUCCESS;
    try {
        auto heap = core::makeUnique<VulkanMemoryHeap>(state_, size, alignment);
        heap->lease->allocation = state_->acquire(size, usage, diagnostic, nativeResult);
        captureLoss(nativeResult);
        if (nativeResult == VK_ERROR_DEVICE_LOST)
            diagnostic = "RHI-VULKAN-HEAP-DEVICE-LOST";
        return heap->lease->allocation ? std::move(heap) : core::UniquePtr<rhi::GpuMemoryHeap>{};
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-VULKAN-HEAP-HOST-ALLOCATION-FAILED";
        return {};
    }
}

rhi::TexturePlacementRequirements VulkanRhiHeapPlacement::query(
    const rhi::TextureDesc& desc, const char*& diagnostic)
{
    diagnostic = admissionDiagnostic();
    if (diagnostic)
        return {};
    VkResult nativeResult = VK_SUCCESS;
    VkImageCreateInfo info{};
    VkImageFormatProperties properties{};
    diagnostic = state_->preflight(desc, info, properties, nativeResult);
    captureLoss(nativeResult);
    if (diagnostic)
        return {};
    try {
        auto owner = core::makeShared<PlacedImagePayload>(
            state_->runtime_, state_->destroyImage, info.format, desc.extent);
        VkImage image = VK_NULL_HANDLE;
        nativeResult =
            state_->createImage(state_->runtime_->logicalDevice(), &info, nullptr, &image);
        captureLoss(nativeResult);
        if (nativeResult != VK_SUCCESS || !image) {
            diagnostic = nativeResult == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-HEAP-DEVICE-LOST"
                : "RHI-VULKAN-HEAP-IMAGE-CREATE-FAILED";
            return {};
        }
        owner->adopt(image);
        VkMemoryRequirements requirements{};
        diagnostic = state_->requirements(image, properties, requirements);
        return diagnostic
            ? rhi::TexturePlacementRequirements{}
            : rhi::TexturePlacementRequirements{requirements.size, requirements.alignment};
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-VULKAN-HEAP-HOST-ALLOCATION-FAILED";
        return {};
    }
}

VulkanRhiPlacedTextureResult VulkanRhiHeapPlacement::createPlaced(
    const rhi::TextureDesc& desc, const rhi::GpuMemoryHeap& heap, std::uint64_t offset)
{
    VulkanRhiPlacedTextureResult result;
    result.diagnostic = admissionDiagnostic();
    if (result.diagnostic)
        return result;
    const auto* nativeHeap = dynamic_cast<const VulkanMemoryHeap*>(&heap);
    if (!nativeHeap || !nativeHeap->lease || !nativeHeap->lease->allocation
        || nativeHeap->lease->allocation->device() != state_->runtime_->logicalDevice()) {
        result.diagnostic = "RHI-VULKAN-HEAP-OWNER-MISMATCH";
        return result;
    }
    // Reject obviously illegal offsets before even the temporary image query.
    if (offset % heap.alignmentBytes() || offset >= heap.sizeBytes()) {
        result.diagnostic =
            offset % heap.alignmentBytes() ? "RHI-HEAP-OFFSET-ALIGNMENT" : "RHI-HEAP-RANGE";
        return result;
    }
    VkResult nativeResult = VK_SUCCESS;
    VkImageCreateInfo info{};
    VkImageFormatProperties properties{};
    result.diagnostic = state_->preflight(desc, info, properties, nativeResult);
    captureLoss(nativeResult);
    if (result.diagnostic)
        return result;
    try {
        auto owner = core::makeShared<PlacedImagePayload>(
            state_->runtime_, state_->destroyImage, info.format, desc.extent, nativeHeap->lease);
        VkImage image = VK_NULL_HANDLE;
        nativeResult =
            state_->createImage(state_->runtime_->logicalDevice(), &info, nullptr, &image);
        captureLoss(nativeResult);
        if (nativeResult != VK_SUCCESS || !image) {
            result.diagnostic = nativeResult == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-HEAP-DEVICE-LOST"
                : "RHI-VULKAN-HEAP-IMAGE-CREATE-FAILED";
            return result;
        }
        owner->adopt(image);
        VkMemoryRequirements requirements{};
        result.diagnostic = state_->requirements(image, properties, requirements);
        if (result.diagnostic)
            return result;
        result.diagnostic = rhi::HeapPlacementContract::placement(heap.sizeBytes(),
            heap.alignmentBytes(),
            offset,
            {requirements.size, requirements.alignment});
        if (!result.diagnostic
            && !core::containsUnsignedRange(std::uint64_t{0},
                nativeHeap->lease->allocation->size(),
                offset,
                std::uint64_t{requirements.size}))
            result.diagnostic = "RHI-HEAP-RANGE";
        if (!result.diagnostic
            && !(requirements.memoryTypeBits
                & (std::uint32_t{1} << nativeHeap->lease->allocation->type())))
            result.diagnostic = "RHI-VULKAN-HEAP-MEMORY-TYPE-MISMATCH";
        if (result.diagnostic)
            return result;
        nativeResult = state_->bindImage(state_->runtime_->logicalDevice(),
            image,
            nativeHeap->lease->allocation->memory(),
            offset);
        captureLoss(nativeResult);
        if (nativeResult != VK_SUCCESS) {
            result.diagnostic = nativeResult == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-HEAP-DEVICE-LOST"
                : "RHI-VULKAN-HEAP-BIND-FAILED";
            return result;
        }
        result.allocation.success = true;
        result.allocation.sizeBytes = requirements.size;
        result.allocation.policy = VulkanRhiMemoryPolicy::DeviceLocal;
        result.payload = std::move(owner);
    } catch (const std::bad_alloc&) {
        result.diagnostic = "RHI-VULKAN-HEAP-HOST-ALLOCATION-FAILED";
    }
    return result;
}

} // namespace arp::rhi_vulkan
