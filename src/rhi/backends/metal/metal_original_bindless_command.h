#pragma once
#include "core/ownership/external_object_slot.h"
#include "rhi/backends/metal/metal_bindless_root.h"
#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"
#include "rhi/backends/metal/metal_original_command_custody.h"
#include "rhi/frontend/bindless_graphics_execution.h"
#include "rhi/frontend/bindless_graphics_pass_sequence.h"
#include "rhi/frontend/bindless_root_fence_execution.h"
#include "rhi/frontend/bindless_root_native_sequence.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/command_buffer_readback.h"

namespace arp::rhi_metal {
class MetalRhiNativeCommandPayload;

// The same original Root/4 buffer stream, recording seal, state journal and
// readback storage. Only the native command can issue submission/completion.
class MetalOriginalBindlessCommand final : public core::QuotaBoundSharedObjectIdentity,
                                           public MetalOriginalCommandCustody {
    friend class MetalRhiNativeCommandPayload;
    friend class MetalOriginalBindlessExecutionView;
    friend class MetalOriginalBindlessGraphicsExecutionView;
    using Closure = MetalRhiBindlessRootNativeClosure;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using Binding = rhi::OriginalCommandBufferReadbackBinding;
    using Life = rhi::OriginalCommandNativeLifecycleContract;
    using PassInput =
        rhi::OriginalBindlessGraphicsPassSequenceContract::Input<MetalOriginalBindlessGraphicsPass>;

    class ConstructionKey {
        friend class MetalOriginalBindlessCommand;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalBindlessCommand(ConstructionKey,
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Binding>,
        rhi::BindlessFaultSubmissionLease,
        std::shared_ptr<const MetalOriginalBindlessGraphicsPass>,
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Closure>,
        const rhi::OriginalBindlessRootReadbackSequence::Input& = {},
        const rhi::OriginalBindlessRootNativeSequence<Closure>::Input& = {},
        const PassInput& = {});
    ~MetalOriginalBindlessCommand() override;
    static std::shared_ptr<MetalRhiNativeCommandPayload> prepareOriginal(
        std::shared_ptr<const Closure>,
        std::shared_ptr<const Reservation>,
        std::shared_ptr<const Binding>,
        rhi::GpuDevice&,
        core::pmr::String&,
        bool deferRecording = false,
        std::shared_ptr<const MetalOriginalBindlessGraphicsPass> = {},
        std::shared_ptr<const Closure> = {},
        std::shared_ptr<core::AtomicCapacityBudget> = {},
        std::shared_ptr<const Closure> = {},
        const rhi::OriginalIndexedDrawFetchContract::Budget& = {},
        const rhi::OriginalBindlessRootReadbackSequence::Input& = {},
        const rhi::OriginalBindlessRootNativeSequence<Closure>::Input& = {},
        const PassInput& = {},
        const rhi::OriginalBindlessProducedGeometryInputs& = {});

private:
    // Shared by public cold admission and the native factory so a capacity
    // refusal happens before consuming the one-shot recording seal.
    static std::uint64_t submissionOwnerCount(const Closure& closure,
        std::uint64_t reservationOwners,
        const Binding& binding,
        const MetalOriginalBindlessGraphicsPass* graphics = nullptr,
        const Closure* next = nullptr,
        std::size_t clearPasses = 0u,
        const Closure* final = nullptr) noexcept
    {
        return std::uint64_t(closure.retainedOwnerCount()) + reservationOwners
            + binding.originalProgram()->retainedOwnerCount() + Binding::retainedOwnerCount + 3u
            + (graphics ? graphics->retainedOwnerCount() + 3u : 0u)
            + (next ? next->retainedOwnerCount() + clearPasses * 4u + 6u : 0u)
            + (final ? final->retainedOwnerCount() : 0u);
    }

    const char* advance(Life::Event) noexcept;
    const char* validatePending() const noexcept;
    static const char* validateGeometryFetch(const std::shared_ptr<const Closure>&,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>&,
        const rhi::OriginalIndexedDrawFetchContract::Budget&) noexcept;
    bool record(MetalRhiNativeCommandPayload&, core::pmr::String&);
    bool beforeSubmit(const MetalRhiNativeCommandPayload&, core::pmr::String&) noexcept override;
    const char* beforeNativeCommit() noexcept override;
    bool nativeCompleted(bool) noexcept override;
    void abandonUnsubmitted() noexcept override;

    struct ClearPass {
        std::size_t commandIndex = SIZE_MAX;
        std::shared_ptr<const rhi::OriginalCommandTextureViewSource> source;
        MetalRhiNativeResourceObservation parentIdentity, viewIdentity;
        MetalRhiNativeRenderPassPreparation native;
    };

    bool prepareStoragePasses(
        rhi::GpuDevice&, const std::shared_ptr<core::AtomicCapacityBudget>&, core::pmr::String&);
    const char* validateStoragePasses() const noexcept;
    const rhi::OriginalBindlessRootNativeSequence<Closure> roots_;
    const std::shared_ptr<const Closure>& closure_;
    const std::shared_ptr<const Closure>& nextClosure_;
    const std::shared_ptr<const Closure>& finalClosure_;
    core::pmr::Vector<ClearPass> clearPasses_{&core::defaultCpuMemoryResource()};
    core::UniquePtr<core::AtomicCapacityLease> attachmentLease_;
    const std::shared_ptr<const Reservation> reservation_;
    const rhi::OriginalBindlessRootReadbackSequence readbacks_;
    const std::shared_ptr<const Binding>& binding_;
    const std::shared_ptr<const MetalOriginalBindlessGraphicsPass> graphics_;
    core::pmr::Vector<std::shared_ptr<const MetalOriginalBindlessGraphicsPass>> producedPasses_{
        &core::defaultCpuMemoryResource()};
    // The authenticated stream forbids overlapping render/compute/transfer
    // scopes. Reuse one cold control block for each sequential native encoder;
    // pass count never causes an owned allocation during recording.
    std::shared_ptr<core::ExternalObjectSlot> encoderSlot_;
    rhi::BindlessFaultSubmissionLease fault_;
    rhi::OriginalBindlessRootFaultCustody additionalFaults_;
    Life::Phase phase_ = Life::Phase::Claimed;
    const MetalRhiNativeCommandPayload* command_ = nullptr;
};

std::shared_ptr<const rhi::OriginalBindlessRootExecution>
prepareMetalRhiOriginalBindlessRootExecution(rhi::GpuDevice&,
    const rhi::OriginalBindlessRootFenceExecutionInputs&,
    const rhi::OriginalBindlessRootFenceExecutionBudget&,
    core::pmr::String&);
std::shared_ptr<const rhi::OriginalBindlessGraphicsExecution>
prepareMetalRhiOriginalBindlessGraphicsExecution(rhi::GpuDevice&,
    const rhi::OriginalBindlessGraphicsExecutionInputs&,
    const rhi::OriginalBindlessGraphicsExecutionBudget&,
    core::pmr::String&);
} // namespace arp::rhi_metal
