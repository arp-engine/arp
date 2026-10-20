#pragma once

#include "rhi/conformance/clear_execution.h"
#include "rhi/gpu_pipeline.h"

namespace arp::shader {
class GraphicsShaderModuleArtifact;
}

namespace arp::rhi {
enum class ConformanceSampledInput { Vertices, Indices, Uniforms, StorageValues };

struct ConformanceSampledArtifactBudget {
    std::uint32_t contractVersion = 0;
    std::uint64_t inputMetadataBytes = 0, creationOwnerBytes = 0, nativeMetadataBytes = 0,
                  callbackBytes = 0, wrapperBytes = 0, targetBytes = 0, diagnosticBytes = 0;
    std::uint64_t pipelineCreationTimeoutNanoseconds = 0;

    const char* diagnostic() const noexcept
    {
        return contractVersion == 1 && inputMetadataBytes && creationOwnerBytes
                && nativeMetadataBytes && callbackBytes && wrapperBytes && targetBytes
                && diagnosticBytes && pipelineCreationTimeoutNanoseconds
                && pipelineCreationTimeoutNanoseconds <= INT64_MAX
            ? nullptr
            : "RHI-CONFORMANCE-SAMPLED-ARTIFACT-BUDGET";
    }
};

struct ConformanceSampledExecutionRequest {
    ConformanceClearExecutionRequest execution;
    std::uint64_t maxNativeInputBufferBytes = 0;
    ConformanceSampledArtifactBudget artifacts;
};

class ConformanceSampledExecutionContract final {
public:
    static const char* loweringDiagnostic(Backend backend) noexcept
    {
        return ConformanceClearExecutionContract::loweringDiagnostic(backend);
    }

    static const char* validate(const ConformanceSampledExecutionRequest& request) noexcept
    {
        if (const auto* id = ConformanceClearExecutionContract::validate(request.execution))
            return id;
        return request.maxNativeInputBufferBytes ? nullptr
                                                 : "RHI-CONFORMANCE-SAMPLED-NATIVE-INPUT-BUDGET";
    }
};

// Original issuer prepares authored inputs; callers never supply proof bytes.
// The inherited execution entry reuses owned output, completion and present.
class OriginalConformanceSampledExecution : public virtual OriginalConformanceClearExecution {
public:
    virtual std::shared_ptr<GpuBuffer> prepareInput(
        ConformanceSampledInput, std::size_t, const char*&) = 0;
    virtual std::shared_ptr<GpuSampler> prepareSampler(const char*&) = 0;
    virtual std::shared_ptr<RenderPipeline> preparePipeline(const RenderPipelineDesc&,
        const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>&,
        const char*&) = 0;
};
} // namespace arp::rhi
