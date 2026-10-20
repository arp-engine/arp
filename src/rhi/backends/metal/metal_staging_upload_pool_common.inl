// Shared owner-thread pool logic. Included only by the selected platform implementation.
#include <limits>
#include <stdexcept>

namespace arp::rhi_metal {
MetalRhiStagingUploadPool::MetalRhiStagingUploadPool(
    MetalRhiStagingUploadPoolDesc desc, std::pmr::memory_resource* memoryResource)
    : desc_(std::move(desc)),
      memoryResource_(memoryResource ? memoryResource : &core::defaultCpuMemoryResource()),
      slices_({desc_.pageSizeBytes, desc_.maxPageCount}, memoryResource_), pages_(memoryResource_),
      lastDiagnostics_(memoryResource_),
      completedSubmissionSerial_(core::makeShared<std::atomic<std::uint64_t>>(0u)),
      completedSubmissionEpoch_(core::makeShared<std::atomic<std::uint64_t>>(0u))
{
    desc_.alignmentBytes = rhi::normalizeStagingAlignment(desc_.alignmentBytes);
}

MetalRhiStagingUploadSlice MetalRhiStagingUploadPool::allocate(
    const MetalRhiStagingUploadRequest& request)
{
    // Only exact resolved tokens can retire storage; public counters are telemetry.
    lastDiagnostics_.clear();
    const auto alignment =
        rhi::resolveStagingAlignment(request.alignmentBytes, desc_.alignmentBytes);
    const auto allocation = slices_.allocate(
        core::FrameRetiredPageSliceRequest{
            request.sizeBytes, alignment, request.frameSerial, request.label.c_str()},
        [this](std::uint32_t pageIndex, std::uint64_t capacityBytes) {
            return createPage(pageIndex, capacityBytes);
        });
    if (!allocation.success) {
        const char* baseDiagnostic = request.sizeBytes == 0
            ? "Metal RHI upload staging allocation size must be greater than zero"
            : allocation.diagnostics != nullptr ? allocation.diagnostics
                                                : "Metal RHI upload staging allocation failed";
        rhi::appendStagingFailureTelemetry(lastDiagnostics_,
            baseDiagnostic,
            allocation.diagnosticId,
            std::string_view(request.label.data(), request.label.size()),
            slices_.stats());
        return fail(lastDiagnostics_.c_str(), allocation.diagnosticId);
    }

    auto& page = pages_[allocation.pageIndex];

    MetalRhiStagingUploadSlice slice;
    slice.success = true;
    slice.mappedData = rhi::stagingMappedDataAt(page.mappedData, allocation.offsetBytes);
    slice.offsetBytes = allocation.offsetBytes;
    slice.sizeBytes = request.sizeBytes;
    slice.frameSerial = allocation.frameSerial;
    slice.pageIndex = allocation.pageIndex;
    slice.nativeHandleExposedToUpperLayer = false;
    slice.payload = page.payload;
    return slice;
}

std::uint64_t MetalRhiStagingUploadPool::beginSubmission()
{
    return submissionIdentities_.tryAcquire();
}

std::shared_ptr<std::atomic<std::uint64_t>>
MetalRhiStagingUploadPool::completedSubmissionSerial() const
{
    return completedSubmissionSerial_;
}

std::shared_ptr<std::atomic<std::uint64_t>>
MetalRhiStagingUploadPool::completedSubmissionEpoch() const
{
    return completedSubmissionEpoch_;
}

bool MetalRhiStagingUploadPool::rollback(const MetalRhiStagingUploadSlice& slice)
{
    core::FrameRetiredPageSliceAllocation allocation;
    allocation.success = slice.success;
    allocation.pageIndex = slice.pageIndex;
    allocation.offsetBytes = slice.offsetBytes;
    allocation.sizeBytes = slice.sizeBytes;
    allocation.frameSerial = slice.frameSerial;
    return slices_.cancel(allocation);
}

bool MetalRhiStagingUploadPool::cancel(const MetalRhiStagingUploadSlice& slice)
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

bool MetalRhiStagingUploadPool::cancel(const rhi::RecordedStagingUploadSlice& slice)
{
    return slices_.cancel({
        true,
        nullptr,
        slice.pageIndex,
        slice.offsetBytes,
        slice.sizeBytes,
        slice.frameSerial,
        nullptr,
    });
}

void MetalRhiStagingUploadPool::retireCompletedFrames(std::uint64_t completedFrameSerial)
{
    (void)completedFrameSerial;
    throw std::logic_error("RHI-METAL-STAGING-SERIAL-COMPLETION-UNAUTHENTICATED");
}

MetalRhiStagingUploadPoolStats MetalRhiStagingUploadPool::stats() const
{
    return buildStats();
}

MetalRhiStagingUploadSlice MetalRhiStagingUploadPool::fail(
    const char* diagnostics, const char* diagnosticId)
{
    MetalRhiStagingUploadSlice slice;
    slice.diagnostics = diagnostics;
    if (diagnosticId != nullptr) {
        slice.diagnosticId = diagnosticId;
    }
    return slice;
}

namespace {
class MetalStagingReservation final {
public:
    explicit MetalStagingReservation(
        std::shared_ptr<core::CompletionPageSliceReservation> reservation)
        : reservation_(std::move(reservation))
    {}

    bool cancel(const rhi::RecordedStagingUploadSlice& slice) noexcept
    {
        return matches(slice) && reservation_->cancel();
    }

    bool retire(const rhi::RecordedStagingUploadSlice& slice) noexcept
    {
        return matches(slice) && reservation_->complete();
    }

private:
    bool matches(const rhi::RecordedStagingUploadSlice& slice) const noexcept
    {
        const auto& a = reservation_->allocation();
        return a.pageIndex == slice.pageIndex && a.offsetBytes == slice.offsetBytes
            && a.sizeBytes == slice.sizeBytes && a.frameSerial == slice.frameSerial;
    }

    std::shared_ptr<core::CompletionPageSliceReservation> reservation_;
};
} // namespace

bool MetalRhiStagingUploadPool::enlist(
    rhi::RecordedStagingUploadJournal& journal, const rhi::RecordedStagingUploadSlice& slice)
{
    const auto reservation = slices_.find({true,
        nullptr,
        slice.pageIndex,
        slice.offsetBytes,
        slice.sizeBytes,
        slice.frameSerial,
        nullptr});
    if (!reservation || slice.pageIndex >= pages_.size())
        return false;
    return journal.enlist(core::makeShared<MetalStagingReservation>(reservation),
        slice,
        pages_[slice.pageIndex].payload);
}

MetalRhiStagingUploadPoolStats MetalRhiStagingUploadPool::buildStats() const
{
    const auto coreStats = slices_.stats();
    MetalRhiStagingUploadPoolStats stats;
    rhi::fillStagingCommonStats(coreStats, stats);
    stats.nativeHandlesHiddenFromUpperLayer = true;
    stats.sharedOrManagedUploadMemory = desc_.preferSharedMemory;
    return stats;
}

} // namespace arp::rhi_metal
