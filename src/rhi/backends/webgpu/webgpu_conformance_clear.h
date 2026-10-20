#pragma once

#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_original_canvas.h"
#include "rhi/conformance/storage_mrt_execution.h"
#include "rhi/frontend/resource_loss_validation.h"
#include "rhi/frontend/transfer_readback_copy.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"

namespace arp::rhi_webgpu {
class WebGpuRhiDevice;
class WebGpuConformanceClearExecution;
class WebGpuConformanceSampledInputs;

// An original encoder's finite journal, not a generic recording grant.
class WebGpuConformanceClearFrame final {
    friend class WebGpuConformanceClearExecution;
    friend struct WebGpuNativeCommandPayload;

    class Key {
        friend class WebGpuConformanceClearExecution;
        Key() = default;
    };

public:
    explicit WebGpuConformanceClearFrame(Key, rhi::ConformanceClearFrame expected)
        : expected_(expected)
    {}

    ~WebGpuConformanceClearFrame();

private:
    using Receipt = WebGpuNativeCompletionReceipt;
    enum class State { Prepared, Color, Pass, Stored, Copied, Finished };

    struct Listener {
        WebGpuConformanceClearFrame* owner = nullptr;
        std::size_t index = 0;
        std::uintptr_t receipt = 0;
        bool drained = false;
    };

    const char* reject(const char*) noexcept;
    bool sampledTransition(WebGpuNativeCommandPayload&,
        const WebGpuNativeTexturePayload&,
        const rhi::ResourceBarrierDesc&,
        core::pmr::String&);
    bool sampledBegin(const rhi::RenderPassDesc&,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>&,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>&,
        const WebGpuNativeTexturePayload*,
        core::pmr::String&);
    bool bufferBarrier(
        const WebGpuNativeBufferPayload&, const rhi::ResourceBarrierDesc&, core::pmr::String&);
    bool pipeline(const WebGpuNativeRenderPipelinePayload&, core::pmr::String&);
    bool viewport(const rhi::ViewportState&, core::pmr::String&);
    bool scissor(const rhi::ScissorState&, core::pmr::String&);
    bool vertex(std::uint32_t, const WebGpuNativeBufferPayload&, std::uint64_t, core::pmr::String&);
    bool index(
        const WebGpuNativeBufferPayload&, rhi::IndexFormat, std::uint64_t, core::pmr::String&);
    bool push(rhi::ShaderStage, std::uint32_t, std::uint32_t, const void*, core::pmr::String&);
    bool descriptors(const WebGpuNativeDescriptorGroupPayload*,
        std::uint32_t,
        std::uint32_t,
        core::pmr::String&);
    bool draw(bool,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::int32_t,
        std::uint32_t,
        core::pmr::String&);
    bool bind(std::uint32_t, core::pmr::String&);
    bool rejectSampled(core::pmr::String&);
    bool transition(WebGpuNativeCommandPayload&,
        const WebGpuNativeTexturePayload&,
        const rhi::ResourceBarrierDesc&,
        core::pmr::String&);
    bool begin(const rhi::RenderPassDesc&,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>&,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>&,
        const WebGpuNativeTexturePayload*,
        core::pmr::String&);
    bool end(core::pmr::String&);
    bool finish(core::pmr::String&);
    const char* arm(const std::shared_ptr<WebGpuConformanceClearFrame>&,
        const std::shared_ptr<WebGpuNativeCommandPayload>&);
    void closeScopes() noexcept;
    void cancel() noexcept;
    void releaseIfDrained() noexcept;
    static void completed(void*, Receipt::Outcome) noexcept;
    static void mapped(void*, WebGpuNativeBufferMapReceipt::Outcome) noexcept;
    static void lost(void*) noexcept;

    struct Readback {
        WebGpuConformanceClearFrame* owner = nullptr;
        std::shared_ptr<WebGpuNativeBufferPayload> staging;
        rhi::semantics::TextureReadbackCopyLowering::Decision copy;
        core::ByteBuffer padded{&core::defaultCpuMemoryResource()};
        WebGpuNativeBufferMapReceipt map;
        bool ready = false;
    };

    bool storageTransition(WebGpuNativeCommandPayload&,
        const WebGpuNativeTexturePayload&,
        const rhi::ResourceBarrierDesc&,
        core::pmr::String&);
    bool storageBegin(const rhi::RenderPassDesc&,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>&,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>&,
        const WebGpuNativeTexturePayload*,
        core::pmr::String&);
    bool computePass(bool, core::pmr::String&);
    bool computePipeline(const WebGpuNativeComputePipelinePayload&, core::pmr::String&);
    bool dispatch(std::uint32_t, std::uint32_t, std::uint32_t, core::pmr::String&);
    bool storageDescriptors(const WebGpuNativeDescriptorGroupPayload*,
        std::uint32_t,
        std::uint32_t,
        core::pmr::String&);
    bool copyOutputs(WebGpuNativeCommandPayload&, core::pmr::String&);
    bool rejectStorage(core::pmr::String&);

    std::size_t readbackCount() const noexcept { return storageMrt_ ? 3 : 1; }

    const rhi::ConformanceClearFrame expected_;
    std::optional<rhi::ConformanceSampledFrame> sampled_;
    std::optional<rhi::ConformanceStorageMrtFrame> storageMrt_;
    bool computeActive_ = false, dispatched_ = false;
    std::shared_ptr<WebGpuConformanceSampledInputs> inputs_;
    std::array<std::shared_ptr<WebGpuNativeTexturePayload>, 5> textures_;
    std::array<rhi::GpuResourceId, 5> textureIds_;
    std::array<rhi::ResourceState, 5> textureStates_{};
    std::size_t pass_ = 0, imageBarriers_ = 0, bufferBarriers_ = 0;
    std::size_t frameIndex_ = 0;
    std::uint32_t bindings_ = 0;
    bool drew_ = false;
    State state_ = State::Prepared;
    core::LifetimeEpoch::Ticket issuer_;
    std::shared_ptr<WebGpuOriginalCanvas> canvas_;
    std::shared_ptr<WebGpuNativeTexturePayload> image_;
    std::shared_ptr<WebGpuNativeCommandPayload> retainedCommand_;
    std::shared_ptr<WebGpuConformanceClearFrame> callbackLease_;
    std::array<Readback, 3> readbacks_;
    std::array<Listener, 1 + WebGpuErrorScopeContract::count> listeners_;
    void* nativeDevice_ = nullptr;
    void* nativeQueue_ = nullptr;
    std::uintptr_t lossRegistration_ = 0;
    const char* failure_ = nullptr;
    bool scopeClaimed_ = false, scopesOpen_ = false, submitted_ = false, queueCompleted_ = false,
         physicalLoss_ = false, outputReady_ = false, terminal_ = false;
};

class WebGpuConformanceClearExecution final : public rhi::OriginalConformanceSampledExecution,
                                              public rhi::OriginalConformanceStorageMrtExecution {
    friend class WebGpuRhiDevice;

    class Key {
        friend class WebGpuRhiDevice;
        Key() = default;
    };

    struct Owner {
        rhi::OriginalDeviceLifetime::Ticket device;
        std::optional<rhi::OriginalDeviceNativeLossObservation> loss;
        core::LifetimeEpoch epoch;
        std::shared_ptr<const shader::GraphicsShaderModuleArtifact> artifact;
        std::shared_ptr<WebGpuConformanceSampledInputs> inputs;
        const char* diagnostic() const noexcept;
    };

public:
    WebGpuConformanceClearExecution(Key,
        rhi::GpuDevice&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        rhi::ConformanceClearExecutionRequest,
        std::shared_ptr<core::CpuMemoryQuota>);
    ~WebGpuConformanceClearExecution() override;
    const char* diagnostic(const rhi::GpuDevice&) const noexcept override;

    rhi::ConformanceSubmissionCompletion submissionCompletion() const noexcept override
    {
        return rhi::ConformanceSubmissionCompletion::IssuerOwnedColdObservation;
    }

    core::UniquePtr<rhi::CommandEncoder> prepare(
        const rhi::CommandEncoderDesc&, rhi::GpuTexture&, const char*&) override;
    rhi::QueueSubmitResult submit(const rhi::CommandBuffer&, rhi::GpuFence*) override;
    const char* finalizePresented() noexcept override;

    const rhi::OriginalConformanceRun* run() const noexcept override { return run_.get(); }

    std::shared_ptr<rhi::GpuBuffer> prepareInput(
        rhi::ConformanceSampledInput, std::size_t, const char*&) override;
    std::shared_ptr<rhi::GpuSampler> prepareSampler(const char*&) override;
    std::shared_ptr<rhi::RenderPipeline> preparePipeline(const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>&,
        const char*&) override;
    std::shared_ptr<rhi::GpuTexture> prepareStorageTexture(
        rhi::ConformanceOutputRole, std::size_t, const char*&) override;
    std::shared_ptr<rhi::GpuBuffer> prepareStorageInput(std::size_t, const char*&) override;
    std::shared_ptr<rhi::ComputePipeline> prepareComputePipeline(const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const shader::SingleEntryShaderArtifact>&,
        const char*&) override;

private:
    const char* initialize(core::UniquePtr<rhi::ConformanceStorageMrtWorkload>,
        const rhi::ConformanceStorageMrtExecutionRequest&);
    const char* prepareReadback(WebGpuConformanceClearFrame&,
        WebGpuConformanceClearFrame::Readback&,
        const rhi::TextureDesc&,
        rhi::GpuResourceId);
    const char* observeAuxiliary();
    const char* initialize(core::UniquePtr<rhi::ConformanceClearWorkload>);
    const char* initialize(core::UniquePtr<rhi::ConformanceSampledWorkload>,
        const rhi::ConformanceSampledExecutionRequest&);
    const char* initializeFrames(core::UniquePtr<rhi::ConformanceClearWorkload>,
        core::UniquePtr<rhi::ConformanceSampledWorkload>,
        core::UniquePtr<rhi::ConformanceStorageMrtWorkload> = {});
    const char* fail(const char*) noexcept;
    rhi::GpuDevice* device_;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_;
    const rhi::ConformanceClearExecutionRequest request_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::shared_ptr<Owner> owner_;
    std::shared_ptr<WebGpuConformanceSampledInputs> inputs_;
    core::UniquePtr<rhi::OriginalConformanceRun> run_;
    core::pmr::Vector<std::shared_ptr<WebGpuConformanceClearFrame>> frames_;
    std::shared_ptr<WebGpuNativeCommandPayload> prepared_;
};
} // namespace arp::rhi_webgpu
