#pragma once

#include "core/image/strided_image_statistics.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_device.h"

#include <cstdint>

namespace arp::rhi {
class Swapchain;
}

namespace arp::tests::postprocess {

void setDdgiParityModeOverride(std::uint32_t mode);

struct FeatureLabAttachmentMetric {
    core::pmr::String attachment;
    core::pmr::String metric;
    float value = 0.0f;
    bool available = false;
};

// Explicit tooling-only recording faults; production callers use None.
enum class DdgiFixtureFault { None, AfterHistoryCopy, AfterResolve };

struct DdgiFrameEvidence {
    // GI, irradiance, distance: completed native all-channel statistics.
    std::array<core::ImageChannelStatistics, 3> channels;
    std::array<std::uint64_t, 3> hashes{};
    std::uint32_t newlyExposedProbes = 0u;
    int readSlot = 0;
};

struct PostProcessFixtureResult {
    bool success = false;
    core::pmr::Vector<core::pmr::String> passTrace;
    core::pmr::Vector<FeatureLabAttachmentMetric> attachmentMetrics;
    std::uint32_t graphPassCount = 0u;
    std::uint32_t submittedFrameCount = 0u;
    std::uint64_t gpuMemoryBytes = 0u;
    // For multi-frame DDGI captures this is the maximum compiled-topology
    // reuse sample; one-frame captures fall back to their cold-start sample.
    double cpuMilliseconds = 0.0;
    core::pmr::String cpuTimingScope;
    // Optional multi-frame DDGI readback capture. These fields are populated
    // only by the DDGI fixture; other feature fixtures leave them at zero.
    core::pmr::String ddgiCaptureScenario;
    std::uint32_t ddgiReadbackFrames = 0u;
    std::uint32_t ddgiFiniteReadbackFrames = 0u;
    std::uint32_t ddgiChangedHashFrames = 0u;
    std::uint32_t ddgiMaximumNewlyExposedProbes = 0u;
    double ddgiFirstMean = 0.0;
    double ddgiLastMean = 0.0;
    double ddgiMaximumMeanDelta = 0.0;
    double ddgiLightingConvergence8 = 0.0;
    double ddgiLightingConvergence30 = 0.0;
    bool ddgiLightingConvergenceAvailable = false;
    bool ddgiReadbackAvailable = false;
    std::uint32_t ddgiRejectedFrames = 0u;
    core::pmr::Vector<DdgiFrameEvidence> ddgiFrameEvidence;
    std::uint32_t ddgiParityMode = 0u;
    double ddgiParityCpuExpected = 0.0;
    double ddgiParityGpuMeasured = 0.0;
    double ddgiParityAbsoluteError = 0.0;
    bool ddgiParityAvailable = false;
    bool ddgiParityPassed = false;
    bool ddgiPresentationAttempted = false;
    bool ddgiPresentationSucceeded = false;
    bool ddgiPresentationReadbackAvailable = false;
    bool ddgiPresentationReadbackFinite = false;
    double ddgiPresentationReadbackMean = 0.0;
    std::uint64_t ddgiPresentationReadbackHash = 0u;
    core::pmr::String diagnostic;
};

PostProcessFixtureResult runSsaoFeatureFixture(rhi::Extent2D extent);
PostProcessFixtureResult runTaaFeatureFixture(rhi::Extent2D extent);
PostProcessFixtureResult runSsaoFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent);
PostProcessFixtureResult runTaaFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent);

// Exercises the production WoCC post chain (bloom high-pass/blur/composite,
// ACES tonemap, grade, and the engine FXAA renderer) with deterministic HDR
// fixture data.  The fixture is intentionally backend-neutral; only the
// shader artifacts and RHI lowering vary by backend.
PostProcessFixtureResult runPostFeatureFixture(rhi::Extent2D extent);
PostProcessFixtureResult runPostFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent);

// Exercises the production SSR trace/composite shader pair with a synthetic
// depth plane and known color pattern.  The hit metric is derived from the
// reflection attachment readback, never from a CPU replay of the algorithm.
PostProcessFixtureResult runSsrFeatureFixture(rhi::Extent2D extent);
PostProcessFixtureResult runSsrFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent);

// Uses the engine-owned DDGI renderer with a minimal probe grid and one
// synthetic triangle. It validates creation, persistent ping-pong resources,
// and the production BVH upload contract without requiring WoCC assets.
PostProcessFixtureResult runDdgiFeatureFixture(rhi::Extent2D extent);
PostProcessFixtureResult runDdgiFeatureFixture(rhi::GpuDevice& device, rhi::Extent2D extent);
PostProcessFixtureResult runDdgiFeatureFixtureFrames(rhi::GpuDevice& device,
    rhi::Extent2D extent,
    std::uint32_t frameCount,
    rhi::Swapchain* presentationSwapchain = nullptr,
    DdgiFixtureFault recordingFault = DdgiFixtureFault::None);
PostProcessFixtureResult runDdgiFeatureFixtureFrames(
    rhi::Extent2D extent, std::uint32_t frameCount);

// Minimal deterministic attachment fixtures used by the first cumulative
// levels.  They validate the production RHI upload/readback and attachment
// contracts while the full scene/PBR/shadow feature is still being migrated.
PostProcessFixtureResult runBasicAttachmentFixture(rhi::Extent2D extent, const char* attachment);
PostProcessFixtureResult runBasicAttachmentFixture(
    rhi::GpuDevice& device, rhi::Extent2D extent, const char* attachment);

} // namespace arp::tests::postprocess
