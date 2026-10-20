#pragma once
#include "rhi/backends/webgl/webgl_execution.h"
#include "rhi/frontend/ordered_graphics_execution.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/webgl_cpu_retirement.h"
#include "rhi/semantics/webgl_device_composition.h"

namespace arp::rhi::webgl {
class WebGlDevice final : public GpuDevice {
    class ConstructionKey {
        friend class WebGlDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGlDevice(
        ConstructionKey, core::UniquePtr<WebGlResources>, std::shared_ptr<core::CpuMemoryQuota>);
    ~WebGlDevice() override;
    static core::UniquePtr<WebGlDevice> create(core::UniquePtr<WebGlResources>,
        const semantics::WebGlDeviceCompositionRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);

    core::UniquePtr<OriginalOrderedGraphicsExecution> createOrderedGraphicsExecution(
        const OrderedGraphicsExecutionRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);

    const char* selectPipelineInputs(const std::shared_ptr<PipelineLayout>&,
        const std::shared_ptr<const shader::WebGlShaderProgramArtifact>&,
        const semantics::WebGlPipelineCreationRequest&);
    const char* prepareCommand(const std::shared_ptr<RenderPipeline>&,
        const std::shared_ptr<WebGlFramebuffer>&,
        const semantics::WebGlPreparedCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&);
    const char* prepareCommand(core::ArrayView<std::shared_ptr<RenderPipeline>>,
        const std::shared_ptr<WebGlFramebuffer>&,
        const semantics::WebGlPreparedCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&);
    const char* prepareCommand(core::ArrayView<std::shared_ptr<RenderPipeline>>,
        core::ArrayView<std::shared_ptr<WebGlFramebuffer>>,
        const semantics::WebGlPreparedCommandsRequest&,
        const std::shared_ptr<core::CpuMemoryQuota>&);
    core::UniquePtr<WebGlFramebuffer> createFramebuffer(GpuTexture&, GpuTexture*);
    std::shared_ptr<WebGlFramebuffer> canvasFramebuffer(Swapchain&, const AcquiredFrame&);
    const char* enableCanvasReuse(Swapchain&, const semantics::WebGlCanvasReuseRequest&);
    const char* recycleCanvas(Swapchain&, const AcquiredFrame&, core::UniquePtr<GpuTexture>&);
    CommandValidationResult uploadTexture(GpuTexture&, const semantics::WebGlColdTextureUpload&);
    const char* diagnostic() const noexcept;
    semantics::WebGlExecutionTelemetryReport executionTelemetry() const noexcept;
    const semantics::WebGlRuntimeProfile& profile() const noexcept;
    const char* enableRecovery(const semantics::WebGlContextRecoveryRequest&);
    const char* enableSurfaceLifecycle(const semantics::WebGlSurfaceLifecycleRequest&);
    semantics::WebGlCpuRetirementReport retireCpu(const semantics::WebGlCpuRetirementRequest&);
    semantics::WebGlContextPhase contextPhase() const noexcept;
    core::UniquePtr<WebGlResources> restoreResources(
        const semantics::WebGlResourceOwnershipRequest&,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);

    Backend backend() const override;
    DeviceCapabilities capabilities() const override;
    MemoryStats memoryStats() const override;
    GpuQueue& queue() override;
    void waitIdle() override;
    semantics::ShutdownDrainReport drainForShutdown(
        const semantics::ShutdownDrainRequest&) override;
    core::UniquePtr<GpuBuffer> createBuffer(const BufferDesc&) override;
    CommandValidationResult writeBuffer(
        GpuBuffer&, const void*, std::uint64_t, std::uint64_t) override;
    core::UniquePtr<GpuTexture> createTexture(const TextureDesc&) override;
    core::UniquePtr<GpuTextureView> createTextureView(
        const GpuTexture&, const TextureViewDesc&) override;
    core::UniquePtr<GpuSampler> createSampler(const SamplerDesc&) override;
    core::UniquePtr<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc&) override;
    core::UniquePtr<DescriptorArena> createDescriptorArena(const DescriptorArenaDesc&) override;
    PipelineValidationReport validateRenderPipeline(const RenderPipelineDesc&) override;
    PipelineValidationReport validateComputePipeline(const ComputePipelineDesc&) override;
    core::UniquePtr<RenderPipeline> createRenderPipeline(const RenderPipelineDesc&) override;
    core::UniquePtr<CommandEncoder> createCommandEncoder(const CommandEncoderDesc&) override;
    PipelineCache& pipelineCache() override;
    DescriptorCache& descriptorCache() override;

    core::UniquePtr<GpuBufferView> createBufferView(
        const GpuBuffer&, const BufferViewDesc&) override;
    core::UniquePtr<GpuMemoryHeap> createMemoryHeap(
        std::uint64_t, std::uint64_t, MemoryUsage) override;
    std::string_view lastMemoryHeapCreationDiagnostic() const override;
    core::UniquePtr<GpuTexture> createPlacedTexture(
        const TextureDesc&, const GpuMemoryHeap&, std::uint64_t) override;
    core::UniquePtr<ComputePipeline> createComputePipeline(const ComputePipelineDesc&) override;
    core::UniquePtr<SecondaryCommandEncoder> createSecondaryCommandEncoder(
        const SecondaryCommandEncoderDesc&) override;
    core::UniquePtr<RenderBundleEncoder> createRenderBundleEncoder(
        const RenderBundleDesc&) override;
    core::UniquePtr<Swapchain> createSwapchain(const SwapchainDesc&) override;
    core::UniquePtr<GpuFence> createFence() override;
    core::UniquePtr<GpuSemaphore> createSemaphore() override;
    core::UniquePtr<GpuTimelineSemaphore> createTimelineSemaphore(std::uint64_t) override;
    core::UniquePtr<QueryPool> createQueryPool(const QueryPoolDesc&) override;

private:
    template <class T> core::UniquePtr<T> reject(const char* id)
    {
        diagnostic_ = id;
        return {};
    }

    std::shared_ptr<WebGlResources> resources_;
    core::LifetimeEpoch frameExecutionLifetime_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::pmr::String pipelinePolicy_{&core::defaultCpuMemoryResource()};
    core::pmr::String descriptorPolicy_{&core::defaultCpuMemoryResource()};
    PipelineCache pipelines_;
    DescriptorCache descriptors_;
    std::shared_ptr<PipelineLayout> layout_;
    std::shared_ptr<const shader::WebGlShaderProgramArtifact> artifact_;
    semantics::WebGlPipelineCreationRequest pipelineRequest_;
    core::UniquePtr<CommandEncoder> prepared_;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::rhi::webgl
