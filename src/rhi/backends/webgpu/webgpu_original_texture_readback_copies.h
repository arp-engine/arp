#pragma once
#include "rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h"
#include "rhi/backends/webgpu/webgpu_original_state_transaction.h"
#include "rhi/frontend/transfer_readback_copy.h"
#include "rhi/semantics/transfer_dependency_lowering.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalTextureReadbackCopiesBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalTransactions = 0u, maxMipMappings = 0u, maxCopyDecisions = 0u;
    std::uint64_t maxRetainedNativeBytes = 0u, maxRetainedContentBytes = 0u,
                  maxUnpublishedResultBytes = 0u, maxCpuMetadataBytes = 0u;
    std::uint32_t maxNativeCompletionOwners = 0u;
};

// Cold original decisions plus allocation-free current native fact binding.
// Native completion can only be advanced by the existing common issuers.
// Production map/result authority remains separate.
class WebGpuOriginalTextureReadbackCopies final : public core::QuotaBoundSharedObjectIdentity {
    using Transaction = WebGpuOriginalStateTransaction;
    using Lowering = rhi::semantics::TextureReadbackCopyLowering;

    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackCopies;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class NativeStatus { Unrecorded, Recorded, Submitted, Completed, Unproven, DeviceLost };

    WebGpuOriginalTextureReadbackCopies(ConstructionKey,
        std::shared_ptr<const Transaction> transaction,
        std::uint32_t capacity,
        std::uint32_t completionOwners)
        : transaction_(std::move(transaction)), decisions_(capacity),
          completionOwnerCapacity_(completionOwners)
    {}

    static const char* validateBudget(const WebGpuOriginalTextureReadbackCopiesBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-WEBGPU-READBACK-COPIES-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-WEBGPU-READBACK-COPIES-OPT-IN";
        if (!b.maxOriginalTransactions || !b.maxMipMappings || !b.maxCopyDecisions
            || !b.maxRetainedNativeBytes || !b.maxRetainedContentBytes
            || !b.maxUnpublishedResultBytes || !b.maxCpuMetadataBytes)
            return "RHI-WEBGPU-READBACK-COPIES-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> prepareOriginal(
        std::shared_ptr<const Transaction> transaction,
        const WebGpuOriginalTextureReadbackCopiesBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-WEBGPU-READBACK-COPIES-QUOTA";
            return {};
        }
        if (!transaction || !transaction->hasQuotaBoundSharedOwner(transaction)) {
            diagnostic = "RHI-WEBGPU-STATE-TRANSACTION-OWNER";
            return {};
        }
        if (!transaction->isReadbackProfile()) {
            diagnostic = "RHI-WEBGPU-READBACK-TRANSACTION-PROFILE";
            return {};
        }
        if (transaction->status() != Transaction::Status::Prepared) {
            diagnostic = "RHI-WEBGPU-READBACK-COPIES-PREPARE-STATUS";
            return {};
        }
        if ((diagnostic = transaction->validate()))
            return {};
        const auto& pair = transaction->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        if (pair->layout()->mipCopies().size() > b.maxMipMappings
            || pair->layout()->mipCopies().size() > b.maxCopyDecisions
            || pair->retainedNativeAllocationBytes() > b.maxRetainedNativeBytes
            || pair->destination()->source()->retainedContentBytes() > b.maxRetainedContentBytes
            || pair->destination()->unpublishedResultCapacityBytes()
                > b.maxUnpublishedResultBytes) {
            diagnostic = "RHI-WEBGPU-READBACK-COPIES-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto result = core::makeUnique<WebGpuOriginalTextureReadbackCopies>(ConstructionKey{},
                std::move(transaction),
                b.maxCopyDecisions,
                b.maxNativeCompletionOwners);
            for (std::size_t i = 0u; i < pair->layout()->mipCopies().size(); ++i) {
                Lowering::Decision decision;
                if ((diagnostic = Lowering::lowerOriginal(rhi::Backend::WebGPU,
                         1u,
                         true,
                         pair->layout(),
                         i,
                         pair->layout()->mipCopies()[i],
                         decision)))
                    return {};
                result->decisions_.append(decision);
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-READBACK-COPIES-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-READBACK-COPIES-ALLOCATION";
            return {};
        }
    }

    static const char* initialDependencyMapping(
        const std::shared_ptr<const Transaction>& transaction,
        rhi::semantics::TransferDependencyLowering::Decision& output) noexcept
    {
        if (!transaction || !transaction->hasQuotaBoundSharedOwner(transaction))
            return "RHI-WEBGPU-STATE-TRANSACTION-OWNER";
        if (!transaction->isReadbackProfile())
            return "RHI-WEBGPU-READBACK-TRANSACTION-PROFILE";
        // Strict original graph validation belongs to Prepared admission. Current
        // mapping requires this exact transaction's owned journal observations;
        // default graph/source validators intentionally keep rejecting Pending.
        if (const auto* id = transaction->validate())
            return id;
        const auto& states = transaction->states();
        if (!states || !states->hasQuotaBoundSharedOwner(states))
            return "RHI-WEBGPU-ORIGINAL-STATE-OWNER";
        const auto& inputs = *states->inputs();
        const auto* original =
            std::get_if<rhi::ResourceDependencyArguments>(&inputs.descriptors()[0].arguments);
        rhi::ResourceBarrierDesc barrier;
        if (inputs.descriptors()[0].commandIndex != 0u || !original
            || !original->write(rhi::CommandUsageKind::TextureBarrier, barrier))
            return "RHI-WEBGPU-ORIGINAL-READBACK-DEPENDENCY-DESCRIPTOR";
        const auto& pair = inputs.scalars()->usage()->projection()->originalReadbacks()[0];
        const auto& source = pair->destination()->source()->resource();
        if (barrier.resource != source.id)
            return "RHI-WEBGPU-ORIGINAL-READBACK-DEPENDENCY-SOURCE";
        rhi::ResourceState before;
        if (const auto* id = WebGpuOriginalTransferStateValidation::stateBefore(
                *states, barrier.resource, 0u, before))
            return id;
        using Dependency = rhi::semantics::TransferDependencyLowering;
        const auto decision = Dependency::transition(rhi::Backend::WebGPU,
            1u,
            true,
            barrier,
            before,
            true,
            rhi::defaultTextureSubresourceRange(source.textureDesc));
        if (decision.diagnostic)
            return decision.diagnostic;
        if (decision.outcome != rhi::CapabilityMode::Equivalent
            || decision.operation != Dependency::Operation::ImplicitOrderedUsage)
            return "RHI-WEBGPU-ORIGINAL-READBACK-DEPENDENCY-UNPROVEN";
        output = decision;
        return nullptr;
    }

    const char* validate() const noexcept
    {
        if (!transaction_ || !transaction_->hasQuotaBoundSharedOwner(transaction_))
            return "RHI-WEBGPU-STATE-TRANSACTION-OWNER";
        if (const auto* id = transaction_->validate())
            return id;
        return validateOriginalDecisions();
    }

    static const char* validateCurrent(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& self,
        std::size_t index,
        const Lowering::Decision& decision,
        void* expectedDevice) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-COPIES-OWNER";
        if (index >= self->decisions_.values().size())
            return "RHI-WEBGPU-READBACK-COPIES-INDEX";
        if (!self->decisions_.values()[index].matches(decision))
            return "RHI-READBACK-COPY-LOWERING-DECISION-MISMATCH";
        const auto& pair = self->pair();
        rhi::TextureReadbackLayout::Mip mapped;
        if (const auto* id = Transaction::mapReadbackMipCurrent(
                self->transaction_, pair->layout()->inputOwner(), index, decision.original, mapped))
            return id;
        return self->validateNativeFacts(decision, mapped, expectedDevice);
    }

    NativeStatus nativeStatus() const noexcept
    {
        if (webGpuNativeDeviceLost()
            || (nativeIdentity_ && webGpuOrdinaryCompletionOwnerClosed(nativeCompletionOwner_)))
            nativeStatus_ = NativeStatus::DeviceLost;
        return nativeStatus_;
    }

    static const char* validateNativeCompletionOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& self) noexcept
    {
        if (const auto* id = validateNativeCompletedDeclarationOriginal(self))
            return id;
        return self->pair()->destination()->unmappedAccessDiagnostic();
    }

    // Native proof and original declarations only; no Pending/Mapped access grant.
    static const char* validateNativeCompletedDeclarationOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-COPIES-OWNER";
        if (self->nativeStatus() == NativeStatus::DeviceLost)
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-DEVICE-LOST";
        if (self->nativeStatus_ != NativeStatus::Completed)
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-COMPLETION-UNPROVEN";
        if (!self->nativeIdentity_ || !self->nativeDevice_ || !self->nativeQueue_
            || !self->ticketGeneration_)
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-IDENTITY";
        if (const auto* id =
                Transaction::validateCommittedReadbackDeclarationOriginal(self->transaction_))
            return id;
        if (const auto* id = self->validateOriginalDecisions())
            return id;
        for (const auto& decision : self->decisions_.values())
            if (const auto* id =
                    self->validateNativeFacts(decision, decision.original, self->nativeDevice_))
                return id;
        return nullptr;
    }

    // Public terminal revocation can only remove proof, never grant it.
    void invalidateNativeCompletion() const noexcept { nativeStatus_ = NativeStatus::Unproven; }

    const auto& transaction() const noexcept { return transaction_; }

    const auto& decisions() const noexcept { return decisions_.values(); }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-READBACK-COPIES-RECORDING-UNPROVEN";
    }

private:
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;

    static const char* markNativeRecorded(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& self,
        std::uint64_t identity,
        void* device,
        void* queue,
        const std::shared_ptr<class WebGpuOrdinaryCompletionOwner>& owner) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-COPIES-OWNER";
        if (!owner || !self->completionOwnerCapacity_ || webGpuOrdinaryCompletionOwnerClosed(owner))
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-COMPLETION-OWNER";
        if (self->nativeStatus_ != NativeStatus::Unrecorded
            || self->transaction_->status() != Transaction::Status::Pending || !identity || !device
            || !queue)
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-RECORDING-STATUS";
        if (const auto* id = self->validate())
            return id;
        for (std::size_t i = 0u; i < self->decisions_.values().size(); ++i)
            if (const auto* id = validateCurrent(self, i, self->decisions_.values()[i], device))
                return id;
        self->nativeCompletionOwner_ = owner;
        self->nativeIdentity_ = identity;
        self->nativeDevice_ = device;
        self->nativeQueue_ = queue;
        self->nativeStatus_ = NativeStatus::Recorded;
        return nullptr;
    }

    static const char* validateNativeRecorded(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& self,
        std::uint64_t identity,
        void* device,
        void* queue) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-COPIES-OWNER";
        if (self->nativeStatus_ != NativeStatus::Recorded || self->nativeIdentity_ != identity
            || self->nativeDevice_ != device || self->nativeQueue_ != queue)
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-RECORDED-IDENTITY";
        return self->validate();
    }

    static const char* markNativeSubmitted(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& self,
        std::uint64_t identity,
        void* device,
        void* queue,
        std::size_t index,
        std::uint64_t generation) noexcept
    {
        if (const auto* id = validateNativeRecorded(self, identity, device, queue))
            return id;
        if (self->transaction_->status() != Transaction::Status::Committed || !generation)
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-SUBMITTED-STATUS";
        self->ticketIndex_ = index;
        self->ticketGeneration_ = generation;
        self->nativeStatus_ = NativeStatus::Submitted;
        return nullptr;
    }

    static const char* markNativeCompleted(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies>& self,
        std::uint64_t identity,
        void* device,
        void* queue,
        std::size_t index,
        std::uint64_t generation) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-COPIES-OWNER";
        if (self->nativeStatus_ != NativeStatus::Submitted || self->nativeIdentity_ != identity
            || self->nativeDevice_ != device || self->nativeQueue_ != queue
            || self->ticketIndex_ != index || self->ticketGeneration_ != generation || !generation
            || self->transaction_->status() != Transaction::Status::Committed)
            return "RHI-WEBGPU-ORIGINAL-READBACK-NATIVE-COMPLETED-IDENTITY";
        if (const auto* id = self->validate())
            return id;
        for (std::size_t i = 0u; i < self->decisions_.values().size(); ++i)
            if (const auto* id = validateCurrent(self, i, self->decisions_.values()[i], device))
                return id;
        self->nativeStatus_ = NativeStatus::Completed;
        return nullptr;
    }

    const char* validateOriginalDecisions() const noexcept
    {
        const auto& layout = pair()->layout();
        if (decisions_.values().size() != layout->mipCopies().size())
            return "RHI-WEBGPU-READBACK-COPIES-COVERAGE";
        for (std::size_t i = 0u; i < decisions_.values().size(); ++i)
            if (const auto* id = Lowering::validateOriginal(rhi::Backend::WebGPU,
                    1u,
                    true,
                    layout,
                    i,
                    layout->mipCopies()[i],
                    decisions_.values()[i]))
                return id;
        return nullptr;
    }

    const char* validateNativeFacts(const Lowering::Decision& decision,
        const rhi::TextureReadbackLayout::Mip& mapped,
        void* expectedDevice) const noexcept
    {
        const auto& pair = this->pair();
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        (void)expectedDevice;
        return "RHI-WEBGPU-READBACK-COPIES-NATIVE-UNAVAILABLE";
#elif !defined(WGPU_TEXEL_COPY_BUFFER_INFO_INIT)
        (void)expectedDevice;
        return "RHI-WEBGPU-READBACK-COPIES-API-UNAVAILABLE";
#else
        const auto& source = *pair->destination()->source();
        if (const auto* id = source.validateReadbackContentProfile())
            return id;
        if (const auto* id = source.validateReadyDeclaration())
            return id;
        const auto& texture = *source.resource().texture;
        const auto& buffer = *pair->destination()->nativeResource();
        if (!expectedDevice || texture.ownerDevice != expectedDevice
            || buffer.ownerDevice != expectedDevice)
            return "RHI-WEBGPU-READBACK-COPIES-DEVICE";
        const auto dimension = decision.dimension == Lowering::Dimension::Texture3D
            ? WGPUTextureDimension_3D
            : WGPUTextureDimension_2D;
        if (texture.dimension != dimension || !texture.texture || !buffer.buffer
            || texture.rhiFormat != pair->layout()->sourceLayout().format)
            return "RHI-WEBGPU-READBACK-COPIES-NATIVE-FACTS";
        return WebGpuNativeTransferCopyLowering::textureDiagnostic(
            texture, mapped.range, mapped.extent);
#endif
    }

    const std::uint32_t completionOwnerCapacity_;
    mutable std::weak_ptr<class WebGpuOrdinaryCompletionOwner> nativeCompletionOwner_;
    mutable NativeStatus nativeStatus_ = NativeStatus::Unrecorded;
    mutable std::uint64_t nativeIdentity_ = 0u, ticketGeneration_ = 0u;
    mutable std::size_t ticketIndex_ = 0u;
    mutable void* nativeDevice_ = nullptr;
    mutable void* nativeQueue_ = nullptr;

    const std::shared_ptr<const WebGpuOriginalTextureReadbackPairProjection>& pair() const noexcept
    {
        return transaction_->states()
            ->inputs()
            ->scalars()
            ->usage()
            ->projection()
            ->originalReadbacks()[0];
    }

    const std::shared_ptr<const Transaction> transaction_;
    core::BoundedSequence<Lowering::Decision> decisions_;
};
} // namespace arp::rhi_webgpu
