#pragma once

#include "core/container/bounded_sequence.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"

namespace arp::rhi_webgpu {

// Frozen, finite non-bindless recording language. Only the public backend
// factory may install it. All records retain factory-owned native closures;
// replay never reads caller arguments or invokes renderer callbacks.
class WebGpuPrototypeBatch final {
    friend struct WebGpuNativeCommandPayload;

public:
    enum class Op {
        BufferBarrier,
        TextureBarrier,
        BufferMemory,
        TextureMemory,
        RenderBegin,
        RenderEnd,
        ComputeBegin,
        ComputeEnd,
        GraphicsPipeline,
        ComputePipeline,
        RenderDescriptors,
        ComputeDescriptors,
        Viewport,
        Scissor,
        Push,
        Draw,
        Dispatch,
        PushDebug,
        PopDebug,
        Marker
    };

    struct Record {
        Op op;
        rhi::ResourceBarrierDesc barrier;
        rhi::ResourceMemoryBarrierDesc memory;
        rhi::ResourceState memoryState = rhi::ResourceState::Undefined;
        rhi::RenderPassDesc render;
        rhi::ComputePassDesc compute;
        rhi::ViewportState viewport;
        rhi::ScissorState scissor;
        rhi::ShaderStage stages = rhi::ShaderStage::None;
        std::array<std::uint32_t, 4> args{};
        std::shared_ptr<WebGpuNativeBufferPayload> buffer;
        std::shared_ptr<WebGpuNativeTexturePayload> texture;
        std::shared_ptr<WebGpuNativeRenderPipelinePayload> graphics;
        std::shared_ptr<WebGpuNativeComputePipelinePayload> pipeline;
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> descriptors;
        core::pmr::Vector<std::shared_ptr<WebGpuNativeTexturePayload>> colors, resolves;
        core::ByteBuffer bytes{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<std::uint32_t> offsets;
        core::pmr::String label;
    };

    struct Completion {
        bool nativeSuccess = false;
        bool accepted = false;
        bool terminal = false;
        const char* diagnostic = "RHI-PROTOTYPE-BATCH-NOT-SUBMITTED";
    };

    class Fence final : public rhi::GpuFence {
    public:
        explicit Fence(std::shared_ptr<Completion> state) : state_(std::move(state)) {}

        void signal() override;

        bool isSignaled() const override { return state_->nativeSuccess; }

        void wait() override;
        void reset() override;

    private:
        std::shared_ptr<Completion> state_;
    };

    WebGpuPrototypeBatch(rhi::PrototypeCommandBatchBudget,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<WebGpuRhiResourceRegistry>);
    ~WebGpuPrototypeBatch();

    const char* diagnostic() const noexcept { return failure_; }

    bool capturing() const noexcept { return !replaying_ && !sealed_; }

    bool replaying() const noexcept { return replaying_; }

    rhi::ResourceState replayMemoryState() const noexcept
    {
        return replayRecord_ ? replayRecord_->memoryState : rhi::ResourceState::Undefined;
    }

    bool sealed() const noexcept { return sealed_; }

    bool append(Record&&, core::pmr::String&);

    template <class Builder> bool capture(Op op, Builder&& build, core::pmr::String& message)
    {
        if (failure_ || !capturing() || !records_.canAppend())
            return reject("RHI-PROTOTYPE-BATCH-COMMAND-CAPACITY-OR-STATE", message);
        try {
            core::CpuMemoryQuotaScope quota(quota_);
            Record record{op};
            return build(record) && append(std::move(record), message);
        } catch (const std::bad_alloc&) {
            return reject("RHI-PROTOTYPE-BATCH-CPU-BUDGET", message);
        }
    }

    const char* latch(const char* id) noexcept
    {
        if (!failure_)
            failure_ = id;
        return failure_;
    }

    bool reject(const char*, core::pmr::String&);
    bool resource(Record&, rhi::GpuResourceId, const void*, bool, core::pmr::String&);
    bool validateLeases(core::pmr::String&);
    bool replay(WebGpuNativeCommandPayload&, core::pmr::String&);
    bool submit(const std::shared_ptr<WebGpuNativeCommandPayload>&, core::pmr::String&);
    void discard() noexcept;

    std::shared_ptr<rhi::GpuFence> fence() const { return fence_; }

private:
    using Receipt = WebGpuNativeCompletionReceipt;

    struct Listener {
        WebGpuPrototypeBatch* owner = nullptr;
        std::uintptr_t receipt = 0;
        bool drained = false;
    };
    enum class Pass { None, Render, Compute };
    bool openScopes(WebGpuNativeCommandPayload&, std::size_t, core::pmr::String&);
    bool closeScopes(std::size_t, core::pmr::String&);
    bool wait(std::size_t, std::size_t, core::pmr::String&);
    bool charge(std::uint64_t, core::pmr::String&);
    static bool textureLeaseValid(const std::shared_ptr<WebGpuNativeTexturePayload>&) noexcept;
    void releaseIfDrained() noexcept;
    void cancelPrepared() noexcept;
    static void completed(void*, Receipt::Outcome) noexcept;
    static void lost(void*) noexcept;
    const rhi::PrototypeCommandBatchBudget budget_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    core::BoundedSequence<Record> records_;
    std::shared_ptr<Completion> completion_;
    std::shared_ptr<Fence> fence_;
    std::array<Listener, 1 + WebGpuErrorScopeContract::count> listeners_{};
    std::shared_ptr<WebGpuNativeCommandPayload> callbackLease_;
    std::weak_ptr<WebGpuNativeCommandPayload> command_;
    void* device_ = nullptr;
    void* queue_ = nullptr;
    std::uintptr_t lossRegistration_ = 0;
    std::uint64_t gpuBytes_ = 0;
    Pass pass_ = Pass::None;
    std::uint32_t debugDepth_ = 0;
    bool pipelineBound_ = false;
    bool replaying_ = false, sealed_ = false, submitted_ = false;
    bool scopeClaimed_ = false, scopesOpen_ = false, physicalLoss_ = false;
    bool nativeUncertain_ = false;
    const char* failure_ = nullptr;
    const Record* replayRecord_ = nullptr;
};
} // namespace arp::rhi_webgpu
