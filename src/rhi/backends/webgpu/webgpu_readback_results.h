#pragma once

#include "core/runtime/completion_retention_pool.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_recording.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_submission.h"
#include "rhi/backends/webgpu/webgpu_original_texture_readback_copies.h"
#include "rhi/frontend/transfer_readback_cold_budget.h"
#include "rhi/frontend/transfer_readback_input.h"
#include "rhi/frontend/transfer_readback_owned_preparation.h"

#include <cstring>

// region Webgpu original texture readback mapping
namespace arp::rhi_webgpu {
class WebGpuOriginalTextureReadbackMapping;

class WebGpuOriginalTextureReadbackResult final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackMapping;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr std::uint32_t contractVersion = 1u;
    enum class Representation { OriginalPaddedBufferBytes };

    static constexpr Representation representation() noexcept
    {
        return Representation::OriginalPaddedBufferBytes;
    }

    WebGpuOriginalTextureReadbackResult(ConstructionKey,
        std::shared_ptr<const WebGpuOriginalTextureReadbackDestination> destination,
        const unsigned char* storage,
        std::size_t bytes,
        core::runtime::CompletionSignal::Ticket ticket)
        : destination_(std::move(destination)), storage_(storage), bytes_(bytes), ticket_(ticket)
    {}

    const auto& layout() const noexcept { return destination_->source()->layout(); }

    rhi::TextureReadbackContentIntent::Mode contentMode() const noexcept
    {
        return destination_->source()->contentMode();
    }

    std::size_t byteCapacity() const noexcept { return bytes_; }

    const unsigned char* publishedBytes() const noexcept
    {
        return destination_->resultPublication_.isComplete(ticket_) ? storage_ : nullptr;
    }

    static const char* validatePublishedOriginal(
        const std::shared_ptr<const WebGpuOriginalTextureReadbackResult>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-RESULT-OWNER";
        if (!self->destination_->resultPublication_.isComplete(self->ticket_))
            return "RHI-WEBGPU-READBACK-RESULT-UNPUBLISHED";
        if (self->storage_ != self->destination_->result_.data()
            || self->bytes_ != self->destination_->result_.size())
            return "RHI-WEBGPU-READBACK-RESULT-STORAGE-IDENTITY";
        return nullptr;
    }

private:
    friend class WebGpuOriginalTextureReadbackMapping;
    const std::shared_ptr<const WebGpuOriginalTextureReadbackDestination> destination_;
    const unsigned char* const storage_;
    const std::size_t bytes_;
    const core::runtime::CompletionSignal::Ticket ticket_;
};

struct WebGpuOriginalTextureReadbackMappingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxMapOperations = 0u, maxCallbacks = 0u, maxResultDeclarations = 0u,
                  maxMipLayouts = 0u;
    std::uint64_t maxRetainedNativeBytes = 0u, maxRetainedContentBytes = 0u, maxResultBytes = 0u;
    std::uint64_t maxCpuMapMetadataBytes = 0u, maxCpuResultMetadataBytes = 0u;
    std::uint32_t maxScopeCallbacks = 0u;
};

// Backend-private original Completed mapping; publication proves a CPU snapshot only.
class WebGpuOriginalTextureReadbackMapping final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuOriginalTextureReadbackPublication;
    friend class WebGpuOriginalTextureReadbackNumericalResult;

    class TerminalConsumer {
    public:
        virtual ~TerminalConsumer() = default;
        virtual void consumeOriginalTerminal() noexcept = 0;
        virtual void consumeOriginalLoss() noexcept = 0;
        virtual void consumeOriginalFailure(const char*) noexcept = 0;
    };

    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackMapping;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Callback {
        std::shared_ptr<WebGpuOriginalTextureReadbackMapping> owner;
        std::uint32_t participant = 0u;
    };

public:
    static constexpr std::uint32_t callbackCount = 1u;
    static constexpr std::uint32_t scopeCallbackCount = WebGpuNativeErrorScopeBatch::count;
    enum class Status { Prepared, Pending, AwaitingScopes, Published, Rejected, DeviceLost };

    struct Lowering {
        bool validatorOnly = false, nativeMappingImplemented = false;
        const char* diagnostic = "RHI-READBACK-MAP-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-READBACK-MAP-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-READBACK-MAP-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-READBACK-MAP-PROTOTYPE-OPT-IN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalTextureReadbackMapping(ConstructionKey,
        std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> copies,
        std::shared_ptr<const WebGpuOriginalTextureReadbackDestination> destination)
        : copies_(std::move(copies)), destination_(std::move(destination))
    {}

    ~WebGpuOriginalTextureReadbackMapping()
    {
        if (destination_->resultPublication_.release(ticket_)
            && !destination_->resultPublication_.isComplete(ticket_))
            destination_->resultPublication_.reset();
    }

    static const char* validateBudget(const WebGpuOriginalTextureReadbackMappingBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-WEBGPU-READBACK-MAP-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-WEBGPU-READBACK-MAP-OPT-IN";
        if (!b.maxMapOperations || b.maxCallbacks < callbackCount || !b.maxResultDeclarations
            || !b.maxMipLayouts || !b.maxRetainedNativeBytes || !b.maxRetainedContentBytes
            || !b.maxResultBytes || !b.maxCpuMapMetadataBytes || !b.maxCpuResultMetadataBytes
            || b.maxScopeCallbacks < scopeCallbackCount)
            return "RHI-WEBGPU-READBACK-MAP-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackMapping> prepareOriginal(
        std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> copies,
        const WebGpuOriginalTextureReadbackMappingBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> mapQuota,
        std::shared_ptr<core::CpuMemoryQuota> declarationQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!mapQuota || !declarationQuota || mapQuota->limitBytes() != b.maxCpuMapMetadataBytes
            || declarationQuota->limitBytes() != b.maxCpuResultMetadataBytes) {
            diagnostic = "RHI-WEBGPU-READBACK-MAP-QUOTA";
            return {};
        }
        if (!copies || !copies->hasQuotaBoundSharedOwner(copies)) {
            diagnostic = "RHI-WEBGPU-READBACK-COPIES-OWNER";
            return {};
        }
        if (copies->nativeStatus() != WebGpuOriginalTextureReadbackCopies::NativeStatus::Unrecorded
            || copies->transaction()->status()
                != WebGpuOriginalStateTransaction::Status::Prepared) {
            diagnostic = "RHI-WEBGPU-READBACK-MAP-COLD-STATUS";
            return {};
        }
        if ((diagnostic = copies->validate()))
            return {};
        const auto& pair = copies->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        const auto& destination = pair->destination();
        if (pair->retainedNativeAllocationBytes() > b.maxRetainedNativeBytes
            || destination->source()->retainedContentBytes() > b.maxRetainedContentBytes
            || destination->unpublishedResultCapacityBytes() > b.maxResultBytes
            || copies->decisions().size() > b.maxMipLayouts) {
            diagnostic = "RHI-WEBGPU-READBACK-MAP-CAPACITY";
            return {};
        }
        if (!destination->resultPublication_.canReserve()) {
            diagnostic = "RHI-WEBGPU-READBACK-MAP-STORAGE-BUSY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(mapQuota);
            auto object = core::makeUnique<WebGpuOriginalTextureReadbackMapping>(
                ConstructionKey{}, std::move(copies), destination);
            object->callback_ = core::makeUnique<Callback>();
            for (std::uint32_t i = 0u; i < scopeCallbackCount; ++i) {
                object->scopeCallbacks_[i] = core::makeUnique<Callback>();
                object->scopeCallbacks_[i]->participant = i + callbackCount;
            }
            auto shared = shareOriginal(std::move(object));
            if (!shared) {
                diagnostic = "RHI-WEBGPU-READBACK-MAP-OWNER";
                return {};
            }
            shared->ticket_ = destination->resultPublication_.reserve();
            if (!shared->ticket_) {
                diagnostic = "RHI-WEBGPU-READBACK-MAP-STORAGE-BUSY";
                return {};
            }
            {
                core::CpuMemoryQuotaScope result(declarationQuota);
                shared->result_ = WebGpuOriginalTextureReadbackResult::shareOriginal(
                    core::makeUnique<WebGpuOriginalTextureReadbackResult>(
                        WebGpuOriginalTextureReadbackResult::ConstructionKey{},
                        destination,
                        destination->result_.data(),
                        destination->result_.size(),
                        shared->ticket_));
            }
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-READBACK-MAP-ALLOCATION";
            return {};
        }
    }

    Status status() const noexcept
    {
        if (status_ != Status::Published
            && (webGpuNativeDeviceLost()
                || copies_->nativeStatus()
                    == WebGpuOriginalTextureReadbackCopies::NativeStatus::DeviceLost))
            return Status::DeviceLost;
        return status_;
    }

    const char* diagnostic() const noexcept
    {
        return status() == Status::DeviceLost ? "RHI-WEBGPU-READBACK-MAP-DEVICE-LOST" : failure_;
    }

    bool mapCompletionObserved() const noexcept { return evidence_.hasReceived(0u); }

    bool callbacksIssued() const noexcept { return callbacksIssued_; }

    bool callbacksDrained() const noexcept { return callbacksIssued_ && evidence_.allReceived(); }

    bool scopeCompletionsObserved() const noexcept
    {
        for (std::uint32_t i = 1u; i <= scopeCallbackCount; ++i)
            if (!evidence_.hasReceived(i))
                return false;
        return true;
    }

    const char* validate() const noexcept
    {
        if (status() == Status::DeviceLost)
            return "RHI-WEBGPU-READBACK-MAP-DEVICE-LOST";
        if (const auto* id = validateStorage())
            return id;
        switch (status_) {
        case Status::Prepared:
            return copies_->validate();
        case Status::Pending:
        case Status::AwaitingScopes:
            return WebGpuOriginalTextureReadbackCopies::validateNativeCompletedDeclarationOriginal(
                copies_);
        case Status::Published:
            return nullptr;
        case Status::Rejected:
            return failure_ ? failure_ : "RHI-WEBGPU-READBACK-MAP-REJECTED";
        case Status::DeviceLost:
            return "RHI-WEBGPU-READBACK-MAP-DEVICE-LOST";
        }
        return "RHI-WEBGPU-READBACK-MAP-STATUS";
    }

    static const char* start(
        const std::shared_ptr<WebGpuOriginalTextureReadbackMapping>& self) noexcept
    {
        return startAdmitted(self, {});
    }

private:
    void notifyTerminalConsumer() noexcept
    {
        if (!terminalConsumer_)
            return;
        if (status() == Status::DeviceLost) {
            auto consumer = std::move(terminalConsumer_);
            consumer->consumeOriginalLoss();
            return;
        }
        if (!evidence_.allReceived())
            return;
        if (status_ == Status::Published || status_ == Status::Rejected) {
            auto consumer = std::move(terminalConsumer_);
            if (status_ == Status::Published)
                consumer->consumeOriginalTerminal();
            else
                consumer->consumeOriginalFailure(failure_);
        }
    }

    static const char* startAdmitted(
        const std::shared_ptr<WebGpuOriginalTextureReadbackMapping>& self,
        core::runtime::CompletionSignal::Ticket publicationTicket) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-WEBGPU-READBACK-MAP-OWNER";
        if (self->status() == Status::DeviceLost)
            return "RHI-WEBGPU-READBACK-MAP-DEVICE-LOST";
        if (self->status_ != Status::Prepared)
            return "RHI-WEBGPU-READBACK-MAP-START-STATUS";
        if (publicationTicket ? !self->publicationReservation_.matches(publicationTicket)
                              : !self->publicationReservation_.canReserve())
            return "RHI-WEBGPU-READBACK-MAP-PUBLICATION-OWNER-REQUIRED";
        if (const auto* id = self->validate())
            return id;
        if (const auto* id = WebGpuOriginalTextureReadbackCopies::validateNativeCompletionOriginal(
                self->copies_))
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-WEBGPU-READBACK-MAP-NATIVE-UNAVAILABLE";
#else
        self->buffer_ = self->destination_->nativeResource()->buffer;
        self->callbacksIssued_ = true;
        self->status_ = Status::Pending;
        const auto device = self->destination_->nativeResource()->ownerDevice;
        WebGpuNativeErrorScopeBatch::push(device);
        self->callback_->owner = self;
        auto info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        info.mode = WGPUCallbackMode_AllowSpontaneous;
        info.callback = &completeMap;
        info.userdata1 = self->callback_.release();
        (void)wgpuBufferMapAsync(self->buffer_, WGPUMapMode_Read, 0u, self->result_->bytes_, info);
        WebGpuNativeErrorScopeBatch::pop(device, self, self->scopeCallbacks_, 0u, &completeScope);
        return self->diagnostic();
#endif
    }

public:
    const auto& copies() const noexcept { return copies_; }

    const auto& resultDeclaration() const noexcept { return result_; }

private:
    const char* validateStorage() const noexcept
    {
        if (!copies_ || !copies_->hasQuotaBoundSharedOwner(copies_) || !result_
            || !result_->hasQuotaBoundSharedOwner(result_))
            return "RHI-WEBGPU-READBACK-MAP-OWNER";
        const auto& pair = copies_->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        if (!core::SharedOwnerProjection::sameOwner(pair->destination(), destination_)
            || !core::SharedOwnerProjection::sameOwner(result_->destination_, destination_)
            || result_->ticket_.generation != ticket_.generation
            || result_->storage_ != destination_->result_.data()
            || result_->bytes_ != destination_->result_.size()
            || (status_ == Status::Published ? !destination_->resultPublication_.isComplete()
                                             : !destination_->resultPublication_.matches(ticket_)))
            return "RHI-WEBGPU-READBACK-MAP-STORAGE-IDENTITY";
        return nullptr;
    }

    void reject(const char* id) noexcept
    {
        if (!failure_)
            failure_ = id;
        status_ = Status::Rejected;
    }

    void joinEvidence() noexcept
    {
        if (status() == Status::DeviceLost || status_ == Status::Rejected
            || status_ == Status::Published)
            return;
        if (!evidence_.allReceived())
            return;
        if (!evidence_.proven()) {
            reject("RHI-WEBGPU-READBACK-MAP-EVIDENCE");
            return;
        }
        if (const auto* id = validateStorage()) {
            reject(id);
            return;
        }
        if (const auto* id =
                WebGpuOriginalTextureReadbackCopies::validateNativeCompletionOriginal(copies_)) {
            reject(id);
            return;
        }
        if (!destination_->resultPublication_.complete(ticket_)) {
            reject("RHI-WEBGPU-READBACK-MAP-PUBLICATION-TICKET");
            return;
        }
        status_ = Status::Published;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    void unmapIssued() noexcept
    {
        if (!buffer_ || unmapIssued_)
            return;
        unmapIssued_ = true;
        if (!webGpuNativeDeviceLost())
            wgpuBufferUnmap(buffer_);
    }

    void observeMap(bool success) noexcept
    {
        if (status() == Status::DeviceLost) {
            status_ = Status::DeviceLost;
            unmapIssued();
            (void)evidence_.observe(0u, false);
            return;
        }
        const char* id = nullptr;
        if (!success)
            id = "RHI-WEBGPU-READBACK-MAP-NATIVE-ERROR";
        else if (status_ == Status::Rejected)
            id = failure_;
        else if ((id = validateStorage())) {
        } else if ((id = WebGpuOriginalTextureReadbackCopies::
                           validateNativeCompletedDeclarationOriginal(copies_))) {
        } else if (!buffer_ || buffer_ != destination_->nativeResource()->buffer
            || !WebGpuNativeBufferMapState::matches(buffer_, WGPUBufferMapState_Mapped))
            id = "RHI-WEBGPU-READBACK-MAP-NATIVE-STATE";
        else if (wgpuBufferReadMappedRange(
                     buffer_, 0u, destination_->result_.data(), result_->bytes_)
            != WGPUStatus_Success)
            id = "RHI-WEBGPU-READBACK-MAP-RANGE-READ";
        unmapIssued();
        if (!id && !WebGpuNativeBufferMapState::matches(buffer_, WGPUBufferMapState_Unmapped))
            id = "RHI-WEBGPU-READBACK-MAP-UNMAP";
        if (!evidence_.observe(0u, !id) && !id)
            id = "RHI-WEBGPU-READBACK-MAP-EVIDENCE";
        if (id) {
            reject(id);
            return;
        }
        status_ = Status::AwaitingScopes;
        joinEvidence();
    }

    void observeScope(std::uint32_t participant, bool proven) noexcept
    {
        if (!evidence_.observe(participant, proven) || !proven) {
            reject("RHI-WEBGPU-READBACK-MAP-NATIVE-SCOPE");
            return;
        }
        joinEvidence();
    }

    static void completeMap(WGPUMapAsyncStatus status, WGPUStringView, void* data, void*) noexcept
    {
        auto* original = static_cast<Callback*>(data);
        core::UniquePtr<Callback> callback(original,
            core::CpuAllocatedDeleter<Callback>{
                original, &core::destroyCpuAllocatedUnique<Callback>});
        if (callback && callback->owner) {
            callback->owner->observeMap(status == WGPUMapAsyncStatus_Success);
            callback->owner->notifyTerminalConsumer();
        }
    }

    static void completeScope(WGPUPopErrorScopeStatus status,
        WGPUErrorType type,
        WGPUStringView,
        void* data,
        void*) noexcept
    {
        auto* original = static_cast<Callback*>(data);
        core::UniquePtr<Callback> callback(original,
            core::CpuAllocatedDeleter<Callback>{
                original, &core::destroyCpuAllocatedUnique<Callback>});
        if (callback && callback->owner) {
            callback->owner->observeScope(callback->participant,
                status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError);
            callback->owner->notifyTerminalConsumer();
        }
    }

    WGPUBuffer buffer_ = nullptr;
    bool unmapIssued_ = false;
#endif
    const std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> copies_;
    const std::shared_ptr<const WebGpuOriginalTextureReadbackDestination> destination_;
    std::shared_ptr<const WebGpuOriginalTextureReadbackResult> result_;
    core::runtime::CompletionSignal::Ticket ticket_;
    core::UniquePtr<Callback> callback_;
    std::array<core::UniquePtr<Callback>, scopeCallbackCount> scopeCallbacks_;
    core::runtime::CompletionEvidenceBatch evidence_{callbackCount + scopeCallbackCount};
    Status status_ = Status::Prepared;
    bool callbacksIssued_ = false;
    const char* failure_ = nullptr;
    mutable core::runtime::CompletionSignal publicationReservation_;
    std::shared_ptr<TerminalConsumer> terminalConsumer_;
};

// endregion Webgpu original texture readback mapping

// region Webgpu original texture readback statistics

class WebGpuOriginalTextureReadbackStatistics final {
public:
    struct Lowering {
        bool publishedConsumerImplemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-READBACK-STATISTICS-NATIVE-CONSUMER-UNIMPLEMENTED";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {false, true, "RHI-READBACK-STATISTICS-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {};
        case rhi::Backend::WebGPU:
            return {true, false, "RHI-READBACK-STATISTICS-PUBLISHED-ORIGINAL-REQUIRED"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    using Analysis = rhi::TextureReadbackViewAnalysis;

    static const char* analyzePublishedOriginal(
        const std::shared_ptr<const rhi::TextureReadbackStatisticsPlan>& plan,
        const std::shared_ptr<const WebGpuOriginalTextureReadbackResult>& result,
        std::size_t index,
        Analysis& output) noexcept
    {
        output = {};
        if (const auto* id = WebGpuOriginalTextureReadbackResult::validatePublishedOriginal(result))
            return id;
        if (const auto* id =
                rhi::TextureReadbackStatisticsPlan::validateOriginal(plan, result->layout()))
            return id;
        if (index >= plan->views().size())
            return "RHI-READBACK-STATISTICS-VIEW-INDEX";
        if (plan->originalByteCapacity() != result->byteCapacity())
            return "RHI-READBACK-STATISTICS-STORAGE-CAPACITY";
        const auto& view = plan->views()[index];
        const auto format = plan->originalLayout()->sourceLayout().format;
        const auto* bytes = result->publishedBytes();
        if (!bytes)
            return "RHI-WEBGPU-READBACK-RESULT-UNPUBLISHED";
        output.scalar = rhi::analyzeTextureReadback(format,
            bytes + view.byteOffset,
            static_cast<std::size_t>(view.byteSpan),
            view.width,
            view.height,
            view.bytesPerRow);
        output.channels = rhi::analyzeTextureReadbackChannels(format,
            bytes + view.byteOffset,
            static_cast<std::size_t>(view.byteSpan),
            view.width,
            view.height,
            view.bytesPerRow);
        if (output.scalar.valid && output.channels.valid)
            return nullptr;
        output = {};
        return "RHI-READBACK-STATISTICS-NONFINITE-OR-INVALID";
    }
};

// endregion Webgpu original texture readback statistics

// region Webgpu original texture readback numerical result

struct WebGpuOriginalTextureReadbackNumericalBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxViews = 0u;
    std::uint64_t maxAnalyzedPixels = 0u, maxRetainedResultBytes = 0u, maxCpuMetadataBytes = 0u;
};

// Cold storage, one original terminal consumer, no mapping/sink ownership cycle.
class WebGpuOriginalTextureReadbackNumericalResult final
    : public core::QuotaBoundSharedObjectIdentity,
      private WebGpuOriginalTextureReadbackMapping::TerminalConsumer {
    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackNumericalResult;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Mapping = WebGpuOriginalTextureReadbackMapping;
    using Result = WebGpuOriginalTextureReadbackResult;
    using Plan = rhi::TextureReadbackStatisticsPlan;

public:
    using Analysis = WebGpuOriginalTextureReadbackStatistics::Analysis;
    enum class Outcome { Unavailable, Completed, Rejected, DeviceLost };

    WebGpuOriginalTextureReadbackNumericalResult(
        ConstructionKey, std::shared_ptr<const Plan> plan, std::shared_ptr<const Result> source)
        : plan_(std::move(plan)), source_(std::move(source))
    {
        values_.resize(plan_->views().size());
    }

    static constexpr auto lowering(rhi::Backend backend) noexcept
    {
        return WebGpuOriginalTextureReadbackStatistics::lowering(backend);
    }

    static const char* validateBudget(
        const WebGpuOriginalTextureReadbackNumericalBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-NUMERICAL-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-NUMERICAL-OPT-IN";
        if (!b.maxViews || !b.maxAnalyzedPixels || !b.maxRetainedResultBytes
            || !b.maxCpuMetadataBytes)
            return "RHI-READBACK-NUMERICAL-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackNumericalResult> prepareOriginal(
        std::shared_ptr<const Plan> plan,
        std::shared_ptr<const Result> source,
        const WebGpuOriginalTextureReadbackNumericalBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-READBACK-NUMERICAL-QUOTA";
            return {};
        }
        if (!source || !source->hasQuotaBoundSharedOwner(source)) {
            diagnostic = "RHI-READBACK-NUMERICAL-SOURCE-OWNER";
            return {};
        }
        if ((diagnostic = Plan::validateOriginal(plan, source->layout())))
            return {};
        if (source->publishedBytes()) {
            diagnostic = "RHI-READBACK-NUMERICAL-COLD-STATUS";
            return {};
        }
        if (plan->views().size() > b.maxViews || plan->analyzedPixels() > b.maxAnalyzedPixels
            || source->byteCapacity() > b.maxRetainedResultBytes
            || source->byteCapacity() != plan->originalByteCapacity()) {
            diagnostic = "RHI-READBACK-NUMERICAL-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return shareOriginal(core::makeUnique<WebGpuOriginalTextureReadbackNumericalResult>(
                ConstructionKey{}, std::move(plan), std::move(source)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-NUMERICAL-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-READBACK-NUMERICAL-ALLOCATION";
            return {};
        }
    }

    static const char* attachOriginal(
        const std::shared_ptr<WebGpuOriginalTextureReadbackNumericalResult>& self,
        const std::shared_ptr<Mapping>& mapping) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-NUMERICAL-OWNER";
        if (!mapping || !mapping->hasQuotaBoundSharedOwner(mapping))
            return "RHI-READBACK-NUMERICAL-MAP-OWNER";
        if (mapping->status() != Mapping::Status::Prepared
            || mapping->copies()->nativeStatus()
                != WebGpuOriginalTextureReadbackCopies::NativeStatus::Unrecorded)
            return "RHI-READBACK-NUMERICAL-COLD-STATUS";
        if (!core::SharedOwnerProjection::sameOwner(self->source_, mapping->resultDeclaration()))
            return "RHI-READBACK-NUMERICAL-SOURCE-MISMATCH";
        if (self->attached_ || mapping->terminalConsumer_)
            return "RHI-READBACK-NUMERICAL-DUPLICATE";
        if (const auto* id = mapping->validate())
            return id;
#if !defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        return "RHI-READBACK-NUMERICAL-NATIVE-UNAVAILABLE";
#else
        self->attached_ = true;
        mapping->terminalConsumer_ = std::shared_ptr<Mapping::TerminalConsumer>(
            self, static_cast<Mapping::TerminalConsumer*>(self.get()));
        return nullptr;
#endif
    }

    Outcome outcome() const noexcept { return outcome_; }

    static const char* validateAttachedOriginal(
        const std::shared_ptr<WebGpuOriginalTextureReadbackNumericalResult>& self,
        const std::shared_ptr<Mapping>& mapping) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-NUMERICAL-OWNER";
        if (!mapping || !mapping->hasQuotaBoundSharedOwner(mapping))
            return "RHI-READBACK-NUMERICAL-MAP-OWNER";
        return self->attached_
                && core::SharedOwnerProjection::sameOwner(self, mapping->terminalConsumer_)
                && core::SharedOwnerProjection::sameOwner(
                    self->source_, mapping->resultDeclaration())
            ? nullptr
            : "RHI-READBACK-NUMERICAL-ATTACHMENT";
    }

    const char* diagnostic() const noexcept { return diagnostic_; }

    const auto& originalPlan() const noexcept { return plan_; }

    const auto& originalResult() const noexcept { return source_; }

    const auto* publishedValues() const noexcept
    {
        return outcome_ == Outcome::Completed ? &values_ : nullptr;
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    void consumeOriginalTerminal() noexcept override
    {
        if (outcome_ != Outcome::Unavailable)
            return;
        for (std::size_t i = 0; i < values_.size(); ++i) {
            if (const auto* id = WebGpuOriginalTextureReadbackStatistics::analyzePublishedOriginal(
                    plan_, source_, i, values_[i])) {
                consumeOriginalFailure(id);
                return;
            }
        }
        outcome_ = Outcome::Completed;
    }

    void consumeOriginalLoss() noexcept override
    {
        if (outcome_ == Outcome::Unavailable) {
            outcome_ = Outcome::DeviceLost;
            diagnostic_ = "RHI-WEBGPU-READBACK-MAP-DEVICE-LOST";
        }
    }

    void consumeOriginalFailure(const char* id) noexcept override
    {
        if (outcome_ == Outcome::Unavailable) {
            outcome_ = Outcome::Rejected;
            diagnostic_ = id ? id : "RHI-READBACK-NUMERICAL-TERMINAL-FAILURE";
        }
    }

    const std::shared_ptr<const Plan> plan_;
    const std::shared_ptr<const Result> source_;
    core::pmr::Vector<Analysis> values_{&core::defaultCpuMemoryResource()};
    Outcome outcome_ = Outcome::Unavailable;
    const char* diagnostic_ = nullptr;
    bool attached_ = false;
};

// endregion Webgpu original texture readback numerical result

// region Webgpu owned texture readback snapshot

// OwnedCpuSnapshot/1: no retained native destination/result/recording owners.
// Cold capacity becomes immutable output only through original publication.
class WebGpuOwnedTextureReadbackSnapshot final : public rhi::TextureReadbackOwnedSnapshot {
    friend class WebGpuOriginalTextureReadbackPublication;

    class ConstructionKey {
        friend class WebGpuOwnedTextureReadbackSnapshot;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Plan = rhi::TextureReadbackColdBudgetPlan;
    using Request = rhi::TextureReadbackRequestDeclaration;
    using Statistics = rhi::TextureReadbackStatisticsPlan;
    using Result = WebGpuOriginalTextureReadbackResult;
    using Numerical = WebGpuOriginalTextureReadbackNumericalResult;

public:
    WebGpuOwnedTextureReadbackSnapshot(ConstructionKey,
        std::shared_ptr<const Request> request,
        std::shared_ptr<const Statistics> statistics,
        rhi::TextureReadbackContentIntent::Mode mode)
        : request_(std::move(request)), statistics_(std::move(statistics)), mode_(mode)
    {}

    static std::shared_ptr<WebGpuOwnedTextureReadbackSnapshot> prepareOriginal(
        const std::shared_ptr<const Plan>& plan,
        std::shared_ptr<const Request> request,
        std::shared_ptr<const Statistics> statistics,
        rhi::TextureReadbackContentIntent::Mode mode,
        const char*& diagnostic)
    {
        diagnostic = rhi::TextureReadbackOwnedSnapshotContract::validateColdOriginal(
            plan, request, statistics, mode);
        if (diagnostic)
            return {};
        try {
            core::CpuMemoryQuotaScope metadata(
                plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)]);
            auto result = core::makeUnique<WebGpuOwnedTextureReadbackSnapshot>(
                ConstructionKey{}, std::move(request), std::move(statistics), mode);
            result->analyses_.resize(result->statistics_->views().size());
            {
                core::CpuMemoryQuotaScope pixels(
                    plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Result)]);
                result->bytes_.resize(
                    static_cast<std::size_t>(result->statistics_->originalByteCapacity()));
            }
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-OWNED-SNAPSHOT-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-READBACK-OWNED-SNAPSHOT-ALLOCATION";
            return {};
        }
    }

    const std::shared_ptr<const Request>& originalRequest() const noexcept override
    {
        return request_;
    }

    const std::shared_ptr<const Statistics>& originalStatisticsPlan() const noexcept override
    {
        return statistics_;
    }

    rhi::TextureReadbackContentIntent::Mode contentMode() const noexcept override { return mode_; }

    const std::uint8_t* publishedBytes() const noexcept override
    {
        return published_ ? bytes_.data() : nullptr;
    }

    std::size_t byteCapacity() const noexcept override { return bytes_.size(); }

    const rhi::TextureReadbackViewAnalysis* publishedAnalyses() const noexcept override
    {
        return published_ ? analyses_.data() : nullptr;
    }

    std::size_t analysisCount() const noexcept override
    {
        return published_ ? analyses_.size() : 0u;
    }

private:
    static const char* validateColdBindingOriginal(
        const std::shared_ptr<WebGpuOwnedTextureReadbackSnapshot>& self,
        const std::shared_ptr<const Request>& request,
        const std::shared_ptr<const Statistics>& statistics) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-OWNED-SNAPSHOT-OWNER";
        if (self->published_ || !core::SharedOwnerProjection::sameOwner(self->request_, request)
            || !core::SharedOwnerProjection::sameOwner(self->statistics_, statistics))
            return "RHI-READBACK-OWNED-SNAPSHOT-BINDING";
        return nullptr;
    }

    const char* publishOriginal(const std::shared_ptr<const Result>& result,
        const std::shared_ptr<const Numerical>& numerical) noexcept
    {
        if (published_)
            return "RHI-READBACK-OWNED-SNAPSHOT-DUPLICATE";
        if (const auto* id = Result::validatePublishedOriginal(result))
            return id;
        if (!numerical || !numerical->hasQuotaBoundSharedOwner(numerical)
            || numerical->outcome() != Numerical::Outcome::Completed
            || !core::SharedOwnerProjection::sameOwner(numerical->originalPlan(), statistics_)
            || !core::SharedOwnerProjection::sameOwner(numerical->originalResult(), result)
            || !core::SharedOwnerProjection::sameOwner(result->layout(), request_->originalLayout())
            || result->contentMode() != mode_ || result->byteCapacity() != bytes_.size()
            || !numerical->publishedValues()
            || numerical->publishedValues()->size() != analyses_.size())
            return "RHI-READBACK-OWNED-SNAPSHOT-PUBLICATION";
        std::memcpy(bytes_.data(), result->publishedBytes(), bytes_.size());
        std::copy(numerical->publishedValues()->begin(),
            numerical->publishedValues()->end(),
            analyses_.begin());
        published_ = true;
        return nullptr;
    }

    const std::shared_ptr<const Request> request_;
    const std::shared_ptr<const Statistics> statistics_;
    const rhi::TextureReadbackContentIntent::Mode mode_;
    core::pmr::Vector<std::uint8_t> bytes_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<rhi::TextureReadbackViewAnalysis> analyses_{
        &core::defaultCpuMemoryResource()};
    bool published_ = false;
};

// endregion Webgpu owned texture readback snapshot

// region Webgpu original texture readback publication

struct WebGpuOriginalTextureReadbackPublicationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingResults = 0u, maxMapCallbacks = 0u, maxMipLayouts = 0u;
    std::uint64_t maxRetainedNativeBytes = 0u, maxRetainedContentBytes = 0u, maxResultBytes = 0u,
                  maxLabelBytes = 0u;
    std::uint64_t maxCpuSinkBytes = 0u, maxCpuRecordBytes = 0u;
};

// OriginalReadbackPublication/1: bounded owner-thread original snapshot delivery.
// Native readiness comes exclusively from the original map; no native proof issuer.
class WebGpuOriginalTextureReadbackPublication final : public core::QuotaBoundSharedObjectIdentity {
public:
    enum class PreparationFailure {
        None,
        SourceContract,
        DestinationCreation,
        RecordingPreparation,
        MappingPreparation,
        SubmissionExecution
    };

private:
    class ConstructionKey {
        friend class WebGpuOriginalTextureReadbackPublication;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Recording = WebGpuOrdinaryRecording;
    using Submission = WebGpuOrdinarySubmission;
    using Drain = core::runtime::CompletionDrainConsumer;
    using Destination = WebGpuOriginalTextureReadbackDestination;
    using Staging = WebGpuOriginalTextureReadbackDeviceStagingBudget;
    using Mapping = WebGpuOriginalTextureReadbackMapping;
    using Result = WebGpuOriginalTextureReadbackResult;
    using Numerical = WebGpuOriginalTextureReadbackNumericalResult;
    using Request = rhi::TextureReadbackRequestDeclaration;
    using Statistics = rhi::TextureReadbackStatisticsPlan;
    using Snapshot = WebGpuOwnedTextureReadbackSnapshot;
    using Plan = rhi::TextureReadbackColdBudgetPlan;

    struct Charges {
        std::uint64_t native = 0u, content = 0u, result = 0u, label = 0u;
        std::uint32_t callbacks = 0u, mips = 0u;
    };
    struct Record;
    using Pool = core::runtime::CompletionRetentionPool<Record>;

    struct Record {
        std::shared_ptr<Mapping> mapping;
        std::shared_ptr<Destination> destination;
        std::shared_ptr<Recording> recording;
        std::shared_ptr<Submission> submission;
        bool preparationIssued = false;
        std::shared_ptr<const Request> request;
        std::shared_ptr<Numerical> numerical;
        std::shared_ptr<const Statistics> statistics;
        std::shared_ptr<Snapshot> ownedSnapshot;
        PreparationFailure preparationFailure = PreparationFailure::None;
        Charges charge;
        core::runtime::CompletionSignal::Ticket source;
        Pool::Ticket ticket;
        bool issued = false, canceled = false;

        Record(std::shared_ptr<Mapping> original, Charges c)
            : mapping(std::move(original)), charge(c)
        {}

        ~Record()
        {
            if (mapping && !issued && mapping->publicationReservation_.release(source))
                mapping->publicationReservation_.reset();
        }
    };

public:
    enum class Outcome { Empty, Published, Rejected, DeviceLost };

    struct Delivery {
        Outcome outcome = Outcome::Empty;
        const char* diagnostic = nullptr;
        std::shared_ptr<const rhi::TextureReadbackInputSnapshot> originalInput;
        std::shared_ptr<const Result> result;
        std::shared_ptr<const Request> originalRequest;
        std::shared_ptr<const Numerical> numericalResult;
        std::shared_ptr<const rhi::TextureReadbackOwnedSnapshot> ownedSnapshot;
    };

    class Reservation final {
        friend class WebGpuOriginalTextureReadbackPublication;

        Reservation(
            std::shared_ptr<WebGpuOriginalTextureReadbackPublication> sink, Pool::Ticket ticket)
            : sink_(std::move(sink)), ticket_(ticket)
        {}

    public:
        Reservation() = default;
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;

        Reservation(Reservation&& other) noexcept
            : sink_(std::move(other.sink_)), ticket_(std::exchange(other.ticket_, {}))
        {}

        Reservation& operator=(Reservation&& other) noexcept
        {
            if (this != &other) {
                cancel();
                sink_ = std::move(other.sink_);
                ticket_ = std::exchange(other.ticket_, {});
            }
            return *this;
        }

        ~Reservation() { cancel(); }

        bool cancel() noexcept
        {
            if (!sink_)
                return false;
            const auto done = sink_->cancel(ticket_);
            ticket_ = {};
            sink_.reset();
            return done;
        }

        bool shutdownRequestedOriginal() const noexcept
        {
            return sink_ && sink_->shutdownRequested_;
        }

        const char* start() noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->start(ticket_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* startOwnedOriginal() noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->startOwned(ticket_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* attachOriginal(
            std::shared_ptr<Mapping> mapping, std::shared_ptr<Numerical> numerical) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->attach(ticket_, std::move(mapping), std::move(numerical))
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* bindOwnedSnapshotOriginalCold(std::shared_ptr<Snapshot> snapshot) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->bindOwnedSnapshotCold(ticket_, std::move(snapshot))
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* initializeDestinationOriginal(std::shared_ptr<Destination> destination) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->initializeDestination(ticket_, std::move(destination), sink_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* bindDestinationOriginalCold(std::shared_ptr<Destination> destination) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->bindDestinationCold(ticket_, std::move(destination), sink_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* bindColdCandidateDestinationOriginal(
            std::shared_ptr<Destination> destination, const rhi::GpuTexture& candidate) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->bindDestinationCold(ticket_, std::move(destination), sink_, &candidate)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* initializeBoundDestinationOriginal() noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->initializeBoundDestination(ticket_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* bindRecordingOriginal(std::shared_ptr<Recording> recording) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->bindRecording(ticket_, std::move(recording), sink_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* bindRecordingAndMappingOriginalCold(std::shared_ptr<Recording> recording,
            std::shared_ptr<Mapping> mapping,
            std::shared_ptr<Numerical> numerical) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->bindRecordingAndMappingCold(ticket_,
                      std::move(recording),
                      std::move(mapping),
                      std::move(numerical),
                      sink_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* recordOriginal() noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->record(ticket_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* bindSubmissionOriginal(std::shared_ptr<Submission> submission) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->bindSubmission(ticket_, std::move(submission), sink_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        const char* submitOriginal() noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                ? sink_->submit(ticket_)
                : "RHI-READBACK-PUBLICATION-RESERVATION";
        }

        bool rejectBeforeAttachment(PreparationFailure failure) noexcept
        {
            return sink_ && sink_->hasQuotaBoundSharedOwner(sink_)
                && sink_->rejectPreparation(ticket_, failure);
        }

        explicit operator bool() const noexcept
        {
            return sink_ && ticket_ && sink_->records_.get(ticket_);
        }

    private:
        std::shared_ptr<WebGpuOriginalTextureReadbackPublication> sink_;
        Pool::Ticket ticket_;
    };

    struct Lowering {
        bool validatorOnly = false, sinkImplemented = false;
        const char* diagnostic = "RHI-READBACK-PUBLICATION-BACKEND";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-READBACK-PUBLICATION-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-READBACK-PUBLICATION-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-READBACK-PUBLICATION-PROTOTYPE-OPT-IN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalTextureReadbackPublication(ConstructionKey,
        WebGpuOriginalTextureReadbackPublicationBudget b,
        std::shared_ptr<core::CpuMemoryQuota> records)
        : budget_(b), recordQuota_(std::move(records)), records_(b.maxPendingResults)
    {}

    static const char* validateBudget(
        const WebGpuOriginalTextureReadbackPublicationBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-READBACK-PUBLICATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-READBACK-PUBLICATION-OPT-IN";
        if (!b.maxPendingResults
            || b.maxMapCallbacks < Mapping::callbackCount + Mapping::scopeCallbackCount
            || !b.maxMipLayouts || !b.maxRetainedNativeBytes || !b.maxRetainedContentBytes
            || !b.maxResultBytes || !b.maxCpuSinkBytes || !b.maxCpuRecordBytes)
            return "RHI-READBACK-PUBLICATION-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackPublication> prepareOriginal(
        const WebGpuOriginalTextureReadbackPublicationBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> sinkQuota,
        std::shared_ptr<core::CpuMemoryQuota> recordQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!sinkQuota || sinkQuota->limitBytes() != b.maxCpuSinkBytes || !recordQuota
            || recordQuota->limitBytes() != b.maxCpuRecordBytes) {
            diagnostic = "RHI-READBACK-PUBLICATION-QUOTA";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope metadata(sinkQuota);
            return shareOriginal(core::makeUnique<WebGpuOriginalTextureReadbackPublication>(
                ConstructionKey{}, b, std::move(recordQuota)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-PUBLICATION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-READBACK-PUBLICATION-ALLOCATION";
            return {};
        }
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackPublication> prepareForOriginalRegistry(
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        const WebGpuOriginalTextureReadbackPublicationBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> sinkQuota,
        std::shared_ptr<core::CpuMemoryQuota> recordQuota,
        const char*& diagnostic)
    {
        if (!registry || !registry->sharedOwnerMatches(registry)) {
            diagnostic = "RHI-READBACK-PUBLICATION-REGISTRY-OWNER";
            return {};
        }
        auto sink = prepareOriginal(b, std::move(sinkQuota), std::move(recordQuota), diagnostic);
        if (sink)
            sink->registry_ = std::move(registry);
        return sink;
    }

    static std::shared_ptr<WebGpuOriginalTextureReadbackPublication> prepareForDevicePlan(
        std::shared_ptr<const Plan> plan,
        std::shared_ptr<WebGpuRhiResourceRegistry> registry,
        const WebGpuOriginalTextureReadbackPublicationBudget& b,
        const char*& diagnostic)
    {
        diagnostic = Plan::validateOriginal(plan);
        if (diagnostic)
            return {};
        const auto& original = plan->budget();
        std::uint32_t callbacks = 0u, mips = 0u;
        if ((diagnostic = Plan::aggregatePublicationCounts(original, callbacks, mips)))
            return {};
        if (b.contractVersion != original.contractVersion
            || b.prototypeOptIn != original.prototypeOptIn
            || b.maxPendingResults != original.maxPendingRequests || b.maxMapCallbacks != callbacks
            || b.maxMipLayouts != mips
            || b.maxRetainedNativeBytes != original.maxDeviceRetainedNativeBytes
            || b.maxRetainedContentBytes != original.maxDeviceRetainedContentBytes
            || b.maxResultBytes != original.maxDeviceRetainedResultBytes
            || b.maxLabelBytes != original.maxDeviceRetainedLabelBytes
            || b.maxCpuSinkBytes != original.maxCpuDeviceBytes
            || b.maxCpuRecordBytes != original.maxCpuRequestBytes) {
            diagnostic = "RHI-READBACK-PUBLICATION-PLAN-LOWERING";
            return {};
        }
        auto sink = prepareForOriginalRegistry(std::move(registry),
            b,
            plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Device)],
            plan->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Request)],
            diagnostic);
        if (sink)
            sink->plan_ = std::move(plan);
        return sink;
    }

    static std::optional<Reservation> reserveRequestOriginal(
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self,
        std::shared_ptr<const Request> request,
        std::shared_ptr<const Statistics> statistics,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (!self || !self->hasQuotaBoundSharedOwner(self)) {
            diagnostic = "RHI-READBACK-PUBLICATION-SINK-OWNER";
            return {};
        }
        if (!self->acceptingOriginal()) {
            diagnostic = "RHI-READBACK-PUBLICATION-CLOSED";
            return {};
        }
        if (!self->plan_ || !self->registry_) {
            diagnostic = "RHI-READBACK-PUBLICATION-PLAN-REQUIRED";
            return {};
        }
        if (webGpuNativeDeviceLost()) {
            diagnostic = "RHI-READBACK-PUBLICATION-PREPARATION-DEVICE-LOST";
            return {};
        }
        if ((diagnostic = Plan::validateRequestOriginal(self->plan_, request, statistics)))
            return {};
        if (!core::SharedOwnerProjection::sameOwner(
                request->originalMetadataQuota(), self->recordQuota_)
            || !core::SharedOwnerProjection::sameOwner(
                statistics->originalMetadataQuota(), self->recordQuota_)) {
            diagnostic = "RHI-READBACK-PUBLICATION-REQUEST-QUOTA";
            return {};
        }
        if (self->containsRequestId(request->identity().requestId)) {
            diagnostic = "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE";
            return {};
        }
        const auto& layout = request->originalLayout();
        const Charges intent{0u,
            0u,
            layout->inputOwner()->scalarFields().byteCapacity(),
            request->label().size(),
            Mapping::callbackCount + Mapping::scopeCallbackCount,
            static_cast<std::uint32_t>(layout->mipCopies().size())};
        if (!self->canCharge(intent)) {
            diagnostic = "RHI-READBACK-PUBLICATION-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(self->recordQuota_);
            auto entry = core::makeUnique<Record>(nullptr, intent);
            entry->request = std::move(request);
            entry->statistics = std::move(statistics);
            const auto ticket = self->records_.reserve(entry);
            if (!ticket) {
                diagnostic = "RHI-READBACK-PUBLICATION-CAPACITY";
                return {};
            }
            self->records_.get(ticket)->ticket = ticket;
            self->charge(intent);
            return Reservation(self, ticket);
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-PUBLICATION-ALLOCATION";
            return {};
        }
    }

    static std::optional<Reservation> reserveTrackedOriginal(
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self,
        std::shared_ptr<Mapping> mapping,
        std::shared_ptr<const Request> request,
        std::shared_ptr<Numerical> numerical,
        const char*& diagnostic)
    {
        return reserveOriginalImpl(
            self, std::move(mapping), std::move(request), std::move(numerical), true, diagnostic);
    }

    static std::optional<Reservation> reserveOriginal(
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self,
        std::shared_ptr<Mapping> mapping,
        const char*& diagnostic)
    {
        return reserveOriginalImpl(self, std::move(mapping), {}, {}, false, diagnostic);
    }

private:
    static std::optional<Reservation> reserveOriginalImpl(
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self,
        std::shared_ptr<Mapping> mapping,
        std::shared_ptr<const Request> request,
        std::shared_ptr<Numerical> numerical,
        bool tracked,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (!self || !self->hasQuotaBoundSharedOwner(self)) {
            diagnostic = "RHI-READBACK-PUBLICATION-SINK-OWNER";
            return {};
        }
        if (!self->acceptingOriginal()) {
            diagnostic = "RHI-READBACK-PUBLICATION-CLOSED";
            return {};
        }
        if (self->plan_) {
            diagnostic = "RHI-READBACK-PUBLICATION-EARLY-ADMISSION-REQUIRED";
            return {};
        }
        if (!mapping || !mapping->hasQuotaBoundSharedOwner(mapping)) {
            diagnostic = "RHI-READBACK-PUBLICATION-MAP-OWNER";
            return {};
        }
        if (tracked && !self->registry_) {
            diagnostic = "RHI-READBACK-PUBLICATION-REGISTRY-REQUIRED";
            return {};
        }
        if (self->registry_) {
            const auto& pair = mapping->copies()
                                   ->transaction()
                                   ->states()
                                   ->inputs()
                                   ->scalars()
                                   ->usage()
                                   ->projection()
                                   ->originalReadbacks()[0];
            if (!core::SharedOwnerProjection::sameOwner(
                    self->registry_, pair->destination()->source()->registry())) {
                diagnostic = "RHI-READBACK-PUBLICATION-REGISTRY-MISMATCH";
                return {};
            }
        }
        if (tracked) {
            if ((diagnostic =
                        Request::validateOriginal(request, mapping->resultDeclaration()->layout())))
                return {};
            if ((diagnostic = Numerical::validateAttachedOriginal(numerical, mapping)))
                return {};
            if (self->containsRequestId(request->identity().requestId)) {
                diagnostic = "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE";
                return {};
            }
        }
        if (self->records_.containsMatching([&](const Record& r) noexcept {
                return core::SharedOwnerProjection::sameOwner(r.mapping, mapping);
            })) {
            diagnostic = "RHI-READBACK-PUBLICATION-DUPLICATE";
            return {};
        }
        if (self->records_.pending() >= self->budget_.maxPendingResults) {
            diagnostic = "RHI-READBACK-PUBLICATION-CAPACITY";
            return {};
        }
        if (mapping->status() != Mapping::Status::Prepared
            || mapping->copies()->nativeStatus()
                != WebGpuOriginalTextureReadbackCopies::NativeStatus::Unrecorded) {
            diagnostic = "RHI-READBACK-PUBLICATION-COLD-STATUS";
            return {};
        }
        if ((diagnostic = mapping->validate()))
            return {};
        if (!mapping->publicationReservation_.canReserve()) {
            diagnostic = "RHI-READBACK-PUBLICATION-SOURCE-BUSY";
            return {};
        }
        const auto& pair = mapping->copies()
                               ->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        const auto& result = mapping->resultDeclaration();
        if (!result || !result->hasQuotaBoundSharedOwner(result)
            || !core::SharedOwnerProjection::sameOwner(result->layout(), pair->layout())) {
            diagnostic = "RHI-READBACK-PUBLICATION-RESULT-OWNER";
            return {};
        }
        const Charges charge{pair->retainedNativeAllocationBytes(),
            pair->destination()->source()->retainedContentBytes(),
            result->byteCapacity(),
            result->layout()->inputOwner()->label().size(),
            Mapping::callbackCount + Mapping::scopeCallbackCount,
            static_cast<std::uint32_t>(result->layout()->mipCopies().size())};
        if (!self->canCharge(charge)) {
            diagnostic = "RHI-READBACK-PUBLICATION-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(self->recordQuota_);
            auto entry = core::makeUnique<Record>(mapping, charge);
            entry->source = mapping->publicationReservation_.reserve();
            if (!entry->source) {
                diagnostic = "RHI-READBACK-PUBLICATION-SOURCE-BUSY";
                return {};
            }
            const auto ticket = self->records_.reserve(entry);
            if (!ticket) {
                diagnostic = "RHI-READBACK-PUBLICATION-CAPACITY";
                return {};
            }
            auto* stored = self->records_.get(ticket);
            stored->ticket = ticket;
            stored->request = std::move(request);
            stored->numerical = std::move(numerical);
            self->charge(charge);
            return Reservation(self, ticket);
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-READBACK-PUBLICATION-ALLOCATION";
            return {};
        }
    }

public:
    static bool pollOriginal(const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self,
        Delivery& output,
        const char*& diagnostic) noexcept
    {
        diagnostic = nullptr;
        if (!self || !self->hasQuotaBoundSharedOwner(self)) {
            diagnostic = "RHI-READBACK-PUBLICATION-SINK-OWNER";
            return false;
        }
        if (output.outcome != Outcome::Empty || output.originalInput || output.result
            || output.originalRequest || output.numericalResult || output.ownedSnapshot) {
            diagnostic = "RHI-READBACK-PUBLICATION-OUTPUT-BUSY";
            return false;
        }
        for (auto remaining = self->pending(); remaining; --remaining) {
            Pool::Ticket selected;
            // Entries are cold-owned; the bounded pool provides their stable scan.
            self->records_.containsMatching([&](const Record& entry) noexcept {
                if (entry.mapping)
                    entry.mapping->notifyTerminalConsumer();
                if (!terminal(entry))
                    return false;
                selected = entry.ticket;
                return true;
            });
            if (!selected)
                return false;
            auto* entry = self->records_.get(selected);
            if (!entry->mapping || entry->preparationFailure != PreparationFailure::None
                || (entry->recording && webGpuNativeDeviceLost() && !entry->issued)) {
                Delivery delivered;
                delivered.originalRequest = entry->request;
                delivered.originalInput = entry->request->originalLayout()->inputOwner();
                const auto loss = webGpuNativeDeviceLost();
                delivered.outcome = loss ? Outcome::DeviceLost : Outcome::Rejected;
                delivered.diagnostic = loss ? "RHI-READBACK-PUBLICATION-PREPARATION-DEVICE-LOST"
                                            : preparationDiagnostic(entry->preparationFailure);
                const auto canceled = entry->canceled;
                self->retire(selected);
                if (canceled)
                    continue;
                output = std::move(delivered);
                return true;
            }
            const auto status = entry->mapping->status();
            Delivery delivered;
            delivered.originalRequest = entry->request;
            delivered.numericalResult = entry->numerical;
            delivered.originalInput = entry->mapping->resultDeclaration()->layout()->inputOwner();
            if (status == Mapping::Status::Published && entry->issued) {
                if (const auto* id =
                        Result::validatePublishedOriginal(entry->mapping->resultDeclaration())) {
                    delivered.outcome = Outcome::Rejected;
                    delivered.diagnostic = id;
                } else if (entry->numerical
                    && entry->numerical->outcome() != Numerical::Outcome::Completed) {
                    delivered.outcome = Outcome::Rejected;
                    delivered.diagnostic = entry->numerical->diagnostic()
                        ? entry->numerical->diagnostic()
                        : "RHI-READBACK-PUBLICATION-NUMERICAL-UNPROVEN";
                } else {
                    delivered.outcome = Outcome::Published;
                    delivered.result = entry->mapping->resultDeclaration();
                }
            } else {
                delivered.outcome =
                    status == Mapping::Status::DeviceLost ? Outcome::DeviceLost : Outcome::Rejected;
                delivered.diagnostic = entry->issued || status == Mapping::Status::DeviceLost
                    ? entry->mapping->diagnostic()
                    : "RHI-READBACK-PUBLICATION-UNOWNED-START";
            }
            if (delivered.outcome == Outcome::Published && entry->ownedSnapshot
                && !entry->canceled) {
                if (const auto* id = entry->ownedSnapshot->publishOriginal(
                        delivered.result, delivered.numericalResult)) {
                    delivered.outcome = Outcome::Rejected;
                    delivered.diagnostic = id;
                    delivered.result.reset();
                    delivered.numericalResult.reset();
                } else
                    delivered.ownedSnapshot = entry->ownedSnapshot;
            }
            const auto canceled = entry->canceled;
            self->retire(selected);
            if (canceled)
                continue;
            output = std::move(delivered);
            return true;
        }
        return false;
    }

    static const char* cancelAllOriginal(
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-READBACK-PUBLICATION-SINK-OWNER";
        self->shutdownRequested_ = true;
        for (auto remaining = self->pending(); remaining; --remaining) {
            Pool::Ticket selected;
            self->records_.containsMatching([&](const Record& entry) noexcept {
                if (entry.canceled)
                    return false;
                selected = entry.ticket;
                return true;
            });
            if (!selected)
                break;
            self->cancel(selected);
        }
        // Canceled terminal records have no deliverable output. Reuse original
        // bounded retirement, including native loss and mapping terminal facts.
        Delivery suppressed;
        const char* diagnostic = nullptr;
        if (pollOriginal(self, suppressed, diagnostic))
            return "RHI-READBACK-PUBLICATION-SHUTDOWN-OUTPUT";
        return diagnostic;
    }

    bool acceptingOriginal() const noexcept { return !shutdownRequested_; }

    bool callbacksOutstandingOriginal() const noexcept
    {
        return records_.containsMatching([](const Record& entry) noexcept {
            return executionOutstanding(entry)
                || (entry.destination && entry.destination->creationCallbacksIssued()
                    && !entry.destination->creationScopesObserved())
                || (entry.mapping && entry.mapping->callbacksIssued()
                    && !entry.mapping->callbacksDrained());
        });
    }

    std::size_t pending() const noexcept { return records_.pending(); }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const char* bindOwnedSnapshotCold(
        Pool::Ticket ticket, std::shared_ptr<Snapshot> snapshot) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry || !plan_ || entry->preparationIssued || entry->issued || entry->canceled
            || entry->ownedSnapshot)
            return "RHI-READBACK-OWNED-SNAPSHOT-BIND-STATUS";
        if (const auto* id =
                Snapshot::validateColdBindingOriginal(snapshot, entry->request, entry->statistics))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                snapshot->originalMetadataQuota(), recordQuota_))
            return "RHI-READBACK-OWNED-SNAPSHOT-REQUEST-QUOTA";
        entry->ownedSnapshot = std::move(snapshot);
        return nullptr;
    }

    static bool terminal(const Record& entry) noexcept
    {
        // Global/native loss may expose a terminal status before issued map
        // callbacks arrive. Original publication must retain its ticket/charges
        // until every native callback participant has actually drained.
        if (entry.mapping && entry.mapping->callbacksIssued() && !entry.mapping->callbacksDrained())
            return false;
        if (executionOutstanding(entry))
            return false;
        if (entry.recording && !entry.issued
            && (entry.canceled || entry.preparationFailure != PreparationFailure::None
                || webGpuNativeDeviceLost()))
            return true;
        if (!entry.mapping) {
            if (entry.destination && entry.preparationIssued
                && !entry.destination->creationScopesObserved())
                return false;
            return entry.canceled || entry.preparationFailure != PreparationFailure::None
                || webGpuNativeDeviceLost();
        }
        const auto status = entry.mapping->status();
        if (status == Mapping::Status::DeviceLost || status == Mapping::Status::Published)
            return true;
        return status == Mapping::Status::Rejected && entry.mapping->mapCompletionObserved()
            && entry.mapping->scopeCompletionsObserved();
    }

    static bool executionOutstanding(const Record& entry) noexcept
    {
        return (entry.recording && entry.recording->readbackRecordingCallbacksIssued()
                   && !entry.recording->readbackRecordingCallbacksDrained())
            || (entry.submission && entry.submission->callbacksIssued()
                && !entry.submission->callbacksDrained());
    }

    class ExecutionDrainRetention final : public Drain {
    public:
        ExecutionDrainRetention(
            std::shared_ptr<WebGpuOriginalTextureReadbackPublication> sink, Pool::Ticket ticket)
            : sink_(std::move(sink)), ticket_(ticket)
        {}

        void consumeOriginalCallbackDrain() noexcept override { sink_->executionDrained(ticket_); }

    private:
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication> sink_;
        const Pool::Ticket ticket_;
    };

    void executionDrained(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry || executionOutstanding(*entry))
            return;
        if (entry->recording && entry->recording->status() == Recording::Status::Rejected)
            entry->preparationFailure = PreparationFailure::RecordingPreparation;
        if (entry->submission && entry->submission->status() == Submission::Status::Unproven)
            entry->preparationFailure = PreparationFailure::SubmissionExecution;
        if (entry->canceled && !entry->issued)
            retire(ticket);
    }

    const char* bindRecording(Pool::Ticket ticket,
        std::shared_ptr<Recording> recording,
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry)
            return "RHI-READBACK-PUBLICATION-RESERVATION";
        if (!plan_ || !entry->destination || entry->recording || entry->issued || entry->canceled
            || entry->preparationFailure != PreparationFailure::None)
            return "RHI-READBACK-PUBLICATION-EXECUTION-BIND-STATUS";
        if (!recording || !recording->hasQuotaBoundSharedOwner(recording))
            return "RHI-READBACK-PUBLICATION-EXECUTION-RECORDING-OWNER";
        if (recording->status() != Recording::Status::Prepared
            || recording->readbackRecordingCallbacksIssued() || recording->readbackDrainConsumer_)
            return "RHI-READBACK-PUBLICATION-EXECUTION-BIND-STATUS";
        const auto& copies = recording->originalReadbackCopies();
        if (!copies || !copies->hasQuotaBoundSharedOwner(copies)
            || copies->nativeStatus()
                != WebGpuOriginalTextureReadbackCopies::NativeStatus::Unrecorded)
            return "RHI-READBACK-PUBLICATION-EXECUTION-COPIES";
        if (const auto* id = copies->validate())
            return id;
        const auto& pair = copies->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        if (!core::SharedOwnerProjection::sameOwner(entry->destination, pair->destination())
            || (entry->mapping
                && !core::SharedOwnerProjection::sameOwner(entry->mapping->copies(), copies)))
            return "RHI-READBACK-PUBLICATION-EXECUTION-ORIGINAL-GRAPH";
        if (!core::SharedOwnerProjection::sameOwner(
                recording->originalMetadataQuota(), recordQuota_)
            || !core::SharedOwnerProjection::sameOwner(
                copies->originalMetadataQuota(), recordQuota_)
            || !core::SharedOwnerProjection::sameOwner(
                copies->transaction()->originalMetadataQuota(), recordQuota_))
            return "RHI-READBACK-PUBLICATION-EXECUTION-REQUEST-QUOTA";
        const auto& owner = recording->originalCompletionOwner();
        if (!owner || !owner->sharedOwnerMatches(owner)
            || !core::SharedOwnerProjection::sameOwner(owner->preparationQuota,
                plan_->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Device)])
            || owner->retained.capacity() != plan_->budget().maxPendingRequests
            || !owner->retained.accepting())
            return "RHI-READBACK-PUBLICATION-EXECUTION-DEVICE-OWNER";
        try {
            core::CpuMemoryQuotaScope scope(recordQuota_);
            auto consumer = core::makeShared<ExecutionDrainRetention>(self, ticket);
            entry->recording = recording;
            recording->readbackDrainConsumer_ = std::move(consumer);
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-READBACK-PUBLICATION-EXECUTION-ALLOCATION";
        }
    }

    const char* bindRecordingAndMappingCold(Pool::Ticket ticket,
        std::shared_ptr<Recording> recording,
        std::shared_ptr<Mapping> mapping,
        std::shared_ptr<Numerical> numerical,
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self) noexcept
    {
        if (const auto* id = bindRecording(ticket, std::move(recording), self))
            return id;
        if (const auto* id = attach(ticket, std::move(mapping), std::move(numerical))) {
            // bindRecording accepted an empty unissued binding; attach fails
            // before its final signal reservation/charge update. Restore only
            // this transaction's cold binding, preserving the original ticket.
            auto* entry = records_.get(ticket);
            entry->recording->readbackDrainConsumer_.reset();
            entry->recording.reset();
            return id;
        }
        return nullptr;
    }

    const char* record(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry || !entry->recording)
            return "RHI-READBACK-PUBLICATION-EXECUTION-NOT-BOUND";
        if (entry->canceled || entry->preparationFailure != PreparationFailure::None)
            return "RHI-READBACK-PUBLICATION-EXECUTION-STATUS";
        const auto* id = Recording::recordOriginalTextureReadback(entry->recording);
        if (id && !entry->recording->readbackRecordingCallbacksIssued()) {
            entry->preparationFailure = PreparationFailure::RecordingPreparation;
            entry->recording->readbackDrainConsumer_.reset();
        }
        return id;
    }

    const char* bindSubmission(Pool::Ticket ticket,
        std::shared_ptr<Submission> submission,
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry)
            return "RHI-READBACK-PUBLICATION-RESERVATION";
        if (!entry->recording || entry->submission || entry->canceled || entry->issued
            || entry->preparationFailure != PreparationFailure::None
            || !entry->recording->readbackRecordingCallbacksDrained()
            || !entry->recording->hasRecordedCommand())
            return "RHI-READBACK-PUBLICATION-EXECUTION-BIND-STATUS";
        if (!submission || !submission->hasQuotaBoundSharedOwner(submission))
            return "RHI-READBACK-PUBLICATION-EXECUTION-SUBMISSION-OWNER";
        if (submission->status() != Submission::Status::Ready || submission->callbacksIssued()
            || submission->readbackDrainConsumer_)
            return "RHI-READBACK-PUBLICATION-EXECUTION-BIND-STATUS";
        if (!core::SharedOwnerProjection::sameOwner(
                submission->originalRecording(), entry->recording))
            return "RHI-READBACK-PUBLICATION-EXECUTION-ORIGINAL-GRAPH";
        if (!core::SharedOwnerProjection::sameOwner(
                submission->originalMetadataQuota(), recordQuota_))
            return "RHI-READBACK-PUBLICATION-EXECUTION-REQUEST-QUOTA";
        try {
            core::CpuMemoryQuotaScope scope(recordQuota_);
            auto consumer = core::makeShared<ExecutionDrainRetention>(self, ticket);
            entry->submission = submission;
            submission->readbackDrainConsumer_ = std::move(consumer);
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-READBACK-PUBLICATION-EXECUTION-ALLOCATION";
        }
    }

    const char* submit(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry || !entry->submission)
            return "RHI-READBACK-PUBLICATION-EXECUTION-NOT-BOUND";
        if (entry->canceled || entry->preparationFailure != PreparationFailure::None)
            return "RHI-READBACK-PUBLICATION-EXECUTION-STATUS";
        const auto* id = Submission::submit(entry->submission);
        if (id && !entry->submission->callbacksIssued()) {
            entry->preparationFailure = PreparationFailure::SubmissionExecution;
            entry->submission->readbackDrainConsumer_.reset();
        }
        return id;
    }

    class CreationDrainRetention final : public Destination::CreationDrainConsumer {
    public:
        CreationDrainRetention(
            std::shared_ptr<WebGpuOriginalTextureReadbackPublication> sink, Pool::Ticket ticket)
            : sink_(std::move(sink)), ticket_(ticket)
        {}

        void consumeOriginalCallbackDrain() noexcept override { sink_->creationDrained(ticket_); }

    private:
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication> sink_;
        const Pool::Ticket ticket_;
    };

    void creationDrained(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry || !entry->destination || !entry->destination->creationScopesObserved())
            return;
        if (entry->destination->status() == Destination::Status::Rejected
            || entry->destination->status() == Destination::Status::DeviceLost)
            entry->preparationFailure = PreparationFailure::DestinationCreation;
        if (entry->canceled)
            retire(ticket);
    }

    const char* bindDestinationCold(Pool::Ticket ticket,
        std::shared_ptr<Destination> destination,
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self,
        const rhi::GpuTexture* coldCandidate = nullptr) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry)
            return "RHI-READBACK-PUBLICATION-RESERVATION";
        if (!plan_ || !entry->request || entry->destination || entry->mapping || entry->issued
            || entry->canceled || entry->preparationFailure != PreparationFailure::None)
            return "RHI-READBACK-PUBLICATION-PREPARATION-STATUS";
        if (!destination || !destination->hasQuotaBoundSharedOwner(destination))
            return "RHI-READBACK-PUBLICATION-PREPARATION-DESTINATION-OWNER";
        if (destination->status() != Destination::Status::Prepared
            || destination->creationCallbacksIssued() || destination->creationDrainConsumer_)
            return "RHI-READBACK-PUBLICATION-PREPARATION-STATUS";
        if (const auto* id = Staging::validateOriginal(destination->originalDeviceStagingBudget()))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                plan_, destination->originalDeviceStagingBudget()->originalPlan()))
            return "RHI-READBACK-PUBLICATION-PREPARATION-PLAN";
        if (!core::SharedOwnerProjection::sameOwner(
                destination->originalMetadataQuota(), recordQuota_)
            || !core::SharedOwnerProjection::sameOwner(destination->resourceQuota_,
                plan_->quotas()[static_cast<std::size_t>(Plan::CpuDomain::Resource)]))
            return "RHI-READBACK-PUBLICATION-PREPARATION-QUOTA";
        const auto& source = *destination->source();
        if (const auto* id = Request::validateOriginal(entry->request, source.layout()))
            return id;
        if (const auto* id = coldCandidate ? source.validateColdCandidateOriginal(*coldCandidate)
                                           : source.validate())
            return id;
        if (!coldCandidate)
            if (const auto* id = source.validateReadbackContentProfile())
                return id;
        const auto& lease = source.publicSourceLease();
        if (!lease
            || !core::SharedOwnerProjection::sameOwner(entry->request, lease->originalRequest()))
            return "RHI-READBACK-PUBLICATION-PREPARATION-REQUEST";
        if (!core::SharedOwnerProjection::sameOwner(source.originalMetadataQuota(), recordQuota_)
            || !core::SharedOwnerProjection::sameOwner(
                lease->originalMetadataQuota(), recordQuota_))
            return "RHI-READBACK-PUBLICATION-PREPARATION-QUOTA";
        if (!core::SharedOwnerProjection::sameOwner(registry_, source.registry()))
            return "RHI-READBACK-PUBLICATION-REGISTRY-MISMATCH";
        const auto staging = destination->memoryStats().allocatedBytes,
                   sourceBytes = source.retainedNativeAllocationBytes();
        if (!core::MemoryBudgetLedger::canAdd(sourceBytes, staging))
            return "RHI-READBACK-PUBLICATION-PREPARATION-BYTE-OVERFLOW";
        const auto native = sourceBytes + staging, content = source.retainedContentBytes();
        if (native > plan_->budget().maxNativeBytesPerRequest
            || content > plan_->budget().maxContentBytesPerRequest
            || native > budget_.maxRetainedNativeBytes - charged_.native
            || content > budget_.maxRetainedContentBytes - charged_.content)
            return "RHI-READBACK-PUBLICATION-CAPACITY";
        try {
            core::CpuMemoryQuotaScope scope(recordQuota_);
            auto consumer = core::makeShared<CreationDrainRetention>(self, ticket);
            entry->destination = destination;
            entry->charge.native = native;
            entry->charge.content = content;
            charged_.native += native;
            charged_.content += content;
            destination->creationDrainConsumer_ = std::move(consumer);
            return nullptr;
        } catch (const std::bad_alloc&) {
            return "RHI-READBACK-PUBLICATION-PREPARATION-ALLOCATION";
        }
    }

    const char* initializeBoundDestination(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry)
            return "RHI-READBACK-PUBLICATION-RESERVATION";
        if (!entry->destination)
            return "RHI-READBACK-PUBLICATION-PREPARATION-NOT-BOUND";
        if (entry->canceled || entry->mapping || entry->issued || entry->preparationIssued
            || entry->preparationFailure != PreparationFailure::None
            || !entry->destination->creationDrainConsumer_
            || entry->destination->creationCallbacksIssued())
            return "RHI-READBACK-PUBLICATION-PREPARATION-STATUS";
        const auto& destination = entry->destination;
        entry->preparationIssued = true;
        const auto* id = Destination::initialize(destination);
        if (!destination->creationCallbacksIssued()) {
            entry->preparationIssued = false;
            entry->preparationFailure = PreparationFailure::DestinationCreation;
            destination->creationDrainConsumer_.reset();
        }
        return id;
    }

    // Selected private compatibility helper explicitly combines a cold bind
    // with hot issue. Public preparation must call the separated entry points.
    const char* initializeDestination(Pool::Ticket ticket,
        std::shared_ptr<Destination> destination,
        const std::shared_ptr<WebGpuOriginalTextureReadbackPublication>& self) noexcept
    {
        if (const auto* id = bindDestinationCold(ticket, std::move(destination), self))
            return id;
        return initializeBoundDestination(ticket);
    }

    bool containsRequestId(std::uint64_t id) const noexcept
    {
        return records_.containsMatching([&](const Record& r) noexcept {
            return r.request && r.request->identity().requestId == id;
        });
    }

    static const char* preparationDiagnostic(PreparationFailure failure) noexcept
    {
        switch (failure) {
        case PreparationFailure::SourceContract:
            return "RHI-READBACK-PUBLICATION-PREPARATION-SOURCE";
        case PreparationFailure::DestinationCreation:
            return "RHI-READBACK-PUBLICATION-PREPARATION-DESTINATION";
        case PreparationFailure::RecordingPreparation:
            return "RHI-READBACK-PUBLICATION-PREPARATION-RECORDING";
        case PreparationFailure::SubmissionExecution:
            return "RHI-READBACK-PUBLICATION-EXECUTION-SUBMISSION";
        case PreparationFailure::MappingPreparation:
            return "RHI-READBACK-PUBLICATION-PREPARATION-MAPPING";
        case PreparationFailure::None:
            break;
        }
        return "RHI-READBACK-PUBLICATION-PREPARATION-UNPROVEN";
    }

    bool rejectPreparation(Pool::Ticket ticket, PreparationFailure failure) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry || entry->mapping || entry->issued || entry->canceled
            || entry->preparationFailure != PreparationFailure::None
            || failure <= PreparationFailure::None
            || failure > PreparationFailure::MappingPreparation)
            return false;
        entry->preparationFailure = failure;
        return true;
    }

    const char* attach(Pool::Ticket ticket,
        std::shared_ptr<Mapping> mapping,
        std::shared_ptr<Numerical> numerical) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry)
            return "RHI-READBACK-PUBLICATION-RESERVATION";
        if (!plan_ || !entry->request || !entry->statistics || entry->mapping || entry->issued
            || entry->canceled || entry->preparationFailure != PreparationFailure::None)
            return "RHI-READBACK-PUBLICATION-ATTACH-STATUS";
        if (!mapping || !mapping->hasQuotaBoundSharedOwner(mapping))
            return "RHI-READBACK-PUBLICATION-MAP-OWNER";
        if (mapping->status() != Mapping::Status::Prepared
            || mapping->copies()->nativeStatus()
                != WebGpuOriginalTextureReadbackCopies::NativeStatus::Unrecorded)
            return "RHI-READBACK-PUBLICATION-COLD-STATUS";
        if (const auto* id = mapping->validate())
            return id;
        if (const auto* id =
                Request::validateOriginal(entry->request, mapping->resultDeclaration()->layout()))
            return id;
        if (const auto* id = Numerical::validateAttachedOriginal(numerical, mapping))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(entry->statistics, numerical->originalPlan()))
            return "RHI-READBACK-PUBLICATION-STATISTICS-MISMATCH";
        if (!core::SharedOwnerProjection::sameOwner(mapping->originalMetadataQuota(), recordQuota_)
            || !core::SharedOwnerProjection::sameOwner(
                mapping->resultDeclaration()->originalMetadataQuota(), recordQuota_)
            || !core::SharedOwnerProjection::sameOwner(
                numerical->originalMetadataQuota(), recordQuota_))
            return "RHI-READBACK-PUBLICATION-ATTACH-QUOTA";
        const auto& pair = mapping->copies()
                               ->transaction()
                               ->states()
                               ->inputs()
                               ->scalars()
                               ->usage()
                               ->projection()
                               ->originalReadbacks()[0];
        if (!core::SharedOwnerProjection::sameOwner(
                registry_, pair->destination()->source()->registry()))
            return "RHI-READBACK-PUBLICATION-REGISTRY-MISMATCH";
        const auto& lease = pair->destination()->source()->publicSourceLease();
        if (lease
            && !core::SharedOwnerProjection::sameOwner(entry->request, lease->originalRequest()))
            return "RHI-READBACK-PUBLICATION-PREPARATION-REQUEST";
        if (entry->recording
            && !core::SharedOwnerProjection::sameOwner(
                entry->recording->originalReadbackCopies(), mapping->copies()))
            return "RHI-READBACK-PUBLICATION-EXECUTION-ORIGINAL-GRAPH";
        if (entry->destination
            && !core::SharedOwnerProjection::sameOwner(entry->destination, pair->destination()))
            return "RHI-READBACK-PUBLICATION-PREPARATION-DESTINATION-MISMATCH";
        if (records_.containsMatching([&](const Record& original) noexcept {
                return original.mapping
                    && core::SharedOwnerProjection::sameOwner(original.mapping, mapping);
            }))
            return "RHI-READBACK-PUBLICATION-DUPLICATE";
        if (!mapping->publicationReservation_.canReserve())
            return "RHI-READBACK-PUBLICATION-SOURCE-BUSY";
        const auto native = pair->retainedNativeAllocationBytes(),
                   content = pair->destination()->source()->retainedContentBytes();
        if (native < entry->charge.native || content < entry->charge.content)
            return "RHI-READBACK-PUBLICATION-PREPARATION-CHARGE-MISMATCH";
        const auto additionalNative = native - entry->charge.native,
                   additionalContent = content - entry->charge.content;
        if (native > plan_->budget().maxNativeBytesPerRequest
            || content > plan_->budget().maxContentBytesPerRequest
            || additionalNative > budget_.maxRetainedNativeBytes - charged_.native
            || additionalContent > budget_.maxRetainedContentBytes - charged_.content)
            return "RHI-READBACK-PUBLICATION-CAPACITY";
        const auto source = mapping->publicationReservation_.reserve();
        if (!source)
            return "RHI-READBACK-PUBLICATION-SOURCE-BUSY";
        entry->source = source;
        entry->mapping = std::move(mapping);
        entry->numerical = std::move(numerical);
        entry->charge.native = native;
        entry->charge.content = content;
        charged_.native += additionalNative;
        charged_.content += additionalContent;
        return nullptr;
    }

    bool canCharge(const Charges& c) const noexcept
    {
        return records_.pending() < budget_.maxPendingResults
            && c.native <= budget_.maxRetainedNativeBytes - charged_.native
            && c.content <= budget_.maxRetainedContentBytes - charged_.content
            && c.result <= budget_.maxResultBytes - charged_.result
            && c.label <= budget_.maxLabelBytes - charged_.label
            && c.callbacks <= budget_.maxMapCallbacks - charged_.callbacks
            && c.mips <= budget_.maxMipLayouts - charged_.mips;
    }

    void charge(const Charges& c) noexcept
    {
        charged_.native += c.native;
        charged_.content += c.content;
        charged_.result += c.result;
        charged_.label += c.label;
        charged_.callbacks += c.callbacks;
        charged_.mips += c.mips;
    }

    void retire(Pool::Ticket ticket) noexcept
    {
        const auto* entry = records_.get(ticket);
        if (!entry)
            return;
        const auto c = entry->charge;
        const auto issued = entry->issued;
        if (entry->destination && !entry->destination->creationCallbacksIssued())
            entry->destination->creationDrainConsumer_.reset();
        if (entry->recording && !entry->recording->readbackRecordingCallbacksIssued())
            entry->recording->readbackDrainConsumer_.reset();
        if (entry->submission && !entry->submission->callbacksIssued())
            entry->submission->readbackDrainConsumer_.reset();
        if (issued)
            records_.resolve(ticket, true);
        else
            records_.cancel(ticket);
        charged_.native -= c.native;
        charged_.content -= c.content;
        charged_.result -= c.result;
        charged_.label -= c.label;
        charged_.callbacks -= c.callbacks;
        charged_.mips -= c.mips;
    }

    bool cancel(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry)
            return false;
        if (entry->issued || executionOutstanding(*entry)
            || (entry->preparationIssued && entry->destination
                && !entry->destination->creationScopesObserved()))
            entry->canceled = true;
        else
            retire(ticket);
        return true;
    }

    const char* start(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry)
            return "RHI-READBACK-PUBLICATION-RESERVATION";
        if (entry->issued || entry->canceled
            || entry->preparationFailure != PreparationFailure::None)
            return "RHI-READBACK-PUBLICATION-START-STATUS";
        if (!entry->mapping)
            return "RHI-READBACK-PUBLICATION-NOT-ATTACHED";
        if (entry->recording
            && (!entry->submission || executionOutstanding(*entry)
                || entry->submission->status() != Submission::Status::Completed))
            return "RHI-READBACK-PUBLICATION-EXECUTION-SUBMISSION-UNPROVEN";
        const char* diagnostic = nullptr;
        const auto accepted = records_.submitWith(ticket, [&](Record& original) noexcept {
            diagnostic = Mapping::startAdmitted(original.mapping, original.source);
            original.issued = original.mapping->status_ != Mapping::Status::Prepared;
            return original.issued;
        });
        return accepted  ? diagnostic
            : diagnostic ? diagnostic
                         : "RHI-READBACK-PUBLICATION-START-STATUS";
    }

    const char* startOwned(Pool::Ticket ticket) noexcept
    {
        auto* entry = records_.get(ticket);
        if (!entry || !plan_ || !entry->recording || !entry->submission || !entry->mapping)
            return "RHI-READBACK-PUBLICATION-OWNED-START-GRAPH";
        const auto* diagnostic = start(ticket);
        // Owned hot contract failure is terminal only after original completion
        // and before actual map issuance. The selected legacy validator start
        // retains its declared unissued retry semantics.
        if (diagnostic && !entry->issued && !entry->mapping->callbacksIssued()
            && entry->submission->callbacksDrained()
            && entry->submission->status() == Submission::Status::Completed)
            entry->preparationFailure = PreparationFailure::MappingPreparation;
        return diagnostic;
    }

    const WebGpuOriginalTextureReadbackPublicationBudget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> recordQuota_;
    Pool records_;
    Charges charged_;
    bool shutdownRequested_ = false;
    std::shared_ptr<WebGpuRhiResourceRegistry> registry_;
    std::shared_ptr<const Plan> plan_;
};
} // namespace arp::rhi_webgpu

// endregion Webgpu original texture readback publication
