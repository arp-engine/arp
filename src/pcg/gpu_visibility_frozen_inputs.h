#pragma once
#include "pcg/gpu_visibility_layout.h"
#include "rhi/frontend/visibility/gpu_visibility_frozen_inputs.h"

namespace arp::pcg {
// Compatibility names for the shared closed shader ABI and cold validators.
using rhi::visibility::GpuVisibilityFrozenInputContract;
} // namespace arp::pcg
