#pragma once

#include "core/runtime/completion_evidence_batch.h"
#include "core/runtime/external_callback_gate.h"
#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/backends/webgpu/webgpu_ordinary_resource_retention.h"
#include "rhi/backends/webgpu/webgpu_staging_upload_pool.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/command_transfer_validation.h"
#include "rhi/frontend/general_initialized_buffer_content.h"
#include "rhi/frontend/resource_texture_validation.h"
#include "rhi/frontend/transfer_upload_content_transaction.h"
#include "rhi/frontend/transfer_upload_content_validation.h"
#include "rhi/frontend/transfer_upload_copy_mapping.h"
#include "rhi/semantics/command_lowering.h"

// region Webgpu original buffer upload source
namespace arp::rhi_webgpu {
struct WebGpuOriginalBufferUploadSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCallbacks = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u, maxCpuPayloadBytes = 0u, maxCpuResourceBytes = 0u;
};

// OriginalBufferUploadSource/1. Only this factory creates original upload
// provenance. Ready source bytes are not recorded-command or submission proof.
class WebGpuOriginalBufferUploadSource final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuOriginalTextureUploadSource;

    class ConstructionKey {
        friend class WebGpuOriginalBufferUploadSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Callback {
        std::shared_ptr<WebGpuOriginalBufferUploadSource> owner;
        std::uint32_t participant = 0u;
    };

public:
    static constexpr std::uint32_t callbackCount = 1u + WebGpuNativeErrorScopeBatch::count;
    // GPUQueue.writeBuffer's normative validation requires size multiple of 4.
    static constexpr std::uint64_t queueWriteSizeAlignment = 4u;
    enum class Status { Prepared, Pending, Ready, Rejected, DeviceLost };
    WebGpuOriginalBufferUploadSource(ConstructionKey,
        std::shared_ptr<WebGpuRhiStagingUploadPool>,
        std::shared_ptr<WebGpuRhiResourceRegistry>,
        std::uint64_t frame,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota);
    ~WebGpuOriginalBufferUploadSource();
    static const char* validateBudget(const WebGpuOriginalBufferUploadSourceBudget&) noexcept;
    static std::shared_ptr<WebGpuOriginalBufferUploadSource> prepareOriginal(
        std::shared_ptr<WebGpuRhiStagingUploadPool>,
        std::shared_ptr<WebGpuRhiResourceRegistry>,
        const void* bytes,
        std::uint64_t size,
        std::uint64_t frame,
        const WebGpuOriginalBufferUploadSourceBudget&,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> payloadQuota,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        const char*& diagnostic);
    static const char* initialize(
        const std::shared_ptr<WebGpuOriginalBufferUploadSource>&) noexcept;
    Status status() const noexcept;
    const char* diagnostic() const noexcept;
    const char* validate() const noexcept;

    const auto& pool() const noexcept { return pool_; }

    const auto& registry() const noexcept { return registry_; }

    const auto& resource() const noexcept { return observation_; }

    rhi::GpuResourceId reservedResourceId() const noexcept { return resourceId_; }

    const auto& originalTextureDestination() const noexcept { return textureDestination_; }

    std::uint64_t frameSerial() const noexcept { return frame_; }

    std::uint64_t sizeBytes() const noexcept { return payload_.size(); }

    bool queueCompletionObserved() const noexcept { return evidence_.hasReceived(0u); }

    bool errorScopesObserved() const noexcept
    {
        for (std::uint32_t i = 1u; i < callbackCount; ++i)
            if (!evidence_.hasReceived(i))
                return false;
        return true;
    }

    const char* executionDiagnostic() const noexcept;

private:
    enum class ValidationScope { InitialAdmission, OriginalDeclaration };
    static std::shared_ptr<WebGpuOriginalBufferUploadSource> prepareOriginalTexture(
        std::shared_ptr<const class WebGpuOriginalTextureUploadDestination>,
        std::shared_ptr<WebGpuRhiStagingUploadPool>,
        const WebGpuOriginalBufferUploadSourceBudget&,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> payloadQuota,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        const char*& diagnostic);
    const char* validateTextureDestination(
        ValidationScope scope = ValidationScope::InitialAdmission) const noexcept;
    const char* validateOriginalSource(
        ValidationScope scope = ValidationScope::InitialAdmission) const noexcept;
    const char* validateReadyDeclaration() const noexcept;
    void start(const std::shared_ptr<WebGpuOriginalBufferUploadSource>&) noexcept;
    void observe(std::uint32_t, bool) noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void completeQueue(WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*) noexcept;
    static void completeScope(
        WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*) noexcept;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
#endif
    const std::shared_ptr<WebGpuRhiStagingUploadPool> pool_;
    const std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    const std::uint64_t frame_;
    const rhi::GpuResourceId resourceId_;
    std::shared_ptr<const class WebGpuOriginalTextureUploadDestination> textureDestination_;
    const std::shared_ptr<core::CpuMemoryQuota> resourceQuota_;
    core::pmr::Vector<unsigned char> payload_{&core::defaultCpuMemoryResource()};
    core::pmr::String nativeDiagnostic_{&core::defaultCpuMemoryResource()};
    WebGpuRhiAllocation allocation_;
    core::UniquePtr<rhi::GpuBuffer> buffer_;
    WebGpuTransferResourceObservation observation_;
    WebGpuOrdinaryResourceObservation registeredObservation_;
    std::array<core::UniquePtr<Callback>, callbackCount> callbacks_;
    core::runtime::CompletionEvidenceBatch evidence_{callbackCount};
    Status status_ = Status::Prepared;
    const char* failure_ = nullptr;
};

// endregion Webgpu original buffer upload source

// region Webgpu original texture upload destination

// OriginalTextureUploadDestination/1: independent per-destination budgets.
// GPU bytes account the exact texel/block footprint, not a native heap size.
struct WebGpuOriginalTextureUploadDestinationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCallbacks = 0u;
    std::uint64_t maxRetainedSourceBytes = 0u, maxGpuTexelBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u, maxCpuResourceBytes = 0u;
    rhi::TextureUploadContentLedger::Budget content;
};

class WebGpuOriginalTextureUploadDestination final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuOriginalBufferUploadSource;
    friend class WebGpuOriginalTextureUploadSource;

    class ConstructionKey {
        friend class WebGpuOriginalTextureUploadDestination;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Callback {
        std::shared_ptr<WebGpuOriginalTextureUploadDestination> owner;
        std::uint32_t participant = 0u;
    };

public:
    static constexpr std::uint32_t callbackCount = WebGpuNativeErrorScopeBatch::count;
    enum class Status { Prepared, Pending, CreationReady, Rejected, DeviceLost };
    WebGpuOriginalTextureUploadDestination(ConstructionKey,
        std::shared_ptr<const rhi::TextureUploadContentSource>,
        std::shared_ptr<WebGpuRhiResourceRegistry>,
        std::shared_ptr<core::CpuMemoryQuota>);
    ~WebGpuOriginalTextureUploadDestination();
    static const char* validateBudget(const WebGpuOriginalTextureUploadDestinationBudget&) noexcept;
    static const char* validateSelectedLayout(
        const rhi::TextureDesc&, std::uint64_t& texelBytes) noexcept;
    static std::shared_ptr<WebGpuOriginalTextureUploadDestination> prepareOriginal(
        std::shared_ptr<const rhi::TextureUploadContentSource>,
        std::shared_ptr<WebGpuRhiResourceRegistry>,
        const rhi::TextureDesc&,
        const WebGpuOriginalTextureUploadDestinationBudget&,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        std::shared_ptr<core::CpuMemoryQuota> contentQuota,
        const char*& diagnostic);
    static const char* initialize(
        const std::shared_ptr<WebGpuOriginalTextureUploadDestination>&) noexcept;
    Status status() const noexcept;
    const char* validate() const noexcept;
    const char* executionDiagnostic() const noexcept;
    const char* validateContentJoin(const std::shared_ptr<const rhi::TextureUploadContentSource>&,
        const std::shared_ptr<rhi::TextureUploadContentLedger>&) const noexcept;

    const auto& source() const noexcept { return source_; }

    const auto& registry() const noexcept { return registry_; }

    const auto& resource() const noexcept { return observation_; }

    const auto& contentLedger() const noexcept { return ledger_; }

    const auto& contentExpectation() const noexcept { return expected_; }

    const rhi::GpuTexture* texture() const noexcept { return texture_.get(); }

    rhi::MemoryStats memoryStats() const noexcept { return allocator_->memoryStats(); }

    bool creationScopesObserved() const noexcept { return evidence_.allReceived(); }

private:
    const char* validateOriginal() const noexcept;
    const char* validateNativeDeclaration() const noexcept;
    const char* validateReadyDeclaration() const noexcept;
    void start(const std::shared_ptr<WebGpuOriginalTextureUploadDestination>&) noexcept;
    void observe(std::uint32_t, bool) noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void completeScope(
        WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*) noexcept;
    WGPUDevice device_ = nullptr;
#endif
    const std::shared_ptr<const rhi::TextureUploadContentSource> source_;
    const std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    const std::shared_ptr<core::CpuMemoryQuota> resourceQuota_;
    rhi::TextureDesc desc_;
    core::UniquePtr<WebGpuRhiAllocator> allocator_;
    WebGpuRhiAllocation allocation_;
    core::UniquePtr<rhi::GpuTexture> texture_;
    WebGpuTransferResourceObservation observation_;
    std::shared_ptr<rhi::TextureUploadContentLedger> ledger_;
    std::optional<rhi::TextureUploadContentLedger::Expectation> expected_;
    std::array<core::UniquePtr<Callback>, callbackCount> callbacks_;
    core::runtime::CompletionEvidenceBatch evidence_{callbackCount};
    Status status_ = Status::Prepared;
    const char* failure_ = nullptr;
};

// endregion Webgpu original texture upload destination

// region Webgpu canonical buffer upload source

// Canonical binding extends the initialized upload-source module without using
// the old source's directly-created object/registry or its CPU transaction.
class WebGpuCanonicalBufferUploadSource final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalNativeClosure;
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuCanonicalUploadJoins;
    friend class WebGpuCanonicalTextureUploadSource;
    using Contract = rhi::OriginalCommandInitializedBufferStagingContract;
    using Receipt = WebGpuNativeCompletionReceipt;
    using Lease = rhi::OriginalCommandBufferLease;

    class ConstructionKey {
        friend class WebGpuCanonicalBufferUploadSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    class State final : public core::QuotaBoundSharedObjectIdentity {
    public:
        struct Slot {
            explicit Slot(std::size_t index) noexcept : index(index) {}

            ~Slot()
            {
                Receipt::release(receipt);
                releaseWebGpuNativeDeviceLossRegistration(loss);
            }

            const std::size_t index;
            core::runtime::ExternalCallbackGate gate;
            std::shared_ptr<State> owner;
            std::uintptr_t receipt = 0u, loss = 0u;
        };

        static constexpr auto lossIndex = Contract::callbackCount - 1u;
        static_assert(Contract::scopeCallbacks == WebGpuNativeErrorScopeBatch::count
            && Contract::queueCallbacks == 1u);

        State(Contract::Budget budget,
            rhi::OriginalDeviceNativeLossObservation loss,
            std::uint64_t frame) noexcept
            : budget(budget), originalLoss(std::move(loss)), frame(frame)
        {}

        ~State()
        {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!webGpuNativeDeviceLost()) {
                if (queue)
                    wgpuQueueRelease(queue);
                if (device)
                    wgpuDeviceRelease(device);
            }
#endif
        }

        const char* validate() const noexcept
        {
            if (const auto* id = validateInitializedFacts())
                return id;
            if (!committed || committed->diagnostic())
                return "RHI-CANONICAL-UPLOAD-SOURCE-STATE-STALE";
            return lease->originalObject()->validateOriginalCommittedState(*committed);
        }

        const char* validateInitializedFacts(bool queryNativeLayout = true) const noexcept
        {
            if (const auto* id = Contract::validateIntent(budget))
                return id;
            if (budget.contractVersion == 3u)
                if (const auto* id = Contract::validateVertexPayload(budget, payload.size()))
                    return id;
            if (budget.contractVersion == 4u)
                if (const auto* id = Contract::validateShaderPayload(budget, payload.size()))
                    return id;
            if (const auto* id = originalLoss.diagnostic())
                return id;
            if (failure)
                return failure;
            if (const auto* id = Lease::validateOriginal(lease))
                return id;
            const auto* object = lease->originalObject();
            if (!object->hostWritesSealed() || !object->gpuWritesSealed()
                || !object->gpuTransferWritesSealed() || object->desc().sizeBytes != payload.size()
                || observation.allocationBytes != payload.size() || observation.id != object->id()
                || object->desc().usage != Contract::expectedUsage(budget)
                || object->desc().initialState != Contract::expectedState(budget))
                return "RHI-CANONICAL-UPLOAD-SOURCE-IDENTITY-CHANGED";
            if (const auto* id = validateOriginalWebGpuResource(*object, observation))
                return id;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!observation.buffer || !observation.buffer->buffer
                || observation.buffer->ownerDevice != device || !queue
                || (queryNativeLayout
                        ? (wgpuBufferGetSize(observation.buffer->buffer) != payload.size()
                              || wgpuBufferGetUsage(observation.buffer->buffer)
                                  != nativeUsage(budget))
                        : (frozenNativeBufferBytes != payload.size()
                              || frozenNativeBufferUsage != nativeUsage(budget))))
                return "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-LAYOUT";
#else
            return "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-UNAVAILABLE";
#endif
            return nullptr;
        }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        static WGPUBufferUsage nativeUsage(const Contract::Budget& budget) noexcept
        {
            const auto usage = budget.contractVersion == 4u
                ? (budget.shaderContent.type == rhi::DescriptorType::UniformBuffer
                          ? WGPUBufferUsage_Uniform
                          : WGPUBufferUsage_Storage)
                    | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst
                : budget.contractVersion == 3u
                ? WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex
                : budget.contractVersion == 2u
                ? WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index
                : WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
            return Contract::shaderReadEnabled(budget) ? usage | WGPUBufferUsage_Storage : usage;
        }
#endif

        bool drained() const noexcept
        {
            using P = core::runtime::ExternalCallbackGate::Phase;
            for (std::size_t i = 0u; i < lossIndex; ++i)
                if (slots[i]->owner || slots[i]->gate.phase() == P::Armed
                    || slots[i]->gate.phase() == P::Delivering)
                    return false;
            return true;
        }

        void retire() noexcept
        {
            if (!drained()
                || (((mayWrite && !queueObserved) || scopeStackUnproven) && !physicalLoss))
                return;
            auto& loss = *slots[lossIndex];
            using P = core::runtime::ExternalCallbackGate::Phase;
            if (loss.gate.phase() != P::Delivered && loss.gate.phase() != P::Detached) {
                if (!detachWebGpuNativeDeviceLossRegistration(loss.loss)) {
                    if (!failure)
                        failure = "RHI-CANONICAL-UPLOAD-SOURCE-LOSS-DETACH";
                    return;
                }
                loss.gate.detach();
            }
            releaseWebGpuNativeDeviceLossRegistration(loss.loss);
            loss.loss = 0u;
            loss.owner.reset();
        }

        void finishInitialization() noexcept
        {
            if (issuanceComplete && evidence.proven() && !failure) {
                if (const auto* id = validate())
                    failure = id;
                else
                    ready = true;
            }
            retire();
        }

        void cancel() noexcept
        {
            if (!failure)
                failure = "RHI-CANONICAL-UPLOAD-SOURCE-CANCELLED";
            retire();
        }

        static void complete(void* data, Receipt::Outcome outcome) noexcept
        {
            auto* slot = static_cast<Slot*>(data);
            if (!slot || !slot->gate.beginDelivery())
                return;
            const auto self = slot->owner;
            if (self && self->slots[slot->index].get() == slot) {
                const bool success = outcome == Receipt::Outcome::Success;
                if (!slot->index && success)
                    self->queueObserved = true;
                if (!self->evidence.observe(static_cast<std::uint32_t>(slot->index), success)
                    || !success)
                    if (!self->failure)
                        self->failure = "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-ERROR";
            }
            slot->gate.finishDelivery();
            slot->owner.reset();
            if (self)
                self->finishInitialization();
        }

        static void lost(void* data) noexcept
        {
            auto* slot = static_cast<Slot*>(data);
            if (!slot || !slot->gate.beginDelivery())
                return;
            const auto self = slot->owner;
            if (self) {
                using P = core::runtime::ExternalCallbackGate::Phase;
                for (std::size_t i = 0u; i < lossIndex; ++i)
                    if (self->slots[i]->gate.phase() == P::Armed
                        || self->slots[i]->gate.phase() == P::Delivering)
                        ++self->outstandingAtLoss;
                self->physicalLoss = true;
                self->ready = false;
                self->failure = "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-LOST";
            }
            slot->gate.finishDelivery();
            slot->owner.reset();
            if (self)
                self->retire();
        }

        const char* initialize(const std::shared_ptr<State>& self) noexcept
        {
            if (started)
                return "RHI-CANONICAL-UPLOAD-SOURCE-ONE-SHOT";
            if (const auto* id = validate())
                return id;
            if (const auto* id = Receipt::inspect(device, queue))
                return id;
            for (std::size_t i = 0u; i < lossIndex; ++i)
                if (!Receipt::prepared(slots[i]->receipt))
                    return "RHI-CANONICAL-UPLOAD-SOURCE-RECEIPT-UNPREPARED";
            started = true;
            if (!Receipt::pushScopes(device)) {
                failure = "RHI-CANONICAL-UPLOAD-SOURCE-SCOPE-STACK";
                // Stack effects are unknown; retain loss custody until teardown.
                scopeStackUnproven = true;
                return failure;
            }
            for (std::size_t i = 0u; i < lossIndex; ++i) {
                slots[i]->owner = self;
                slots[i]->gate.arm();
            }
            mayWrite = true;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (!Receipt::writeBuffer(
                    queue, observation.buffer->buffer, payload.data(), payload.size()))
                failure = "RHI-CANONICAL-UPLOAD-SOURCE-WRITE-UNCERTAIN";
#endif
            // Pop all three real scopes before registering queue completion.
            for (std::size_t position = 0u; position < lossIndex; ++position) {
                const auto i = (position + 1u) % lossIndex;
                auto& slot = *slots[i];
                if (!Receipt::bind(slot.receipt)) {
                    if (i && Receipt::inspect(device, queue))
                        scopeStackUnproven = true;
                    slot.gate.detach();
                    slot.owner.reset();
                    if (!failure)
                        failure = "RHI-CANONICAL-UPLOAD-SOURCE-REGISTRATION-FAILED";
                }
            }
            issuanceComplete = true;
            finishInitialization();
            return failure;
        }

        const Contract::Budget budget;
        const rhi::OriginalDeviceNativeLossObservation originalLoss;
        const std::uint64_t frame;
        // Native size/usage are immutable WebGPU buffer properties. Freeze the
        // genuine queries before adoption; hot finite associations additionally
        // authenticate the unchanged registered object/device/control blocks.
        std::uint64_t frozenNativeBufferBytes = 0u, frozenNativeBufferUsage = 0u;
        std::shared_ptr<const Lease> lease;
        std::optional<rhi::GpuBuffer::CommittedStateObservation> committed;
        WebGpuTransferResourceObservation observation;
        core::ByteBuffer payload{&core::defaultCpuMemoryResource()};
        std::array<core::UniquePtr<Slot>, Contract::callbackCount> slots;
        core::runtime::CompletionEvidenceBatch evidence{lossIndex};
        bool started = false, mayWrite = false, issuanceComplete = false, ready = false,
             queueObserved = false, physicalLoss = false, scopeStackUnproven = false;
        std::uint32_t outstandingAtLoss = 0u;
        const char* failure = nullptr;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
#else
        void* device = nullptr;
        void* queue = nullptr;
#endif
    };

public:
    using Budget = Contract::Budget;

    WebGpuCanonicalBufferUploadSource(ConstructionKey, std::shared_ptr<State> state) noexcept
        : state_(std::move(state))
    {}

    ~WebGpuCanonicalBufferUploadSource() { state_->cancel(); }

    static std::shared_ptr<const WebGpuCanonicalBufferUploadSource> prepareOriginal(
        rhi::GpuDevice& device,
        core::UniquePtr<rhi::GpuBuffer>& candidate,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const void* bytes,
        std::uint64_t size,
        std::uint64_t frame,
        const Budget& budget,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 5>& quotas,
        const char*& diagnostic)
    {
        diagnostic = Contract::validateIntent(budget);
        if (diagnostic)
            return {};
        if (!candidate) {
            diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-CANDIDATE";
            return {};
        }
        if (!bytes || size > SIZE_MAX
            || size % WebGpuOriginalBufferUploadSource::queueWriteSizeAlignment) {
            diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-PAYLOAD";
            return {};
        }
        if ((diagnostic = Contract::inspectOriginal(device, *candidate, loss, size, budget)))
            return {};
        const auto decision =
            arp::rhi::semantics::OriginalCommandInitializedBufferStagingLowering::lowering(
                device.backend(), budget.contractVersion);
        if (!decision.privateInitializationAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        const std::array limits{budget.maxCpuMetadataBytes,
            budget.maxCpuPayloadBytes,
            budget.maxCpuResourceBytes,
            budget.maxCpuLeaseBytes,
            budget.maxCpuCallbackBytes};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                quotas.size(), [&](std::size_t i) noexcept {
                    return quotas[i];
                })) {
            diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-QUOTA-DOMAIN";
            return {};
        }
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            if (quotas[i]->limitBytes() != limits[i]) {
                diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-QUOTA";
                return {};
            }
        const auto charge = core::inspectCpuAllocationCharge(candidate.get());
        if (!charge.found || charge.quota != quotas[2].get()) {
            diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-RESOURCE-QUOTA";
            return {};
        }
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-UNAVAILABLE";
        return {};
#else
        try {
            core::CpuMemoryQuotaScope metadata(quotas[0]);
            auto state = shareState(core::makeUnique<State>(budget, loss, frame));
            if ((diagnostic = observeOriginalWebGpuResource(*candidate, state->observation)))
                return {};
            state->device = state->observation.buffer->ownerDevice;
            wgpuDeviceAddRef(state->device);
            state->queue = wgpuDeviceGetQueue(state->device);
            if (!state->queue) {
                diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-QUEUE";
                return {};
            }
            if ((diagnostic = Receipt::inspect(state->device, state->queue)))
                return {};
            state->frozenNativeBufferBytes = wgpuBufferGetSize(state->observation.buffer->buffer);
            state->frozenNativeBufferUsage = wgpuBufferGetUsage(state->observation.buffer->buffer);
            if (state->frozenNativeBufferBytes != size
                || state->frozenNativeBufferUsage != State::nativeUsage(budget)) {
                diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-NATIVE-LAYOUT";
                return {};
            }
            state->committed.emplace(*candidate->observeOriginalCommittedState());
            {
                core::CpuMemoryQuotaScope payload(quotas[1]);
                state->payload.resize(static_cast<std::size_t>(size));
                std::memcpy(state->payload.data(), bytes, static_cast<std::size_t>(size));
            }
            auto lease = Lease::prepareColdOriginal(candidate,
                {1u, true, Lease::retainedOwnerCount, budget.maxCpuLeaseBytes},
                quotas[3],
                diagnostic);
            if (!lease)
                return {};
            state->lease = lease;
            {
                core::CpuMemoryQuotaScope callbacks(quotas[4]);
                for (std::size_t i = 0u; i < Contract::callbackCount; ++i)
                    state->slots[i] = core::makeUnique<State::Slot>(i);
            }
            for (std::size_t i = 0u; i < State::lossIndex; ++i) {
                auto receipt = Receipt::prepare(state->device,
                    state->queue,
                    i ? Receipt::Kind::Scope : Receipt::Kind::Queue,
                    state->slots[i].get(),
                    &State::complete);
                if (!receipt) {
                    diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-RECEIPT";
                    return {};
                }
                state->slots[i]->receipt = receipt.release();
            }
            auto result = shareOriginal(
                core::makeUnique<WebGpuCanonicalBufferUploadSource>(ConstructionKey{}, state));
            auto& nativeLoss = *state->slots[State::lossIndex];
            nativeLoss.gate.arm();
            nativeLoss.loss =
                prepareWebGpuNativeDeviceLossRegistration(state->device, &nativeLoss, &State::lost);
            if (!nativeLoss.loss) {
                nativeLoss.gate.detach();
                diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-LOSS-BINDING";
                return {};
            }
            // Publish no userdata owner cycle or write restriction until every
            // fallible cold allocation/native adapter exists.
            auto* originalObject = candidate.get();
            if ((diagnostic = Lease::adoptOriginal(lease, candidate)))
                return {};
            originalObject->sealWrites();
            nativeLoss.owner = state;
            diagnostic = nullptr;
            return result;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-CANONICAL-UPLOAD-SOURCE-ALLOCATION";
            return {};
        }
#endif
    }

    static const char* initialize(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self) noexcept
    {
        if (const auto* id = validateOwner(self))
            return id;
        return self->state_->initialize(self->state_);
    }

    static const char* cancelOriginal(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self) noexcept
    {
        if (const auto* id = validateOwner(self))
            return id;
        self->state_->cancel();
        return nullptr;
    }

    const char* readyDiagnostic() const noexcept
    {
        if (const auto* id = state_->validate())
            return id;
        return state_->ready ? nullptr : "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY";
    }

    const char* compareOriginalUpload(
        const std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>& original)
        const noexcept
    {
        if (const auto* id = readyDiagnostic())
            return id;
        return compareUploadMetadata(original);
    }

private:
    const char* initializedReadyFacts(bool queryNativeLayout = true) const noexcept
    {
        if (const auto* id = state_->validateInitializedFacts(queryNativeLayout))
            return id;
        if (!state_->committed || !state_->ready || !state_->drained() || !state_->queueObserved
            || !state_->evidence.proven())
            return "RHI-CANONICAL-UPLOAD-SOURCE-NOT-READY";
        return nullptr;
    }

    const char* initializedCommittedReadyFacts() const noexcept
    {
        if (const auto* id = initializedReadyFacts(false))
            return id;
        return state_->lease->originalObject()->validateOriginalCommittedState(*state_->committed);
    }

    const char* compareInitializedUploadFacts(
        const std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>& original)
        const noexcept
    {
        if (const auto* id = initializedReadyFacts())
            return id;
        return compareUploadMetadata(original);
    }

    const char* compareUploadMetadata(
        const std::shared_ptr<const rhi::OriginalCommandBufferUploadSource>& original)
        const noexcept
    {
        if (state_->budget.contractVersion != 1u)
            return "RHI-CANONICAL-UPLOAD-SOURCE-STAGING-PROFILE";
        if (const auto* id = rhi::OriginalCommandBufferUploadSource::validateOriginal(original))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(original->originalSourceLease(), state_->lease)
            || original->sizeBytes() != state_->payload.size()
            || original->arguments().frameSerial() != state_->frame)
            return "RHI-CANONICAL-UPLOAD-SOURCE-ORIGINAL-JOIN";
        return nullptr;
    }

public:
    // Exact frozen-byte provenance only; callers separately require Ready.
    // Frame remains an authoring equality, never a native generation issuer.
    const char* compareFrozenPayload(
        const void* bytes, std::uint64_t size, std::uint64_t frame) const noexcept
    {
        if (const auto* id = state_->validate())
            return id;
        return compareFrozenPayloadMetadata(bytes, size, frame);
    }

private:
    const char* compareFrozenPayloadMetadata(
        const void* bytes, std::uint64_t size, std::uint64_t frame) const noexcept
    {
        if (!bytes || size != state_->payload.size() || frame != state_->frame
            || std::memcmp(bytes, state_->payload.data(), state_->payload.size()))
            return "RHI-CANONICAL-UPLOAD-SOURCE-PAYLOAD-JOIN";
        return nullptr;
    }

public:
    const auto& originalLease() const noexcept { return state_->lease; }

    static constexpr std::uint32_t initializedIndexOwnerBound = Contract::retainedOwnerBound;

    std::uint64_t initializedPayloadBytes() const noexcept { return state_->payload.size(); }

    template <class Ready>
    static const char* validateInitializedShaderBufferWithReady(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        rhi::DescriptorType type,
        std::uint64_t offset,
        std::uint64_t bytes,
        Ready&& ready) noexcept
    {
        if (const auto* id = validateOwner(self))
            return id;
        const auto& state = *self->state_;
        if (const auto* id = Contract::validateShaderPayload(state.budget, state.payload.size()))
            return id;
        if (const auto* id = Lease::validateOriginal(lease))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(lease, state.lease))
            return "RHI-INITIALIZED-SHADER-LEASE-JOIN";
        if (type != state.budget.shaderContent.type || offset || bytes != state.payload.size())
            return "RHI-INITIALIZED-SHADER-BINDING-JOIN";
        if (const auto* id = ready(*self))
            return id;
        return self->initializedReadyFacts();
    }

    static const char* validateGeneralFiniteInitializedContent(const rhi::GpuDevice& device,
        const rhi::OriginalGeneralFiniteInitializedBufferContentContract::Request& request,
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        std::size_t frozenOrdinal,
        const void* completePayload,
        std::uint64_t completePayloadBytes,
        std::uint64_t frame,
        rhi::DescriptorBufferBindingRangeContract::Resolved& output) noexcept
    {
        using Content = rhi::OriginalGeneralFiniteInitializedBufferContentContract;
        output = {};
        if (const auto* id = Content::validateRequest(request))
            return id;
        const auto decision = Content::lowering(device.backend());
        if (!decision.associationAvailable)
            return decision.diagnostic;
        if (const auto* id = validateOwner(self))
            return id;
        if (const auto* id =
                rhi::OriginalCommandDescriptorSetSource::validateOriginal(descriptor, device))
            return id;
        return validateGeneralFiniteInitializedContentWithReady(
            request,
            self,
            descriptor,
            frozenOrdinal,
            [&](const auto& initialized) noexcept -> const char* {
                const auto& state = *initialized.state_;
                if (const auto* id = state.lease->originalObject()->validateOriginalCommittedState(
                        *state.committed))
                    return id;
                return initialized.compareFrozenPayloadMetadata(
                    completePayload, completePayloadBytes, frame);
            },
            output);
    }

private:
    // Only the original closure/preparation friends may supply a Pending seal
    // validator. Public borrowed association always authenticates committed
    // state and the complete caller-authored frozen payload above.
    template <class Ready>
    static const char* validateGeneralFiniteInitializedContentWithReady(
        const rhi::OriginalGeneralFiniteInitializedBufferContentContract::Request& request,
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        std::size_t frozenOrdinal,
        Ready&& ready,
        rhi::DescriptorBufferBindingRangeContract::Resolved& output) noexcept
    {
        using Content = rhi::OriginalGeneralFiniteInitializedBufferContentContract;
        output = {};
        if (const auto* id = Content::validateRequest(request))
            return id;
        if (const auto* id = validateOwner(self))
            return id;
        if (const auto* id = rhi::OriginalCommandDescriptorSetSource::validateOriginal(descriptor))
            return id;
        if (descriptor->sourceContractVersion() != 5u)
            return "RHI-GENERAL-FINITE-CONTENT-SOURCE-PROFILE";
        if (frozenOrdinal >= descriptor->frozenElements().size())
            return "RHI-GENERAL-FINITE-CONTENT-ELEMENT";
        // Borrow the exact authenticated Source sequence; callers cannot supply
        // a fitting replacement element or lease with forged shared ownership.
        const auto& frozen = descriptor->frozenElements()[frozenOrdinal];
        const auto* lease = std::get_if<std::shared_ptr<const Lease>>(&frozen.resourceOwner);
        if (!lease || !core::SharedOwnerProjection::sameOwner(*lease, self->state_->lease))
            return "RHI-INITIALIZED-SHADER-LEASE-JOIN";
        const auto* binding = rhi::findDescriptorSetBinding(
            descriptor->frozenDefinition(), frozen.declaration.write.binding);
        if (!binding)
            return "RHI-GENERAL-FINITE-CONTENT-ELEMENT";
        const auto& state = *self->state_;
        if (const auto* id = self->initializedReadyFacts(false))
            return id;
        if (const auto* id = ready(*self))
            return id;
        return Content::validateDeclaration(request,
            descriptor->sourceContractVersion(),
            state.budget,
            *binding,
            frozen.declaration,
            state.lease->layout().sizeBytes,
            state.payload.size(),
            state.observation.allocationBytes,
            output);
    }

public:
    static const char* validateInitializedIndexContent(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        rhi::IndexFormat format) noexcept
    {
        return validateInitializedIndexContentWithReady(
            self, lease, format, [](const auto& source) noexcept {
                return source.readyDiagnostic();
            });
    }

    static const char* analyzeInitializedIndices(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        rhi::IndexFormat format,
        std::uint64_t bindingOffset,
        std::uint64_t firstIndex,
        std::uint64_t indexCount,
        std::int32_t baseVertex,
        rhi::IndexInputConsumptionContract::DrawRange& output) noexcept
    {
        return analyzeInitializedIndicesWithReady(self,
            lease,
            format,
            bindingOffset,
            firstIndex,
            indexCount,
            baseVertex,
            output,
            [](const auto& source) noexcept {
                return source.readyDiagnostic();
            });
    }

    static const char* validateInitializedVertexContent(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        const rhi::VertexInputConsumptionContract::Stream& stream) noexcept
    {
        return validateInitializedVertexContentWithReady(
            self, lease, stream, [](const auto& source) noexcept {
                return source.readyDiagnostic();
            });
    }

    static const char* validateInitializedVertexRange(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        const rhi::VertexInputConsumptionContract::Stream& stream,
        std::uint64_t bindingOffset,
        std::uint64_t vertexCount,
        std::uint64_t firstVertex,
        std::uint64_t instanceCount,
        std::uint64_t firstInstance) noexcept
    {
        return validateInitializedVertexRangeWithReady(self,
            lease,
            stream,
            bindingOffset,
            vertexCount,
            firstVertex,
            instanceCount,
            firstInstance,
            [](const auto& source) noexcept {
                return source.readyDiagnostic();
            });
    }

private:
    template <class Ready>
    static const char* validateInitializedIndexContentWithReady(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        rhi::IndexFormat format,
        Ready&& ready) noexcept
    {
        if (const auto* id = validateOwner(self))
            return id;
        const auto& state = *self->state_;
        if (state.budget.contractVersion != 2u)
            return "RHI-INITIALIZED-INDEX-CONTENT-PROFILE";
        if (const auto* id = Lease::validateOriginal(lease))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(lease, state.lease))
            return "RHI-INITIALIZED-INDEX-LEASE-JOIN";
        if (format != state.budget.indexContent.format)
            return "RHI-INITIALIZED-INDEX-FORMAT-JOIN";
        if (const auto* id = ready(*self))
            return id;
        return self->initializedReadyFacts();
    }

    // Actual initialized content is inspected only through its genuine source
    // owner and exact original lease. The range remains CPU fetch evidence;
    // native DrawIndexed/recording/submission authority is still separate.
    template <class Ready>
    static const char* analyzeInitializedIndicesWithReady(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        rhi::IndexFormat format,
        std::uint64_t bindingOffset,
        std::uint64_t firstIndex,
        std::uint64_t indexCount,
        std::int32_t baseVertex,
        rhi::IndexInputConsumptionContract::DrawRange& output,
        Ready&& ready) noexcept
    {
        output = {};
        if (const auto* id = validateInitializedIndexContentWithReady(self, lease, format, ready))
            return id;
        const auto& state = *self->state_;
        return rhi::IndexInputConsumptionContract::analyzeLittleEndianBytes(state.payload.data(),
            state.payload.size(),
            format,
            bindingOffset,
            firstIndex,
            indexCount,
            baseVertex,
            state.budget.indexContent.maxAnalysisIndices,
            output);
    }

    // Same immutable actor/lease and genuine initialization completion. This
    // proves content/range compatibility only; native pipeline custody and draw
    // authority must be checked by the original closure and executor.
    template <class Ready>
    static const char* validateInitializedVertexContentWithReady(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        const rhi::VertexInputConsumptionContract::Stream& stream,
        Ready&& ready) noexcept
    {
        if (const auto* id = validateOwner(self))
            return id;
        const auto& state = *self->state_;
        if (const auto* id = Contract::validateVertexStream(state.budget, stream))
            return id;
        if (const auto* id = Lease::validateOriginal(lease))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(lease, state.lease))
            return "RHI-INITIALIZED-VERTEX-LEASE-JOIN";
        if (const auto* id = ready(*self))
            return id;
        return self->initializedReadyFacts();
    }

    template <class Ready>
    static const char* validateInitializedVertexRangeWithReady(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self,
        const std::shared_ptr<const Lease>& lease,
        const rhi::VertexInputConsumptionContract::Stream& stream,
        std::uint64_t bindingOffset,
        std::uint64_t vertexCount,
        std::uint64_t firstVertex,
        std::uint64_t instanceCount,
        std::uint64_t firstInstance,
        Ready&& ready) noexcept
    {
        if (const auto* id = validateInitializedVertexContentWithReady(self, lease, stream, ready))
            return id;
        const auto& state = *self->state_;
        return Contract::validateVertexRange(state.budget,
            state.payload.size(),
            stream,
            bindingOffset,
            vertexCount,
            firstVertex,
            instanceCount,
            firstInstance);
    }

public:
    // Cold ownership admission for dependent wrappers. Allocation headers retain
    // each quota; no second retained domain list or hot allocation is needed.
    bool overlapsCpuQuota(const core::CpuMemoryQuota* quota) const noexcept
    {
        return quota
            && (quota == originalMetadataQuota().get()
                || quota == state_->lease->originalMetadataQuota().get()
                || quota == core::inspectCpuAllocationCharge(state_->payload.data()).quota
                || quota == core::inspectCpuAllocationCharge(state_->lease->originalObject()).quota
                || quota == core::inspectCpuAllocationCharge(state_->slots[0].get()).quota);
    }

    bool overlapsCpuOwner(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        if (!quota)
            return false;
        if (core::SharedOwnerProjection::sameControlBlock(quota, originalMetadataQuota())
            || core::SharedOwnerProjection::sameControlBlock(
                quota, state_->lease->originalMetadataQuota()))
            return true;
        for (const auto* allocation : std::array<const void*, 5>{this,
                 state_.get(),
                 state_->payload.data(),
                 state_->lease->originalObject(),
                 state_->slots[0].get()})
            if (core::cpuAllocationOverlapsQuotaOwner(allocation, quota))
                return true;
        return overlapsCpuQuota(quota.get());
    }

    std::uint64_t frameSerial() const noexcept { return state_->frame; }

    bool callbacksDrained() const noexcept { return state_->drained(); }

    bool queueCompletionObserved() const noexcept { return state_->queueObserved; }

    std::uint32_t callbacksOutstandingAtLoss() const noexcept { return state_->outstandingAtLoss; }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    static const char* validateOwner(
        const std::shared_ptr<const WebGpuCanonicalBufferUploadSource>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
                || !self->state_->hasQuotaBoundSharedOwner(self->state_)
            ? "RHI-CANONICAL-UPLOAD-SOURCE-OWNER"
            : nullptr;
    }

    static std::shared_ptr<State> shareState(core::UniquePtr<State> state)
    {
        return State::shareOriginal(std::move(state));
    }

    const std::shared_ptr<State> state_;
};

// endregion Webgpu canonical buffer upload source

// region Webgpu original texture upload source

struct WebGpuOriginalTextureUploadSourceBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCpuMetadataBytes = 0u, maxRetainedContentBytes = 0u,
                  maxRetainedDestinationTexelBytes = 0u, maxPackedBytes = 0u;
    WebGpuOriginalBufferUploadSourceBudget buffer;
};

// OriginalTextureUploadSource/1: immutable original packed source provenance;
// no target write, generation completion or recorded-command proof authority.
class WebGpuOriginalTextureUploadSource final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuOriginalTextureUploadTransaction;

    class ConstructionKey {
        friend class WebGpuOriginalTextureUploadSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class Status { Prepared, Pending, Ready, Rejected, DeviceLost };

    struct Lowering {
        bool validatorOnly = false, nativeSourceImplemented = false;
        const char* diagnostic = "RHI-TEXTURE-SOURCE-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-TEXTURE-SOURCE-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
            return {false, false, "RHI-TEXTURE-SOURCE-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::Metal:
            return {false, false, "RHI-TEXTURE-SOURCE-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-TEXTURE-SOURCE-NATIVE-EVIDENCE-REQUIRED"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalTextureUploadSource(
        ConstructionKey, std::shared_ptr<WebGpuOriginalBufferUploadSource> source)
        : buffer_(std::move(source))
    {}

    static const char* validateBudget(const WebGpuOriginalTextureUploadSourceBudget&) noexcept;
    static std::shared_ptr<WebGpuOriginalTextureUploadSource> prepareOriginal(
        std::shared_ptr<const WebGpuOriginalTextureUploadDestination>,
        std::shared_ptr<WebGpuRhiStagingUploadPool>,
        const WebGpuOriginalTextureUploadSourceBudget&,
        std::shared_ptr<core::CpuMemoryQuota> wrapperQuota,
        std::shared_ptr<core::CpuMemoryQuota> sourceMetadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> payloadQuota,
        std::shared_ptr<core::CpuMemoryQuota> resourceQuota,
        const char*& diagnostic);
    static const char* initialize(
        const std::shared_ptr<WebGpuOriginalTextureUploadSource>&) noexcept;
    Status status() const noexcept;
    const char* validate() const noexcept;
    const char* executionDiagnostic() const noexcept;

    const auto& destination() const noexcept { return buffer_->originalTextureDestination(); }

    const auto& contentSource() const noexcept { return destination()->source(); }

    const auto& packing() const noexcept { return contentSource()->packing(); }

    const std::shared_ptr<WebGpuOriginalBufferUploadSource>& bufferSource() const noexcept
    {
        return buffer_;
    }

    const auto& resource() const noexcept { return buffer_->resource(); }

    static const char* mapPackedCopyOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureUploadSource>& self,
        const std::shared_ptr<const rhi::TextureUploadInputSnapshot>& input,
        std::size_t mipIndex,
        rhi::semantics::TextureUploadPackedCopy& output) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-TEXTURE-SOURCE-OWNER";
        if (const auto* id = self->validate())
            return id;
        if (!input || !input->hasQuotaBoundSharedOwner(input)
            || input.get() != self->packing()->inputOwner().get()
            || !core::SharedOwnerProjection::sameOwner(input, self->packing()->inputOwner()))
            return "RHI-TEXTURE-SOURCE-FOREIGN-INPUT";
        return rhi::semantics::TextureUploadPackedCopyMapping::mapOriginal(
            1u, true, self->packing(), self->resource().id, mipIndex, output);
    }

private:
    const char* validateOriginalJoin() const noexcept;
    const char* validateReadyDeclaration() const noexcept;
    const std::shared_ptr<WebGpuOriginalBufferUploadSource> buffer_;
};

// endregion Webgpu original texture upload source

// region Webgpu canonical texture upload source

// CanonicalTextureUploadStaging/1 binds initialized canonical buffer bytes to
// exact original texture packing. Destination state is an expectation only;
// native target content generation and ordered target writes remain closed.
class WebGpuCanonicalTextureUploadSource final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalUploadJoins;
    friend class WebGpuCanonicalTextureContentUploadSource;
    using Binding = rhi::OriginalCommandTextureUploadPackingSource;
    using Buffer = WebGpuCanonicalBufferUploadSource;

    class ConstructionKey {
        friend class WebGpuCanonicalTextureUploadSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxRetainedOwners = 0u, maxNativeTargets = 0u;
        std::uint64_t maxPackedBytes = 0u, maxDestinationTexelBytes = 0u, maxCpuMetadataBytes = 0u;
    };

    // Wrapper + binding closure + initialized buffer closure + target native
    // payload/allocation + existing target loss/domain. Shared owners deduped.
    static constexpr std::uint32_t retainedOwnerBound = 1u + Binding::retainedOwnerCount
        + rhi::OriginalCommandInitializedBufferStagingContract::retainedOwnerBound + 2u;

    WebGpuCanonicalTextureUploadSource(ConstructionKey,
        std::shared_ptr<const Binding> binding,
        std::shared_ptr<const Buffer> buffer,
        rhi::OriginalDeviceNativeLossObservation loss,
        rhi::GpuTexture::CommittedStateObservation expected,
        WebGpuTransferResourceObservation target,
        Budget budget)
        : binding_(std::move(binding)), buffer_(std::move(buffer)), loss_(std::move(loss)),
          expected_(std::move(expected)), target_(std::move(target)), budget_(budget)
    {}

    static std::shared_ptr<const WebGpuCanonicalTextureUploadSource> prepareOriginal(
        rhi::GpuDevice& device,
        std::shared_ptr<const Binding> binding,
        std::shared_ptr<const Buffer> buffer,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (budget.contractVersion != 1u) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-VERSION";
            return {};
        }
        if (!budget.prototypeOptIn) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-OPT-IN";
            return {};
        }
        if (budget.maxRetainedOwners < retainedOwnerBound || budget.maxNativeTargets != 1u
            || !budget.maxPackedBytes || !budget.maxDestinationTexelBytes
            || !budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-BUDGET";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-QUOTA";
            return {};
        }
        if ((diagnostic = Binding::validateOriginal(binding)))
            return {};
        const auto decision =
            arp::rhi::semantics::OriginalCommandTextureUploadPackingSourceLowering::lowering(
                device.backend());
        if (!decision.privateStagingJoinAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if (!buffer || !buffer->hasQuotaBoundSharedOwner(buffer)) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-BUFFER-OWNER";
            return {};
        }
        if (binding->packing()->packedBytes().size() > budget.maxPackedBytes) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-BYTE-CAPACITY";
            return {};
        }
        const std::array domains{
            quota, binding->originalMetadataQuota(), buffer->originalMetadataQuota()};
        if (!core::SharedOwnerProjection::hasIndependentOwners(domains.size(),
                [&](std::size_t i) noexcept {
                    return domains[i];
                })
            || binding->overlapsCpuQuota(quota.get()) || buffer->overlapsCpuQuota(quota.get())) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-QUOTA-DOMAIN";
            return {};
        }
        if ((diagnostic = device.validateOriginalDeviceNativeLoss(loss)))
            return {};
        const auto* destination = binding->originalDestinationLease()->originalObject();
        if (!destination->allocation().estimatedSizeBytes
            || destination->allocation().estimatedSizeBytes > budget.maxDestinationTexelBytes) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-TARGET-BYTE-CAPACITY";
            return {};
        }
        if ((diagnostic = device.validateOriginalResourceRegistration(*destination)))
            return {};
        if ((diagnostic = device.validateOriginalResourceRegistration(
                 *buffer->originalLease()->originalObject())))
            return {};
        rhi::UploadTextureDesc fields;
        binding->originalInput()->scalarFields().writeScalarFields(fields);
        if ((diagnostic = destination->originalCommittedStateDiagnostic(fields.subresources)))
            return {};
        auto expected = destination->observeOriginalCommittedState(fields.subresources);
        if (!expected || expected->state() != rhi::ResourceState::CopyDst) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-TARGET-STATE";
            return {};
        }
        if ((diagnostic = buffer->compareFrozenPayload(binding->packing()->packedBytes().data(),
                 binding->packing()->packedBytes().size(),
                 fields.frameSerial)))
            return {};
        WebGpuTransferResourceObservation target;
        if ((diagnostic = observeOriginalWebGpuResource(*destination, target)))
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(
                core::makeUnique<WebGpuCanonicalTextureUploadSource>(ConstructionKey{},
                    std::move(binding),
                    std::move(buffer),
                    loss,
                    *expected,
                    std::move(target),
                    budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CANONICAL-TEXTURE-SOURCE-ALLOCATION";
            return {};
        }
    }

    const char* stagingReadyDiagnostic() const noexcept
    {
        if (const auto* id = stagingMetadataDiagnostic())
            return id;
        if (const auto* id = buffer_->readyDiagnostic())
            return id;
        return binding_->originalDestinationLease()
            ->originalObject()
            ->validateOriginalCommittedState(expected_);
    }

private:
    const char* stagingInitializedFacts() const noexcept
    {
        if (const auto* id = stagingMetadataDiagnostic())
            return id;
        return buffer_->initializedReadyFacts();
    }

    const char* stagingMetadataDiagnostic() const noexcept
    {
        if (const auto* id = Binding::validateOriginal(binding_))
            return id;
        if (const auto* id = loss_.diagnostic())
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-CANONICAL-TEXTURE-SOURCE-QUOTA";
        const auto* target = binding_->originalDestinationLease()->originalObject();
        if (const auto* id = validateOriginalWebGpuResource(*target, target_))
            return id;
        rhi::UploadTextureDesc fields;
        binding_->originalInput()->scalarFields().writeScalarFields(fields);
        return buffer_->compareFrozenPayloadMetadata(binding_->packing()->packedBytes().data(),
            binding_->packing()->packedBytes().size(),
            fields.frameSerial);
    }

public:
    static const char* mapPackedCopyOriginal(
        const std::shared_ptr<const WebGpuCanonicalTextureUploadSource>& self,
        const std::shared_ptr<const Binding>& original,
        std::size_t mip,
        rhi::semantics::TextureUploadPackedCopy& output) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-CANONICAL-TEXTURE-SOURCE-OWNER";
        if (!core::SharedOwnerProjection::sameOwner(original, self->binding_))
            return "RHI-CANONICAL-TEXTURE-SOURCE-ORIGINAL-JOIN";
        if (const auto* id = self->stagingReadyDiagnostic())
            return id;
        return rhi::semantics::TextureUploadPackedCopyMapping::mapOriginal(1u,
            true,
            self->binding_->packing(),
            self->buffer_->originalLease()->resource(),
            mip,
            output);
    }

    const auto& originalPackingSource() const noexcept { return binding_; }

    const auto& initializedBuffer() const noexcept { return buffer_; }

    const auto& destinationStateExpectation() const noexcept { return expected_; }

    bool overlapsCpuQuota(const core::CpuMemoryQuota* quota) const noexcept
    {
        return quota
            && (quota == originalMetadataQuota().get() || binding_->overlapsCpuQuota(quota)
                || buffer_->overlapsCpuQuota(quota));
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = stagingReadyDiagnostic())
            return id;
        return arp::rhi::semantics::OriginalCommandTextureUploadPackingSourceLowering::lowering(
            rhi::Backend::WebGPU)
            .diagnostic;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::shared_ptr<const Binding> binding_;
    const std::shared_ptr<const Buffer> buffer_;
    const rhi::OriginalDeviceNativeLossObservation loss_;
    const rhi::GpuTexture::CommittedStateObservation expected_;
    const WebGpuTransferResourceObservation target_;
    const Budget budget_;
};

// endregion Webgpu canonical texture upload source

// region Webgpu canonical texture content upload source

// CanonicalTextureContentStaging/1: immutable cold composition, never a command
// or completion token. Target state/content expectations remain distinct.
class WebGpuCanonicalTextureContentUploadSource final
    : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalUploadJoins;
    using Staging = WebGpuCanonicalTextureUploadSource;

    class ConstructionKey {
        friend class WebGpuCanonicalTextureContentUploadSource;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalTextureContentStagingContract::Budget;
    static constexpr std::uint32_t retainedOwnerBound = Staging::retainedOwnerBound + 2u;

    WebGpuCanonicalTextureContentUploadSource(ConstructionKey,
        std::shared_ptr<const Staging> staging,
        rhi::GpuTexture::ContentExpectation content,
        Budget budget)
        : staging_(std::move(staging)), content_(std::move(content)), budget_(budget)
    {}

    static constexpr auto lowering(rhi::Backend backend) noexcept
    {
        return rhi::OriginalTextureContentStagingContract::lowering(backend);
    }

    static std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource> prepareOriginal(
        const rhi::GpuDevice& device,
        std::shared_ptr<const Staging> staging,
        const rhi::GpuTexture::ContentExpectation& content,
        const Budget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (budget.contractVersion != 1u) {
            diagnostic = "RHI-CANONICAL-CONTENT-STAGING-VERSION";
            return {};
        }
        if (!budget.prototypeOptIn) {
            diagnostic = "RHI-CANONICAL-CONTENT-STAGING-OPT-IN";
            return {};
        }
        if (budget.maxRetainedOwners < retainedOwnerBound || !budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-CANONICAL-CONTENT-STAGING-BUDGET";
            return {};
        }
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-CANONICAL-CONTENT-STAGING-QUOTA";
            return {};
        }
        const auto decision = lowering(device.backend());
        if (!decision.prerequisiteAvailable) {
            diagnostic = decision.diagnostic;
            return {};
        }
        if (!staging || !staging->hasQuotaBoundSharedOwner(staging)) {
            diagnostic = "RHI-CANONICAL-CONTENT-STAGING-OWNER";
            return {};
        }
        if ((diagnostic = staging->stagingReadyDiagnostic()))
            return {};
        const auto& target =
            *staging->originalPackingSource()->originalDestinationLease()->originalObject();
        if ((diagnostic = device.validateOriginalResourceRegistration(target)))
            return {};
        if ((diagnostic = target.validateOriginalContent(content)))
            return {};
        if ((diagnostic = rhi::semantics::OriginalTextureContentUploadValidation::inspect(
                 *staging->originalPackingSource()->originalInput(), target.desc())))
            return {};
        const auto contentQuota = target.originalContentCpuQuota();
        const std::array domains{quota, staging->originalMetadataQuota(), contentQuota};
        if (!core::SharedOwnerProjection::hasIndependentOwners(domains.size(),
                [&](std::size_t i) noexcept {
                    return domains[i];
                })
            || staging->overlapsCpuQuota(quota.get())
            || staging->overlapsCpuQuota(contentQuota.get())
            || device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-CANONICAL-CONTENT-STAGING-QUOTA-DOMAIN";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<WebGpuCanonicalTextureContentUploadSource>(
                ConstructionKey{}, std::move(staging), content, budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-CANONICAL-CONTENT-STAGING-ALLOCATION";
            return {};
        }
    }

    const char* validate() const noexcept
    {
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-CANONICAL-CONTENT-STAGING-QUOTA";
        const auto& target =
            *staging_->originalPackingSource()->originalDestinationLease()->originalObject();
        if (const auto* id = target.validateOriginalContent(content_))
            return id;
        return staging_->stagingReadyDiagnostic();
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self)
            ? "RHI-CANONICAL-CONTENT-STAGING-OWNER"
            : self->validate();
    }

    const auto& originalStaging() const noexcept { return staging_; }

    const auto& destinationContentExpectation() const noexcept { return content_; }

    static const char* validateCompletedInputFacts(
        const std::shared_ptr<const WebGpuCanonicalTextureContentUploadSource>& self,
        const rhi::GpuTexture::ContentExpectation& completed) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-CANONICAL-CONTENT-STAGING-OWNER";
        if (const auto* id = self->staging_->stagingInitializedFacts())
            return id;
        const auto& target =
            *self->staging_->originalPackingSource()->originalDestinationLease()->originalObject();
        if (const auto* id = target.validateOriginalContent(completed))
            return id;
        if (completed.resourceGeneration() != self->content_.resourceGeneration()
            || completed.state() != rhi::common::OriginalTextureContentControl::State::Completed)
            return "RHI-COMPLETED-SHADER-TEXTURE-COMPLETION";
        return nullptr;
    }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return lowering(rhi::Backend::WebGPU).diagnostic;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::shared_ptr<const Staging> staging_;
    const rhi::GpuTexture::ContentExpectation content_;
    const Budget budget_;
};
} // namespace arp::rhi_webgpu

// endregion Webgpu canonical texture content upload source
