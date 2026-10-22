#pragma once
#include "core/hash/stable_hash.h"
#include "rhi/backends/metal/metal_native_pipelines.h"

namespace arp::rhi_metal {
// Fixed-size observation of backend-owned render state. Native immutable
// blend/depth/pipeline objects are protected separately by owner identity.
class MetalRhiRenderStateObservation final {
public:
    explicit MetalRhiRenderStateObservation(const MetalRhiNativePipelinePayload& payload) noexcept
        : identity_(identity(payload))
    {}

    bool matches(const MetalRhiNativePipelinePayload& payload) const noexcept
    {
        return identity_ == identity(payload);
    }

private:
    static std::uint64_t identity(const MetalRhiNativePipelinePayload& p) noexcept
    {
        core::StableHash64 hash;
        const auto add = [&](const auto& value) {
            hash.appendBytes(&value, sizeof(value));
        };
        add(p.shaderModulePayloadPresent);
        add(p.renderPipelinePayloadPresent);
        add(p.computePipelinePayloadPresent);
        add(p.nativeRuntimeBacked);
        add(p.nativeRuntimeDiagnosticOnly);
        add(p.nativeHandleExposedToUpperLayer);
        add(p.supportsIndirectCommands);
        add(p.pipelineCacheKeyStable);
        add(p.pipelineCacheKey);
        add(p.rasterStateRecorded);
        add(p.depthStencilStateRecorded);
        add(p.depthTestEnabled);
        add(p.depthWriteEnabled);
        add(p.depthOnlyRenderPipeline);
        add(p.preparedGraphicsStages);
        add(p.renderPassCompatibilityEnabled);
        add(p.colorFormats.size());
        for (const auto format : p.colorFormats)
            add(format);
        add(p.depthStencilFormat);
        add(p.writesRenderTargetArrayIndex);
        add(p.usesDepthStencilAttachment);
        add(p.depthCompare);
        add(p.cullMode);
        add(p.frontFace);
        add(p.sampleMask);
        add(p.sampleCount);
        add(p.alphaToCoverageEnabled);
        add(p.subpassIndex);
        add(p.primitiveTopology);
        add(p.primitiveType);
        add(p.patchControlPointCount);
        add(p.hasViewport);
        add(p.viewport.x);
        add(p.viewport.y);
        add(p.viewport.width);
        add(p.viewport.height);
        add(p.viewport.minDepth);
        add(p.viewport.maxDepth);
        add(p.hasScissor);
        add(p.scissor.x);
        add(p.scissor.y);
        add(p.scissor.width);
        add(p.scissor.height);
        add(p.depthBiasEnabled);
        add(p.depthBiasConstantFactor);
        add(p.depthBiasClamp);
        add(p.depthBiasSlopeFactor);
        add(p.stencilEnabled);
        add(p.stencilReadMask);
        add(p.stencilWriteMask);
        for (const auto* face : {&p.stencilFront, &p.stencilBack}) {
            add(face->failOp);
            add(face->depthFailOp);
            add(face->passOp);
            add(face->compare);
        }
        return hash.value();
    }

    std::uint64_t identity_;
};
} // namespace arp::rhi_metal
