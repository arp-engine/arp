#pragma once

#include "rhi/gpu_common.h"
#include "rhi/semantics/webgl_memory_budget.h"

namespace arp::rhi::semantics {
enum class WebGlSubmissionOutcome { Accepted, Failed, Lost };

struct WebGlExecutionCounters {
    std::uint64_t submitAttempts = 0, accepted = 0, failed = 0, lost = 0;
    std::uint64_t acceptedOperations = 0, nativeCalls = 0, draws = 0;
    std::uint64_t uniformCalls = 0, uniformBytes = 0;
    std::uint64_t bufferUpdateCalls = 0, bufferUpdateBytes = 0;
    std::uint64_t textureUpdateCalls = 0, textureUpdateBytes = 0;
};

struct WebGlExecutionTelemetryReport {
    std::uint32_t contractVersion = 1;
    bool complete = true, gpuCompletionKnown = false;
    const char* diagnostic = nullptr;
    WebGlExecutionCounters execution;
    WebGlMemoryCounters live, peak;
};

class WebGlExecutionTelemetryLowering final {
public:
    static const char* backendDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-EXECUTION-TELEMETRY-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-EXECUTION-TELEMETRY-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-EXECUTION-TELEMETRY-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
