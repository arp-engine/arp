#pragma once
#include "pcg/gpu_visibility_root_producer.h"
#include "rhi/frontend/visibility/gpu_visibility_root_consumer.h"

namespace arp::pcg {
// Compatibility names for the shared closed shader ABI and cold validators.
using rhi::visibility::GpuVisibilityDrawSelection;
using rhi::visibility::GpuVisibilityRootConsumerContract;
} // namespace arp::pcg
