#pragma once

#include "core/diagnostics/cpu_work_measurement.h"
#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::core {

struct TimingSample {
    pmr::String label;
    double milliseconds = 0.0;
};

struct MemoryBudgetReport {
    uint64_t buffersBytes = 0;
    uint64_t texturesBytes = 0;
    uint64_t transientRenderTargetsBytes = 0;
    uint64_t descriptorPoolBytes = 0;
    uint64_t pipelineCacheBytes = 0;
};

struct FrameTelemetry {
    pmr::Vector<TimingSample> cpuTimings;
    pmr::Vector<TimingSample> gpuTimings;
    MemoryBudgetReport memory;
    pmr::Vector<pmr::String> captureLabels;

    void addCpuTiming(const pmr::String& label, double milliseconds)
    {
        cpuTimings.push_back(TimingSample{label, milliseconds});
    }

    void addGpuTiming(const pmr::String& label, double milliseconds)
    {
        gpuTimings.push_back(TimingSample{label, milliseconds});
    }

    uint64_t totalMemoryBytes() const
    {
        return memory.buffersBytes + memory.texturesBytes + memory.transientRenderTargetsBytes
            + memory.descriptorPoolBytes + memory.pipelineCacheBytes;
    }
};

struct MobilePerformanceBudget {
    double cpuFrameTimeMs = 16.7;
    uint64_t transientRenderTargetsBytes = 0;
    uint64_t texturesBytes = 0;
};

struct PerformanceBudgetEvaluation {
    bool cpuFrameTimeWarning = false;
    bool textureMemoryWarning = false;
    bool transientMemoryWarning = false;
    pmr::String diagnostics;
};

inline PerformanceBudgetEvaluation evaluatePerformanceBudget(
    const FrameTelemetry& frame, const MobilePerformanceBudget& budget)
{
    PerformanceBudgetEvaluation result;
    for (const auto& timing : frame.cpuTimings) {
        if (timing.milliseconds > budget.cpuFrameTimeMs) {
            result.cpuFrameTimeWarning = true;
        }
    }
    result.textureMemoryWarning =
        budget.texturesBytes > 0 && frame.memory.texturesBytes > budget.texturesBytes;
    result.transientMemoryWarning = budget.transientRenderTargetsBytes > 0
        && frame.memory.transientRenderTargetsBytes > budget.transientRenderTargetsBytes;

    if (result.cpuFrameTimeWarning) {
        arp::core::appendDiagnostic(result.diagnostics, "CPU frame time over budget");
    }
    if (result.textureMemoryWarning) {
        arp::core::appendDiagnostic(result.diagnostics, "texture memory over budget");
    }
    if (result.transientMemoryWarning) {
        arp::core::appendDiagnostic(
            result.diagnostics, "transient render target memory over budget");
    }
    return result;
}

} // namespace arp::core
