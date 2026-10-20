#pragma once

#include "rhi/conformance/sampled_execution.h"
#include "rhi/conformance/storage_mrt_workload.h"

namespace arp::shader {
class SingleEntryShaderArtifact;
}

namespace arp::rhi {
struct ConformanceStorageMrtExecutionRequest {
    ConformanceClearExecutionRequest execution;
    std::uint64_t maxNativeInputBufferBytes = 0, maxNativeTextureBytes = 0;
    ConformanceSampledArtifactBudget artifacts;
};

class ConformanceStorageMrtExecutionContract final {
public:
    static const char* validate(const ConformanceStorageMrtExecutionRequest& request) noexcept
    {
        if (const auto* id = ConformanceSampledExecutionContract::validate(
                {request.execution, request.maxNativeInputBufferBytes}))
            return id;
        return request.maxNativeTextureBytes ? nullptr
                                             : "RHI-CONFORMANCE-STORAGE-MRT-NATIVE-TEXTURE-BUDGET";
    }

    static const char* loweringDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Vulkan:
        case Backend::WebGPU:
            return nullptr;
        case Backend::Null:
            return "RHI-CONFORMANCE-NULL-VALIDATOR-NO-OUTPUT";
        case Backend::Metal:
            return "RHI-CONFORMANCE-METAL-ORIGINAL-OUTPUT-JOIN-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-CONFORMANCE-STORAGE-MRT-WEBGL-UNSUPPORTED";
        default:
            return "RHI-CONFORMANCE-ORIGINAL-BACKEND-UNKNOWN";
        }
    }

    static const char* validateWebGpu(const ConformanceStorageMrtExecutionRequest& request) noexcept
    {
        if (const auto* id = validate(request))
            return id;
        return request.artifacts.diagnostic();
    }
};

class OriginalConformanceStorageMrtExecution : public virtual OriginalConformanceClearExecution {
public:
    virtual std::shared_ptr<GpuTexture> prepareStorageTexture(
        ConformanceOutputRole, std::size_t, const char*&) = 0;
    virtual std::shared_ptr<GpuBuffer> prepareStorageInput(std::size_t, const char*&) = 0;
    virtual std::shared_ptr<RenderPipeline> preparePipeline(const RenderPipelineDesc&,
        const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>&,
        const char*&) = 0;
    virtual std::shared_ptr<ComputePipeline> prepareComputePipeline(const ComputePipelineDesc&,
        const std::shared_ptr<const shader::SingleEntryShaderArtifact>&,
        const char*&) = 0;
};
} // namespace arp::rhi
