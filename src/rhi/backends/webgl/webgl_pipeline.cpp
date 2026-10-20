#include "rhi/backends/webgl/webgl_pipeline_internal.h"
#include "rhi/semantics/webgl_descriptor_creation.h"
#include "rhi/semantics/webgl_shader_coordinates.h"

#ifdef __EMSCRIPTEN__
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

namespace arp::rhi::webgl {
namespace {
const char* layoutProfile(const PipelineLayoutDesc& d)
{
    if (!validatePipelineLayout(d).success || !d.descriptorTables.empty())
        return "RHI-WEBGL1-PIPELINE-LAYOUT";
    for (const auto& layout : d.descriptorLayouts)
        for (const auto& b : layout.bindings)
            if (const auto* id = semantics::WebGlDescriptorCreationContract::binding(b))
                return id;
    return nullptr;
}
} // namespace

core::UniquePtr<PipelineLayout> WebGlResources::createPipelineLayout(const PipelineLayoutDesc& desc)
{
    auto& d = *domain_;
    if (d.preflight(0, 0) || (d.lastDiagnostic = layoutProfile(desc)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto object = core::makeShared<WebGlResourcePayload>(
            WebGlResourceFactoryKey{}, domain_, WebGlObjectKind::PipelineLayout);
        auto result =
            core::makeUnique<WebGlPipelineLayout>(WebGlResourceFactoryKey{}, object, desc);
        if ((d.lastDiagnostic = d.publish(object)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
        return {};
    }
}

core::UniquePtr<RenderPipeline> WebGlResources::createRenderPipeline(const RenderPipelineDesc& desc,
    const std::shared_ptr<PipelineLayout>& layout,
    const std::shared_ptr<const shader::WebGlShaderProgramArtifact>& artifact,
    const semantics::WebGlPipelineCreationRequest& request)
{
    auto& d = *domain_;
    auto p = WebGlRenderPipeline::prepare(
        domain_, desc, layout, artifact, request, domain_->runtime->shaderLimits());
    if (!p)
        return {};
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        p->program = createProgram(artifact, request.program);
        if (!p->program || d.preflight(0, 0))
            return {};
        if (d.extensionDecision.native.vertexArrayObjects) {
            p->vertexArray = core::makeShared<WebGlResourcePayload>(
                WebGlResourceFactoryKey{}, domain_, WebGlObjectKind::VertexArray);
#ifdef __EMSCRIPTEN__
            if (d.nativeResult())
                return {};
            glGenVertexArraysOES(1, &p->vertexArray->native);
            if (d.nativeResult())
                return {};
            if (!p->vertexArray->native) {
                d.lastDiagnostic = "RHI-WEBGL1-VERTEX-ARRAY-NATIVE-CREATE";
                return {};
            }
#else
            d.lastDiagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
            return {};
#endif
            if ((d.lastDiagnostic = d.publish(p->vertexArray)) || d.preflight(0, 0))
                return {};
        }
        p->object = core::makeShared<WebGlResourcePayload>(
            WebGlResourceFactoryKey{}, domain_, WebGlObjectKind::RenderPipeline);
        auto result = core::makeUnique<WebGlRenderPipeline>(WebGlResourceFactoryKey{}, p);
        if ((d.lastDiagnostic = d.publish(p->object)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
        return {};
    }
}

PipelineValidationReport WebGlResources::validateRenderPipeline(const RenderPipelineDesc& desc,
    const std::shared_ptr<PipelineLayout>& layout,
    const std::shared_ptr<const shader::WebGlShaderProgramArtifact>& artifact,
    const semantics::WebGlPipelineCreationRequest& request)
{
    auto p = WebGlRenderPipeline::prepare(
        domain_, desc, layout, artifact, request, domain_->runtime->shaderLimits());
    if (p)
        return p->report;
    PipelineValidationReport result;
    result.backend = Backend::WebGL;
    result.diagnostics = diagnostic() ? diagnostic() : "RHI-WEBGL1-PIPELINE-VALIDATION";
    return result;
}

const char* WebGlRenderPipeline::current(const Impl& p) noexcept
{
    if (!p.object || !p.layout || !p.program)
        return "RHI-WEBGL1-PIPELINE-ORIGINAL-PAYLOAD";
    if (const auto* id = p.object->domain->currentDiagnostic())
        return id;
    if (!p.object->current() || !p.layout->object->current())
        return "RHI-WEBGL1-PIPELINE-OWNER-RETIRED";
    if (p.object->domain->extensionDecision.native.vertexArrayObjects
        && (!p.vertexArray || !p.vertexArray->current() || !p.vertexArray->native))
        return "RHI-WEBGL1-VERTEX-ARRAY-OWNER-RETIRED";
    return p.program->currentDiagnostic();
}

#ifdef __EMSCRIPTEN__
namespace {
GLenum compare(CompareOp v) noexcept
{
    switch (v) {
    case CompareOp::Never:
        return GL_NEVER;
    case CompareOp::Less:
        return GL_LESS;
    case CompareOp::Equal:
        return GL_EQUAL;
    case CompareOp::LessEqual:
        return GL_LEQUAL;
    case CompareOp::Greater:
        return GL_GREATER;
    case CompareOp::NotEqual:
        return GL_NOTEQUAL;
    case CompareOp::GreaterEqual:
        return GL_GEQUAL;
    case CompareOp::Always:
        return GL_ALWAYS;
    }
    std::terminate();
}

GLenum op(StencilOp v) noexcept
{
    switch (v) {
    case StencilOp::Keep:
        return GL_KEEP;
    case StencilOp::Zero:
        return GL_ZERO;
    case StencilOp::Replace:
        return GL_REPLACE;
    case StencilOp::IncrementClamp:
        return GL_INCR;
    case StencilOp::DecrementClamp:
        return GL_DECR;
    case StencilOp::Invert:
        return GL_INVERT;
    }
    std::terminate();
}

GLenum factor(BlendFactor v) noexcept
{
    switch (v) {
    case BlendFactor::Zero:
        return GL_ZERO;
    case BlendFactor::One:
        return GL_ONE;
    case BlendFactor::SrcAlpha:
        return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:
        return GL_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
        return GL_ONE_MINUS_DST_ALPHA;
    }
    std::terminate();
}

GLenum blend(BlendOp v) noexcept
{
    switch (v) {
    case BlendOp::Add:
        return GL_FUNC_ADD;
    case BlendOp::Subtract:
        return GL_FUNC_SUBTRACT;
    case BlendOp::ReverseSubtract:
        return GL_FUNC_REVERSE_SUBTRACT;
    case BlendOp::Min:
    case BlendOp::Max:
        break;
    }
    std::terminate();
}

void enabled(WebGlResourceDomain& domain, GLenum state, bool value) noexcept
{
    if (value)
        domain.nativeCall([&] {
            return glEnable(state);
        });
    else
        domain.nativeCall([&] {
            return glDisable(state);
        });
}
} // namespace
#endif

const char* WebGlRenderPipeline::issueState(const Impl& p) noexcept
{
    if (const auto* id = current(p))
        return id;
    auto& domain = *p.object->domain;
    if (const auto* id = domain.nativeResult())
        return id;
#ifdef __EMSCRIPTEN__
    const auto& d = p.definition;
    const auto& z = d.depthStencil;
    const auto& c = d.colorTargets[0];
    domain.nativeCall([&] {
        return glUseProgram(program(p).object->native);
    });
    enabled(domain, GL_CULL_FACE, d.cullMode != CullMode::None);
    domain.nativeCall([&] {
        return glCullFace(d.cullMode == CullMode::Front ? GL_FRONT : GL_BACK);
    });
    const bool clockwise = (d.frontFace == FrontFace::Clockwise)
        != semantics::WebGlShaderCoordinates::nativeFrontFaceReversed;
    domain.nativeCall([&] {
        return glFrontFace(clockwise ? GL_CW : GL_CCW);
    });
    enabled(domain, GL_DEPTH_TEST, z.depthTestEnabled);
    domain.nativeCall([&] {
        return glDepthMask(z.depthWriteEnabled ? GL_TRUE : GL_FALSE);
    });
    domain.nativeCall([&] {
        return glDepthFunc(compare(z.depthCompare));
    });
    enabled(domain, GL_STENCIL_TEST, z.stencilEnabled);
    const auto reference = p.request.stencilReference.value_or(0);
    const auto stencil = [&](GLenum face, const StencilFaceState& f) {
        domain.nativeCall([&] {
            return glStencilFuncSeparate(face, compare(f.compare), reference, z.stencilReadMask);
        });
        domain.nativeCall([&] {
            return glStencilMaskSeparate(face, z.stencilWriteMask);
        });
        domain.nativeCall([&] {
            return glStencilOpSeparate(face, op(f.failOp), op(f.depthFailOp), op(f.passOp));
        });
    };
    stencil(GL_FRONT, z.front);
    stencil(GL_BACK, z.back);
    enabled(domain, GL_POLYGON_OFFSET_FILL, z.depthBiasEnabled);
    domain.nativeCall([&] {
        return glPolygonOffset(z.depthBiasSlopeFactor, z.depthBiasConstantFactor);
    });
    enabled(domain, GL_BLEND, c.blendEnabled);
    domain.nativeCall([&] {
        return glBlendEquationSeparate(
            blend(c.colorBlend.operation), blend(c.alphaBlend.operation));
    });
    domain.nativeCall([&] {
        return glBlendFuncSeparate(factor(c.colorBlend.sourceFactor),
            factor(c.colorBlend.destinationFactor),
            factor(c.alphaBlend.sourceFactor),
            factor(c.alphaBlend.destinationFactor));
    });
    const auto mask = static_cast<std::uint32_t>(c.writeMask);
    domain.nativeCall([&] {
        return glColorMask(mask & 1, mask & 2, mask & 4, mask & 8);
    });
    // These states have no variable admitted L2 value in this single-sample
    // triangle profile. Explicit constants are its versioned lowering output.
    domain.nativeCall([&] {
        return glDisable(GL_DITHER);
    });
    domain.nativeCall([&] {
        return glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    });
    domain.nativeCall([&] {
        return glDisable(GL_SAMPLE_COVERAGE);
    });
    domain.nativeCall([&] {
        return glEnable(GL_SCISSOR_TEST);
    });
    return domain.nativeResult();
#else
    return "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#endif
}
} // namespace arp::rhi::webgl
