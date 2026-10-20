#include "core/cpu_allocator.h"
#include "core/performance_telemetry.h"

#include <cassert>
#include <cmath>
#include <string>

int main()
{
    using namespace arp::core;

    struct Observer final : CpuWorkMeasurementObserver {
        unsigned starts = 0, ends = 0;

        void begin() noexcept override { ++starts; }

        void end() noexcept override { ++ends; }
    } observer;

    CpuWorkMeasurement disabled;
    {
        CpuWorkMeasurementScope scope(nullptr, &observer);
    }
    assert(!disabled.observed);
    assert(observer.starts == 0 && observer.ends == 0);
    CpuWorkMeasurement measured;
    const auto baseline = defaultCpuAllocator().stats();
    {
        CpuWorkMeasurementScope scope(&measured, &observer);
        {
            pmr::Vector<std::uint8_t> bytes{&defaultCpuMemoryResource()};
            bytes.resize(4096);
        }
        scope.finish();
        const auto first = measured;
        scope.finish();
        assert(first.allocations == measured.allocations);
    }
    assert(measured.observed && std::isfinite(measured.milliseconds) && measured.milliseconds >= 0);
    assert(observer.starts == 1 && observer.ends == 1);
    assert(measured.allocations == 1 && measured.deallocations == 1);
    assert(measured.liveBytesBefore == baseline.liveBytes);
    assert(measured.liveBytesAfter == baseline.liveBytes);
    assert(measured.processPeakBytes >= baseline.liveBytes + 4096);
    CpuWorkMeasurement unwound;
    try {
        CpuWorkMeasurementScope scope(&unwound);
        throw 1;
    } catch (int) {
    }
    assert(unwound.observed);

    auto frame = FrameTelemetry{};
    frame.addCpuTiming("Frame", 12.5);
    frame.addGpuTiming("Opaque", 3.25);
    frame.memory.buffersBytes = 1024;
    frame.memory.texturesBytes = 4096;
    frame.memory.transientRenderTargetsBytes = 2048;
    frame.memory.descriptorPoolBytes = 512;
    frame.memory.pipelineCacheBytes = 256;
    frame.captureLabels.push_back("FrameGraph/Opaque");

    assert(frame.totalMemoryBytes() == 7936);
    assert(frame.cpuTimings[0].label == std::pmr::string("Frame"));
    assert(frame.gpuTimings[0].milliseconds == 3.25);

    const auto budget = MobilePerformanceBudget{16.7, 4 * 1024, 8 * 1024};
    const auto report = evaluatePerformanceBudget(frame, budget);
    assert(report.cpuFrameTimeWarning == false);
    assert(report.textureMemoryWarning == false);
    assert(report.transientMemoryWarning == false);

    frame.addCpuTiming("LateFrame", 20.0);
    frame.memory.texturesBytes = 16 * 1024;
    const auto warning = evaluatePerformanceBudget(frame, budget);
    assert(warning.cpuFrameTimeWarning);
    assert(warning.textureMemoryWarning);
    assert(warning.diagnostics.find("CPU") != std::pmr::string::npos);

    return 0;
}
