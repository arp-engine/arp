#pragma once
#include "rhi/gpu_pipeline.h"

#include <cmath>

namespace arp::rhi::semantics {
// SceneRaster/1 composes existing explicit RHI raster state with the selected
// bindless color/depth/shadow interface. It grants preparation only. Device
// limits, shader outputs, attachments and execution remain separate checks.
class BindlessSceneRasterContract final {
public:
    static constexpr std::uint32_t version = 1u;

    struct Decision {
        bool preparationAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-SCENE-RASTER-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t requested = version) noexcept
    {
        if (requested != version)
            return {false, false, false, "RHI-BINDLESS-SCENE-RASTER-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-SCENE-RASTER-UNSUPPORTED"};
        }
        return {};
    }

    // Called in addition to the complete shared pipeline-state validator.
    // No native default, color-format substitution or depth bias rounding.
    static const char* inspect(Backend backend,
        const RenderPipelineDesc& state,
        std::uint32_t requested = version) noexcept
    {
        const auto decision = lowering(backend, requested);
        if (!decision.preparationAvailable)
            return decision.diagnostic;
        const auto count = effectiveColorTargetCount(state);
        const auto depth = effectiveDepthStencilFormat(state);
        if (count > 1u || (depth != TextureFormat::Unknown && depth != TextureFormat::D32Float)
            || (!count && depth == TextureFormat::Unknown)
            || state.topology != PrimitiveTopology::TriangleList
            || state.multisample.sampleCount != 1u || state.multisample.alphaToCoverageEnabled
            || !(state.multisample.sampleMask & 1u) || state.depthStencil.stencilEnabled)
            return "RHI-BINDLESS-SCENE-RASTER-SCOPE";
        if (count) {
            const auto color = effectiveColorTargetAt(state, 0u);
            if ((color.format != TextureFormat::Rgba8Unorm
                    && color.format != TextureFormat::Rgba16Float)
                || color.resolveTargetRequired)
                return "RHI-BINDLESS-SCENE-RASTER-COLOR";
        }
        if (state.depthStencil.depthBiasEnabled) {
            const auto bias = state.depthStencil.depthBiasConstantFactor;
            if (!std::isfinite(bias) || !std::isfinite(state.depthStencil.depthBiasSlopeFactor)
                || !std::isfinite(state.depthStencil.depthBiasClamp))
                return "RHI-BINDLESS-SCENE-RASTER-BIAS";
            if (backend == Backend::WebGPU
                && (double(bias) < double(INT32_MIN) || double(bias) > double(INT32_MAX)
                    || std::trunc(bias) != bias))
                return "RHI-BINDLESS-SCENE-RASTER-BIAS-NONREPRESENTABLE";
        }
        return nullptr;
    }
};
} // namespace arp::rhi::semantics
