#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/memory_staging_upload_pool.h"
#include "rhi/frontend/memory_upload_staging_budget.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace arp::rhi_vulkan {

struct VulkanRhiStagingUploadPool::Ledger {
    explicit Ledger(core::FrameRetiredPageSliceAllocatorDesc desc) : slices(desc) {}

    static core::FrameRetiredPageSliceAllocation allocation(
        const rhi::RecordedStagingUploadSlice& slice)
    {
        return {true,
            nullptr,
            slice.pageIndex,
            slice.offsetBytes,
            slice.sizeBytes,
            slice.frameSerial,
            nullptr};
    }

    bool cancel(const rhi::RecordedStagingUploadSlice& slice) noexcept
    {
        return slices.cancelAllocation(allocation(slice));
    }

    bool retire(const rhi::RecordedStagingUploadSlice& slice) noexcept
    {
        if (!slices.retireAllocation(allocation(slice)))
            return false;
        advanceEpoch();
        return true;
    }

    void advanceEpoch() noexcept
    {
        const auto current = epoch.load(std::memory_order_relaxed);
        if (current != std::numeric_limits<std::uint64_t>::max())
            epoch.store(current + 1u, std::memory_order_release);
    }

    core::FrameRetiredPageSliceAllocator slices;
    std::atomic<std::uint64_t> epoch{0u};
};

struct VulkanRhiStagingUploadPool::Page {
    VkBuffer buffer = VK_NULL_HANDLE;
    void* mappedData = nullptr;
    std::shared_ptr<void> payload;
    std::shared_ptr<VulkanRhiMappedBufferWriteFlusher> writeFlusher;
};

VulkanRhiStagingUploadPool::VulkanRhiStagingUploadPool(VulkanRhiStagingUploadPoolDesc desc,
    VulkanRhiAllocator& allocator,
    std::pmr::memory_resource* memoryResource)
    : desc_(std::move(desc)), allocator_(allocator),
      memoryResource_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource()),
      ledger_(core::makeShared<Ledger>(
          core::FrameRetiredPageSliceAllocatorDesc{desc_.pageSizeBytes, desc_.maxPageCount})),
      pages_(memoryResource_)
{
    desc_.alignmentBytes = rhi::normalizeStagingAlignment(desc_.alignmentBytes);
}

VulkanRhiStagingUploadPool::~VulkanRhiStagingUploadPool() = default;

VulkanRhiStagingUploadSlice VulkanRhiStagingUploadPool::allocate(
    const VulkanRhiStagingUploadRequest& request)
{
    const auto rejectPolicy = [&](const char* id) {
        if (policyRejections_ != std::numeric_limits<std::uint32_t>::max())
            ++policyRejections_;
        return fail(id, id);
    };
    if (request.alignmentBytes != 0u
        && !rhi::semantics::isUploadStagingPowerOfTwo(request.alignmentBytes))
        return rejectPolicy(core::kFrameRetiredPageSliceDiagnosticAlignmentInvalid);
    if (!rhi::semantics::isUploadStagingPowerOfTwo(desc_.alignmentBytes))
        return rejectPolicy(rhi::semantics::kUploadStagingBudgetDiagnosticAlignment);
    if (desc_.maxRequestBytes != 0u && request.sizeBytes > desc_.maxRequestBytes)
        return rejectPolicy("ARP-RHI-STAGING-REQUEST-LIMIT");
    const auto alignment = std::max(request.alignmentBytes, desc_.alignmentBytes);
    const auto allocation = ledger_->slices.allocate(
        core::FrameRetiredPageSliceRequest{
            request.sizeBytes, alignment, request.frameSerial, request.label.c_str()},
        [this](std::uint32_t pageIndex, std::uint64_t capacityBytes) {
            return createPage(pageIndex, capacityBytes);
        });
    if (!allocation.success) {
        std::pmr::string diagnostics(memoryResource_);
        const char* baseDiagnostic = allocation.diagnostics != nullptr
            ? allocation.diagnostics
            : "Vulkan RHI staging upload allocation failed";
        rhi::appendStagingFailureTelemetry(diagnostics,
            baseDiagnostic,
            allocation.diagnosticId,
            std::string_view(request.label.data(), request.label.size()),
            ledger_->slices.stats());
        return fail(diagnostics, allocation.diagnosticId);
    }

    auto& page = pages_[allocation.pageIndex];

    VulkanRhiStagingUploadSlice slice;
    slice.success = true;
    slice.buffer = page.buffer;
    // Consumers apply offsetBytes when writing and when recording the GPU copy.
    // Keep this pointer at the start of the mapped page; returning an already
    // offset pointer made every non-zero slice write at 2 * offsetBytes while
    // vkCmdCopyBufferToImage still read from offsetBytes.
    slice.mappedData = page.mappedData;
    slice.offsetBytes = allocation.offsetBytes;
    slice.sizeBytes = allocation.sizeBytes;
    slice.frameSerial = allocation.frameSerial;
    slice.pageIndex = allocation.pageIndex;
    slice.nativeHandleExposedToUpperLayer = false;
    slice.payload = page.payload;
    slice.writeFlusher = page.writeFlusher;
    return slice;
}

bool VulkanRhiStagingUploadPool::cancel(const VulkanRhiStagingUploadSlice& slice)
{
    if (!slice.success)
        return false;
    return cancel({
        slice.pageIndex,
        slice.offsetBytes,
        slice.sizeBytes,
        slice.frameSerial,
    });
}

bool VulkanRhiStagingUploadPool::cancel(const rhi::RecordedStagingUploadSlice& slice)
{
    return ledger_->cancel(slice);
}

bool VulkanRhiStagingUploadPool::enlist(
    rhi::RecordedStagingUploadJournal& journal, const rhi::RecordedStagingUploadSlice& slice)
{
    if (slice.pageIndex >= pages_.size())
        return false;
    return journal.enlist(ledger_, slice, pages_[slice.pageIndex].payload);
}

std::uint64_t VulkanRhiStagingUploadPool::completedSubmissionEpoch() const noexcept
{
    return ledger_->epoch.load(std::memory_order_acquire);
}

VulkanRhiStagingUploadPoolStats VulkanRhiStagingUploadPool::stats() const
{
    return buildStats();
}

VulkanRhiStagingUploadSlice VulkanRhiStagingUploadPool::fail(
    const std::pmr::string& diagnostics, const char* diagnosticId)
{
    VulkanRhiStagingUploadSlice slice;
    slice.diagnostics = diagnostics;
    if (diagnosticId != nullptr) {
        slice.diagnosticId = diagnosticId;
    }
    return slice;
}

bool VulkanRhiStagingUploadPool::createPage(std::uint32_t pageIndex, std::uint64_t capacityBytes)
{
    if (!rhi::stagingCreatePageInvariant(pageIndex, pages_.size(), capacityBytes)) {
        return false;
    }

    rhi::BufferDesc bufferDesc;
    bufferDesc.label = desc_.label.empty() ? "VulkanRhiStagingUploadPage" : desc_.label + "Page";
    bufferDesc.sizeBytes = capacityBytes;
    bufferDesc.usage = rhi::ResourceUsage::CopySrc;
    bufferDesc.memory = rhi::MemoryUsage::CpuToGpu;
    bufferDesc.initialState = rhi::ResourceState::CopySrc;

    auto allocation = allocator_.createBuffer(bufferDesc, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!allocation.success) {
        return false;
    }

    Page page;
    page.buffer = allocation.buffer;
    page.mappedData = allocation.mappedData;
    page.payload = allocation.payload;
    page.writeFlusher = allocation.writeFlusher;
    pages_.push_back(std::move(page));
    return true;
}

VulkanRhiStagingUploadPoolStats VulkanRhiStagingUploadPool::buildStats() const
{
    const auto coreStats = ledger_->slices.stats();
    VulkanRhiStagingUploadPoolStats stats;
    rhi::fillStagingCommonStats(coreStats, stats);
    stats.failedAllocationCount += std::min(
        policyRejections_, std::numeric_limits<std::uint32_t>::max() - stats.failedAllocationCount);
    stats.nativeHandlesHiddenFromUpperLayer = true;
    return stats;
}

} // namespace arp::rhi_vulkan
