#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_device.h"

#include <cstdint>

namespace arp::tools::render_feature_lab {

struct ForwardPlusFixtureResult {
    bool success = false;
    std::uint32_t graphPassCount = 0u;
    bool depthToCullDependency = false;
    bool uploadToCullDependency = false;
    float opaqueCpuGpuMatch = 0.0f;
    float conservativeCpuGpuMatch = 0.0f;
    bool tileReadbackAvailable = false;
    std::uint64_t gpuMemoryBytes = 0u;
    core::pmr::String diagnostic;
};

ForwardPlusFixtureResult runForwardPlusFeatureFixture(std::uint32_t localLightCount);
ForwardPlusFixtureResult runForwardPlusFeatureFixture(
    rhi::GpuDevice& device, rhi::Extent2D extent, std::uint32_t localLightCount);

} // namespace arp::tools::render_feature_lab
