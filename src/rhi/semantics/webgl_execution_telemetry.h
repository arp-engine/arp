#pragma once

#include "rhi/frontend/memory_budget_ledger.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/webgl_memory_budget.h"

#include <algorithm>

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

class WebGlExecutionTelemetry final {
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

    void add(std::uint64_t WebGlExecutionCounters::* field, std::uint64_t count = 1) noexcept
    {
        auto& value = counters_.*field;
        if (!core::MemoryBudgetLedger::canAdd(value, count)) {
            complete_ = false;
            return;
        }
        value += count;
    }

    void submission(WebGlSubmissionOutcome outcome, std::uint64_t operations) noexcept
    {
        add(&WebGlExecutionCounters::submitAttempts);
        add(outcome == WebGlSubmissionOutcome::Accepted   ? &WebGlExecutionCounters::accepted
                : outcome == WebGlSubmissionOutcome::Lost ? &WebGlExecutionCounters::lost
                                                          : &WebGlExecutionCounters::failed);
        add(&WebGlExecutionCounters::acceptedOperations, operations);
    }

    void observe(const WebGlMemoryCounters& live) noexcept
    {
        constexpr std::array fields{&WebGlMemoryCounters::cpuMetadataBytes,
            &WebGlMemoryCounters::cpuUploadBytes,
            &WebGlMemoryCounters::cpuReloadBytes,
            &WebGlMemoryCounters::logicalBufferBytes,
            &WebGlMemoryCounters::logicalTextureBytes,
            &WebGlMemoryCounters::objectCount,
            &WebGlMemoryCounters::commandCount,
            &WebGlMemoryCounters::referenceCount};
        for (auto field : fields)
            peak_.*field = std::max(peak_.*field, live.*field);
    }

    WebGlExecutionTelemetryReport report(const WebGlMemoryCounters& live) noexcept
    {
        observe(live);
        return {1,
            complete_,
            false,
            complete_ ? nullptr : "RHI-WEBGL1-EXECUTION-TELEMETRY-OVERFLOW",
            counters_,
            live,
            peak_};
    }

private:
    WebGlExecutionCounters counters_;
    WebGlMemoryCounters peak_;
    bool complete_ = true;
};
} // namespace arp::rhi::semantics
