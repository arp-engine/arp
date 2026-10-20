#pragma once

#include "renderer/gi/ddgi_renderer.h"

#include <array>
#include <cstring>

namespace arp::tests {

// Shared deterministic inputs for Null ownership and native byte-consumption
// tests. These test storage identity, not sky/CSM lighting or projection math.
struct DdgiUniformInputs {
    renderer::DdgiSkyEnvironmentDesc sky{};
    renderer::DdgiCsmShadowDesc csm{};

    explicit DdgiUniformInputs(float value)
    {
        for (unsigned i = 0; i < 3; ++i) {
            sky.horizonRadiance[i] = value + i;
            sky.zenithRadiance[i] = value + 3 + i;
            sky.sunRadiance[i] = value + 6 + i;
            sky.sunDirection[i] = value + i;
            for (unsigned j = 0; j < 16; ++j)
                csm.lightViewProjection[i][j] = value + (i * 16 + j) / 64.0f;
            for (unsigned j = 0; j < 4; ++j)
                csm.atlasUvTransforms[i][j] = value + (i * 4 + j) / 16.0f;
        }
        sky.glowPower = 6 + value;
        sky.glowStrength = 2 + value;
        sky.corePower = 45 + value;
        sky.coreStrength = 3 + value;
        sky.horizonPower = 1 + value;
        sky.preExposure = 2 + value;
        for (unsigned i = 0; i < 4; ++i)
            csm.cascadeSplits[i] = value + i;
        csm.shadowParams[0] = 1;
        csm.shadowParams[1] = value;
        csm.shadowParams[2] = value;
        csm.shadowParams[3] = 256;
    }

    renderer::RendererResult write(renderer::DdgiRenderer& renderer, bool isSky) const
    {
        return isSky ? renderer.setSkyEnvironment(sky) : renderer.setCsmShadow(csm);
    }

    static const char* label(bool isSky)
    {
        return isSky ? "DdgiSkyEnvironmentUbo" : "DdgiCsmShadowUbo";
    }

    static const char* busyDiagnostic(bool isSky)
    {
        return isSky ? "DDGI-SHARED-SKY-INPUT-NOT-READY" : "DDGI-SHARED-CSM-INPUT-NOT-READY";
    }

    static const char* invalidDiagnostic(bool isSky)
    {
        return isSky ? "DDGI-SKY-INPUT-INVALID" : "DDGI-CSM-INPUT-INVALID";
    }

    using Bytes = std::array<float, 92>;

    Bytes bytes() const
    {
        static_assert(sizeof(Bytes) == sizeof(renderer::DdgiSkyEnvironmentGpu) + sizeof(csm));
        const auto packed = renderer::packDdgiSkyEnvironment(sky);
        Bytes result{};
        std::memcpy(result.data(), &packed, sizeof(packed));
        std::memcpy(result.data() + sizeof(packed) / sizeof(float), &csm, sizeof(csm));
        return result;
    }
};

} // namespace arp::tests
