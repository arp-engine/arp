#include "rhi/backends/vulkan/vulkan_staging_upload_pool.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_original_texture_upload.h"
#include "rhi/frontend/memory_staging_upload_pool.h"
#include "rhi/frontend/memory_upload_staging_budget.h"
#include "rhi/frontend/transfer_upload_pool_initialization.h"

#include <algorithm>
#include <cstring>
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

core::UniquePtr<VulkanRhiStagingUploadPool> VulkanRhiStagingUploadPool::prepareCold(
    const VulkanRhiStagingUploadPoolDesc& desc,
    VulkanRhiAllocator& allocator,
    const rhi::semantics::UploadStagingColdStorageBudget& budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic) noexcept
{
    diagnostic =
        rhi::semantics::UploadStagingColdStorageContract::lowering(rhi::Backend::Vulkan, budget)
            .diagnostic;
    if (diagnostic)
        return {};
    // Existing allocator cold-label temporaries use the engine PMR default.
    // Require its core binding rather than silently bypassing this quota.
    if (std::pmr::get_default_resource() != &core::defaultCpuMemoryResource()) {
        diagnostic = "RHI-STAGING-COLD-STORAGE-CPU-RESOURCE";
        return {};
    }
    if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
        diagnostic = "RHI-STAGING-COLD-STORAGE-CPU-QUOTA";
        return {};
    }
    const auto& policy = budget.capacity;
    if (desc.pageSizeBytes != policy.pageSizeBytes || desc.maxPageCount != policy.maxPageCount
        || desc.alignmentBytes != policy.alignmentBytes
        || desc.maxRequestBytes != policy.maxRequestBytes) {
        diagnostic = "RHI-STAGING-COLD-STORAGE-POLICY-MISMATCH";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        VulkanRhiStagingUploadPoolDesc owned;
        owned.label.assign(desc.label.data(), desc.label.size());
        owned.pageSizeBytes = desc.pageSizeBytes;
        owned.maxPageCount = desc.maxPageCount;
        owned.alignmentBytes = desc.alignmentBytes;
        owned.maxRequestBytes = desc.maxRequestBytes;
        auto pool = core::makeUnique<VulkanRhiStagingUploadPool>(std::move(owned), allocator);
        pool->coldStorage_ = true;
        pool->pages_.reserve(desc.maxPageCount);
        diagnostic = pool->ledger_->slices.prepareCold(
            budget.maxLiveSlicesPerPage, [&](std::uint32_t index, std::uint64_t bytes) {
                return pool->createPage(index, bytes);
            });
        if (diagnostic)
            return {};
        return pool;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-STAGING-COLD-STORAGE-CPU-ALLOCATION";
    } catch (...) {
        diagnostic = "RHI-STAGING-COLD-STORAGE-PREPARATION-EXCEPTION";
    }
    return {};
}

bool VulkanRhiStagingUploadPool::coldStoragePrepared() const noexcept
{
    return coldStorage_ && ledger_->slices.coldStoragePrepared();
}

bool VulkanRhiStagingUploadPool::overlapsColdCpuQuota(
    const core::CpuMemoryQuota* quota) const noexcept
{
    return quota && coldStoragePrepared()
        && core::inspectCpuAllocationCharge(ledger_.get()).quota == quota;
}

const char* VulkanRhiStagingUploadPool::prepareColdStorage(
    const rhi::semantics::UploadStagingColdStorageBudget& budget,
    const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
{
    if (coldStorage_ || !pages_.empty() || memoryResource_ != &core::defaultCpuMemoryResource())
        return "RHI-STAGING-COLD-STORAGE-NOT-FRESH";
    const char* diagnostic = nullptr;
    auto candidate = prepareCold(desc_, allocator_, budget, quota, diagnostic);
    if (!candidate)
        return diagnostic;
    ledger_.swap(candidate->ledger_);
    pages_.swap(candidate->pages_);
    coldStorage_ = true;
    return nullptr;
}

VulkanRhiStagingUploadSlice VulkanRhiStagingUploadPool::allocate(
    const VulkanRhiStagingUploadRequest& request)
{
    const auto coldFailure = [](const char* id) {
        VulkanRhiStagingUploadSlice result;
        result.storageDiagnostic = id;
        return result;
    };
    const auto rejectPolicy = [&](const char* id) {
        if (policyRejections_ != std::numeric_limits<std::uint32_t>::max())
            ++policyRejections_;
        return coldStorage_ ? coldFailure(id) : fail(id, id);
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
        if (coldStorage_)
            return coldFailure(allocation.diagnosticId);
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

VulkanRhiStagingUploadSlice VulkanRhiStagingUploadPool::initializeOriginalTexture(
    const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
    const rhi::OriginalCommandStreamBudget& stream,
    std::size_t commandIndex,
    const std::shared_ptr<const rhi::OriginalCommandTextureUploadPackingSource>& packing,
    const rhi::OriginalTexturePoolInitializationBudget& budget,
    rhi::RecordedStagingUploadJournal& journal,
    std::uint64_t frameSerial,
    rhi::GpuDevice* originalDevice) noexcept
{
    const auto reject = [](const char* id) {
        VulkanRhiStagingUploadSlice result;
        result.storageDiagnostic = id;
        return result;
    };
    using Contract = rhi::OriginalTexturePoolInitializationContract;
    const auto lowering = Contract::lowering(rhi::Backend::Vulkan, budget);
    if (const auto* id = lowering.diagnostic)
        return reject(id);
    if (!coldStoragePrepared())
        return reject("RHI-TEXTURE-POOL-INITIALIZATION-COLD-STORAGE");
    if (!journal.bounded())
        return reject("RHI-TEXTURE-POOL-INITIALIZATION-BOUNDED-JOURNAL");
    if (const auto* id = journal.preflightEnlist())
        return reject(id);
    if (const auto* id = Contract::inspect(inputs, stream, commandIndex, packing, budget))
        return reject(id);
    if (lowering.registeredDestinationRequired) {
        if (!originalDevice || !allocator_.runtime_)
            return reject("RHI-TEXTURE-POOL-INITIALIZATION-DEVICE");
        if (const auto* id = VulkanOriginalTextureUploadStaging::inspectDestination(
                packing, *originalDevice, *allocator_.runtime_))
            return reject(id);
    } else if (originalDevice) {
        return reject("RHI-TEXTURE-POOL-INITIALIZATION-LEGACY-DEVICE");
    }
    const auto& bytes = packing->packing()->packedBytes();
    auto slice = allocate({{}, bytes.size(), desc_.alignmentBytes, frameSerial});
    if (!slice.success)
        return slice;
    std::memcpy(static_cast<std::uint8_t*>(slice.mappedData) + slice.offsetBytes,
        bytes.data(),
        bytes.size());
    if (const auto* id =
            slice.writeFlusher->flushMappedWriteBounded(slice.offsetBytes, bytes.size())) {
        (void)cancel(slice);
        return reject(id);
    }
    if (!enlist(
            journal, {slice.pageIndex, slice.offsetBytes, slice.sizeBytes, slice.frameSerial})) {
        (void)cancel(slice);
        return reject("RHI-TEXTURE-POOL-INITIALIZATION-JOURNAL");
    }
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

    rhi::BufferDesc bufferDesc{core::pmr::String{&core::defaultCpuMemoryResource()}};
    if (desc_.label.empty())
        bufferDesc.label = "VulkanRhiStagingUploadPage";
    else {
        bufferDesc.label.assign(desc_.label.data(), desc_.label.size());
        bufferDesc.label.append("Page");
    }
    bufferDesc.sizeBytes = capacityBytes;
    bufferDesc.usage = rhi::ResourceUsage::CopySrc;
    bufferDesc.memory = rhi::MemoryUsage::CpuToGpu;
    bufferDesc.initialState = rhi::ResourceState::CopySrc;

    auto allocation = allocator_.createBuffer(bufferDesc, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!allocation.success) {
        return false;
    }
    if (coldStorage_
        && (!allocation.allocation.success || !allocation.buffer || !allocation.mappedData
            || !allocation.payload || !allocation.writeFlusher))
        return false;

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
