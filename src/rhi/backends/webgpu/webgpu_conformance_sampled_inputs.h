#pragma once

#include "core/memory/initialized_byte_buffer.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/conformance/sampled_execution.h"
#include "shader/graphics_shader_module_artifact.h"

namespace arp::rhi_webgpu {
class WebGpuOriginalArtifactGraphicsCreation;
class WebGpuOriginalArtifactComputeCreation;

class WebGpuConformanceSampledInputs final {
    friend class WebGpuConformanceClearExecution;
    friend class WebGpuConformanceClearFrame;

    struct Buffer {
        rhi::ConformanceSampledInput role;
        std::size_t frame;
        std::shared_ptr<rhi::GpuBuffer> object;
        std::shared_ptr<WebGpuNativeBufferPayload> native;
        core::InitializedByteBuffer content;
    };

public:
    WebGpuConformanceSampledInputs(
        std::uint64_t budget, rhi::ConformanceSampledArtifactBudget artifacts)
        : budget_(budget), artifacts_(artifacts)
    {}

private:
    const Buffer* buffer(const WebGpuNativeBufferPayload&,
        rhi::ConformanceSampledInput,
        const void* = nullptr,
        std::size_t = 0) const noexcept;
    const char* diagnostic() const noexcept;
    const char* sourceDiagnostic() const noexcept;
    core::pmr::Vector<Buffer> buffers_{&core::defaultCpuMemoryResource()};
    std::shared_ptr<rhi::GpuSampler> sampler_;
    std::shared_ptr<WebGpuNativeSamplerPayload> nativeSampler_;
    std::array<std::shared_ptr<rhi::RenderPipeline>, 2> pipelines_;
    std::array<std::shared_ptr<WebGpuNativeRenderPipelinePayload>, 2> nativePipelines_;
    std::array<std::shared_ptr<WebGpuOriginalArtifactGraphicsCreation>, 2> creations_;
    std::shared_ptr<const shader::GraphicsShaderModuleArtifact> artifact_;
    std::shared_ptr<const shader::SingleEntryShaderArtifact> computeArtifact_;
    std::shared_ptr<WebGpuOriginalArtifactComputeCreation> computeCreation_;
    std::shared_ptr<rhi::ComputePipeline> compute_;
    std::shared_ptr<WebGpuNativeComputePipelinePayload> nativeCompute_;
    core::pmr::Vector<std::shared_ptr<rhi::GpuTexture>> textures_{
        &core::defaultCpuMemoryResource()};
    bool storageMrt_ = false;
    std::uint64_t textureBudget_ = 0, nativeTextureBytes_ = 0;
    std::uint32_t dispatchLimit_ = 0;
    std::uint64_t budget_, nativeBytes_ = 0;
    const rhi::ConformanceSampledArtifactBudget artifacts_;
};
} // namespace arp::rhi_webgpu
