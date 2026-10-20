#pragma once

#include "rhi/frontend/pipeline_definition_identity.h"

namespace arp::rhi {
// One complete L2 definition shared by recorder and original native issuer.
class ConformanceSampledPipeline final {
public:
    static RenderPipelineDesc definition(
        const PipelineLayout& layout, const ShaderPackageDesc& package, TextureFormat format)
    {
        RenderPipelineDesc state;
        state.label = "ConformanceSampledWorkload";
        state.layout = &layout;
        state.shaderPackage = &package;
        if (package.entrypoints.size() == 2) {
            state.vertexShader = package.entrypoints[0];
            state.fragmentShader = package.entrypoints[1];
        }
        state.colorFormat = format;
        state.colorTargets.push_back({format});
        state.dynamicViewportScissor = true;
        VertexBufferLayoutDesc stream;
        stream.slot = 3;
        stream.strideBytes = 9 * sizeof(float);
        stream.attributes = {{0, 0, VertexFormat::Float32x3},
            {1, 3 * sizeof(float), VertexFormat::Float32x2},
            {2, 5 * sizeof(float), VertexFormat::Float32x4}};
        state.vertexBuffers.push_back(std::move(stream));
        return state;
    }
};
} // namespace arp::rhi
