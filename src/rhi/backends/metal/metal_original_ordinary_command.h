#pragma once

#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_original_command_custody.h"
#include "rhi/frontend/command_render_pass_preparation.h"

#include <thread>

namespace arp::rhi_metal {
class MetalRhiNativeCommandPayload;

// The ordinary encoder's one original Program and adopted journal. This is
// backend storage for Preparation6, never a second stream or cloud executor.
class MetalOriginalOrdinaryCommand final : public core::QuotaBoundSharedObjectIdentity,
                                           public MetalOriginalCommandCustody {
    friend class MetalRhiNativeCommandPayload;
    using Program = rhi::OriginalCommandStreamProgram;
    using K = rhi::CommandUsageKind;
    using Frozen = rhi::OriginalComputeDispatchRecordingAdmission::Frozen;

    class ConstructionKey {
        friend class MetalOriginalOrdinaryCommand;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalOrdinaryCommand(
        ConstructionKey, std::shared_ptr<const Program> program, rhi::CommandRecordingMeter meter)
        : program_(std::move(program)), meter_(std::move(meter))
    {}

    ~MetalOriginalOrdinaryCommand() override
    {
        if (owner_ != std::this_thread::get_id() && !terminal_)
            std::terminate();
    }

    // Complete semantic preflight before native storage or journal transfer.
    static const char* inspect(const std::shared_ptr<const Program>&,
        const rhi::GpuDevice&,
        const rhi::OriginalRenderPassPreparationRequest&) noexcept;

private:
    struct Pass {
        explicit Pass(std::size_t index) : command(index) {}

        const std::size_t command;
        MetalRhiNativeRenderPassPreparation native;
    };

    struct Attachment {
        std::size_t command, operand;
        rhi::GpuTexture::ContentRangeExpectation initialContent;
        rhi::GpuTexture::CommittedStateObservation initialState;
        bool finalStored, claimOwner;
        rhi::common::OriginalTextureContentControl::Value proposed;
        std::optional<rhi::common::OriginalTextureContentControl::Cells::PendingObservation>
            pending;
    };

    struct Dispatch {
        std::size_t command, pipelineSource;
        Frozen frozen;
    };

    static std::shared_ptr<MetalOriginalOrdinaryCommand> prepare(
        const std::shared_ptr<const Program>&,
        const rhi::GpuDevice&,
        const rhi::OriginalRenderPassPreparationRequest&,
        MetalRhiNativeCommandPayload&,
        core::pmr::String&);
    const char* validate() const noexcept;
    const char* validatePending() const noexcept;
    const char* claim() noexcept;
    const char* preflight(K, std::uint32_t) const noexcept;
    void consume() noexcept;
    rhi::CommandRecordingUsage increment(std::size_t) const noexcept;
    const rhi::OriginalCommandTextureViewSource& attachmentSource(const Attachment&) const;

    template <class Visitor> const char* visitBuffers(Visitor&& visitor) const noexcept
    {
        const auto& sources = program_->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources()
                                  ->sources();
        for (const auto& source : sources)
            if (const auto* buffer =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(&source))
                if (const auto* id = visitor(*(*buffer)->originalObject()))
                    return id;
        return nullptr;
    }

    bool beforeSubmit(const MetalRhiNativeCommandPayload&, core::pmr::String&) noexcept override;
    const char* beforeNativeCommit() noexcept override;
    bool nativeCompleted(bool) noexcept override;
    void abandonUnsubmitted() noexcept override;

    const std::shared_ptr<const Program> program_;
    const std::thread::id owner_ = std::this_thread::get_id();
    rhi::CommandRecordingMeter meter_;
    // Declared after Program: every journal participant remains alive at drain.
    core::UniquePtr<core::MutationJournal> journal_;
    core::pmr::Vector<Pass> passes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Attachment> attachments_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Dispatch> dispatches_{&core::defaultCpuMemoryResource()};
    std::shared_ptr<core::ExternalObjectSlot> encoderSlot_;
    std::shared_ptr<MetalRhiResourceRegistry> registry_;
    std::shared_ptr<void> nativeDevice_;
    // Recording identity only. Completion must never dereference this pointer.
    const MetalRhiNativeCommandPayload* command_ = nullptr;
    const rhi::ComputePipeline* computePipeline_ = nullptr;
    const rhi::RenderPipeline* graphicsPipeline_ = nullptr;
    const rhi::DescriptorSet* descriptor_ = nullptr;
    bool viewport_ = false, scissor_ = false;
    bool claimed_ = false, sealed_ = false, terminal_ = false;
    mutable const char* failure_ = nullptr;
};
} // namespace arp::rhi_metal
