#pragma once

#include "core/runtime/completion_retention_pool.h"
#include "core/runtime/external_callback_gate.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_original_state_transaction.h"
#include "rhi/backends/webgpu/webgpu_original_texture_readback_copies.h"
#include "rhi/backends/webgpu/webgpu_original_texture_upload_transaction.h"
#include "rhi/frontend/transfer_readback_cold_budget.h"

namespace arp::test {
class WebGpuOriginalDeviceLossRegistrationFixture;
}

namespace arp::rhi_webgpu {

class WebGpuOrdinaryCompletionOwner;
struct WebGpuOrdinaryCompletionBudget;
std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOrdinaryCompletionOwner(
    const WebGpuOrdinaryCompletionBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    const char*& diagnostic);
std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOriginalReadbackCompletionOwner(
    const std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>&,
    const WebGpuOrdinaryCompletionBudget&,
    const rhi::DeviceLossRegistrationBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    const char*&);

// WebOrdinaryCompletionPreparation/1: callback metadata only. Resource graph,
// native byte budgets and executable completion lowering remain independent.
struct WebGpuOrdinaryCompletionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingRecords = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class WebGpuOrdinaryCompletionGraph final {
public:
    WebGpuOrdinaryCompletionGraph(std::shared_ptr<WebGpuNativeCommandPayload> command,
        std::shared_ptr<WebGpuNativeCompletionState> completion)
        : command_(std::move(command)), completion_(std::move(completion)),
          transaction_(command_->originalStateTransaction()),
          textureTransaction_(command_->originalTextureTransaction()),
          readbackCopies_(command_->originalReadbackCopies()), queue_(command_->nativeQueue()),
          identity_(command_->recordingIdentity())
    {}

    ~WebGpuOrdinaryCompletionGraph()
    {
        if (transaction_)
            transaction_->abandon();
        if (textureTransaction_)
            textureTransaction_->abandon();
        if (completion_)
            completion_->releaseSignal(signalTicket_);
    }

    WebGpuOrdinaryCompletionGraph(const WebGpuOrdinaryCompletionGraph&) = delete;
    WebGpuOrdinaryCompletionGraph& operator=(const WebGpuOrdinaryCompletionGraph&) = delete;

    const auto& command() const noexcept { return command_; }

    const auto& completion() const noexcept { return completion_; }

    bool matches(void* device) const noexcept
    {
        return command_->sharedOwnerMatches(command_) && command_->completionDeviceMatches(device)
            && command_->nativeQueue() == queue_ && command_->recordingIdentity() == identity_
            && core::SharedOwnerProjection::sameOwner(
                command_->originalStateTransaction(), transaction_)
            && core::SharedOwnerProjection::sameOwner(
                command_->originalTextureTransaction(), textureTransaction_)
            && core::SharedOwnerProjection::sameOwner(
                command_->originalReadbackCopies(), readbackCopies_)
            && (!readbackCopies_
                || (readbackCopies_->hasQuotaBoundSharedOwner(readbackCopies_)
                    && !readbackCopies_->validate() && transaction_
                    && core::SharedOwnerProjection::sameOwner(
                        readbackCopies_->transaction(), transaction_)))
            && (!completion_
                || (completion_->ordinarySignalOwnerMatches(completion_)
                    && completion_->signalMatches(signalTicket_)));
    }

    bool reserveSignal() noexcept
    {
        if (!completion_)
            return true;
        if (signalTicket_)
            return false;
        signalTicket_ = completion_->reserveSignal();
        return bool(signalTicket_);
    }

    bool publishSignal() noexcept
    {
        return !completion_ || completion_->completeSignal(signalTicket_);
    }

    void quarantineState() noexcept
    {
        if (readbackCopies_)
            readbackCopies_->invalidateNativeCompletion();
        if (transaction_)
            transaction_->invalidate();
        if (textureTransaction_)
            textureTransaction_->invalidate();
    }

private:
    const std::shared_ptr<WebGpuNativeCommandPayload> command_;
    const std::shared_ptr<WebGpuNativeCompletionState> completion_;
    const std::shared_ptr<WebGpuOriginalStateTransaction> transaction_;
    const std::shared_ptr<WebGpuOriginalTextureUploadTransaction> textureTransaction_;
    const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> readbackCopies_;
    void* const queue_;
    const std::uint64_t identity_;
    WebGpuNativeCompletionState::SignalTicket signalTicket_;
};

class WebGpuOrdinaryCompletionOwner final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOrdinaryCompletionOwner(
            const WebGpuOrdinaryCompletionBudget&,
            std::shared_ptr<core::CpuMemoryQuota>,
            const char*&);
        friend std::shared_ptr<WebGpuOrdinaryCompletionOwner>
        prepareWebGpuOriginalReadbackCompletionOwner(
            const std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>&,
            const WebGpuOrdinaryCompletionBudget&,
            const rhi::DeviceLossRegistrationBudget&,
            std::shared_ptr<core::CpuMemoryQuota>,
            const char*&);
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOrdinaryCompletionOwner(
        ConstructionKey, std::size_t capacity, std::shared_ptr<core::CpuMemoryQuota> quota)
        : retained(capacity), preparationQuota(std::move(quota))
    {}

    ~WebGpuOrdinaryCompletionOwner();

    static const char* validateBudget(const WebGpuOrdinaryCompletionBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-COMPLETION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-COMPLETION-OPT-IN";
        if (!budget.maxPendingRecords || !budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORDINARY-COMPLETION-BUDGET";
        return nullptr;
    }

    core::runtime::CompletionRetentionPool<WebGpuOrdinaryCompletionGraph> retained;
    const std::shared_ptr<core::CpuMemoryQuota> preparationQuota;
    static const char* detachOriginalDeviceLoss(
        const std::shared_ptr<WebGpuOrdinaryCompletionOwner>&) noexcept;

    const auto& originalReadbackPlan() const noexcept { return originalPlan_; }

    bool originalLossArmed() const noexcept
    {
        return originalLoss_ && originalLoss_->nativeState
            && originalLoss_->registrationCapacity.charged()
            && originalLoss_->callbackCapacity.charged()
            && originalLoss_->gate.phase() == core::runtime::ExternalCallbackGate::Phase::Armed;
    }

    bool originalLossDelivered() const noexcept
    {
        return originalLoss_
            && originalLoss_->gate.phase() == core::runtime::ExternalCallbackGate::Phase::Delivered;
    }

    void* nativeDevice() const noexcept
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return device_;
#else
        return nullptr;
#endif
    }

private:
    friend class arp::test::WebGpuOriginalDeviceLossRegistrationFixture;
    friend std::shared_ptr<WebGpuOrdinaryCompletionOwner>
    prepareWebGpuOriginalReadbackCompletionOwner(
        const std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>&,
        const WebGpuOrdinaryCompletionBudget&,
        const rhi::DeviceLossRegistrationBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);

    struct OriginalLossRegistration {
        explicit OriginalLossRegistration(
            const std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan>& plan) noexcept
            : registrationCapacity(
                  rhi::TextureReadbackColdBudgetPlan::originalLossRegistrationCapacity(plan), 1u),
              callbackCapacity(
                  rhi::TextureReadbackColdBudgetPlan::originalLossCallbackCapacity(plan), 1u)
        {}

        core::AtomicCapacityLease registrationCapacity, callbackCapacity;
        core::runtime::ExternalCallbackGate gate;
        std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner;
        std::uintptr_t nativeState = 0u;

        ~OriginalLossRegistration() { releaseWebGpuNativeDeviceLossRegistration(nativeState); }
    };

    static void consumeOriginalDeviceLoss(void*) noexcept;
    std::shared_ptr<const rhi::TextureReadbackColdBudgetPlan> originalPlan_;
    core::UniquePtr<OriginalLossRegistration> originalLoss_;
    friend std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOrdinaryCompletionOwner(
        const WebGpuOrdinaryCompletionBudget&, std::shared_ptr<core::CpuMemoryQuota>, const char*&);
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUDevice device_ = nullptr;
#endif
};

class WebGpuOrdinaryCompletionReservation final {
public:
    using Ticket = core::runtime::CompletionRetentionPool<WebGpuOrdinaryCompletionGraph>::Ticket;

    explicit WebGpuOrdinaryCompletionReservation(
        std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner)
        : owner_(std::move(owner))
    {}

    ~WebGpuOrdinaryCompletionReservation()
    {
        if (owner_)
            owner_->retained.cancel(ticket_);
    }

    const auto& owner() const noexcept { return owner_; }

    Ticket ticket() const noexcept { return ticket_; }

    static core::UniquePtr<WebGpuOrdinaryCompletionReservation> prepare(
        const std::shared_ptr<WebGpuOrdinaryCompletionOwner>& owner,
        const std::shared_ptr<WebGpuNativeCommandPayload>& command,
        const std::shared_ptr<WebGpuNativeCompletionState>& completion,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!owner || !owner->sharedOwnerMatches(owner) || !owner->retained.accepting()
            || webGpuNativeDeviceLost()) {
            diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
            return {};
        }
        if (!command || !command->sharedOwnerMatches(command)
            || !command->completionDeviceMatches(owner->nativeDevice()) || !command->nativeQueue()
            || !command->recordingIdentity() || command->finished()
            || (completion && !completion->ordinarySignalOwnerMatches(completion))) {
            diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-ORIGINAL-COMMAND";
            return {};
        }
        if (owner->retained.containsMatching([&](const auto& graph) noexcept {
                return graph.command().get() == command.get();
            })) {
            diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-DUPLICATE";
            return {};
        }
        if (owner->retained.pending() == owner->retained.capacity()) {
            diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CAPACITY";
            return {};
        }
        if (completion && !completion->canReserveSignal()) {
            diagnostic = "RHI-WEBGPU-ORDINARY-SIGNAL-UNAVAILABLE";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(owner->preparationQuota);
            auto graph = core::makeUnique<WebGpuOrdinaryCompletionGraph>(command, completion);
            if (!graph->reserveSignal()) {
                diagnostic = "RHI-WEBGPU-ORDINARY-SIGNAL-UNAVAILABLE";
                return {};
            }
            auto reservation = core::makeUnique<WebGpuOrdinaryCompletionReservation>(owner);
            reservation->ticket_ = owner->retained.reserve(graph);
            if (!reservation->ticket_) {
                diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-CLOSED";
                return {};
            }
            return reservation;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-ALLOCATION";
            return {};
        }
#else
        (void)owner;
        (void)command;
        (void)completion;
        diagnostic = "RHI-WEBGPU-ORDINARY-COMPLETION-NATIVE-UNAVAILABLE";
        return {};
#endif
    }

private:
    const std::shared_ptr<WebGpuOrdinaryCompletionOwner> owner_;
    Ticket ticket_;
};

std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOrdinaryCompletionOwner(
    const WebGpuOrdinaryCompletionBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    const char*& diagnostic);

} // namespace arp::rhi_webgpu
