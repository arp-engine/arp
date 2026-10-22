#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "rhi/conformance/original_output.h"

namespace arp::rhi::webgl::test {
EM_JS(int, conformanceObservationFault, (), {
    // clang-format off
    return Number(new URLSearchParams(location.search).get('conformanceFault') || 0);
    // clang-format on
});

EM_JS(int, conformanceLoseOriginalContext, (), {
    // clang-format off
    const gl = GL.currentContext.GLctx;
    const extension = gl.getExtension('WEBGL_lose_context');
    if (!extension) return 0;
    extension.loseContext();
    return gl.isContextLost() ? 1 : 0;
    // clang-format on
});

// Intentional fixture mutations, confined to negative integration tests.
class ConformanceObservationFaults final {
public:
    static core::pmr::String recorderSource(core::StringView source)
    {
        core::pmr::String result(source, &core::defaultCpuMemoryResource());
        switch (conformanceObservationFault()) {
        case 1:
            replace(result, "sourceA: [32,", "sourceA: [33,");
            break;
        case 2:
            replace(result, "push: [0,", "push: [0.125,");
            break;
        case 3:
            replace(result, "indexed: false", "indexed: true");
            break;
        case 4:
            replace(result, "vertices: [-4,", "vertices: [-3,");
            break;
        case 6:
            replace(result, "indices: [0, 1, 2]", "indices: [0, 2, 1]");
            break;
        case 7:
            replace(result, "sourceA: SourceA", "sourceA: SourceB");
            replace(result, "sourceB: SourceB", "sourceB: SourceA");
            break;
        case 9:
            replace(result, "uniforms: [1,", "uniforms: [0.5,");
            break;
        default:
            break;
        }
        return result;
    }

    static core::pmr::String issuerSource(core::StringView source)
    {
        core::pmr::String result(source, &core::defaultCpuMemoryResource());
        if (conformanceObservationFault() == 16)
            replace(result, "shaderSourceSha256: 7", "shaderSourceSha256: 8");
        if (conformanceObservationFault() == 5)
            replace(result, "expected: [32,", "expected: [33,");
        return result;
    }

    static OriginalConformanceRequest request()
    {
        OriginalConformanceRequest r{1, 3, true, 4096 * 4 * 3, 256 * 1024};
        switch (conformanceObservationFault()) {
        case 10:
            r.prototypeOptIn = false;
            break;
        case 11:
            r.maxOutputBytes = 1;
            break;
        case 12:
            r.maxCpuMetadataBytes = 1;
            break;
        case 13:
            r.maxFrames = 1;
            break;
        default:
            break;
        }
        return r;
    }

private:
    static void replace(core::pmr::String& source, core::StringView from, core::StringView to)
    {
        const auto offset = source.find(from);
        if (offset == core::pmr::String::npos)
            throw core::DiagnosticError("CONFORMANCE-FAULT-MUTATION-NOT-APPLIED");
        source.replace(offset, from.size(), to);
    }
};
} // namespace arp::rhi::webgl::test
