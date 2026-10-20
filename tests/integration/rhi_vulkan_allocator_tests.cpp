#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::pmr::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

std::filesystem::path projectRoot()
{
#if defined(ARP_SOURCE_DIR)
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src" / "rhi")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
#endif
}

void assertVmaIsBackendPrivate()
{
    const auto root = projectRoot();
    assert(std::filesystem::exists(
        root / "3rd" / "VulkanMemoryAllocator" / "include" / "vk_mem_alloc.h"));
    const auto publicRhiSources = readText(root / "src" / "rhi" / "gpu_resources.h")
        + readText(root / "src" / "rhi" / "gpu_device.h")
        + readText(root / "src" / "rhi" / "gpu_common.h");
    assert(publicRhiSources.find("VmaAllocator") == std::pmr::string::npos);
    assert(publicRhiSources.find("VmaAllocation") == std::pmr::string::npos);
    const auto allocatorSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_allocator.cpp");
    assert(allocatorSource.find("vk_mem_alloc.h") != std::pmr::string::npos);
    assert(allocatorSource.find("vmaCreateAllocator") != std::pmr::string::npos);
    assert(allocatorSource.find("vmaCreateBuffer") != std::pmr::string::npos);
    assert(allocatorSource.find("vmaCreateImage") != std::pmr::string::npos);
    assert(allocatorSource.find("vmaDestroyBuffer") != std::pmr::string::npos);
    assert(allocatorSource.find("vmaDestroyImage") != std::pmr::string::npos);
}

void assertMappedBufferWritesHaveDeterministicFlushContract()
{
    const auto root = projectRoot();
    const auto allocatorHeader =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_allocator.h");
    const auto allocatorSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_allocator.cpp");
    const auto nativeResourceHeader =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_resources.h");
    const auto nativeResourceSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_native_resources.cpp");
    const auto deviceSource =
        readText(root / "src" / "rhi" / "backends" / "vulkan" / "vulkan_device.cpp");

    assert(allocatorHeader.find("VulkanRhiMappedBufferWriteFlusher") != std::pmr::string::npos);
    assert(allocatorHeader.find("writeFlusher") != std::pmr::string::npos);
    assert(allocatorSource.find("vmaFlushAllocation") != std::pmr::string::npos);
    assert(nativeResourceHeader.find("backendFlushMappedWrite") != std::pmr::string::npos);
    assert(nativeResourceHeader.find("Vulkan RHI native payload has no mapped write flush contract")
        != std::pmr::string::npos);
    assert(nativeResourceSource.find("backendFlushMappedWrite") != std::pmr::string::npos);
    assert(deviceSource.find("backendFlushMappedWrite(offsetBytes, sizeBytes")
        != std::pmr::string::npos);
    assert(deviceSource.find(
               "std::memcpy(static_cast<unsigned char*>(mapped) + offsetBytes, data, sizeBytes);")
        != std::pmr::string::npos);
}

} // namespace

int main()
{
    assertVmaIsBackendPrivate();
    assertMappedBufferWritesHaveDeterministicFlushContract();

    using namespace arp::rhi;
    using namespace arp::rhi_vulkan;

    VulkanRhiAllocatorDesc desc;
    desc.label = "VmaBackedRhiAllocator";
    desc.memoryBudgetBytes = 16u * 1024u * 1024u;
    desc.enableVmaAllocator = true;
    desc.exposeNativeAllocationsToUpperLayer = false;

    const auto allocatorReport = describeVulkanRhiAllocator(desc);
    assert(allocatorReport.success);
    assert(allocatorReport.vmaAllocatorIntegrated);
    assert(allocatorReport.vmaHiddenFromPublicApi);
    assert(allocatorReport.memoryBudgetTelemetrySanitized);
    assert(!allocatorReport.nativeAllocationExposedToUpperLayer);

    VulkanRhiAllocator allocator(desc);
    auto deviceLocal = allocator.allocate({
        "DeviceLocalVertex",
        4096,
        MemoryUsage::GpuOnly,
    });
    assert(deviceLocal.success);
    assert(deviceLocal.policy == VulkanRhiMemoryPolicy::DeviceLocal);
    assert(!deviceLocal.nativeAllocationExposedToUpperLayer);
    assert(!deviceLocal.vmaAllocationVisibleToUpperLayer);

    auto upload = allocator.allocate({
        "UploadStaging",
        2048,
        MemoryUsage::CpuToGpu,
    });
    assert(upload.success);
    assert(upload.policy == VulkanRhiMemoryPolicy::HostUpload);

    auto readback = allocator.allocate({
        "ReadbackStaging",
        2048,
        MemoryUsage::GpuToCpu,
    });
    assert(readback.success);
    assert(readback.policy == VulkanRhiMemoryPolicy::HostReadback);

    auto transient = allocator.allocate({
        "TransientAttachment",
        1024,
        MemoryUsage::Transient,
    });
    assert(transient.success);
    assert(transient.policy == VulkanRhiMemoryPolicy::LazilyReusedTransient);

    const auto stats = allocator.memoryStats();
    assert(stats.allocatedBytes == 9216u);
    assert(stats.liveResourceCount == 4u);
    assert(stats.backendFallbacksHiddenFromUpperLayer);

    auto oversized = allocator.allocate({
        "Oversized",
        desc.memoryBudgetBytes,
        MemoryUsage::GpuOnly,
    });
    assert(!oversized.success);
    assert(oversized.diagnostics.find("budget") != std::pmr::string::npos);

    auto leakingDesc = desc;
    leakingDesc.exposeNativeAllocationsToUpperLayer = true;
    const auto leakingReport = describeVulkanRhiAllocator(leakingDesc);
    assert(!leakingReport.success);
    assert(leakingReport.diagnostics.find("native") != std::pmr::string::npos);

    return 0;
}
