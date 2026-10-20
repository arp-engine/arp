#pragma once

// Shared L2 base class for RHI command encoders.
//
// The Vulkan, Metal, WebGPU and Null command encoders previously duplicated
// the same L2 bookkeeping verbatim:
//   - commandCount_ / activePass_ / lastValidation_ state triple
//   - commandOk / commandFail helper factories
//   - "pass already active" guard at the top of every begin*Pass
//   - "unclosed pass on finish" guard at the top of finish()
//   - the three trivial transition(...) overloads that delegate to the
//     ResourceBarrierDesc overload
//
// This base class consolidates that shared state and guard logic. Backends
// inherit CommandEncoderBase instead of rhi::CommandEncoder directly and
// override only the backend-specific virtuals:
//   - transition(GpuBuffer&, const ResourceBarrierDesc&)
//   - transition(GpuTexture&, const ResourceBarrierDesc&)
//   - beginRenderPass / beginComputePass / beginTransferPass
//   - finish
//
// The three transition overloads WITHOUT a ResourceBarrierDesc are declared
// final on the base so backends cannot accidentally re-shadow them; they build
// a ResourceBarrierDesc and dispatch to the backend's desc overload.
//
// The base also introduces a finished_ guard that the native backends
// previously lacked (only Null enforced one-shot finish). finish() called twice
// now deterministically fails with a diagnostic instead of silently producing
// a second command buffer.
//
// Layering: header-only, namespace arp::rhi, depends only on the L2
// gpu_commands.h types. No native API dependency. The core-owned mutation
// journal is created once per encoder and transferred at finish.
//
// Status: Contract Complete.

#include "rhi/frontend/command_recorded_state.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_resources.h"

#include <cstdint>
#include <utility>

namespace arp::rhi {

inline PipelineStage pipelineStageForResourceState(const ResourceState state)
{
    switch (state) {
    case ResourceState::Undefined:
        return PipelineStage::TopOfPipe;
    case ResourceState::CopySrc:
    case ResourceState::CopyDst:
        return PipelineStage::Transfer;
    case ResourceState::ShaderRead:
        return PipelineStage::VertexShader | PipelineStage::FragmentShader
            | PipelineStage::ComputeShader;
    case ResourceState::ShaderWrite:
        return PipelineStage::ComputeShader;
    case ResourceState::ColorAttachment:
        return PipelineStage::ColorAttachmentOutput;
    case ResourceState::DepthAttachment:
        return PipelineStage::DepthStencil;
    case ResourceState::Present:
        return PipelineStage::Present;
    }
    return PipelineStage::TopOfPipe;
}

inline ResourceAccess resourceAccessForState(const ResourceState state)
{
    switch (state) {
    case ResourceState::Undefined:
        return ResourceAccess::None;
    case ResourceState::CopySrc:
        return ResourceAccess::TransferRead;
    case ResourceState::CopyDst:
        return ResourceAccess::TransferWrite;
    case ResourceState::ShaderRead:
        return ResourceAccess::ShaderRead;
    case ResourceState::ShaderWrite:
        return ResourceAccess::ShaderWrite;
    case ResourceState::ColorAttachment:
        return ResourceAccess::ColorAttachmentWrite;
    case ResourceState::DepthAttachment:
        return ResourceAccess::DepthStencilWrite;
    case ResourceState::Present:
        return ResourceAccess::PresentRead;
    }
    return ResourceAccess::None;
}

inline ResourceBarrierDesc makeInferredResourceBarrier(const GpuResourceId resource,
    const TextureSubresourceRange& range,
    const ResourceState before,
    const ResourceState after)
{
    ResourceBarrierDesc desc{
        resource,
        range,
        before,
        after,
        pipelineStageForResourceState(before),
        pipelineStageForResourceState(after),
        resourceAccessForState(before),
        resourceAccessForState(after),
    };
    return desc;
}

class CommandEncoderBase : public CommandEncoder {
public:
    // Bring the two ResourceBarrierDesc overloads from the base interface into
    // this class's scope so the three final overloads below can dispatch to the
    // backend's desc-overriding implementation via virtual resolution. Without
    // this using-declaration the final overloads would shadow the pure-virtual
    // desc overloads and fail to compile.
    using CommandEncoder::transition;

    CommandValidationResult lastValidation() const override final { return lastValidation_; }

    ResourceBarrierResult transition(
        GpuBuffer& buffer, ResourceState before, ResourceState after) override final
    {
        return transition(buffer, makeInferredResourceBarrier(buffer.id(), {}, before, after));
    }

    ResourceBarrierResult transition(
        GpuTexture& texture, ResourceState before, ResourceState after) override final
    {
        return transition(texture,
            makeInferredResourceBarrier(
                texture.id(), defaultTextureSubresourceRange(texture.desc()), before, after));
    }

    ResourceBarrierResult transition(GpuTexture& texture,
        const TextureSubresourceRange& range,
        ResourceState before,
        ResourceState after) override final
    {
        return transition(texture, makeInferredResourceBarrier(texture.id(), range, before, after));
    }

protected:
    core::UniquePtr<core::MutationJournal> recordedStates_ =
        core::makeUnique<core::MutationJournal>();
    // ---- Shared state -----------------------------------------------------
    //
    // These members are protected (not private) so backend subclasses can
    // hand references to their pass-encoder sub-objects, which stamp the
    // shared counter / active flag as they record commands. The pass encoders
    // historically take `std::uint32_t& commandCount, bool& active` by
    // reference; keeping these protected preserves that pattern without
    // accessor churn.

    std::uint32_t commandCount_ = 0;
    bool activePass_ = false;
    bool finished_ = false;
    CommandValidationResult lastValidation_{true, "", 0};

    std::uint32_t commandCount() const noexcept { return commandCount_; }

    bool hasActivePass() const noexcept { return activePass_; }

    bool isFinished() const noexcept { return finished_; }

    // ---- Shared helpers ---------------------------------------------------

    // Records a successful command: increments the counter and stamps the
    // validation result. Returns the new validation state.
    CommandValidationResult markCommandOk() noexcept
    {
        ++commandCount_;
        lastValidation_ = {true, "", commandCount_, CommandValidationStatus::Success, ""};
        return lastValidation_;
    }

    // Records a failed command: stamps the diagnostics without advancing the
    // counter. Returns the new validation state. Backends call this when a
    // native record call fails.
    CommandValidationResult markCommandFail(std::pmr::string diagnostics) noexcept
    {
        lastValidation_ = {false, std::move(diagnostics), commandCount_};
        return lastValidation_;
    }

    // Guard for begin*Pass implementations. When the guard fails it stamps
    // lastValidation_ with the supplied diagnostic and returns false; the
    // caller returns nullptr. When the guard passes, activePass_ is set true
    // and the caller proceeds with the backend-specific native pass begin.
    //
    // `alreadyActiveDiagnostic` is the message used when a pass is already
    // active (e.g. "Metal RHI command encoder has active pass"). `finished`
    // failures use a fixed message since they indicate a programming error.
    bool beginPassGuard(std::string_view alreadyActiveDiagnostic) noexcept
    {
        if (finished_) {
            lastValidation_ = {
                false, "RHI command encoder has already been finished", commandCount_};
            return false;
        }
        if (activePass_) {
            lastValidation_ = {false,
                core::pmr::String(alreadyActiveDiagnostic, &core::defaultCpuMemoryResource()),
                commandCount_};
            return false;
        }
        activePass_ = true;
        return true;
    }

    // Rolls back activePass_ after a failed native pass begin. Backends call
    // this when the native record*PassBegin call fails so the encoder returns
    // to the no-active-pass state.
    void rollbackPassBegin() noexcept { activePass_ = false; }

    // Guard for finish() implementations. When the guard fails it stamps
    // lastValidation_ with the supplied diagnostic and returns false; the
    // caller returns nullptr. When the guard passes, finished_ is set true so
    // a second finish() call deterministically fails, and the caller proceeds
    // with the backend-specific native finish.
    bool finishGuard(std::string_view unclosedPassDiagnostic) noexcept
    {
        if (finished_) {
            lastValidation_ = {
                false, "RHI command encoder has already been finished", commandCount_};
            return false;
        }
        if (activePass_) {
            lastValidation_ = {false,
                core::pmr::String(unclosedPassDiagnostic, &core::defaultCpuMemoryResource()),
                commandCount_};
            return false;
        }
        finished_ = true;
        return true;
    }

    // Resets the shared state. Intended for tests and full teardown.
    void resetEncoderState() noexcept
    {
        commandCount_ = 0;
        activePass_ = false;
        finished_ = false;
        lastValidation_ = {true, "", 0};
    }
};

} // namespace arp::rhi
