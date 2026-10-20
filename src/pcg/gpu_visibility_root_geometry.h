#pragma once
#include "pcg/gpu_visibility_indirect_fetch.h"
#include "pcg/gpu_visibility_root_consumer.h"
#include "pcg/gpu_visibility_root_inputs.h"
#include "pcg/gpu_visibility_root_readback.h"
#include "rhi/frontend/visibility/gpu_visibility_root_geometry.h"

namespace arp::pcg {
// Compatibility names for the shared closed shader ABI and cold validators.
using rhi::visibility::GpuVisibilityRootGeometryContract;
} // namespace arp::pcg
