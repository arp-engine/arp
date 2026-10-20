#pragma once
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/semantics/original_query_source_lowering.h"

namespace arp::rhi_webgpu {
class WebGpuOriginalQuerySourceGate final {
public:
    using Lowering = rhi::semantics::OriginalQuerySourceLowering;

    static Lowering::Decision decideOriginal(std::uint32_t version,
        bool optIn,
        const std::optional<rhi::QueryPoolOriginalInputArguments>& input) noexcept
    {
        return Lowering::decide(
            rhi::Backend::WebGPU, version, optIn, input, webGpuNativeTimestampCommandPointFacts());
    }

    static const char* admissionDiagnostic(const Lowering::Decision& decision) noexcept
    {
        if (webGpuNativeDeviceLost())
            return "RHI-WEBGPU-ORIGINAL-QUERY-DEVICE-LOST";
        // No source payload exists. API support and CPU declaration never open
        // native allocation or Query references through this gate.
        return decision.diagnostic();
    }
};
} // namespace arp::rhi_webgpu
