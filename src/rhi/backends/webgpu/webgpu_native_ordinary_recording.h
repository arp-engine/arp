#pragma once
#include "core/runtime/completion_drain_consumer.h"
#include "core/runtime/completion_evidence_batch.h"
#include "rhi/backends/webgpu/webgpu_canonical_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_ordinary_graphics_attachments.h"
#include "rhi/backends/webgpu/webgpu_original_buffer_upload_stream_validation.h"
#include "rhi/backends/webgpu/webgpu_original_graphics_stream_validation.h"
#include "rhi/backends/webgpu/webgpu_original_texture_readback_copies.h"
#include "rhi/backends/webgpu/webgpu_original_texture_upload_copy_validation.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_dependency_validation.h"
#include "rhi/backends/webgpu/webgpu_original_transfer_stream_validation.h"
#include "rhi/backends/webgpu/webgpu_recorded_command_proof.h"

namespace arp::rhi_webgpu {
class WebGpuOriginalTypedNativePayload;
class WebGpuBindlessRootNativeProgram;
class WebGpuBindlessRootNativeClosure;

struct WebGpuOrdinaryComputeRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxEncoders = 0u, maxComputePasses = 0u, maxCommandBuffers = 0u;
    std::uint32_t maxCallbacks = 0u, maxGroups = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

struct WebGpuOrdinaryGraphicsRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommands = 0u, maxEncoders = 0u, maxRenderPasses = 0u, maxCommandBuffers = 0u;
    std::uint32_t maxCallbacks = 0u, maxGroups = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

struct WebGpuOriginalComputeRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalCommands = 0u;
    WebGpuOrdinaryComputeRecordingBudget native;
};

struct WebGpuOriginalGraphicsRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalCommands = 0u;
    WebGpuOrdinaryGraphicsRecordingBudget native;
};

struct WebGpuOriginalTransferRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxOriginalCommands = 0u, maxCommands = 0u, maxEncoders = 0u, maxDebugScopes = 0u,
                  maxCommandBuffers = 0u, maxCallbacks = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

struct WebGpuOriginalTransferDependencyRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    WebGpuOriginalTransferRecordingBudget native;
    WebGpuOriginalStateTransactionBudget transaction;
};

struct WebGpuOriginalBufferUploadRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    WebGpuOriginalTransferRecordingBudget native;
    WebGpuOriginalStateTransactionBudget transaction;
};

struct WebGpuOriginalTextureUploadRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    WebGpuOriginalTransferRecordingBudget native;
    WebGpuOriginalTextureUploadTransactionBudget transaction;
    std::uint32_t maxMipCopies = 0u;
};

struct WebGpuOriginalTextureReadbackRecordingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    WebGpuOriginalTransferRecordingBudget native;
    WebGpuOriginalReadbackStateTransactionBudget transaction;
    WebGpuOriginalTextureReadbackCopiesBudget copies;
    std::uint32_t maxMipCopies = 0u;
};

class WebGpuOrdinaryRecording final : public WebGpuRecordedCommandProof {
    friend class WebGpuBindlessRootIssuerPreparation;
    static const char* recordRoot(std::shared_ptr<WebGpuOrdinaryRecording>) noexcept;

    struct RootEmission {
        WebGpuOrdinaryRecording* self;
        const WebGpuBindlessRootNativeProgram* program;
        const WebGpuBindlessRootNativeClosure* closure;
    };

    static void emitRoot(void*) noexcept;
    const char* validateRootLive() const noexcept;
    using TypedState = WebGpuCanonicalCallbackCustody::RuntimeState<WebGpuOriginalTypedColdCommand>;
    using RootState = WebGpuCanonicalCallbackCustody::RuntimeState<WebGpuBindlessRootColdCommand>;

    class ConstructionKey {
        friend class WebGpuOrdinaryRecording;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Callback {
        std::shared_ptr<WebGpuOrdinaryRecording> owner;
    };

public:
    static constexpr std::uint32_t scopeCount = WebGpuOrdinaryShaderSizeValidationOwner::scopeCount;
    static constexpr std::uint32_t callbackCount = 2u * scopeCount;
    enum class Status {
        PreparationPending,
        Prepared,
        RecordingPending,
        Recorded,
        Rejected,
        DeviceLost,
        NativeInvocationUncertain
    };

    WebGpuOrdinaryRecording(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryScalarComputeRange> range,
        std::shared_ptr<WebGpuNativeCommandPayload> command)
        : WebGpuRecordedCommandProof(std::move(command)), range_(std::move(range))
    {}

    WebGpuOrdinaryRecording(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryGraphicsAttachments> attachments,
        std::shared_ptr<WebGpuNativeCommandPayload> command)
        : WebGpuRecordedCommandProof(std::move(command)), attachments_(std::move(attachments))
    {}

    WebGpuOrdinaryRecording(ConstructionKey,
        std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> range,
        std::shared_ptr<WebGpuNativeCommandPayload> command)
        : WebGpuRecordedCommandProof(std::move(command)), originalGraphicsRange_(std::move(range))
    {}

    WebGpuOrdinaryRecording(ConstructionKey,
        std::shared_ptr<const WebGpuOriginalResourceStates> states,
        std::shared_ptr<WebGpuNativeCommandPayload> command)
        : WebGpuRecordedCommandProof(std::move(command)), originalStates_(std::move(states))
    {}

    WebGpuOrdinaryRecording(ConstructionKey,
        std::shared_ptr<WebGpuNativeCommandPayload> command,
        std::weak_ptr<WebGpuCanonicalCallbackCustody::State> state)
        : WebGpuRecordedCommandProof(std::move(command)), canonicalState_(std::move(state)),
          canonicalMode_(true), status_(Status::Prepared)
    {}

    WebGpuOrdinaryRecording(ConstructionKey,
        std::shared_ptr<WebGpuNativeCommandPayload> command,
        std::weak_ptr<TypedState> state)
        : WebGpuRecordedCommandProof(std::move(command)), canonicalState_(std::move(state)),
          canonicalMode_(true), typedCanonicalMode_(true), status_(Status::Prepared)
    {}

    ~WebGpuOrdinaryRecording() override;

    WebGpuOrdinaryRecording(ConstructionKey,
        std::shared_ptr<WebGpuNativeCommandPayload> command,
        std::weak_ptr<RootState> state)
        : WebGpuRecordedCommandProof(std::move(command)), canonicalState_(std::move(state)),
          canonicalMode_(true), rootCanonicalMode_(true), status_(Status::Prepared)
    {}

    static const char* validateBudget(const WebGpuOrdinaryComputeRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-RECORDING-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-RECORDING-OPT-IN";
        if (!budget.maxCommands || !budget.maxEncoders || !budget.maxComputePasses
            || !budget.maxCommandBuffers || budget.maxCallbacks < callbackCount || !budget.maxGroups
            || !budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORDINARY-RECORDING-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepare(
        std::shared_ptr<const WebGpuOrdinaryScalarComputeRange>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOrdinaryComputeRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* record(const std::shared_ptr<WebGpuOrdinaryRecording>&,
        std::uint32_t x,
        std::uint32_t y,
        std::uint32_t z) noexcept;

    static const char* validateBudget(const WebGpuOriginalComputeRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-COMPUTE-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-COMPUTE-OPT-IN";
        if (budget.maxOriginalCommands < 6u)
            return "RHI-WEBGPU-ORIGINAL-COMPUTE-COMMAND-CAPACITY";
        return validateBudget(budget.native);
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepareOriginalCompute(
        std::shared_ptr<const WebGpuOriginalResourceStates>,
        std::shared_ptr<const WebGpuOrdinaryScalarComputeRange>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOriginalComputeRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* recordOriginalCompute(
        const std::shared_ptr<WebGpuOrdinaryRecording>&) noexcept;

    static const char* validateBudget(const WebGpuOrdinaryGraphicsRecordingBudget& budget) noexcept
    {
        return validateBudget(WebGpuOrdinaryComputeRecordingBudget{budget.contractVersion,
            budget.prototypeOptIn,
            budget.maxCommands,
            budget.maxEncoders,
            budget.maxRenderPasses,
            budget.maxCommandBuffers,
            budget.maxCallbacks,
            budget.maxGroups,
            budget.maxCpuMetadataBytes});
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepare(
        std::shared_ptr<const WebGpuOrdinaryGraphicsAttachments>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOrdinaryGraphicsRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* record(const std::shared_ptr<WebGpuOrdinaryRecording>&,
        std::uint32_t vertices,
        std::uint32_t instances,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance) noexcept;

    static const char* validateBudget(const WebGpuOriginalGraphicsRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-OPT-IN";
        if (budget.maxOriginalCommands < 8u)
            return "RHI-WEBGPU-ORIGINAL-GRAPHICS-COMMAND-CAPACITY";
        return validateBudget(budget.native);
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepareOriginalGraphics(
        std::shared_ptr<const WebGpuOriginalResourceStates>,
        std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOriginalGraphicsRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* recordOriginalGraphics(
        const std::shared_ptr<WebGpuOrdinaryRecording>&) noexcept;

    static const char* validateBudget(const WebGpuOriginalTransferRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-TRANSFER-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-TRANSFER-OPT-IN";
        if (budget.maxOriginalCommands < 4u)
            return "RHI-WEBGPU-ORIGINAL-TRANSFER-COMMAND-CAPACITY";
        if (!budget.maxCommands || !budget.maxEncoders || !budget.maxDebugScopes
            || !budget.maxCommandBuffers || budget.maxCallbacks < callbackCount
            || !budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORIGINAL-TRANSFER-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepareOriginalTransfer(
        std::shared_ptr<const WebGpuOriginalResourceStates>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOriginalTransferRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* recordOriginalTransfer(
        const std::shared_ptr<WebGpuOrdinaryRecording>&) noexcept;

    static const char* validateBudget(
        const WebGpuOriginalTransferDependencyRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-OPT-IN";
        if (budget.native.maxOriginalCommands < 6u)
            return "RHI-WEBGPU-ORIGINAL-DEPENDENCY-COMMAND-CAPACITY";
        if (const auto* id = validateBudget(budget.native))
            return id;
        return WebGpuOriginalStateTransaction::validateBudget(budget.transaction);
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepareOriginalTransferDependency(
        std::shared_ptr<const WebGpuOriginalResourceStates>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOriginalTransferDependencyRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* recordOriginalTransferDependency(
        const std::shared_ptr<WebGpuOrdinaryRecording>&) noexcept;

    static const char* validateBudget(
        const WebGpuOriginalBufferUploadRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-OPT-IN";
        if (const auto* id = validateBudget(budget.native))
            return id;
        if (budget.native.maxDebugScopes < 2u)
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-DEBUG-CAPACITY";
        if (const auto* id = WebGpuOriginalStateTransaction::validateBudget(budget.transaction))
            return id;
        if (budget.transaction.maxStateControls < 2u || budget.transaction.maxTransitions
            || budget.transaction.maxDebugDepth)
            return "RHI-WEBGPU-ORIGINAL-UPLOAD-TRANSACTION-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepareOriginalBufferUpload(
        std::shared_ptr<const WebGpuOriginalResourceStates>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOriginalBufferUploadRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* recordOriginalBufferUpload(
        const std::shared_ptr<WebGpuOrdinaryRecording>&) noexcept;

    static const char* validateBudget(
        const WebGpuOriginalTextureUploadRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-OPT-IN";
        if (const auto* id = validateBudget(budget.native))
            return id;
        if (budget.native.maxDebugScopes < 2u || !budget.maxMipCopies)
            return "RHI-WEBGPU-ORIGINAL-TEXTURE-UPLOAD-BUDGET";
        return WebGpuOriginalTextureUploadTransaction::validateBudget(budget.transaction);
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepareOriginalTextureUpload(
        std::shared_ptr<const WebGpuOriginalResourceStates>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOriginalTextureUploadRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* recordOriginalTextureUpload(
        const std::shared_ptr<WebGpuOrdinaryRecording>&) noexcept;

    const auto& originalTextureTransaction() const noexcept { return originalTextureTransaction_; }

    static const char* validateBudget(
        const WebGpuOriginalTextureReadbackRecordingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORIGINAL-READBACK-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORIGINAL-READBACK-OPT-IN";
        if (const auto* id = validateBudget(budget.native))
            return id;
        if (budget.native.maxOriginalCommands < 5u || budget.native.maxDebugScopes < 2u
            || !budget.maxMipCopies || !budget.copies.maxNativeCompletionOwners)
            return "RHI-WEBGPU-ORIGINAL-READBACK-BUDGET";
        if (const auto* id = WebGpuOriginalStateTransaction::validateBudget(budget.transaction))
            return id;
        return WebGpuOriginalTextureReadbackCopies::validateBudget(budget.copies);
    }

    static std::shared_ptr<WebGpuOrdinaryRecording> prepareOriginalTextureReadback(
        std::shared_ptr<const WebGpuOriginalResourceStates>,
        std::shared_ptr<WebGpuOrdinaryCompletionOwner>,
        const WebGpuOriginalTextureReadbackRecordingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&,
        std::shared_ptr<WebGpuNativeCompletionState> signal = {});
    static const char* recordOriginalTextureReadback(
        const std::shared_ptr<WebGpuOrdinaryRecording>&) noexcept;

    std::shared_ptr<WebGpuOrdinaryCompletionOwner> originalCompletionOwner() const noexcept
    {
        return completion_ ? completion_->owner()
                           : std::shared_ptr<WebGpuOrdinaryCompletionOwner>{};
    }

    bool readbackRecordingCallbacksIssued() const noexcept { return readbackRecordingIssued_; }

    std::size_t readbackRecordingCallbacksRemaining() const noexcept { return remaining_; }

    bool readbackRecordingCallbacksDrained() const noexcept
    {
        return readbackRecordingIssued_ && !remaining_;
    }

    const auto& originalReadbackCopies() const noexcept { return originalReadbackCopies_; }

    const auto& originalTransaction() const noexcept { return originalTransaction_; }

    Status status() const noexcept;
    const char* diagnostic() const noexcept override;

    bool hasRecordedCommand() const noexcept override { return status() == Status::Recorded; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = diagnostic())
            return id;
        return "RHI-WEBGPU-ORDINARY-COMPLETION-UNPREPARED";
    }

private:
    static const char* recordCanonical(
        std::shared_ptr<WebGpuOrdinaryRecording>, const WebGpuCanonicalIssuerPreparation&) noexcept;
    const char* validateCanonicalLive() const noexcept;
    void finishCanonicalRecording() noexcept;
    void rollbackCanonicalRecording() noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void completeCanonical(
        WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*) noexcept;
#endif
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuCanonicalIssuerPreparation;
    static std::shared_ptr<WebGpuOrdinaryRecording> allocateCanonical(
        std::shared_ptr<WebGpuNativeCommandPayload>,
        std::weak_ptr<WebGpuCanonicalCallbackCustody::State>);
    std::shared_ptr<WebGpuCanonicalCallbackCustody::State> legacyCanonicalState() const noexcept;
    std::shared_ptr<TypedState> typedCanonicalState() const noexcept;
    std::shared_ptr<RootState> rootCanonicalState() const noexcept;
    static std::shared_ptr<WebGpuOrdinaryRecording> allocateRoot(
        std::shared_ptr<WebGpuNativeCommandPayload>, std::weak_ptr<RootState>);
    static void completeRoot(void*, WebGpuNativeCompletionReceipt::Outcome) noexcept;
    template <class State>
    static void completePrepared(void*,
        WebGpuNativeCompletionReceipt::Outcome,
        WebGpuNativeCompletionReceipt::Callback) noexcept;
    const char* rootColdDiagnostic() const noexcept;
    static std::shared_ptr<WebGpuOrdinaryRecording> allocateTyped(
        std::shared_ptr<WebGpuNativeCommandPayload>, std::weak_ptr<TypedState>);
    static void completeTyped(void*, WebGpuNativeCompletionReceipt::Outcome) noexcept;
    friend class WebGpuOriginalTypedIssuerPreparation;
    static const char* recordTyped(std::shared_ptr<WebGpuOrdinaryRecording>) noexcept;

    struct TypedEmission {
        WebGpuOrdinaryRecording* self;
        const WebGpuOriginalTypedNativePayload* payload;
    };

    static void emitTyped(void*) noexcept;
    const char* validateTypedLive() const noexcept;
    const char* typedColdDiagnostic() const noexcept;
    Status canonicalStatus() const noexcept;
    const char* validateCanonicalCold() const noexcept;
    const char* canonicalDiagnostic() const noexcept;
    friend class WebGpuOriginalTextureReadbackPublication;
    friend class WebGpuOrdinarySubmission;
    static std::shared_ptr<WebGpuOrdinaryRecording> allocate(
        std::shared_ptr<const WebGpuOrdinaryScalarComputeRange>,
        std::shared_ptr<const WebGpuOrdinaryGraphicsAttachments>,
        const std::shared_ptr<WebGpuOrdinaryCompletionOwner>&,
        std::shared_ptr<WebGpuNativeCompletionState>,
        const char*&,
        std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> originalGraphics = {},
        std::shared_ptr<const WebGpuOriginalResourceStates> originalTransfer = {},
        std::shared_ptr<WebGpuOriginalStateTransaction> originalTransaction = {},
        std::shared_ptr<WebGpuOriginalTextureUploadTransaction> originalTextureTransaction = {},
        std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> originalReadbackCopies = {});

    bool originalTransfer() const noexcept
    {
        return originalStates_ && !range_ && !attachments_ && !originalGraphicsRange_;
    }

    static const char* validateOriginalTransferInputs(
        const std::shared_ptr<const WebGpuOriginalResourceStates>&, void*) noexcept;
    const char* validateOriginalDependencyInputs() const noexcept;
    const char* validateOriginalUploadInputs() const noexcept;
    static const char* validateOriginalUploadInputs(
        const std::shared_ptr<const WebGpuOriginalResourceStates>&, void*) noexcept;
    const char* validateOriginalTextureUploadInputs() const noexcept;
    const char* validateOriginalTextureReadbackInputs() const noexcept;
    const char* recordDiagnostic() const noexcept;
    const WebGpuOrdinaryShaderSizeValidation& validation() const noexcept;
    const char* validateOriginal() const noexcept;
    static const char* validateOriginalComputeInputs(
        const std::shared_ptr<const WebGpuOriginalResourceStates>&,
        const std::shared_ptr<const WebGpuOrdinaryScalarComputeRange>&) noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void complete(
        WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*) noexcept;
    void finishBuffer() noexcept;
    void pushScopes() noexcept;
    void popScopes(const std::shared_ptr<WebGpuOrdinaryRecording>&, std::size_t first) noexcept;
#endif
    const std::shared_ptr<const WebGpuOrdinaryScalarComputeRange> range_;
    const std::shared_ptr<const WebGpuOrdinaryGraphicsAttachments> attachments_;
    const std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange> originalGraphicsRange_;
    std::shared_ptr<const WebGpuOriginalResourceStates> originalStates_;
    std::shared_ptr<WebGpuOriginalStateTransaction> originalTransaction_;
    std::shared_ptr<WebGpuOriginalTextureUploadTransaction> originalTextureTransaction_;
    std::shared_ptr<const WebGpuOriginalTextureReadbackCopies> originalReadbackCopies_;
    std::array<rhi::semantics::TransferDependencyLowering::Decision, 2>
        originalDependencyDecisions_;
    std::array<core::UniquePtr<Callback>, callbackCount> callbacks_;
    // Identity retention only. Private constructor tag selects the exact type;
    // no public untyped owner or execution interface exists.
    const std::weak_ptr<void> canonicalState_;
    const bool canonicalMode_ = false;
    const bool typedCanonicalMode_ = false;
    const bool rootCanonicalMode_ = false;
    core::runtime::CompletionEvidenceBatch canonicalEvidence_{scopeCount};
    bool canonicalIssuanceComplete_ = false;
    std::shared_ptr<core::runtime::CompletionDrainConsumer> readbackDrainConsumer_;
    bool readbackRecordingIssued_ = false;
    Status status_ = Status::PreparationPending;
    std::uint32_t remaining_ = scopeCount;
    bool callbackSuccess_ = true, noErrors_ = true;
    bool originalUpload_ = false;
    const char* failure_ = nullptr;
};

using WebGpuOrdinaryComputeRecording = WebGpuOrdinaryRecording;
using WebGpuOrdinaryGraphicsRecording = WebGpuOrdinaryRecording;
} // namespace arp::rhi_webgpu
