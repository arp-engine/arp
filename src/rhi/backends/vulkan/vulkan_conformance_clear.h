#pragma once

#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/conformance/clear_execution.h"
#include "rhi/conformance/sampled_execution.h"
#include "rhi/conformance/storage_mrt_execution.h"
#include "rhi/frontend/resource_loss_validation.h"

#include <optional>

namespace arp::rhi_vulkan {
class VulkanRhiDevice;
class VulkanConformanceClearExecution;
class VulkanConformanceSampledInputs;

// Backend-private original recording journal. No public writable proof fields.
class VulkanConformanceClearFrame final {
    friend class VulkanConformanceClearExecution;
    friend class VulkanRhiNativeCommandPayload;

    class Key {
        friend class VulkanConformanceClearExecution;
        Key() = default;
    };

public:
    explicit VulkanConformanceClearFrame(Key, rhi::ConformanceClearFrame expected)
        : expected_(expected)
    {}

    bool reject(core::pmr::String& diagnostic);
    bool computePass(bool begin, core::pmr::String&);
    bool computePipeline(const VulkanRhiNativeResourcePayload&, core::pmr::String&);
    bool dispatch(std::uint32_t, std::uint32_t, std::uint32_t, core::pmr::String&);
    bool begin(const rhi::RenderPassDesc&,
        const core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>&,
        const VulkanRhiNativeResourcePayload*,
        core::pmr::String&);
    bool end(core::pmr::String&);
    bool transition(VulkanRhiNativeCommandPayload&,
        const VulkanRhiNativeResourcePayload&,
        const rhi::ResourceBarrierDesc&,
        core::pmr::String&);
    bool bufferBarrier(
        const VulkanRhiNativeResourcePayload&, const rhi::ResourceBarrierDesc&, core::pmr::String&);
    bool pipeline(const VulkanRhiNativeResourcePayload&, core::pmr::String&);
    bool viewport(const rhi::ViewportState&, core::pmr::String&);
    bool scissor(const rhi::ScissorState&, core::pmr::String&);
    bool vertex(
        std::uint32_t, const VulkanRhiNativeResourcePayload&, std::uint64_t, core::pmr::String&);
    bool index(
        const VulkanRhiNativeResourcePayload&, rhi::IndexFormat, std::uint64_t, core::pmr::String&);
    bool push(rhi::ShaderStage, std::uint32_t, std::uint32_t, const void*, core::pmr::String&);
    bool descriptors(
        const VulkanRhiNativeResourcePayload&, std::uint32_t, std::uint32_t, core::pmr::String&);
    bool draw(bool,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::int32_t,
        std::uint32_t,
        core::pmr::String&);

private:
    enum class State { Prepared, Color, Pass, Stored, Copied };
    rhi::ConformanceClearFrame expected_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> image_, staging_;
    std::shared_ptr<VulkanRhiSwapchainFrameSyncState> sync_;
    rhi::GpuResourceId imageId_;
    std::uint64_t serial_ = 0;
    State state_ = State::Prepared;
    const char* failure_ = nullptr;
    bool internalCopy_ = false;
    bool submitAuthorized_ = false;
    std::shared_ptr<VulkanConformanceSampledInputs> inputs_;
    core::UniquePtr<rhi::ConformanceSampledFrame> sampled_;
    core::UniquePtr<rhi::ConformanceStorageMrtFrame> storageMrt_;
    std::array<std::shared_ptr<VulkanRhiNativeResourcePayload>, 2> auxiliaryStaging_;
    bool computeActive_ = false, dispatched_ = false;
    bool storageBegin(const rhi::RenderPassDesc&,
        const core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>&,
        const VulkanRhiNativeResourcePayload*,
        core::pmr::String&);
    bool storageTransition(VulkanRhiNativeCommandPayload&,
        const VulkanRhiNativeResourcePayload&,
        const rhi::ResourceBarrierDesc&,
        core::pmr::String&);
    bool storageDescriptors(const VulkanRhiNativeResourcePayload&, core::pmr::String&);
    bool copyAuxiliary(VulkanRhiNativeCommandPayload&, core::pmr::String&);
    core::LifetimeEpoch::Ticket issuer_;
    std::size_t pass_ = 0;
    std::array<std::shared_ptr<VulkanRhiNativeResourcePayload>, 5> textures_;
    std::array<rhi::GpuResourceId, 5> textureIds_;
    std::array<rhi::ResourceState, 5> textureStates_{};
    std::uint32_t bindings_ = 0;
    std::size_t imageBarriers_ = 0, bufferBarriers_ = 0;
    bool drew_ = false;
    bool sampledBegin(const rhi::RenderPassDesc&,
        const core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>&,
        const VulkanRhiNativeResourcePayload*,
        core::pmr::String&);
    bool sampledTransition(VulkanRhiNativeCommandPayload&,
        const VulkanRhiNativeResourcePayload&,
        const rhi::ResourceBarrierDesc&,
        core::pmr::String&);
    bool copyToPresent(VulkanRhiNativeCommandPayload&,
        const VulkanRhiNativeResourcePayload&,
        const rhi::ResourceBarrierDesc&,
        core::pmr::String&);
};

class VulkanConformanceClearExecution final : public rhi::OriginalConformanceSampledExecution,
                                              public rhi::OriginalConformanceStorageMrtExecution {
    friend class VulkanRhiDevice;

    class Key {
        friend class VulkanRhiDevice;
        Key() = default;
    };

    struct Owner {
        rhi::OriginalDeviceLifetime::Ticket device;
        std::weak_ptr<VulkanRhiRuntime> runtime;
        core::LifetimeEpoch lifetime;
        std::optional<rhi::OriginalDeviceNativeLossObservation> loss;
        std::shared_ptr<const shader::GraphicsShaderModuleArtifact> artifact;
        std::shared_ptr<const shader::SingleEntryShaderArtifact> computeArtifact;
        const char* diagnostic() const noexcept;
    };

public:
    VulkanConformanceClearExecution(Key,
        rhi::GpuDevice&,
        VulkanRhiNativeResourceAllocator,
        const rhi::semantics::NativeBackendCapabilityContract&,
        rhi::ConformanceClearExecutionRequest,
        std::shared_ptr<core::CpuMemoryQuota>);
    const char* diagnostic(const rhi::GpuDevice&) const noexcept override;
    core::UniquePtr<rhi::CommandEncoder> prepare(
        const rhi::CommandEncoderDesc&, rhi::GpuTexture&, const char*&) override;
    rhi::QueueSubmitResult submit(const rhi::CommandBuffer&, rhi::GpuFence*) override;
    const char* finalizePresented() noexcept override;
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

    const rhi::OriginalConformanceRun* run() const noexcept override { return run_.get(); }

private:
    const char* initialize(core::UniquePtr<rhi::ConformanceClearWorkload>);
    const char* initialize(core::UniquePtr<rhi::ConformanceSampledWorkload>, std::uint64_t);
    const char* initialize(
        core::UniquePtr<rhi::ConformanceStorageMrtWorkload>, std::uint64_t, std::uint64_t);
    const char* initializeFrames(core::UniquePtr<rhi::ConformanceClearWorkload>,
        core::UniquePtr<rhi::ConformanceSampledWorkload>,
        core::UniquePtr<rhi::ConformanceStorageMrtWorkload> = {});
    const char* observeAuxiliary();
    const char* fail(const char* id) noexcept;
    rhi::GpuDevice* device_;
    VulkanRhiNativeResourceAllocator allocator_;
    const rhi::semantics::NativeBackendCapabilityContract* capabilityContract_;
    rhi::ConformanceClearExecutionRequest request_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::shared_ptr<Owner> owner_;
    core::UniquePtr<rhi::OriginalConformanceRun> run_;
    core::pmr::Vector<std::shared_ptr<VulkanConformanceClearFrame>> frames_;
    std::shared_ptr<VulkanRhiNativeCommandPayload> prepared_;
    bool submitted_ = false;
    std::shared_ptr<VulkanConformanceSampledInputs> inputs_;
};
} // namespace arp::rhi_vulkan
