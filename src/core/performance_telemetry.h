#pragma once

#include "core/cpu_allocator.h"
#include "core/diagnostic_builder.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::core {

// Fixed-size observation only. The peak is the allocator's process-lifetime
// high-water mark, not a peak attributed to this scope or GPU allocation.
struct CpuWorkMeasurement {
    double milliseconds = 0;
    std::uint64_t allocations = 0, deallocations = 0;
    std::uint64_t liveBytesBefore = 0, liveBytesAfter = 0, processPeakBytes = 0;
    bool observed = false;
};

class CpuWorkMeasurementObserver {
public:
    virtual ~CpuWorkMeasurementObserver() = default;
    virtual void begin() noexcept = 0;
    virtual void end() noexcept = 0;
};

class CpuWorkMeasurementScope final {
public:
    explicit CpuWorkMeasurementScope(
        CpuWorkMeasurement* output, CpuWorkMeasurementObserver* observer = nullptr) noexcept
        : output_(output), observer_(observer)
    {
        if (output_) {
            *output_ = {};
            if (observer_)
                observer_->begin();
            before_ = defaultCpuAllocator().stats();
            started_ = std::chrono::steady_clock::now();
        }
    }

    ~CpuWorkMeasurementScope() { finish(); }

    CpuWorkMeasurementScope(const CpuWorkMeasurementScope&) = delete;
    CpuWorkMeasurementScope& operator=(const CpuWorkMeasurementScope&) = delete;

    void finish() noexcept
    {
        if (!output_)
            return;
        const auto ended = std::chrono::steady_clock::now();
        const auto after = defaultCpuAllocator().stats();
        *output_ = {std::chrono::duration<double, std::milli>(ended - started_).count(),
            after.allocationCount - before_.allocationCount,
            after.deallocationCount - before_.deallocationCount,
            before_.liveBytes,
            after.liveBytes,
            after.peakBytes,
            true};
        if (observer_)
            observer_->end();
        output_ = nullptr;
    }

private:
    CpuWorkMeasurement* output_;
    CpuWorkMeasurementObserver* observer_;
    CpuMemoryStats before_;
    std::chrono::steady_clock::time_point started_;
};

struct TimingSample {
    std::pmr::string label;
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
    std::pmr::vector<TimingSample> cpuTimings;
    std::pmr::vector<TimingSample> gpuTimings;
    MemoryBudgetReport memory;
    std::pmr::vector<std::pmr::string> captureLabels;

    void addCpuTiming(const std::pmr::string& label, double milliseconds)
    {
        cpuTimings.push_back(TimingSample{label, milliseconds});
    }

    void addGpuTiming(const std::pmr::string& label, double milliseconds)
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
    std::pmr::string diagnostics;
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
