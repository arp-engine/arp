#pragma once
#include "rhi/backends/webgl/webgl_program_internal.h"
#include "rhi/frontend/pipeline_state_copy.h"

namespace arp::rhi::webgl {
class WebGlPipelineLayout final : public PipelineLayout {
public:
    WebGlPipelineLayout(WebGlResourceFactoryKey,
        std::shared_ptr<WebGlResourcePayload> p,
        const PipelineLayoutDesc& d)
        : object(std::move(p)), definition(PipelineLayoutStateCopy::copy(d))
    {}

    const PipelineLayoutDesc& desc() const override { return definition; }

    std::uint32_t descriptorSetCount() const override { return definition.descriptorSets.size(); }

    std::uint32_t descriptorTableCount() const override { return 0; }

    std::uint32_t pushConstantByteCount() const override
    {
        return definition.pushConstantRanges.empty() ? 0
                                                     : definition.pushConstantRanges[0].sizeBytes;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    std::shared_ptr<WebGlResourcePayload> object;
    PipelineLayoutDesc definition;
};

class WebGlRenderPipeline final : public RenderPipeline {
public:
    struct Impl {
        Impl(const RenderPipelineDesc& d)
            : definition(RenderPipelineStateCopy::copy(d)), vertexInput(definition.vertexBuffers)
        {}

        std::shared_ptr<WebGlResourcePayload> object;
        std::shared_ptr<WebGlResourcePayload> vertexArray;
        std::shared_ptr<WebGlPipelineLayout> layout;
        core::UniquePtr<WebGlProgram> program;
        RenderPipelineDesc definition;
        VertexInputConsumptionContract vertexInput;
        ShaderPackageDesc package;
        semantics::WebGlPipelineCreationRequest request;
        PipelineValidationReport report;
    };

    WebGlRenderPipeline(WebGlResourceFactoryKey, std::shared_ptr<Impl> p) : impl(std::move(p)) {}

    const PipelineLayout* layout() const override { return impl->layout.get(); }

    const PipelineValidationReport& validationReport() const override { return impl->report; }

    const ShaderReflectionDesc& shaderReflection() const override
    {
        return impl->package.reflection;
    }

    ShaderSourceLanguage shaderSourceLanguage() const override
    {
        return ShaderSourceLanguage::Slang;
    }

    bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

    bool nativeHandleExposedToUpperLayer() const override { return false; }

    PrimitiveTopology primitiveTopology() const override { return impl->definition.topology; }

    static const char* current(const Impl&) noexcept;
    static bool compatiblePreparedInputs(const Impl&, const Impl&) noexcept;
    static std::shared_ptr<Impl> prepare(const std::shared_ptr<WebGlResourceDomain>&,
        const RenderPipelineDesc&,
        const std::shared_ptr<PipelineLayout>&,
        const std::shared_ptr<const shader::WebGlShaderProgramArtifact>&,
        const semantics::WebGlPipelineCreationRequest&,
        const shader::WebGlShaderLimits&);
    // Fixed raster/depth/stencil/blend lowering. Target-relative viewport and
    // scissor belong to the executor. This entry rechecks original custody.
    static const char* issueState(const Impl&) noexcept;

    static const WebGlProgram::Impl& program(const Impl& p) noexcept { return *p.program->impl_; }

    std::shared_ptr<Impl> impl;
};
} // namespace arp::rhi::webgl
