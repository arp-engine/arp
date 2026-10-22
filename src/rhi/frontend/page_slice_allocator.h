#pragma once

#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace arp::core {

struct FrameRetiredPageSliceAllocatorDesc {
    std::uint64_t pageSizeBytes = 0;
    std::uint32_t maxPageCount = 0;
};

struct FrameRetiredPageSliceRequest {
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 1;
    std::uint64_t frameSerial = 0;
    // Non-owning label supplied by the backend-owned upload intent. The
    // allocator uses it only during the request; backends retain any label
    // they need for post-call telemetry.
    const char* label = nullptr;
};

struct FrameRetiredPageSliceAllocation {
    bool success = false;
    const char* diagnostics = nullptr;
    std::uint32_t pageIndex = 0;
    std::uint64_t offsetBytes = 0;
    std::uint64_t sizeBytes = 0;
    std::uint64_t frameSerial = 0;
    const char* diagnosticId = nullptr;
};

struct FrameRetiredPageSliceAllocatorStats {
    std::uint32_t allocatedPageCount = 0;
    std::uint32_t successfulAllocationCount = 0;
    std::uint32_t liveSliceCount = 0;
    std::uint32_t retiredSliceCount = 0;
    std::uint32_t failedAllocationCount = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t liveBytes = 0;
    std::uint64_t peakLiveBytes = 0;
    bool boundedCapacity = true;
    // Last-request telemetry is scalar/non-owning so the L2 allocator does
    // not retain backend-owned labels or native objects.
    std::uint64_t lastRequestBytes = 0;
    std::uint64_t lastRequestAlignmentBytes = 0;
    std::uint64_t lastRequestFrameSerial = 0;
    std::uint64_t lastPageSizeBytes = 0;
    std::uint64_t lastAllocatedBytes = 0;
    std::uint32_t lastAllocatedPageCount = 0;
    std::uint32_t lastMaxPageCount = 0;
    const char* lastDiagnosticId = nullptr;
    bool lastRequestSucceeded = false;
};

inline constexpr const char* kFrameRetiredPageSliceDiagnosticSizeZero = "ARP-RHI-STAGING-SIZE-ZERO";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticNoCapacity =
    "ARP-RHI-STAGING-NO-CAPACITY";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticRequestExceedsPage =
    "ARP-RHI-STAGING-REQUEST-EXCEEDS-PAGE";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticAlignmentRange =
    "ARP-RHI-STAGING-ALIGNMENT-RANGE";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticAlignmentInvalid =
    "ARP-RHI-STAGING-ALIGNMENT-INVALID";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticPageCount =
    "ARP-RHI-STAGING-PAGE-COUNT-EXCEEDED";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticPageCreate =
    "ARP-RHI-STAGING-PAGE-CREATE-FAILED";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticOffsetInvalid =
    "ARP-RHI-STAGING-OFFSET-INVALID";
inline constexpr const char* kFrameRetiredPageSliceDiagnosticPageCapacity =
    "ARP-RHI-STAGING-PAGE-CAPACITY";

class FrameRetiredPageSliceAllocator {
public:
    explicit FrameRetiredPageSliceAllocator(FrameRetiredPageSliceAllocatorDesc desc) : desc_(desc)
    {}

    template <typename PageFactory>
    FrameRetiredPageSliceAllocation allocate(
        const FrameRetiredPageSliceRequest& request, PageFactory&& pageFactory)
    {
        beginRequestTelemetry(request);
        if (request.sizeBytes == 0) {
            return fail("page slice allocation size must be greater than zero",
                kFrameRetiredPageSliceDiagnosticSizeZero);
        }
        if (desc_.pageSizeBytes == 0 || desc_.maxPageCount == 0) {
            return fail("page slice allocator has no bounded capacity",
                kFrameRetiredPageSliceDiagnosticNoCapacity);
        }
        if (request.sizeBytes > desc_.pageSizeBytes) {
            return fail("page slice allocation exceeds bounded page capacity",
                kFrameRetiredPageSliceDiagnosticRequestExceedsPage);
        }
        if (request.alignmentBytes
            > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return fail("page slice allocation alignment exceeds addressable range",
                kFrameRetiredPageSliceDiagnosticAlignmentRange);
        }
        if (!isValidCpuAllocationAlignment(static_cast<std::size_t>(request.alignmentBytes))) {
            return fail("allocation alignment must be a non-zero power of two",
                kFrameRetiredPageSliceDiagnosticAlignmentInvalid);
        }

        auto* page = findPage(request.sizeBytes, request.alignmentBytes);
        if (!page) {
            if (pages_.size() >= desc_.maxPageCount) {
                return fail("page slice allocation exceeds bounded page capacity",
                    kFrameRetiredPageSliceDiagnosticPageCount);
            }

            const auto pageIndex = static_cast<std::uint32_t>(pages_.size());
            if (!pageFactory(pageIndex, desc_.pageSizeBytes)) {
                return fail("page slice allocator page creation failed",
                    kFrameRetiredPageSliceDiagnosticPageCreate);
            }

            Page newPage;
            newPage.index = pageIndex;
            newPage.capacityBytes = desc_.pageSizeBytes;
            pages_.push_back(std::move(newPage));
            stats_.allocatedPageCount = static_cast<std::uint32_t>(pages_.size());
            stats_.allocatedBytes += desc_.pageSizeBytes;
            page = &pages_.back();
        }

        const auto offset = alignedOffset(*page, request.alignmentBytes);
        if (!offset.success) {
            return fail(offset.diagnostics, kFrameRetiredPageSliceDiagnosticOffsetInvalid);
        }
        if (offset.value > page->capacityBytes
            || request.sizeBytes > page->capacityBytes - offset.value) {
            return fail("page slice allocation exceeds bounded page capacity",
                kFrameRetiredPageSliceDiagnosticPageCapacity);
        }

        page->cursorBytes = offset.value + request.sizeBytes;
        page->liveSlices.push_back(Slice{offset.value, request.sizeBytes, request.frameSerial});
        ++stats_.successfulAllocationCount;
        ++stats_.liveSliceCount;
        stats_.liveBytes += request.sizeBytes;
        stats_.peakLiveBytes = std::max(stats_.peakLiveBytes, stats_.liveBytes);
        stats_.lastAllocatedBytes = stats_.allocatedBytes;
        stats_.lastAllocatedPageCount = stats_.allocatedPageCount;
        stats_.lastDiagnosticId = nullptr;
        stats_.lastRequestSucceeded = true;

        return FrameRetiredPageSliceAllocation{
            true,
            nullptr,
            page->index,
            offset.value,
            request.sizeBytes,
            request.frameSerial,
            nullptr,
        };
    }

    std::uint32_t retireCompletedFrames(std::uint64_t completedFrameSerial)
    {
        std::uint32_t retired = 0;
        std::uint64_t retiredBytes = 0;
        for (auto& page : pages_) {
            page.liveSlices.erase(std::remove_if(page.liveSlices.begin(),
                                      page.liveSlices.end(),
                                      [&](const Slice& slice) {
                                          if (slice.frameSerial > completedFrameSerial) {
                                              return false;
                                          }
                                          ++retired;
                                          retiredBytes += slice.sizeBytes;
                                          return true;
                                      }),
                page.liveSlices.end());
            if (page.liveSlices.empty()) {
                page.cursorBytes = 0;
            }
        }

        stats_.retiredSliceCount += retired;
        stats_.liveSliceCount -= std::min(stats_.liveSliceCount, retired);
        stats_.liveBytes = retiredBytes > stats_.liveBytes ? 0 : stats_.liveBytes - retiredBytes;
        return retired;
    }

    /// Cancel the most recent allocation when recording the command that owns
    /// it fails before submission. A canceled slice must be the newest slice
    /// on its page; this keeps reclamation deterministic without introducing a
    /// free-list or allowing an older in-flight slice to be reused early.
    bool rollbackAllocation(const FrameRetiredPageSliceAllocation& allocation)
    {
        if (!allocation.success || allocation.pageIndex >= pages_.size()) {
            return false;
        }
        auto& page = pages_[allocation.pageIndex];
        if (page.liveSlices.empty()) {
            return false;
        }
        const auto& slice = page.liveSlices.back();
        if (allocation.sizeBytes
            > std::numeric_limits<std::uint64_t>::max() - allocation.offsetBytes) {
            return false;
        }
        if (slice.offsetBytes != allocation.offsetBytes || slice.sizeBytes != allocation.sizeBytes
            || slice.frameSerial != allocation.frameSerial
            || page.cursorBytes != allocation.offsetBytes + allocation.sizeBytes) {
            return false;
        }
        if (stats_.liveSliceCount == 0u || allocation.sizeBytes > stats_.liveBytes) {
            return false;
        }
        page.liveSlices.pop_back();
        page.cursorBytes = allocation.offsetBytes;
        --stats_.liveSliceCount;
        stats_.liveBytes =
            allocation.sizeBytes > stats_.liveBytes ? 0u : stats_.liveBytes - allocation.sizeBytes;
        return true;
    }

    // Cancels one exact unaccepted allocation. Unlike rollbackAllocation(),
    // this remains valid when a later recording allocated another slice on
    // the same page. Holes are not reused while newer slices remain live;
    // only the contiguous tail is reclaimed, preserving the linear allocator
    // and preventing overlap with accepted or independently recorded work.
    bool cancelAllocation(const FrameRetiredPageSliceAllocation& allocation)
    {
        if (!allocation.success || allocation.pageIndex >= pages_.size()) {
            return false;
        }
        auto& page = pages_[allocation.pageIndex];
        const auto found =
            std::find_if(page.liveSlices.begin(), page.liveSlices.end(), [&](const Slice& slice) {
                return slice.offsetBytes == allocation.offsetBytes
                    && slice.sizeBytes == allocation.sizeBytes
                    && slice.frameSerial == allocation.frameSerial;
            });
        if (found == page.liveSlices.end() || stats_.liveSliceCount == 0u
            || allocation.sizeBytes > stats_.liveBytes) {
            return false;
        }
        page.liveSlices.erase(found);
        page.cursorBytes = 0u;
        for (const auto& slice : page.liveSlices) {
            if (slice.sizeBytes > std::numeric_limits<std::uint64_t>::max() - slice.offsetBytes) {
                return false;
            }
            page.cursorBytes = std::max(page.cursorBytes, slice.offsetBytes + slice.sizeBytes);
        }
        --stats_.liveSliceCount;
        stats_.liveBytes -= allocation.sizeBytes;
        return true;
    }

    // The caller must own completion evidence for this exact allocation.
    // Completion is not a serial prefix and cannot retire other recordings.
    bool retireAllocation(const FrameRetiredPageSliceAllocation& allocation)
    {
        if (!cancelAllocation(allocation))
            return false;
        ++stats_.retiredSliceCount;
        return true;
    }

    const FrameRetiredPageSliceAllocatorStats& stats() const { return stats_; }

private:
    struct Slice {
        std::uint64_t offsetBytes = 0;
        std::uint64_t sizeBytes = 0;
        std::uint64_t frameSerial = 0;
    };

    struct Page {
        std::uint32_t index = 0;
        std::uint64_t capacityBytes = 0;
        std::uint64_t cursorBytes = 0;
        pmr::Vector<Slice> liveSlices;
    };

    struct OffsetResult {
        bool success = false;
        std::uint64_t value = 0;
        const char* diagnostics = nullptr;
    };

    void beginRequestTelemetry(const FrameRetiredPageSliceRequest& request)
    {
        stats_.lastRequestBytes = request.sizeBytes;
        stats_.lastRequestAlignmentBytes = request.alignmentBytes;
        stats_.lastRequestFrameSerial = request.frameSerial;
        stats_.lastPageSizeBytes = desc_.pageSizeBytes;
        stats_.lastAllocatedBytes = stats_.allocatedBytes;
        stats_.lastAllocatedPageCount = stats_.allocatedPageCount;
        stats_.lastMaxPageCount = desc_.maxPageCount;
        stats_.lastDiagnosticId = nullptr;
        stats_.lastRequestSucceeded = false;
    }

    FrameRetiredPageSliceAllocation fail(const char* diagnostics, const char* diagnosticId)
    {
        ++stats_.failedAllocationCount;
        stats_.lastAllocatedBytes = stats_.allocatedBytes;
        stats_.lastAllocatedPageCount = stats_.allocatedPageCount;
        stats_.lastDiagnosticId = diagnosticId;
        stats_.lastRequestSucceeded = false;
        return FrameRetiredPageSliceAllocation{
            false,
            diagnostics,
            0,
            0,
            0,
            0,
            diagnosticId,
        };
    }

    Page* findPage(std::uint64_t sizeBytes, std::uint64_t alignmentBytes)
    {
        for (auto& page : pages_) {
            const auto offset = alignedOffset(page, alignmentBytes);
            if (offset.success && offset.value <= page.capacityBytes
                && sizeBytes <= page.capacityBytes - offset.value) {
                return &page;
            }
        }
        return nullptr;
    }

    OffsetResult alignedOffset(const Page& page, std::uint64_t alignmentBytes) const
    {
        if (page.cursorBytes
            > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return {
                false, page.cursorBytes, "page slice allocation cursor exceeds addressable range"};
        }
        const auto aligned = alignCpuAllocationOffsetUp(
            static_cast<std::size_t>(page.cursorBytes), static_cast<std::size_t>(alignmentBytes));
        if (!aligned.success) {
            return {false, page.cursorBytes, aligned.diagnostics};
        }
        return {true, static_cast<std::uint64_t>(aligned.alignedValue), nullptr};
    }

    FrameRetiredPageSliceAllocatorDesc desc_;
    pmr::Vector<Page> pages_;
    FrameRetiredPageSliceAllocatorStats stats_;
};

} // namespace arp::core
