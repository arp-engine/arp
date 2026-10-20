#pragma once

#include "rhi/semantics/webgl_runtime_profile.h"

namespace arp::test {

// Shared, explicitly synthetic facts. Never a production capability source.
class WebGlProfileProbeFixture final {
public:
    static rhi::semantics::WebGlRuntimeProbe baseline()
    {
        using namespace rhi::semantics;
        WebGlRuntimeProbe probe;
        probe.contractVersion = kWebGl1GraphicsContractVersion;
        probe.probeVersion = kWebGlRuntimeProbeVersion;
        probe.contextAvailable = true;
        probe.contextMajorVersion = 1u;
        probe.limitsQueried = probe.precisionQueried = probe.extensionsQueried = true;
        probe.contextAttributesQueried = true;
        probe.limits = {8u, 128u, 16u, 8u, 0u, 8u, 8u, 64u, 16u, 64u, 64u, 64u};
        probe.precision.vertex = {{{true, 8, 8, 8},
            {true, 14, 14, 10},
            {true, 127, 127, 23},
            {true, 8, 8, 0},
            {true, 10, 10, 0},
            {true, 31, 30, 0}}};
        probe.precision.fragment = probe.precision.vertex;
        probe.precision.fragment[static_cast<std::size_t>(WebGlNumericPrecision::HighFloat)] = {
            true, 0, 0, 0};
        probe.formats = {1u,
            WebGlFormatQueryOutcome::Observed,
            true,
            WebGlFormatQueryOutcome::Observed,
            true,
            WebGlFormatQueryOutcome::Observed,
            24,
            8,
            true};
        probe.identity.queried.fill(true);
        probe.identity.values = {"WebGL 1.0",
            "WebGL GLSL ES 1.0",
            "Synthetic vendor",
            "Synthetic renderer",
            "Synthetic test browser"};
        return probe;
    }
};

} // namespace arp::test
