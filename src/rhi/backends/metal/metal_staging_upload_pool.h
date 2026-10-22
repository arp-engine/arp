#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/completion_page_slice_allocator.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arp::rhi_metal {

struct MetalRhiStagingUploadPoolDesc {
    std::pmr::string label;
    // Zero means no workload-owned staging budget was supplied.
    std::uint64_t pageSizeBytes = 0u;
    std::uint32_t maxPageCount = 0u;
    std::uint64_t alignmentBytes = 256;
    bool preferSharedMemory = true;
};

struct MetalRhiStagingUploadRequest {
    std::pmr::string label;
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 256;
    std::uint64_t frameSerial = 0;
};

struct MetalRhiStagingUploadSlice {
    bool success = false;
    std::pmr::string diagnostics;
    std::pmr::string diagnosticId;
    void* mappedData = nullptr;
    std::uint64_t offsetBytes = 0;
    std::uint64_t sizeBytes = 0;
    std::uint64_t frameSerial = 0;
    std::uint32_t pageIndex = 0;
    bool nativeHandleExposedToUpperLayer = false;
    std::shared_ptr<void> payload;
};

struct MetalRhiStagingUploadPoolStats {
    std::uint32_t allocatedPageCount = 0;
    std::uint32_t liveSliceCount = 0;
    std::uint32_t retiredSliceCount = 0;
    std::uint32_t failedAllocationCount = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t peakAllocatedBytes = 0;
    bool boundedCapacity = true;
    bool nativeHandlesHiddenFromUpperLayer = true;
    bool sharedOrManagedUploadMemory = true;
};

struct MetalRhiNativeStagingBufferInfo {
    std::shared_ptr<void> nativeDevice;
    std::shared_ptr<void> nativeBuffer;
    std::uint64_t sizeBytes = 0;
    bool nativeRuntimeBacked = false;
    bool managedStorage = false;
};

/// Backend-private inspection used by Metal command lowering. The opaque
/// payload never crosses the RHI boundary.
MetalRhiNativeStagingBufferInfo inspectMetalRhiNativeStagingBuffer(
    const std::shared_ptr<void>& payload);

/// Publish CPU writes to a managed staging buffer before a blit encoder reads
/// it. Shared staging buffers require no explicit flush.
bool prepareMetalRhiNativeStagingBufferForGpu(const std::shared_ptr<void>& payload,
    std::uint64_t offsetBytes,
    std::uint64_t sizeBytes,
    std::pmr::string& diagnostics);

class MetalRhiStagingUploadPool {
public:
    /// Construct with an explicit CPU memory resource. When omitted, the
    /// process-wide default resource is used. Threaded through to the pmr
    /// members so a backend can bind a per-instance intent/budget (e.g.
    /// RhiHostStaging) instead of silently falling back to the global
    /// default.
    explicit MetalRhiStagingUploadPool(MetalRhiStagingUploadPoolDesc desc,
        std::pmr::memory_resource* memoryResource = &core::defaultCpuMemoryResource());

    MetalRhiStagingUploadSlice allocate(const MetalRhiStagingUploadRequest& request);
    std::uint64_t beginSubmission();
    // Legacy telemetry only; never completion authority. Exact reservation
    // tokens belong to the original command, not this public counter.
    std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionSerial() const;
    // Retry telemetry only. Even a forged epoch never authorizes storage reuse.
    // The native handler publishes after resolving its exact reservations.
    std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionEpoch() const;
    bool rollback(const MetalRhiStagingUploadSlice& slice);
    bool cancel(const MetalRhiStagingUploadSlice& slice);
    bool cancel(const rhi::RecordedStagingUploadSlice& slice);
    // Legacy serial retirement has no original command proof and always rejects.
    void retireCompletedFrames(std::uint64_t completedFrameSerial);
    bool enlist(
        rhi::RecordedStagingUploadJournal& journal, const rhi::RecordedStagingUploadSlice& slice);
    MetalRhiStagingUploadPoolStats stats() const;

private:
    struct Page {
        void* mappedData = nullptr;
        std::shared_ptr<void> payload;
    };

    MetalRhiStagingUploadSlice fail(const char* diagnostics, const char* diagnosticId = nullptr);
    bool createPage(std::uint32_t pageIndex, std::uint64_t capacityBytes);
    MetalRhiStagingUploadPoolStats buildStats() const;

    MetalRhiStagingUploadPoolDesc desc_;
    std::pmr::memory_resource* memoryResource_;
    mutable core::CompletionPageSliceAllocator slices_;
    core::pmr::Vector<Page> pages_;
    std::pmr::string lastDiagnostics_;
    std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionSerial_;
    std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionEpoch_;
    core::MonotonicIdSequence submissionIdentities_;
};

} // namespace arp::rhi_metal
