#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {

// WebTimestampCommandPointGate/1. Actual queried API facts, never OS/backend
// names or timestamp-query feature alone. This gate creates no native payload.
struct WebGpuTimestampCommandPointFacts {
    bool deviceFeatureEnabled = false;
    bool encoderWrite = false;
    bool renderPassWrite = false;
    bool computePassWrite = false;
    bool resolve = false;
    bool bufferCopy = false;
    bool mapRead = false;
    bool mappedRead = false;
    bool unmap = false;
};

struct WebGpuTimestampCommandPointDecision {
    CapabilityMode mode = CapabilityMode::Degraded;
    bool apiSupported = false;
    const char* diagnosticId = "RHI-WEBGPU-TIMESTAMP-FEATURE-UNAVAILABLE";
};

class WebGpuTimestampCommandPointGate final {
public:
    static WebGpuTimestampCommandPointDecision decide(
        const WebGpuTimestampCommandPointFacts& facts) noexcept
    {
        if (!facts.deviceFeatureEnabled)
            return {};
        if (!facts.encoderWrite || !facts.renderPassWrite || !facts.computePassWrite)
            return {
                CapabilityMode::Degraded, false, "RHI-WEBGPU-TIMESTAMP-COMMAND-API-UNAVAILABLE"};
        if (!facts.resolve || !facts.bufferCopy || !facts.mapRead || !facts.mappedRead
            || !facts.unmap)
            return {
                CapabilityMode::Degraded, false, "RHI-WEBGPU-TIMESTAMP-READBACK-API-UNAVAILABLE"};
        // Native query/resolve/readback preparation is an additional mandatory
        // gate. Native API support alone never marks a query Available.
        return {CapabilityMode::Native, true, nullptr};
    }
};

} // namespace arp::rhi::semantics
