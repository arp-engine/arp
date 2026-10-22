#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/frame_resources.h"
#include "rhi/gpu_device.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::rhi {

struct NullHiddenFrameSmokeConfig {
    std::pmr::string surfaceName = "NullHiddenFrameSmoke";
    Extent2D extent{320, 180};
    TextureFormat format = TextureFormat::Bgra8Unorm;
    std::uint32_t frameCount = 2;
};

struct NullHiddenFrameSmokeFrameReport {
    std::uint32_t frameSlot = 0;
    std::uint64_t frameSerial = 0;
    std::uint32_t submittedCommandCount = 0;
    bool presentSucceeded = false;
    std::pmr::string diagnostics;
    FrameResourceOperationResult releaseStats;
};

struct NullHiddenFrameSmokeReport {
    bool success = false;
    std::pmr::string diagnostics;
    Backend backend = Backend::Null;
    std::uint32_t swapchainImageCount = 0;
    std::uint32_t maxFramesInFlight = 0;
    std::uint32_t submittedFrameCount = 0;
    std::uint32_t totalSubmittedCommandCount = 0;
    bool descriptorCompletenessValidated = false;
    std::pmr::vector<NullHiddenFrameSmokeFrameReport> frames;
    FrameResourceRecyclerStats resourceStats;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
};

class NullHiddenFrameSmokeRunner final {
public:
    explicit NullHiddenFrameSmokeRunner(GpuDevice& device);

    NullHiddenFrameSmokeReport run(const NullHiddenFrameSmokeConfig& config);

private:
    GpuDevice& device_;
};

} // namespace arp::rhi
