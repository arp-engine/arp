#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_allocator.h"
#include "rhi/frontend/page_slice_allocator.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arp::rhi::semantics {
struct UploadStagingColdStorageBudget;
}

namespace arp::rhi {
class GpuDevice;
class OriginalCommandInputs;
class OriginalCommandTextureUploadPackingSource;
struct OriginalCommandStreamBudget;
struct OriginalTexturePoolInitializationBudget;
} // namespace arp::rhi

namespace arp::rhi_vulkan {

struct VulkanRhiStagingUploadPoolDesc {
    core::pmr::String label{&core::defaultCpuMemoryResource()};
    std::uint64_t pageSizeBytes = 1024u * 1024u;
    std::uint32_t maxPageCount = 4;
    std::uint64_t alignmentBytes = 256;
    // Zero uses the explicit page-size bound for legacy backend callers.
    std::uint64_t maxRequestBytes = 0;
};

struct VulkanRhiStagingUploadRequest {
    core::pmr::String label{&core::defaultCpuMemoryResource()};
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 256;
    std::uint64_t frameSerial = 0;
};

struct VulkanRhiStagingUploadSlice {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnosticId{&core::defaultCpuMemoryResource()};
    VkBuffer buffer = VK_NULL_HANDLE;
    void* mappedData = nullptr;
    std::uint64_t offsetBytes = 0;
    std::uint64_t sizeBytes = 0;
    std::uint64_t frameSerial = 0;
    std::uint32_t pageIndex = 0;
    bool nativeHandleExposedToUpperLayer = false;
    std::shared_ptr<void> payload;
    std::shared_ptr<VulkanRhiMappedBufferWriteFlusher> writeFlusher;
    // Bounded refusal owns no diagnostic string and performs no hot allocation.
    const char* storageDiagnostic = nullptr;
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

    static core::UniquePtr<VulkanRhiStagingUploadPool> prepareCold(
        const VulkanRhiStagingUploadPoolDesc& desc,
        VulkanRhiAllocator& allocator,
        const rhi::semantics::UploadStagingColdStorageBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) noexcept;

    bool coldStoragePrepared() const noexcept;

    VulkanRhiStagingUploadSlice allocate(const VulkanRhiStagingUploadRequest& request);
    VulkanRhiStagingUploadSlice initializeOriginalTexture(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::OriginalCommandStreamBudget& stream,
        std::size_t commandIndex,
        const std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource>& packing,
        const rhi::OriginalTexturePoolInitializationBudget& budget,
        rhi::RecordedStagingUploadJournal& journal,
        std::uint64_t frameSerial,
        rhi::GpuDevice* originalDevice = nullptr) noexcept;
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
    std::uint32_t policyRejections_ = 0;
    bool coldStorage_ = false;
};

} // namespace arp::rhi_vulkan
