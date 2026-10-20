#include "rhi/backends/webgl/webgl_pipeline_internal.h"

namespace arp::rhi::webgl {
bool WebGlRenderPipeline::compatiblePreparedInputs(const Impl& a, const Impl& b) noexcept
{
    if (a.layout != b.layout || program(a).artifact != program(b).artifact
        || a.definition.depthFormat != b.definition.depthFormat
        || !a.definition.dynamicViewportScissor || !b.definition.dynamicViewportScissor)
        return false;
    const auto& x = a.definition.vertexBuffers;
    const auto& y = b.definition.vertexBuffers;
    if (x.size() != y.size())
        return false;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i].slot != y[i].slot || x[i].strideBytes != y[i].strideBytes
            || x[i].inputRate != y[i].inputRate || x[i].attributes.size() != y[i].attributes.size())
            return false;
        for (std::size_t j = 0; j < x[i].attributes.size(); ++j) {
            const auto& u = x[i].attributes[j];
            const auto& v = y[i].attributes[j];
            if (u.shaderLocation != v.shaderLocation || u.offsetBytes != v.offsetBytes
                || u.format != v.format)
                return false;
        }
    }
    return true;
}

std::shared_ptr<WebGlRenderPipeline::Impl> WebGlRenderPipeline::prepare(
    const std::shared_ptr<WebGlResourceDomain>& domain,
    const RenderPipelineDesc& desc,
    const std::shared_ptr<PipelineLayout>& layout,
    const std::shared_ptr<const shader::WebGlShaderProgramArtifact>& artifact,
    const semantics::WebGlPipelineCreationRequest& request,
    const shader::WebGlShaderLimits& limits)
{
    auto& d = *domain;
    if (d.preflight(0, 0)
        || (d.lastDiagnostic =
                semantics::WebGlPipelineCreationContract::state(Backend::WebGL, desc, request))
        || (d.lastDiagnostic = shader::WebGlShaderProgramArtifact::validateOriginal(artifact))
        || (d.lastDiagnostic = artifact->assess(limits))
        || (d.lastDiagnostic = d.shaderAdmission(*artifact)))
        return {};
    for (const auto& stream : desc.vertexBuffers) {
        if (stream.inputRate == VertexInputRate::Instance
            && !d.extensionDecision.native.instancing) {
            d.lastDiagnostic = "RHI-WEBGL1-EXTENSION-EXECUTION-INSTANCING-CLOSED";
            return {};
        }
    }
    auto original = std::dynamic_pointer_cast<WebGlPipelineLayout>(layout);
    if (!original || original->object->domain != domain || !original->object->current()
        || desc.layout != layout.get() || desc.shaderPackage) {
        d.lastDiagnostic = "RHI-WEBGL1-PIPELINE-ORIGINAL-LAYOUT";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto p = core::makeShared<WebGlRenderPipeline::Impl>(desc);
        p->layout = original;
        p->request = request;
        if ((d.lastDiagnostic = p->vertexInput.diagnostic()))
            return {};
        if (!validateSingleEntryVertexInput(p->vertexInput, *artifact->vertex().original).success) {
            d.lastDiagnostic = "RHI-WEBGL1-PIPELINE-VERTEX-INTERFACE";
            return {};
        }
        const auto reflected = populateSelectedGraphicsShaderInterface(
            *artifact->vertex().original, artifact->fragment().original.get(), p->package);
        if (!reflected.success) {
            d.lastDiagnostic = "RHI-WEBGL1-PIPELINE-ORIGINAL-INTERFACE";
            d.nativeDiagnostic = reflected.diagnostics;
            return {};
        }
        const auto& entries = p->package.entrypoints;
        if (entries.size() != 2 || !desc.vertexShader.matchesDefinition(entries[0])
            || !desc.fragmentShader.matchesDefinition(entries[1])
            || !validatePipelineLayoutAgainstShaderReflection(
                original->desc(), p->package.reflection)
                .success) {
            d.lastDiagnostic = "RHI-WEBGL1-PIPELINE-INTERFACE-MISMATCH";
            return {};
        }
        p->definition.shaderPackage = &p->package;
        const auto state = validateRenderPipelineState(p->definition);
        if (!state.success) {
            d.lastDiagnostic = "RHI-WEBGL1-PIPELINE-L2-STATE";
            d.nativeDiagnostic = state.diagnostics;
            return {};
        }
        for (const auto& a : p->vertexInput.attributes())
            if (a.layout.shaderLocation >= d.runtime->profile().limits().maxVertexAttributes) {
                d.lastDiagnostic = "RHI-WEBGL1-PIPELINE-ATTRIBUTE-LIMIT";
                return {};
            }
        if (desc.hasViewport
            && (desc.viewport.width > d.runtime->profile().limits().maxViewportWidth
                || desc.viewport.height > d.runtime->profile().limits().maxViewportHeight)) {
            d.lastDiagnostic = "RHI-WEBGL1-PIPELINE-VIEWPORT-LIMIT";
            return {};
        }
        auto& r = p->report;
        r.backend = Backend::WebGL;
        r.entrypointCount = 2;
        r.descriptorBindingCount = p->package.reflection.descriptorBindings.size();
        r.descriptorLayoutCount = original->desc().descriptorLayouts.size();
        r.descriptorSetCount = original->descriptorSetCount();
        r.pushConstantBytes = original->pushConstantByteCount();
        r.shaderPackageValidated = r.reflectionMatched = true;
        r.colorTargetCount = state.colorTargetCount;
        r.colorFormats = state.colorFormats;
        r.depthStencilFormat = state.depthStencilFormat;
        r.sampleCount = 1;
        r.blendStateValidated = r.colorWriteMaskValidated = r.stencilStateValidated = true;
        r.depthBiasStateValidated = r.rasterStateValidated = r.topologyValidated = true;
        r.msaaStateValidated = r.resolveStateValidated = r.viewportScissorStateValidated = true;
        r.pipelineCacheKeyStable = true;
        r.pipelineCacheKey = state.pipelineCacheKey;
        r.pipelineCacheKey = hashShaderValue(r.pipelineCacheKey, request.contractVersion);
        r.pipelineCacheKey =
            hashShaderValue(r.pipelineCacheKey, request.stencilReference.has_value());
        r.pipelineCacheKey =
            hashShaderValue(r.pipelineCacheKey, request.stencilReference.value_or(0));
        const auto& extensions = d.extensionDecision.native;
        for (const bool enabled : {extensions.uint32Indices,
                 extensions.instancing,
                 extensions.vertexArrayObjects,
                 extensions.depthTextures,
                 extensions.derivatives})
            r.pipelineCacheKey = hashShaderValue(r.pipelineCacheKey, enabled);
        for (const auto byte : artifact->identity())
            r.pipelineCacheKey = hashShaderValue(r.pipelineCacheKey, byte);
        r.success = true;
        return p;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
        return {};
    }
}
} // namespace arp::rhi::webgl
