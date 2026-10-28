#pragma once

#include "rhi/frontend/memory_budget_ledger.h"
#include "rhi/semantics/webgl_execution_telemetry_values.h"

#include <algorithm>
#include <array>

namespace arp::rhi::semantics {

class WebGlExecutionTelemetry final {
public:
    static const char* backendDiagnostic(Backend backend) noexcept
    {
        return WebGlExecutionTelemetryLowering::backendDiagnostic(backend);
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
