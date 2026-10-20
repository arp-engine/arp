#pragma once

// Test-only device proxy used to exercise FrameGraph capability branches
// against a real Null backend without changing production capabilities.

#include "compute_record_failure.h"
#include "rhi/gpu_device.h"

#include <array>
#include <cassert>
#include <string_view>

namespace arp::tests {

class CapabilityOverrideDevice final : public rhi::GpuDevice {
public:
    using Mutator = void (*)(rhi::DeviceCapabilities&);

    CapabilityOverrideDevice(rhi::GpuDevice& inner, Mutator mutator)
        : inner_(inner), mutator_(mutator)
    {}

    rhi::Backend backend() const override { return inner_.backend(); }

    rhi::DeviceCapabilities capabilities() const override
    {
        auto caps = inner_.capabilities();
        if (mutator_ != nullptr) {
            mutator_(caps);
        }
        return caps;
    }

    rhi::MemoryStats memoryStats() const override { return inner_.memoryStats(); }

    rhi::GpuTimingSample latestGpuTiming() const override { return inner_.latestGpuTiming(); }

    void overrideQueue(rhi::GpuQueue* queue) { queueOverride_ = queue; }

    // Explicit negative accepted-candidate injection; never production authority.
    using AcceptedComputeFactory = core::UniquePtr<rhi::ComputePipeline> (*)(void*,
        const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const rhi::OriginalComputePipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);
    using AcceptedGraphicsFactory = core::UniquePtr<rhi::RenderPipeline> (*)(void*,
        const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*&);

    void injectAcceptedFactory(
        void* context, AcceptedComputeFactory compute, AcceptedGraphicsFactory graphics) noexcept
    {
        acceptedFactoryContext_ = context;
        acceptedComputeFactory_ = compute;
        acceptedGraphicsFactory_ = graphics;
    }

    // Negative/logical publication fixtures only. This does not certify the
    // forwarded backend's native payload or grant execution authority.
    template <class T>
    core::UniquePtr<T> completeOriginalFixtureCandidate(core::UniquePtr<T> candidate)
    {
        return completeOriginalResourceCreation(std::move(candidate));
    }

    void failTexturePlacementQuery(bool fail) { failTexturePlacementQuery_ = fail; }

    std::uint64_t texturePlacementQueryCount() const { return texturePlacementQueryCount_; }

    void failFenceCreation(bool fail) { failFenceCreation_ = fail; }

    void failComputeRecording(std::string_view passLabel, ComputeRecordFailure::Point point)
    {
        failComputePassLabel_ = passLabel;
        failComputePoint_ = point;
        computeFailureStats_ = {};
    }

    void clearComputeRecordingFailure() { failComputePassLabel_ = {}; }

    const ComputeRecordFailure::Stats& computeRecordingFailureStats() const
    {
        return computeFailureStats_;
    }

    void failBufferCreation(std::string_view label) { failBufferCreateLabel_ = label; }

    void failBufferWrites(std::string_view label, bool afterWrite = false)
    {
        failBufferWriteLabel_ = label;
        failBufferWriteAfter_ = afterWrite;
        customBufferWriteDiagnostic_ = false;
    }

    void failBufferWritesWithDiagnostic(
        std::string_view label, bool afterWrite, std::string_view diagnostic)
    {
        failBufferWrites(label, afterWrite);
        customBufferWriteDiagnostic_ = true;
        bufferWriteDiagnostic_ = diagnostic;
    }

    std::uint64_t bufferWriteAttempts() const { return bufferWriteAttempts_; }

    std::uint64_t injectedBufferWriteFailures() const { return injectedBufferWriteFailures_; }

    std::uint64_t bufferCreateAttempts() const { return bufferCreateAttempts_; }

    // Observe an actual allocation by its explicit test fixture label. The
    // caller must not retain this non-owning pointer beyond the resource owner.
    void observeBuffer(std::string_view label, std::size_t slot = 0u)
    {
        assert(slot < observedBuffers_.size());
        observedBuffers_[slot] = {label, nullptr};
    }

    rhi::GpuBuffer* observedBuffer(std::size_t slot = 0u) const
    {
        assert(slot < observedBuffers_.size());
        return observedBuffers_[slot].buffer;
    }

    std::uint64_t observedBufferWriteAttempts(std::size_t slot = 0u) const
    {
        assert(slot < observedBuffers_.size());
        return observedBuffers_[slot].writes;
    }

    rhi::GpuQueue& queue() override { return queueOverride_ ? *queueOverride_ : inner_.queue(); }

    void waitIdle() override { inner_.waitIdle(); }

    rhi::GpuQueue* computeQueue() override { return inner_.computeQueue(); }

    rhi::TexturePlacementRequirements queryTexturePlacementRequirements(
        const rhi::TextureDesc& desc) override
    {
        ++texturePlacementQueryCount_;
        return failTexturePlacementQuery_ ? rhi::TexturePlacementRequirements{}
                                          : inner_.queryTexturePlacementRequirements(desc);
    }

    arp::core::UniquePtr<rhi::GpuBuffer> createBuffer(const rhi::BufferDesc& desc) override
    {
        ++bufferCreateAttempts_;
        if (!failBufferCreateLabel_.empty() && desc.label == failBufferCreateLabel_)
            return nullptr;
        auto buffer = inner_.createBuffer(desc);
        for (auto& observed : observedBuffers_)
            if (desc.label == observed.label)
                observed.buffer = buffer.get();
        return buffer;
    }

    rhi::CommandValidationResult writeBuffer(rhi::GpuBuffer& buffer,
        const void* data,
        std::uint64_t sizeBytes,
        std::uint64_t offsetBytes = 0) override
    {
        const bool fail =
            !failBufferWriteLabel_.empty() && buffer.desc().label == failBufferWriteLabel_;
        ++bufferWriteAttempts_;
        for (auto& observed : observedBuffers_)
            if (buffer.desc().label == observed.label)
                ++observed.writes;
        const auto failure = [this](std::string_view diagnostic) {
            ++injectedBufferWriteFailures_;
            return rhi::CommandValidationResult{false,
                core::pmr::String(
                    customBufferWriteDiagnostic_ ? bufferWriteDiagnostic_ : diagnostic),
                0u};
        };
        if (fail && !failBufferWriteAfter_)
            return failure("TEST-BUFFER-UPLOAD-FAILED");
        auto result = inner_.writeBuffer(buffer, data, sizeBytes, offsetBytes);
        if (fail && result.success)
            return failure("TEST-BUFFER-UPLOAD-FAILED-AFTER-WRITE");
        return result;
    }

    arp::core::UniquePtr<rhi::GpuBufferView> createBufferView(
        const rhi::GpuBuffer& buffer, const rhi::BufferViewDesc& desc) override
    {
        return inner_.createBufferView(buffer, desc);
    }

    arp::core::UniquePtr<rhi::GpuTexture> createTexture(const rhi::TextureDesc& desc) override
    {
        return inner_.createTexture(desc);
    }

    rhi::ResourceBarrierResult prepareTextureForPoolReuse(rhi::GpuTexture& texture) override
    {
        return inner_.prepareTextureForPoolReuse(texture);
    }

    rhi::ResourceBarrierResult prepareBufferForPoolReuse(rhi::GpuBuffer& buffer) override
    {
        return inner_.prepareBufferForPoolReuse(buffer);
    }

    arp::core::UniquePtr<rhi::GpuMemoryHeap> createMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, rhi::MemoryUsage usage) override
    {
        return inner_.createMemoryHeap(sizeBytes, alignmentBytes, usage);
    }

    std::string_view lastMemoryHeapCreationDiagnostic() const override
    {
        return inner_.lastMemoryHeapCreationDiagnostic();
    }

    arp::core::UniquePtr<rhi::GpuTexture> createPlacedTexture(const rhi::TextureDesc& desc,
        const rhi::GpuMemoryHeap& heap,
        std::uint64_t offsetBytes) override
    {
        return inner_.createPlacedTexture(desc, heap, offsetBytes);
    }

    arp::core::UniquePtr<rhi::GpuTextureView> createTextureView(
        const rhi::GpuTexture& texture, const rhi::TextureViewDesc& desc) override
    {
        return inner_.createTextureView(texture, desc);
    }

    arp::core::UniquePtr<rhi::GpuSampler> createSampler(const rhi::SamplerDesc& desc) override
    {
        return inner_.createSampler(desc);
    }

    arp::core::UniquePtr<rhi::PipelineLayout> createPipelineLayout(
        const rhi::PipelineLayoutDesc& desc) override
    {
        return inner_.createPipelineLayout(desc);
    }

    rhi::PipelineValidationReport validateRenderPipeline(
        const rhi::RenderPipelineDesc& desc) override
    {
        return inner_.validateRenderPipeline(desc);
    }

    rhi::PipelineValidationReport validateComputePipeline(
        const rhi::ComputePipelineDesc& desc) override
    {
        return inner_.validateComputePipeline(desc);
    }

    arp::core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        const rhi::RenderPipelineDesc& desc) override
    {
        return inner_.createRenderPipeline(desc);
    }

    arp::core::UniquePtr<rhi::ComputePipeline> createComputePipeline(
        const rhi::ComputePipelineDesc& desc) override
    {
        return inner_.createComputePipeline(desc);
    }

    arp::core::UniquePtr<rhi::DescriptorArena> createDescriptorArena(
        const rhi::DescriptorArenaDesc& desc) override
    {
        return inner_.createDescriptorArena(desc);
    }

    arp::core::UniquePtr<rhi::CommandEncoder> createCommandEncoder(
        const rhi::CommandEncoderDesc& desc) override
    {
        auto encoder = inner_.createCommandEncoder(desc);
        if (!encoder || failComputePassLabel_.empty())
            return encoder;
        return core::makeUnique<ComputeRecordFailure>(std::move(encoder),
            failComputePoint_,
            std::string_view(failComputePassLabel_),
            &computeFailureStats_);
    }

    arp::core::UniquePtr<rhi::SecondaryCommandEncoder> createSecondaryCommandEncoder(
        const rhi::SecondaryCommandEncoderDesc& desc) override
    {
        return inner_.createSecondaryCommandEncoder(desc);
    }

    arp::core::UniquePtr<rhi::RenderBundleEncoder> createRenderBundleEncoder(
        const rhi::RenderBundleDesc& desc) override
    {
        return inner_.createRenderBundleEncoder(desc);
    }

    arp::core::UniquePtr<rhi::Swapchain> createSwapchain(const rhi::SwapchainDesc& desc) override
    {
        return inner_.createSwapchain(desc);
    }

    arp::core::UniquePtr<rhi::GpuFence> createFence() override
    {
        return failFenceCreation_ ? nullptr : inner_.createFence();
    }

    arp::core::UniquePtr<rhi::GpuSemaphore> createSemaphore() override
    {
        return inner_.createSemaphore();
    }

    arp::core::UniquePtr<rhi::GpuTimelineSemaphore> createTimelineSemaphore(
        std::uint64_t initialValue = 0) override
    {
        return inner_.createTimelineSemaphore(initialValue);
    }

    rhi::PipelineCache& pipelineCache() override { return inner_.pipelineCache(); }

    rhi::DescriptorCache& descriptorCache() override { return inner_.descriptorCache(); }

    arp::core::UniquePtr<rhi::QueryPool> createQueryPool(const rhi::QueryPoolDesc& desc) override
    {
        return inner_.createQueryPool(desc);
    }

    void* queryInterface(std::string_view extensionId) noexcept override
    {
        return inner_.queryInterface(extensionId);
    }

protected:
    core::UniquePtr<rhi::ComputePipeline> createAcceptedOriginalComputePipeline(
        const rhi::ComputePipelineDesc& desc,
        const std::shared_ptr<const rhi::OriginalComputePipelineCreation>& creation,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        return acceptedComputeFactory_
            ? acceptedComputeFactory_(acceptedFactoryContext_, desc, creation, quota, diagnostic)
            : GpuDevice::createAcceptedOriginalComputePipeline(desc, creation, quota, diagnostic);
    }

    core::UniquePtr<rhi::RenderPipeline> createAcceptedOriginalGraphicsPipeline(
        const rhi::RenderPipelineDesc& desc,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>& creation,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic) override
    {
        return acceptedGraphicsFactory_
            ? acceptedGraphicsFactory_(acceptedFactoryContext_, desc, creation, quota, diagnostic)
            : GpuDevice::createAcceptedOriginalGraphicsPipeline(desc, creation, quota, diagnostic);
    }

private:
    std::uint64_t texturePlacementQueryCount_ = 0u;
    void* acceptedFactoryContext_ = nullptr;
    AcceptedComputeFactory acceptedComputeFactory_ = nullptr;
    AcceptedGraphicsFactory acceptedGraphicsFactory_ = nullptr;
    rhi::GpuQueue* queueOverride_ = nullptr;
    bool failFenceCreation_ = false;
    bool failTexturePlacementQuery_ = false;
    std::uint64_t bufferWriteAttempts_ = 0u;
    std::uint64_t injectedBufferWriteFailures_ = 0u;
    std::uint64_t bufferCreateAttempts_ = 0u;
    std::string_view failBufferWriteLabel_;
    bool failBufferWriteAfter_ = false;
    bool customBufferWriteDiagnostic_ = false;
    std::string_view bufferWriteDiagnostic_;
    std::string_view failBufferCreateLabel_;
    std::string_view failComputePassLabel_;
    ComputeRecordFailure::Point failComputePoint_ = ComputeRecordFailure::Point::Begin;
    ComputeRecordFailure::Stats computeFailureStats_{};

    struct ObservedBuffer {
        std::string_view label;
        rhi::GpuBuffer* buffer = nullptr;
        std::uint64_t writes = 0u;
    };

    std::array<ObservedBuffer, 8> observedBuffers_{};
    rhi::GpuDevice& inner_;
    Mutator mutator_ = nullptr;
};

} // namespace arp::tests
