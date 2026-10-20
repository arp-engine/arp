#pragma once

#include "rhi/gpu_common.h"

#include <cstdint>

namespace arp::rhi::semantics {

// OrderedResourceReuse/1 is a workload eligibility assessment. It is never a
// completion receipt, resource lease or authorization to release native owners.
struct OrderedResourceReuseIntent {
    std::uint32_t contractVersion = 0;
    bool sameContext = false;
    bool sameQueue = false;
    bool allRecordedReferencesIssued = false;
    bool cpuReadback = false;
    bool externalSharing = false;
    bool aliasHeap = false;
    bool publishesGpuCompletion = false;
    bool contextLost = false;
    std::uint64_t contextGeneration = 0;
    std::uint64_t resourceContextGeneration = 0;
};

struct OrderedResourceReuseDecision {
    bool orderingCompatible = false;
    bool validatorOnly = false;
    bool executable = false;
    bool provesGpuCompletion = false;
    const char* diagnostic = "RHI-ORDERED-REUSE-NATIVE-CUSTODY-UNAVAILABLE";
};

class OrderedResourceReuseContract final {
public:
    static OrderedResourceReuseDecision assess(
        Backend backend, const OrderedResourceReuseIntent& intent) noexcept
    {
        if (intent.contractVersion != 1u)
            return {false, false, false, false, "RHI-ORDERED-REUSE-VERSION"};
        if (intent.contextLost || !intent.contextGeneration
            || intent.contextGeneration != intent.resourceContextGeneration)
            return {false, false, false, false, "RHI-ORDERED-REUSE-STALE-CONTEXT"};
        if (!intent.sameContext || !intent.sameQueue || !intent.allRecordedReferencesIssued
            || intent.cpuReadback || intent.externalSharing || intent.aliasHeap
            || intent.publishesGpuCompletion)
            return {false, false, false, false, "RHI-ORDERED-REUSE-INTENT-UNSUPPORTED"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, false, "RHI-ORDERED-REUSE-VALIDATOR-ONLY"};
        case Backend::WebGL:
            return {true, false, false, false, "RHI-ORDERED-REUSE-NATIVE-CUSTODY-UNAVAILABLE"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, false, false, false, "RHI-ORDERED-REUSE-COMPLETION-PATH-REQUIRED"};
        }
        return {false, false, false, false, "RHI-ORDERED-REUSE-BACKEND-UNKNOWN"};
    }
};

} // namespace arp::rhi::semantics
