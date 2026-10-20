#pragma once

#include "core/cpu_allocator.h"
#include "core/page_slice_allocator.h"
#include "rhi/backends/webgpu/webgpu_allocator.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace arp::rhi_webgpu {
class WebGpuOriginalBufferUploadSource;

struct WebGpuRhiStagingUploadPoolDesc {
    std::pmr::string label;
    // Zero means no workload-owned staging budget was supplied.
    std::uint64_t byteCapacity = 0u;
    std::uint64_t alignmentBytes = 256;
    bool preferQueueWrite = true;
    std::uint32_t originalSourceContractVersion = 0u;
    bool originalSourcePrototypeOptIn = false;
    std::uint32_t maxOriginalSourceBuffers = 0u;
    std::uint64_t maxOriginalSourceGpuBytes = 0u;
};

struct WebGpuRhiStagingUploadRequest {
    std::pmr::string label;
    std::uint64_t sizeBytes = 0;
    std::uint64_t alignmentBytes = 256;
    std::uint64_t frameSerial = 0;
    std::uint32_t pageIndex = 0;
};

struct WebGpuRhiStagingUploadSlice {
    bool success = false;
    std::pmr::string diagnostics;
    std::pmr::string diagnosticId;
    std::uint64_t offsetBytes = 0;
    std::uint64_t sizeBytes = 0;
    std::uint64_t accountedBytes = 0;
    std::uint64_t frameSerial = 0;
    std::uint32_t pageIndex = 0;
    bool nativeHandleExposedToUpperLayer = false;
    bool queueWritePath = true;
    bool immediateQueueWrite = false;
};

struct WebGpuRhiStagingUploadPoolStats {
    std::uint32_t writeCount = 0;
    std::uint32_t liveSliceCount = 0;
    std::uint32_t retiredSliceCount = 0;
    std::uint32_t failedWriteCount = 0;
    std::uint64_t liveBytes = 0;
    std::uint64_t peakBytes = 0;
    std::uint64_t liveImmediateQueueWriteBytes = 0;
    std::uint64_t peakImmediateQueueWriteBytes = 0;
    std::uint32_t immediateQueueWriteCount = 0;
    bool boundedCapacity = true;
    bool nativeHandlesHiddenFromUpperLayer = true;
    bool queueWritePath = true;
};

class WebGpuRhiStagingUploadPool : public core::SharedObjectIdentity {
public:
    explicit WebGpuRhiStagingUploadPool(WebGpuRhiStagingUploadPoolDesc desc);

    WebGpuRhiStagingUploadSlice allocate(const WebGpuRhiStagingUploadRequest& request);
    bool cancel(const WebGpuRhiStagingUploadSlice& slice);
    bool cancel(const rhi::RecordedStagingUploadSlice& slice);
    // Queue-write uploads have no persistent GPU staging allocation. Reserve
    // the temporary CPU/JS copy explicitly so the immediate path still obeys
    // the backend upload budget, then release it after the queue API returns.
    WebGpuRhiStagingUploadSlice reserveImmediateQueueWrite(
        const WebGpuRhiStagingUploadRequest& request);
    void releaseImmediateQueueWrite(const WebGpuRhiStagingUploadSlice& slice);
    // Returns a monotonically increasing backend submission ticket. The
    // ticket is retired only after WebGPU reports queue completion.
    std::uint64_t beginSubmission();
    std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionSerial() const;

    bool prefersQueueWrite() const noexcept { return desc_.preferQueueWrite; }

    void retireCompletedFrames(std::uint64_t completedFrameSerial);
    WebGpuRhiStagingUploadPoolStats stats() const;

    std::uint32_t originalSourceCount() const
    {
        return originalSourceAllocator_.memoryStats().liveResourceCount;
    }

    rhi::MemoryStats originalSourceMemoryStats() const
    {
        return originalSourceAllocator_.memoryStats();
    }

private:
    friend class WebGpuOriginalBufferUploadSource;
    static std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment);
    WebGpuRhiStagingUploadSlice fail(const std::pmr::string& diagnostics, const char* diagnosticId);
    WebGpuRhiStagingUploadPoolStats buildStats() const;

    WebGpuRhiStagingUploadPoolDesc desc_;
    WebGpuRhiAllocator originalSourceAllocator_;
    core::FrameRetiredPageSliceAllocator slices_;
    std::shared_ptr<std::atomic<std::uint64_t>> completedSubmissionSerial_;
    std::atomic<std::uint64_t> nextSubmissionSerial_{0};
    std::uint32_t failedWriteCount_ = 0;
    std::uint64_t maxLiveSliceCount_ = 1;
    std::uint64_t immediateQueueWriteLiveBytes_ = 0;
    std::uint64_t peakImmediateQueueWriteBytes_ = 0;
    std::uint32_t immediateQueueWriteCount_ = 0;
    std::uint64_t peakLiveBytes_ = 0;
};

} // namespace arp::rhi_webgpu
