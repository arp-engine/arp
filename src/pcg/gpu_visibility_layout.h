#pragma once
#include "pcg/foliage_instance_batch.h"
#include "rhi/frontend/visibility/gpu_visibility_layout.h"

namespace arp::pcg {
// Compatibility names for the shared closed shader ABI and cold validators.
using rhi::visibility::GpuVisibilityDispatchContract;
using rhi::visibility::GpuVisibilityDrawGroup;
using rhi::visibility::GpuVisibilityLayoutContract;
using rhi::visibility::GpuVisibilityOutputRanges;
using rhi::visibility::GpuVisibilityParameters;
} // namespace arp::pcg
