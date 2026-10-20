#pragma once
#include "rhi/gpu_pipeline.h"

namespace arp::rhi {
// Report projection only. Callers must prove the interface/state first; these
// helpers do not validate, lower, create a payload or issue execution authority.
class PipelineValidationReportBuilder final {
public:
    template <class Desc>
    static void validatedInterface(PipelineValidationReport& report, const Desc& desc) noexcept
    {
        report.success = true;
        report.shaderPackageValidated = true;
        report.reflectionMatched = true;
        report.entrypointCount = static_cast<std::uint32_t>(desc.shaderPackage->entrypoints.size());
        report.descriptorLayoutCount =
            static_cast<std::uint32_t>(desc.layout->desc().descriptorLayouts.size());
        report.descriptorSetCount = desc.layout->descriptorSetCount();
        report.descriptorTableCount = desc.layout->descriptorTableCount();
        report.pushConstantBytes = desc.layout->pushConstantByteCount();
    }

    static void validatedGraphicsState(
        PipelineValidationReport& report, const PipelineStateValidationResult& state)
    {
        report.colorTargetCount = state.colorTargetCount;
        report.colorFormats = state.colorFormats;
        report.depthStencilFormat = state.depthStencilFormat;
        report.unsupportedStateDiagnosed = state.unsupportedStateDiagnosed;
        report.sampleCount = state.sampleCount;
        report.blendStateValidated = state.blendStateValidated;
        report.colorWriteMaskValidated = state.colorWriteMaskValidated;
        report.stencilStateValidated = state.stencilStateValidated;
        report.depthBiasStateValidated = state.depthBiasStateValidated;
        report.rasterStateValidated = state.rasterStateValidated;
        report.topologyValidated = state.topologyValidated;
        report.msaaStateValidated = state.msaaStateValidated;
        report.resolveStateValidated = state.resolveStateValidated;
        report.viewportScissorStateValidated = state.viewportScissorStateValidated;
        report.depthOnlyStateValidated = state.depthOnlyStateValidated;
        report.pipelineCacheKey = state.pipelineCacheKey;
        report.pipelineCacheKeyStable = state.success && state.pipelineCacheKey != 0;
    }
};
} // namespace arp::rhi
