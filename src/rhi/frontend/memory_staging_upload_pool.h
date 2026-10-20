#pragma once

// Shared helpers for RHI backend staging upload pools.
//
// The paged staging upload pools (Vulkan and Metal) are thin shells over
// core::FrameRetiredPageSliceAllocator, which already owns the ring buffer,
// suballocation and frame-fence retirement. The shells previously duplicated
// the same boilerplate verbatim across vulkan_staging_upload_pool.cpp and
// metal_staging_upload_pool.{cpp,mm}:
//   - alignment == 0 normalization (in constructor)
//   - per-request alignment resolution (request.alignment ?? desc.alignment)
//   - createPage index/capacity invariant check
//   - mappedData pointer advancement (page.mappedData + offset)
//   - common stats field mapping (8 fields from core::FrameRetiredPageSliceAllocatorStats)
// This header consolidates those pure functions so both backends share a single
// validated source. WebGPU's staging pool uses a different single-page ring
// model with queue.writeBuffer semantics and different stats field names, so it
// does not use these helpers (its buildStats/fail/allocate stay backend-specific).
//
// Layering: header-only, namespace arp::rhi, no native API dependency. The
// helpers operate on POD scalars and core types only, so they compile in every
// backend translation unit (including Metal stubs on non-Apple platforms).

#include "core/cpu_allocator.h"
#include "core/number_format.h"
#include "core/page_slice_allocator.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace arp::rhi {

// Normalizes a zero alignment to 1 (the smallest legal suballocation alignment
// for core::FrameRetiredPageSliceAllocator). Applied to the pool desc at
// construction time. Previously inlined in both vulkan and metal constructors.
inline std::uint64_t normalizeStagingAlignment(std::uint64_t alignment) noexcept
{
    return alignment == 0 ? 1 : alignment;
}

// Resolves the effective per-request alignment: request overrides desc when
// non-zero, otherwise desc wins. Previously duplicated as a ternary in both
// vulkan and metal allocate() bodies.
inline std::uint64_t resolveStagingAlignment(
    std::uint64_t requestAlignment, std::uint64_t descAlignment) noexcept
{
    return requestAlignment != 0 ? requestAlignment : descAlignment;
}

// createPage invariant: a new page may only be appended at the end of the
// pages vector and must have non-zero capacity. Returns true if the caller
// should proceed with the native allocation. Previously duplicated as
// `if (pageIndex != pages_.size() || capacityBytes == 0) return false;` in
// both vulkan and metal createPage().
inline bool stagingCreatePageInvariant(
    std::uint32_t pageIndex, std::size_t pageCount, std::uint64_t capacityBytes) noexcept
{
    return pageIndex == pageCount && capacityBytes != 0;
}

// Advances a page's mapped base pointer by the slice offset, returning nullptr
// if the page is not CPU-mapped. Previously duplicated verbatim in both
// vulkan and metal allocate() as the mappedData assignment expression.
inline void* stagingMappedDataAt(void* pageMappedData, std::uint64_t offsetBytes) noexcept
{
    return pageMappedData
        ? static_cast<void*>(static_cast<unsigned char*>(pageMappedData) + offsetBytes)
        : nullptr;
}

// Maps the common subset of core::FrameRetiredPageSliceAllocatorStats onto a
// backend stats struct. The caller fills backend-specific fields
// (nativeHandlesHiddenFromUpperLayer, Metal's sharedOrManagedUploadMemory,
// etc.) after this call. The 6 fields mapped here (allocatedPageCount,
// liveSliceCount, retiredSliceCount, failedAllocationCount, allocatedBytes,
// boundedCapacity) plus peakAllocatedBytes = max(allocatedBytes, peakLiveBytes)
// were previously copied field-by-field in both vulkan and metal buildStats().
template <typename BackendStats>
inline void fillStagingCommonStats(
    const arp::core::FrameRetiredPageSliceAllocatorStats& coreStats, BackendStats& out)
{
    out.allocatedPageCount = coreStats.allocatedPageCount;
    out.liveSliceCount = coreStats.liveSliceCount;
    out.retiredSliceCount = coreStats.retiredSliceCount;
    out.failedAllocationCount = coreStats.failedAllocationCount;
    out.allocatedBytes = coreStats.allocatedBytes;
    out.peakAllocatedBytes = coreStats.allocatedBytes > coreStats.peakLiveBytes
        ? coreStats.allocatedBytes
        : coreStats.peakLiveBytes;
    out.boundedCapacity = coreStats.boundedCapacity;
}

template <typename Integer>
inline void appendStagingUnsignedField(
    std::pmr::string& output, std::string_view name, Integer value)
{
    output.append(" ");
    output.append(name.data(), name.size());
    output.append("=");
    arp::core::appendUnsigned(output, value);
}

// Adds the L2 allocation facts to a backend diagnostic without exposing a
// native page or allocation handle. The label is borrowed only for the call;
// the resulting diagnostic owns its bytes in the caller's pmr string.
inline void appendStagingFailureTelemetry(std::pmr::string& output,
    std::string_view baseDiagnostic,
    const char* diagnosticId,
    std::string_view uploadLabel,
    const arp::core::FrameRetiredPageSliceAllocatorStats& stats)
{
    output.clear();
    output.append(baseDiagnostic.data(), baseDiagnostic.size());
    output.append(" [diagnosticId=");
    const auto* stableId = diagnosticId != nullptr ? diagnosticId : "ARP-RHI-STAGING-UNKNOWN";
    output.append(stableId);
    output.append(" uploadLabel=");
    if (uploadLabel.empty()) {
        output.append("<unnamed>");
    } else {
        output.append(uploadLabel.data(), uploadLabel.size());
    }
    appendStagingUnsignedField(output, "requestBytes", stats.lastRequestBytes);
    appendStagingUnsignedField(output, "pageSizeBytes", stats.lastPageSizeBytes);
    appendStagingUnsignedField(output, "allocatedBytes", stats.lastAllocatedBytes);
    appendStagingUnsignedField(output, "frameSerial", stats.lastRequestFrameSerial);
    appendStagingUnsignedField(output, "alignmentBytes", stats.lastRequestAlignmentBytes);
    appendStagingUnsignedField(output, "pageCount", stats.lastAllocatedPageCount);
    appendStagingUnsignedField(output, "maxPageCount", stats.lastMaxPageCount);
    output.append("]");
}

} // namespace arp::rhi
