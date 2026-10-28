#pragma once

#include "rhi/gpu_common.h"
#include "rhi/semantics/memory_admission_lowering.h"

#include <cstdint>

namespace arp::rhi {

enum class AllocatorBackingState { Unsupported, Observed, DiagnosticFailure };

// Cold diagnostic only; excludes direct heaps, other allocators and driver
// storage. No enforcement or full physical residency/peak claim is issued.
struct AllocatorBackingObservation {
    std::uint32_t contractVersion = 1u;
    AllocatorBackingState state = AllocatorBackingState::Unsupported;
    const char* diagnostic = "RHI-ALLOCATOR-BACKING-UNINSTRUMENTED";
    std::uint64_t backingBytes = 0u, peakBackingBytes = 0u;
    std::uint64_t blockCount = 0u, peakBlockCount = 0u;
    std::uint64_t occupiedBytes = 0u, allocationCount = 0u, unusedBytes = 0u;
    std::uint64_t allocationEvents = 0u, releaseEvents = 0u;
};

class AllocatorBackingContract final {
public:
    static AllocatorBackingObservation unavailable(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        AllocatorBackingObservation result;
        if (version != 1u) {
            result.state = AllocatorBackingState::DiagnosticFailure;
            result.diagnostic = "RHI-ALLOCATOR-BACKING-VERSION";
            return result;
        }
        if (const auto* diagnostic =
                semantics::MemoryAdmissionLowering::allocatorBackingDiagnostic(backend)) {
            result.diagnostic = diagnostic;
        } else {
            result.state = AllocatorBackingState::DiagnosticFailure;
            result.diagnostic = "RHI-ALLOCATOR-BACKING-BACKEND";
        }
        return result;
    }
};

} // namespace arp::rhi
