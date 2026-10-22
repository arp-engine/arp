#pragma once
#include "core/diagnostics/diagnostic_error.h"
#include "rhi/gpu_common.h"

#include <algorithm>
#include <cmath>

namespace arp::renderer {
struct RenderResolutionPolicy {
    float scale = 1.0f, minimum = 0.5f, maximum = 1.0f;
    float targetGpuMs = 14.0f, hysteresis = 0.1f, step = 0.05f;
    std::uint32_t interval = 60u;
    bool dynamic = false;

    void validate() const
    {
        if (!std::isfinite(scale) || !std::isfinite(minimum) || !std::isfinite(maximum)
            || minimum <= 0 || maximum > 1 || minimum > maximum || scale < minimum
            || scale > maximum || !std::isfinite(targetGpuMs) || targetGpuMs <= 0
            || !std::isfinite(hysteresis) || hysteresis < 0 || hysteresis >= 1
            || !std::isfinite(step) || step <= 0 || step > maximum || interval == 0)
            throw core::DiagnosticError("RENDER-RESOLUTION-POLICY-INVALID");
    }

    rhi::Extent2D extent(rhi::Extent2D display, float value) const
    {
        return {std::max(1u, static_cast<std::uint32_t>(std::round(display.width * value))),
            std::max(1u, static_cast<std::uint32_t>(std::round(display.height * value)))};
    }

    float resolve(float current, float gpuMs) const
    {
        if (!dynamic || !std::isfinite(gpuMs) || gpuMs <= 0)
            return current;
        if (gpuMs > targetGpuMs * (1 + hysteresis))
            return std::max(minimum, current - step);
        if (gpuMs < targetGpuMs * (1 - hysteresis))
            return std::min(maximum, current + step);
        return current;
    }
};
} // namespace arp::renderer
