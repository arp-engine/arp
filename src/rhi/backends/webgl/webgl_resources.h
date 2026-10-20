#pragma once

#include "rhi/backends/webgl/webgl_runtime.h"
#include "rhi/gpu_resources.h"
#include "rhi/semantics/webgl_canvas_lease.h"
#include "rhi/semantics/webgl_execution_telemetry.h"
#include "rhi/semantics/webgl_pipeline_creation.h"
#include "rhi/semantics/webgl_program_creation.h"
#include "rhi/semantics/webgl_resource_creation.h"

namespace arp::rhi {
class GpuQueue;
class Swapchain;
struct SwapchainDesc;
struct AcquiredFrame;
} // namespace arp::rhi

namespace arp::shader {
class WebGlShaderProgramArtifact;
}

namespace arp::rhi::webgl {

class WebGlDevice;
class WebGlResourceDomain;
class WebGlFramebuffer;
class WebGlProgram;
class WebGlExecution;

// Backend-private custody shared by the forthcoming device/command adapters.
// This is not a second public RHI nor permission to use generic WebGL factories.
class WebGlResources final {
    class ConstructionKey {
        friend class WebGlResources;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGlResources(ConstructionKey, std::shared_ptr<WebGlResourceDomain>);
    ~WebGlResources();
    WebGlResources(const WebGlResources&) = delete;
    WebGlResources& operator=(const WebGlResources&) = delete;

    static core::UniquePtr<WebGlResources> create(core::UniquePtr<WebGlRuntime>,
        const semantics::WebGlResourceOwnershipRequest&,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> uploadQuota,
        const char*& diagnostic);
    const char* enableRecovery(const semantics::WebGlContextRecoveryRequest&);
    const char* enableSurfaceLifecycle(const semantics::WebGlSurfaceLifecycleRequest&);
    semantics::WebGlContextPhase contextPhase() const noexcept;
    core::UniquePtr<WebGlResources> restore(const semantics::WebGlResourceOwnershipRequest&,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> uploadQuota,
        const char*& diagnostic);

    core::UniquePtr<GpuBuffer> createBuffer(const BufferDesc&);
    CommandValidationResult writeBuffer(GpuBuffer&, const void*, std::uint64_t, std::uint64_t);
    core::UniquePtr<GpuTexture> createTexture(const TextureDesc&);
    CommandValidationResult uploadTexture(GpuTexture&, const semantics::WebGlColdTextureUpload&);
    core::UniquePtr<GpuTextureView> createTextureView(const GpuTexture&, const TextureViewDesc&);
    core::UniquePtr<GpuSampler> createSampler(const SamplerDesc&);
    core::UniquePtr<WebGlFramebuffer> createFramebuffer(
        GpuTexture& color, GpuTexture* depthStencil);
    std::shared_ptr<WebGlFramebuffer> retainFramebuffer(GpuTexture&, GpuTexture*);
    core::UniquePtr<WebGlProgram> createProgram(
        const std::shared_ptr<const shader::WebGlShaderProgramArtifact>&,
        const semantics::WebGlProgramCreationRequest&);
    core::UniquePtr<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc&);
    core::UniquePtr<DescriptorArena> createDescriptorArena(const DescriptorArenaDesc&);
    core::UniquePtr<RenderPipeline> createRenderPipeline(const RenderPipelineDesc&,
        const std::shared_ptr<PipelineLayout>&,
        const std::shared_ptr<const shader::WebGlShaderProgramArtifact>&,
        const semantics::WebGlPipelineCreationRequest&);
    PipelineValidationReport validateRenderPipeline(const RenderPipelineDesc&,
        const std::shared_ptr<PipelineLayout>&,
        const std::shared_ptr<const shader::WebGlShaderProgramArtifact>&,
        const semantics::WebGlPipelineCreationRequest&);

    core::UniquePtr<Swapchain> createCanvas(const SwapchainDesc&);
    const char* enableCanvasReuse(Swapchain&, const semantics::WebGlCanvasReuseRequest&);
    const char* recycleCanvas(Swapchain&, const AcquiredFrame&, core::UniquePtr<GpuTexture>&);
    std::shared_ptr<WebGlFramebuffer> canvasFramebuffer(Swapchain&, const AcquiredFrame&);
    GpuQueue& queue() noexcept;
    const char* diagnostic() const noexcept;
    std::string_view nativeDiagnostic() const noexcept;
    const char* currentDiagnostic() const noexcept;
    semantics::WebGlMemoryCounters counters() const noexcept;
    semantics::WebGlExecutionTelemetryReport executionTelemetry() const noexcept;
    const semantics::WebGlRuntimeProfile& profile() const noexcept;

private:
    const char* retireCpuDomain() noexcept;
    friend class WebGlDevice;
    friend class WebGlFramebuffer;
    friend class WebGlExecution;
    friend class WebGlOrderedGraphicsPayload;
    std::shared_ptr<WebGlResourceDomain> domain_;
    core::UniquePtr<GpuQueue> queue_;
};

class WebGlFramebuffer final {
    class ConstructionKey {
        friend class WebGlResources;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Impl;
    WebGlFramebuffer(ConstructionKey, core::UniquePtr<Impl>);
    ~WebGlFramebuffer();
    WebGlFramebuffer(const WebGlFramebuffer&) = delete;
    WebGlFramebuffer& operator=(const WebGlFramebuffer&) = delete;
    GpuResourceId id() const noexcept;
    const char* currentDiagnostic() const noexcept;

private:
    friend class WebGlResources;
    friend class WebGlDevice;
    friend class WebGlExecution;
    core::UniquePtr<Impl> impl_;
};
} // namespace arp::rhi::webgl
