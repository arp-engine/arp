#pragma once

#include "rhi/gpu_common.h"

#include <cstdint>

namespace arp::rhi {

// Explicit diagnostic/validation profile. It may block at finish/submit and
// allocate recording scratch; it never promotes ordinary recording or NativeSync.
struct PrototypeCommandBatchBudget {
    std::uint32_t contractVersion = 0;
    bool optIn = false;
    std::uint32_t maxCommands = 0;
    std::uint64_t maxCpuBytes = 0;
    std::uint64_t maxRetainedGpuBytes = 0;
    std::uint64_t timeoutNanoseconds = 0;
};

class PrototypeCommandBatchContract final {
public:
    static constexpr std::uint32_t version = 2;

    struct Decision {
        bool executable = false;
        PrototypeCommandBatchMode mode = PrototypeCommandBatchMode::DiagnosticFailure;
        PrototypeCommandBatchBudget budget;
        const char* diagnostic = "RHI-PROTOTYPE-BATCH-UNSUPPORTED";
    };

    static const char* validate(const PrototypeCommandBatchBudget& b) noexcept
    {
        if (b.contractVersion != version)
            return "RHI-PROTOTYPE-BATCH-VERSION";
        if (!b.optIn)
            return "RHI-PROTOTYPE-BATCH-OPT-IN";
        if (!b.maxCommands || !b.maxCpuBytes || !b.maxRetainedGpuBytes || !b.timeoutNanoseconds
            || b.timeoutNanoseconds > INT64_MAX)
            return "RHI-PROTOTYPE-BATCH-BUDGET";
        return nullptr;
    }

    // Versioned backend contract. This profile currently has only the WebGPU
    // implementation; other native recording profiles are separate contracts.
    static Decision lowering(Backend backend,
        const PrototypeCommandBatchBudget& budget,
        const PrototypeCommandBatchCapabilities& caps) noexcept
    {
        Decision result;
        result.budget = budget;
        if (const auto* id = validate(budget)) {
            result.diagnostic = id;
            return result;
        }
        // This is the versioned implementation mapping, not a capability query.
        // WebGPU still needs independently published native execution facts.
        switch (backend) {
        case Backend::WebGPU:
            break;
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGL:
        default:
            return result;
        }
        if (caps.contractVersion != version || caps.mode != PrototypeCommandBatchMode::Prototype)
            return result;
        if (!caps.boundedRecording || !caps.nativeCompletion || !caps.passBoundaryOrdering
            || !caps.acquiredImageLeaseValidation) {
            result.diagnostic = "RHI-PROTOTYPE-BATCH-CAPABILITY-INCOMPLETE";
            return result;
        }
        result.executable = true;
        result.mode = PrototypeCommandBatchMode::Prototype;
        result.diagnostic = nullptr;
        return result;
    }
};
} // namespace arp::rhi
