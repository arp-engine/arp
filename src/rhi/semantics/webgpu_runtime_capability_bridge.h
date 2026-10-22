#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

#include <string>

namespace arp::rhi::semantics {

struct WebGpuRuntimeCapabilityBridgeResult {
    bool success = false;
    NativeBackendCapabilityContract contract;
    std::pmr::string diagnostics;
};

WebGpuRuntimeCapabilityBridgeResult buildWebGpuRuntimeCapabilityContract(
    const rhi::WebGpuRuntimeProfile& profile);

} // namespace arp::rhi::semantics
