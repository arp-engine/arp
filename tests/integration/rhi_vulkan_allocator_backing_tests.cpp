#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "tests/fixtures/vulkan_headless_runtime.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_vulkan;

namespace {
// Independent real vkAllocateMemory/vkFreeMemory oracle. No fake payload or
// fabricated native size; the actual driver call executes before accounting.
class NativeMemoryProbe final {
public:
    static inline std::uint64_t current = 0u, peak = 0u, allocations = 0u, frees = 0u;
    // This fixture performs allocations serially and uses one live allocation
    // size sum from the VMA callbacks to match frees independently via a map.
    static inline core::pmr::UnorderedMap<VkDeviceMemory, VkDeviceSize> sizes;

    static VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice device,
        const VkMemoryAllocateInfo* info,
        const VkAllocationCallbacks* callbacks,
        VkDeviceMemory* memory)
    {
        const auto result = vkAllocateMemory(device, info, callbacks, memory);
        if (result == VK_SUCCESS) {
            assert(sizes.emplace(*memory, info->allocationSize).second);
            current += info->allocationSize;
            if (current > peak)
                peak = current;
            ++allocations;
        }
        return result;
    }

    static VKAPI_ATTR void VKAPI_CALL release(
        VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* callbacks)
    {
        const auto found = sizes.find(memory);
        assert(found != sizes.end() && current >= found->second);
        current -= found->second;
        sizes.erase(found);
        ++frees;
        vkFreeMemory(device, memory, callbacks);
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name)
    {
        if (!std::strcmp(name, "vkAllocateMemory"))
            return reinterpret_cast<PFN_vkVoidFunction>(&allocate);
        if (!std::strcmp(name, "vkFreeMemory"))
            return reinterpret_cast<PFN_vkVoidFunction>(&release);
        return vkGetDeviceProcAddr(device, name);
    }

    static AllocatorBackingObservation check(const VulkanRhiAllocator& allocator)
    {
        const auto report = allocator.observeBacking();
        assert(report.state == AllocatorBackingState::Observed && !report.diagnostic);
        assert(report.backingBytes == current && report.peakBackingBytes == peak);
        assert(report.blockCount == sizes.size());
        assert(report.allocationEvents == allocations && report.releaseEvents == frees);
        assert(report.occupiedBytes <= report.backingBytes
            && report.unusedBytes == report.backingBytes - report.occupiedBytes);
        return report;
    }
};
} // namespace

int main()
{
    tests::VulkanHeadlessRuntime bootstrap(false, NativeMemoryProbe::resolve);
    assert(bootstrap.runtime() && bootstrap.runtime()->valid());
    VulkanRhiAllocatorDesc descriptor;
    descriptor.label = "AllocatorBackingNative";
    descriptor.memoryBudgetBytes = 1024u * 1024u;
    VulkanRhiAllocator unavailable(descriptor);
    assert(unavailable.observeBacking().state == AllocatorBackingState::Unsupported);
    {
        VulkanRhiAllocator allocator(descriptor, bootstrap.runtime());
        assert(allocator.observeBacking(2u).state == AllocatorBackingState::DiagnosticFailure);
        assert(NativeMemoryProbe::check(allocator).occupiedBytes == 0u);
        BufferDesc desc;
        desc.sizeBytes = 65536u;
        desc.usage = ResourceUsage::StorageBuffer;
        desc.memory = MemoryUsage::CpuToGpu;
        auto buffer = allocator.createBuffer(desc, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        assert(buffer.success && buffer.buffer && buffer.mappedData && buffer.payload);
        const auto retained = NativeMemoryProbe::check(allocator);
        assert(retained.occupiedBytes >= desc.sizeBytes && retained.allocationCount == 1u);
        auto payload = buffer.payload;
        buffer = {};
        assert(NativeMemoryProbe::check(allocator).occupiedBytes == retained.occupiedBytes);

        TextureDesc texture;
        texture.extent = {16u, 16u, 1u};
        texture.format = TextureFormat::Rgba16Float;
        texture.memory = MemoryUsage::GpuOnly;
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        imageInfo.extent = {16u, 16u, 1u};
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        auto image = allocator.createImage(texture, imageInfo);
        assert(image.success && image.image && image.payload);
        assert(NativeMemoryProbe::check(allocator).allocationCount == 2u);
        image = {};
        assert(NativeMemoryProbe::check(allocator).allocationCount == 1u);
        const auto rejectedBefore = allocator.memoryStats().budgetRejectionCount;
        desc.sizeBytes = descriptor.memoryBudgetBytes * 2u;
        assert(!allocator.createBuffer(desc, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).success);
        assert(allocator.memoryStats().budgetRejectionCount == rejectedBefore + 1u);
        assert(NativeMemoryProbe::check(allocator).allocationCount == 1u);
        // VMA/ledger owners survive a facade copy. The retained native payload
        // continues to occupy storage until its final owner releases.
        auto observer = allocator;
        assert(NativeMemoryProbe::check(observer).occupiedBytes == retained.occupiedBytes);
        payload.reset();
        assert(NativeMemoryProbe::check(observer).occupiedBytes == 0u);
    }
    assert(!NativeMemoryProbe::current && NativeMemoryProbe::sizes.empty());
    assert(NativeMemoryProbe::allocations
        && NativeMemoryProbe::allocations == NativeMemoryProbe::frees);
    std::printf("ALLOCATOR_BACKING_NATIVE_PASS peakBytes=%llu allocations=%llu frees=%llu\n",
        static_cast<unsigned long long>(NativeMemoryProbe::peak),
        static_cast<unsigned long long>(NativeMemoryProbe::allocations),
        static_cast<unsigned long long>(NativeMemoryProbe::frees));
}
