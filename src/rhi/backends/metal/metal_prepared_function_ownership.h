#pragma once

#include "rhi/backends/metal/metal_native_descriptors.h"
#include "rhi/backends/metal/metal_native_pipelines.h"

namespace arp::rhi_metal {

class MetalRhiPreparedFunctionOwnership final {
public:
    // Payload/device/library/function/pipeline state, no Render owners.
    static constexpr std::size_t computeOwnerCount = 5u;
    // Existing Render factory always creates an explicit depth-stencil object,
    // including disabled depth/stencil: payload/device/2 libraries/2 functions/
    // pipeline/depth-stencil. Compute function is absent.
    static constexpr std::size_t renderOwnerCount = 8u;

    static auto pipelineOwners(
        const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline) noexcept
    {
        return std::array<std::shared_ptr<const void>, 9>{pipeline,
            pipeline->nativeDevice,
            pipeline->shaderLibrary,
            pipeline->fragmentShaderLibrary,
            pipeline->vertexFunction,
            pipeline->fragmentFunction,
            pipeline->computeFunction,
            pipeline->pipelineState,
            pipeline->depthStencilState};
    }

    template <class Visitor>
    static void visit(const std::shared_ptr<const MetalRhiNativePreparedFunctionBindings>& function,
        const std::shared_ptr<const void>& registry,
        Visitor&& visitor)
    {
        const auto add = [&](std::shared_ptr<const void> owner) {
            if (owner)
                visitor(std::move(owner));
        };
        add(function);
        for (auto& owner : pipelineOwners(function->pipeline))
            add(std::move(owner));
        add(function->function);
        add(function->descriptors);
        add(registry);
        add(function->descriptors->nativeDevice);
        add(function->descriptors->nativeObject);
        for (const auto& binding : function->descriptors->bindings) {
            for (auto& owner :
                MetalRhiResourceOwnerProjection::owners(
                    binding.resourceKind, binding.buffer, {}, binding.textureView, binding.sampler))
                add(std::move(owner));
        }
        for (const auto& array : function->arrays)
            add(array.buffer);
    }
};

} // namespace arp::rhi_metal
