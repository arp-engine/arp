#pragma once

#include "core/cpu_allocator.h"
#include "core/page_slice_allocator.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arp::rhi_vulkan {

struct VulkanRhiStagingUploadPoolDesc {
    std::pmr::string label;
    std::uint64_t pageSizeBytes = 1024u * 1024u;
    std::uint32_t maxPageCount = 4;
    std::uint64_t alignmentBytes = 256;
};

struct VulkanRhiStagingUploadRequest {
    std::pmr::string label;
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 256;
    std::uint64_t frameSerial = 0;
};

struct VulkanRhiStagingUploadSlice {
    bool success = false;
    std::pmr::string diagnostics;
    std::pmr::string diagnosticId;
    VkBuffer buffer = VK_NULL_HANDLE;
    void* mappedData = nullptr;
    std::uint64_t offsetBytes = 0;
    std::uint64_t sizeBytes = 0;
    std::uint64_t frameSerial = 0;
    std::uint32_t pageIndex = 0;
    bool nativeHandleExposedToUpperLayer = false;
    std::shared_ptr<void> payload;
    std::shared_ptr<VulkanRhiMappedBufferWriteFlusher> writeFlusher;
};

struct VulkanRhiStagingUploadPoolStats {
    std::uint32_t allocatedPageCount = 0;
    std::uint32_t liveSliceCount = 0;
    std::uint32_t retiredSliceCount = 0;
    std::uint32_t failedAllocationCount = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t peakAllocatedBytes = 0;
    bool boundedCapacity = true;
    bool nativeHandlesHiddenFromUpperLayer = true;
};

class VulkanRhiStagingUploadPool {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// pages vector so a backend can bind a per-instance intent/budget
    /// (e.g. RhiHostStaging) instead of silently falling back to the global
    /// default.
    VulkanRhiStagingUploadPool(VulkanRhiStagingUploadPoolDesc desc,
        VulkanRhiAllocator& allocator,
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource());
    ~VulkanRhiStagingUploadPool();

    VulkanRhiStagingUploadSlice allocate(const VulkanRhiStagingUploadRequest& request);
    bool cancel(const VulkanRhiStagingUploadSlice& slice);
    bool cancel(const rhi::RecordedStagingUploadSlice& slice);
    bool enlist(
        rhi::RecordedStagingUploadJournal& journal, const rhi::RecordedStagingUploadSlice& slice);
    /// Backend-private completion observation. The returned counter advances
    /// after exact retirement. This is a retry epoch, never a serial prefix.
    std::uint64_t completedSubmissionEpoch() const noexcept;
    VulkanRhiStagingUploadPoolStats stats() const;

private:
    struct Page;
    struct Ledger;

    VulkanRhiStagingUploadSlice fail(
        const std::pmr::string& diagnostics, const char* diagnosticId = nullptr);
    bool createPage(std::uint32_t pageIndex, std::uint64_t capacityBytes);
    VulkanRhiStagingUploadPoolStats buildStats() const;

    VulkanRhiStagingUploadPoolDesc desc_;
    VulkanRhiAllocator& allocator_;
    std::pmr::memory_resource* memoryResource_;
    std::shared_ptr<Ledger> ledger_;
    core::pmr::Vector<Page> pages_;
};

} // namespace arp::rhi_vulkan
