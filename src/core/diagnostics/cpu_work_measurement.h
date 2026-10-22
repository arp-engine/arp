#pragma once

#include "core/memory/cpu_allocator.h"

#include <chrono>
#include <cstdint>

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

} // namespace arp::core
