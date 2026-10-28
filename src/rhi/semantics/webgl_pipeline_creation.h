#pragma once
#include "rhi/frontend/vertex_input_validation.h"
#include "rhi/semantics/webgl_program_creation.h"
#include "rhi/semantics/webgl_runtime_profile.h"

namespace arp::rhi::semantics {
// Composed input: stencil reference has no field in the existing L2 descriptor.
// Presence is mandatory only when stencil is enabled; no backend default leaks.
struct WebGlPipelineCreationRequest {
    std::uint32_t contractVersion = 0;
    WebGlProgramCreationRequest program;
    std::optional<std::uint32_t> stencilReference;
};

class WebGlPipelineCreationContract final {
public:
    static const char* state(Backend backend,
        const RenderPipelineDesc& d,
        const WebGlPipelineCreationRequest& request) noexcept
    {
        if (request.contractVersion != 1)
            return "RHI-WEBGL1-PIPELINE-CREATION-VERSION";
        if (const auto* id = WebGlProgramCreationContract::validate(backend, request.program))
            return id;
        if (d.colorTargets.size() != 1 || d.colorTargets[0].format != TextureFormat::Rgba8Unorm
            || d.colorFormat != d.colorTargets[0].format || d.colorTargets[0].resolveTargetRequired)
            return "RHI-WEBGL1-PIPELINE-COLOR-LAYOUT";
        const auto& z = d.depthStencil;
        if (d.depthFormat != z.format
            || (z.format != TextureFormat::Unknown && z.format != TextureFormat::Depth24Stencil8)
            || (z.format == TextureFormat::Unknown
                && (z.depthTestEnabled || z.depthWriteEnabled || z.stencilEnabled)))
            return "RHI-WEBGL1-PIPELINE-DEPTH-LAYOUT";
        if ((z.stencilEnabled != request.stencilReference.has_value())
            || (request.stencilReference && *request.stencilReference > 255)
            || z.stencilReadMask > 255 || z.stencilWriteMask > 255)
            return "RHI-WEBGL1-PIPELINE-STENCIL-REFERENCE";
        if (d.multisample.sampleCount != 1 || d.multisample.sampleMask != UINT64_MAX
            || d.multisample.alphaToCoverageEnabled)
            return "RHI-WEBGL1-PIPELINE-MULTISAMPLE";
        if (d.subpassIndex || d.renderPassCompatibility.enabled()
            || d.topology != PrimitiveTopology::TriangleList || d.patchControlPointCount
            || d.tessellationControlShader.stage != ShaderStage::None
            || !d.tessellationControlShader.entryPoint.empty()
            || !d.tessellationControlShader.modulePath.empty()
            || d.tessellationEvaluationShader.stage != ShaderStage::None
            || !d.tessellationEvaluationShader.entryPoint.empty()
            || !d.tessellationEvaluationShader.modulePath.empty())
            return "RHI-WEBGL1-PIPELINE-TOPOLOGY";
        if ((!d.dynamicViewportScissor && (!d.hasViewport || !d.hasScissor))
            || (d.dynamicViewportScissor && (d.hasViewport || d.hasScissor)))
            return "RHI-WEBGL1-PIPELINE-VIEWPORT-SCISSOR";
        if (!compare(z.depthCompare) || !face(z.front) || !face(z.back)
            || !std::isfinite(z.depthBiasConstantFactor) || !std::isfinite(z.depthBiasSlopeFactor)
            || !std::isfinite(z.depthBiasClamp) || z.depthBiasClamp != 0)
            return "RHI-WEBGL1-PIPELINE-DEPTH-STATE";
        const auto& c = d.colorTargets[0];
        if (!blend(c.colorBlend) || !blend(c.alphaBlend)
            || (static_cast<std::uint32_t>(c.writeMask) & ~std::uint32_t{15}))
            return "RHI-WEBGL1-PIPELINE-BLEND-STATE";
        for (const auto& stream : d.vertexBuffers) {
            VertexInputStreamInspection inspected{};
            if (const auto* id = VertexInputValidation::inspectStream(stream, inspected))
                return id;
            // ES2 vertexAttribPointer: float alignment and maximum byte stride
            // are specified constraints, not guessed device limits.
            if ((stream.inputRate != VertexInputRate::Vertex
                    && stream.inputRate != VertexInputRate::Instance)
                || stream.strideBytes > 255 || stream.strideBytes % sizeof(float))
                return "RHI-WEBGL1-PIPELINE-VERTEX-STRIDE";
            for (const auto& a : stream.attributes)
                if (a.offsetBytes % sizeof(float))
                    return "RHI-WEBGL1-PIPELINE-VERTEX-ALIGNMENT";
        }
        return nullptr;
    }

private:
    static bool compare(CompareOp v) noexcept
    {
        switch (v) {
        case CompareOp::Never:
        case CompareOp::Less:
        case CompareOp::Equal:
        case CompareOp::LessEqual:
        case CompareOp::Greater:
        case CompareOp::NotEqual:
        case CompareOp::GreaterEqual:
        case CompareOp::Always:
            return true;
        }
        return false;
    }

    static bool op(StencilOp v) noexcept
    {
        switch (v) {
        case StencilOp::Keep:
        case StencilOp::Zero:
        case StencilOp::Replace:
        case StencilOp::IncrementClamp:
        case StencilOp::DecrementClamp:
        case StencilOp::Invert:
            return true;
        }
        return false;
    }

    static bool face(const StencilFaceState& f) noexcept
    {
        return compare(f.compare) && op(f.failOp) && op(f.depthFailOp) && op(f.passOp);
    }

    static bool factor(BlendFactor v) noexcept
    {
        switch (v) {
        case BlendFactor::Zero:
        case BlendFactor::One:
        case BlendFactor::SrcAlpha:
        case BlendFactor::OneMinusSrcAlpha:
        case BlendFactor::DstAlpha:
        case BlendFactor::OneMinusDstAlpha:
            return true;
        }
        return false;
    }

    static bool blend(const BlendComponent& b) noexcept
    {
        return factor(b.sourceFactor) && factor(b.destinationFactor)
            && (b.operation == BlendOp::Add || b.operation == BlendOp::Subtract
                || b.operation == BlendOp::ReverseSubtract);
    }
};
} // namespace arp::rhi::semantics
