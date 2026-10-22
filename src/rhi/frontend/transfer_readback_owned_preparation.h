#pragma once
#include "core/text/utf8.h"
#include "rhi/frontend/transfer_readback_cold_budget.h"
#include "rhi/frontend/transfer_readback_content_intent.h"
#include "rhi/semantics/transfer_dependency_lowering.h"
#include "rhi/semantics/transfer_lowering.h"

namespace arp::rhi {
inline constexpr std::string_view kExtensionTextureReadbackOwnedPreparation =
    "arp.rhi.TextureReadbackOwnedPreparation/1";

// Inputs are borrowed for the cold call only; accepted preparation must retain
// the exact request/statistics owners and copy passLabel before returning.
struct TextureReadbackOwnedPreparationIntent {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    TextureReadbackContentIntent::Descriptor content;
    ResourceBarrierDesc sourceBarrier;
    std::string_view passLabel;
};

class TextureReadbackOwnedPreparationContract final {
public:
    enum class Disposition {
        DiagnosticFailure,
        ValidatorOnly,
        PrototypeRequiresDeviceContext,
        PrototypeContextReady
    };

    struct Decision {
        std::uint32_t contractVersion = 1u;
        Disposition disposition = Disposition::DiagnosticFailure;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-READBACK-OWNED-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::TransferLowering::TextureReadbackOwnedPreparation_lowering<Decision,
            Disposition>(backend);
    }

    static const char* validateOriginal(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& plan,
        const core::UniquePtr<GpuTexture>& source,
        const std::shared_ptr<const TextureReadbackRequestDeclaration>& request,
        const std::shared_ptr<const TextureReadbackStatisticsPlan>& statistics,
        const TextureReadbackOwnedPreparationIntent& intent) noexcept
    {
        if (intent.contractVersion != 1u)
            return "RHI-READBACK-OWNED-VERSION";
        if (!intent.prototypeOptIn)
            return "RHI-READBACK-OWNED-OPT-IN";
        if (const auto* id = TextureReadbackContentIntent::validate(intent.content))
            return id;
        if (const auto* id =
                TextureReadbackColdBudgetPlan::validateRequestOriginal(plan, request, statistics))
            return id;
        if (!source)
            return "RHI-READBACK-OWNED-SOURCE-MISSING";
        if (const auto* id = TextureReadbackSourceLease::validateSourceOriginal(request, *source))
            return id;
        const auto& quota = plan->quotas()[static_cast<std::size_t>(
            TextureReadbackColdBudgetPlan::CpuDomain::Request)];
        const auto& layout = request->originalLayout();
        const auto& input = layout->inputOwner();
        for (const auto& original : {request->originalMetadataQuota(),
                 statistics->originalMetadataQuota(),
                 layout->originalMetadataQuota(),
                 input->originalMetadataQuota(),
                 input->labelOwner()->originalMetadataQuota()})
            if (!core::SharedOwnerProjection::sameOwner(original, quota))
                return "RHI-READBACK-OWNED-REQUEST-QUOTA";
        if (intent.passLabel.size() > plan->budget().graph.maxPassLabelBytes)
            return "RHI-READBACK-OWNED-PASS-LABEL-CAPACITY";
        if (intent.passLabel.find('\0') != std::string_view::npos
            || core::Utf8Decoder::validate(intent.passLabel)
            || request->label().find('\0') != std::string_view::npos
            || core::Utf8Decoder::validate(request->label()))
            return "RHI-READBACK-OWNED-LABEL-ENCODING";
        if (intent.sourceBarrier.resource != request->source()
            || intent.sourceBarrier.after != ResourceState::CopySrc)
            return "RHI-READBACK-OWNED-SOURCE-BARRIER";
        return semantics::TransferDependencyLowering::validateTransitionIntent(intent.sourceBarrier,
            intent.sourceBarrier.before,
            true,
            defaultTextureSubresourceRange(source->desc()));
    }

    // Selected Prototype extension gate; this never promotes base capabilities.
    struct ContextFacts {
        bool configured = false, nativePayload = false, originalLossArmed = false,
             accepting = false, actualLoss = false;
    };

    static constexpr const char* validateExecutionContext(const ContextFacts& facts) noexcept
    {
        if (!facts.configured)
            return "RHI-READBACK-OWNED-CONTEXT-UNCONFIGURED";
        if (facts.actualLoss)
            return "RHI-READBACK-OWNED-CONTEXT-DEVICE-LOST";
        if (!facts.accepting)
            return "RHI-READBACK-OWNED-CONTEXT-CLOSED";
        if (!facts.nativePayload || !facts.originalLossArmed)
            return "RHI-READBACK-OWNED-CONTEXT-NATIVE-UNPROVEN";
        return nullptr;
    }

    // Static lowering above describes an unconfigured extension. Resolve only
    // from original backend context observations after explicit plan opt-in.
    // This decision admits the selected Prototype; it is no completion proof
    // and grants no generic/tracked/bindless/native synchronization capability.
    static constexpr Decision resolveLowering(Backend backend, const ContextFacts& facts) noexcept
    {
        return semantics::TransferLowering::resolveOwnedReadback<Decision>(
            backend, facts, [](const ContextFacts& value) noexcept {
                return validateExecutionContext(value);
            });
    }
    // Actions describe intent only. Native callback owners alone may expose the
    // next phase; this table has no transition/proof mutation or success signal.
    enum class Phase {
        CreationPrepared,
        CreationPending,
        CreationReady,
        RecordingPrepared,
        RecordingPending,
        RecordingReady,
        SubmissionPrepared,
        SubmissionPending,
        SubmissionCompleted,
        MappingPending,
        Published,
        Rejected,
        DeviceLost,
        CanceledDraining,
        Retired
    };
    enum class Action {
        InitializeCreation,
        PrepareRecordingCold,
        Record,
        PrepareSubmissionCold,
        Submit,
        StartMapping
    };

    static const char* validateAction(Phase phase, Action action) noexcept
    {
        switch (action) {
        case Action::InitializeCreation:
            if (phase == Phase::CreationPrepared)
                return nullptr;
            break;
        case Action::PrepareRecordingCold:
            if (phase == Phase::CreationReady)
                return nullptr;
            break;
        case Action::Record:
            if (phase == Phase::RecordingPrepared)
                return nullptr;
            break;
        case Action::PrepareSubmissionCold:
            if (phase == Phase::RecordingReady)
                return nullptr;
            break;
        case Action::Submit:
            if (phase == Phase::SubmissionPrepared)
                return nullptr;
            break;
        case Action::StartMapping:
            if (phase == Phase::SubmissionCompleted)
                return nullptr;
            break;
        }
        return "RHI-READBACK-OWNED-ACTION-PHASE";
    }
    enum class ShutdownDisposition {
        DetachIdleOrCompleted,
        DrainIssuedCallbacks,
        AwaitNativeLoss,
        DrainUnretiredRequests
    };

    struct ShutdownFacts {
        std::size_t outstandingCallbacks = 0u, quarantinedSubmissions = 0u, unretiredRequests = 0u;
        bool actualDeviceLossObserved = false;
    };

    static constexpr ShutdownDisposition shutdownDisposition(const ShutdownFacts& facts) noexcept
    {
        if (facts.outstandingCallbacks)
            return ShutdownDisposition::DrainIssuedCallbacks;
        if (facts.quarantinedSubmissions && !facts.actualDeviceLossObserved)
            return ShutdownDisposition::AwaitNativeLoss;
        if (facts.unretiredRequests)
            return ShutdownDisposition::DrainUnretiredRequests;
        return ShutdownDisposition::DetachIdleOrCompleted;
    }
};

// Immutable CPU output only. The concrete backend owns bytes/analyses under
// Result/Request quotas; no backend handle, object or state crosses this API.
class TextureReadbackOwnedSnapshot : public core::QuotaBoundSharedObjectIdentity {
public:
    virtual ~TextureReadbackOwnedSnapshot() = default;
    virtual const std::shared_ptr<const TextureReadbackRequestDeclaration>&
    originalRequest() const noexcept = 0;
    virtual const std::shared_ptr<const TextureReadbackStatisticsPlan>&
    originalStatisticsPlan() const noexcept = 0;
    virtual TextureReadbackContentIntent::Mode contentMode() const noexcept = 0;
    virtual const std::uint8_t* publishedBytes() const noexcept = 0;
    virtual std::size_t byteCapacity() const noexcept = 0;
    virtual const TextureReadbackViewAnalysis* publishedAnalyses() const noexcept = 0;
    virtual std::size_t analysisCount() const noexcept = 0;
};

// OwnedCpuSnapshot/1. Result quota explicitly pays for two simultaneous CPU
// byte copies: original native-map destination and detached immutable output.
// Request quota pays for both original numerical and detached analysis storage.
class TextureReadbackOwnedSnapshotContract final {
public:
    static constexpr auto lowering(Backend backend) noexcept
    {
        return TextureReadbackOwnedPreparationContract::lowering(backend);
    }

    static const char* validateColdOriginal(
        const std::shared_ptr<const TextureReadbackColdBudgetPlan>& plan,
        const std::shared_ptr<const TextureReadbackRequestDeclaration>& request,
        const std::shared_ptr<const TextureReadbackStatisticsPlan>& statistics,
        TextureReadbackContentIntent::Mode mode) noexcept
    {
        if (const auto* id =
                TextureReadbackColdBudgetPlan::validateRequestOriginal(plan, request, statistics))
            return id;
        if (const auto* id = TextureReadbackContentIntent::validate({1u, true, mode}))
            return id;
        const auto& quota = plan->quotas()[static_cast<std::size_t>(
            TextureReadbackColdBudgetPlan::CpuDomain::Request)];
        if (!core::SharedOwnerProjection::sameOwner(request->originalMetadataQuota(), quota)
            || !core::SharedOwnerProjection::sameOwner(statistics->originalMetadataQuota(), quota))
            return "RHI-READBACK-OWNED-SNAPSHOT-REQUEST-QUOTA";
        const auto bytes = statistics->originalByteCapacity();
        if (bytes > plan->budget().maxCpuResultBytes
            || bytes > plan->budget().maxCpuResultBytes - bytes)
            return "RHI-READBACK-OWNED-SNAPSHOT-RESULT-CAPACITY";
        return nullptr;
    }

    // This validates CPU output shape/identity only, never native completion.
    static const char* validatePublishedOriginal(
        const std::shared_ptr<const TextureReadbackOwnedSnapshot>& snapshot) noexcept
    {
        if (!snapshot || !snapshot->hasQuotaBoundSharedOwner(snapshot))
            return "RHI-READBACK-OWNED-SNAPSHOT-OWNER";
        const auto& request = snapshot->originalRequest();
        if (!request || !request->hasQuotaBoundSharedOwner(request))
            return "RHI-READBACK-OWNED-SNAPSHOT-REQUEST";
        if (const auto* id = TextureReadbackStatisticsPlan::validateOriginal(
                snapshot->originalStatisticsPlan(), request->originalLayout()))
            return id;
        if (!snapshot->publishedBytes() || !snapshot->publishedAnalyses())
            return "RHI-READBACK-OWNED-SNAPSHOT-UNPUBLISHED";
        if (snapshot->byteCapacity() != snapshot->originalStatisticsPlan()->originalByteCapacity()
            || snapshot->analysisCount() != snapshot->originalStatisticsPlan()->views().size())
            return "RHI-READBACK-OWNED-SNAPSHOT-SHAPE";
        return TextureReadbackContentIntent::validate({1u, true, snapshot->contentMode()});
    }
};

struct TextureReadbackOwnedDelivery {
    enum class Outcome { Empty, Published, Rejected, DeviceLost };
    Outcome outcome = Outcome::Empty;
    const char* diagnostic = nullptr;
    std::shared_ptr<const TextureReadbackRequestDeclaration> originalRequest;
    std::shared_ptr<const TextureReadbackOwnedSnapshot> snapshot;
};

class TextureReadbackOwnedPreparation : public core::QuotaBoundSharedObjectIdentity {
public:
    using Phase = TextureReadbackOwnedPreparationContract::Phase;
    virtual ~TextureReadbackOwnedPreparation() = default;
    virtual const std::shared_ptr<const TextureReadbackRequestDeclaration>&
    originalRequest() const noexcept = 0;
    virtual Phase phase() const noexcept = 0;
    virtual const char* diagnostic() const noexcept = 0;
    virtual const char* prepareNextCold() = 0;
    virtual const char* initializeCreation() noexcept = 0;
    virtual const char* record() noexcept = 0;
    virtual const char* submit() noexcept = 0;
    virtual const char* startMapping() noexcept = 0;
    virtual bool cancel() noexcept = 0;
};

class TextureReadbackOwnedPreparationExtension {
public:
    virtual ~TextureReadbackOwnedPreparationExtension() = default;

    // Fail-closed until a real device context consumes the approved lowering.
    virtual const char* configureOriginalPlan(std::shared_ptr<const TextureReadbackColdBudgetPlan>)
    {
        return "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED";
    }

    virtual TextureReadbackOwnedPreparationContract::Decision executionDecision() const noexcept
    {
        return {1u,
            TextureReadbackOwnedPreparationContract::Disposition::DiagnosticFailure,
            false,
            "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED"};
    }

    virtual bool executionAvailable() const noexcept
    {
        return executionDecision().executionAvailable;
    }

    virtual std::shared_ptr<TextureReadbackOwnedPreparation> prepareOwnedOriginal(
        core::UniquePtr<GpuTexture>&,
        std::shared_ptr<const TextureReadbackRequestDeclaration>,
        std::shared_ptr<const TextureReadbackStatisticsPlan>,
        const TextureReadbackOwnedPreparationIntent&,
        const char*& diagnostic)
    {
        diagnostic = "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED";
        return {};
    }

    virtual bool pollOriginal(TextureReadbackOwnedDelivery&, const char*& diagnostic) noexcept
    {
        diagnostic = "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED";
        return false;
    }

    // Stop admission; cancel publication and retain issued/quarantined owners.
    // Native detachment requires the original runtime's actual shutdown facts.
    virtual const char* shutdownOriginal() noexcept
    {
        return "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED";
    }
};
} // namespace arp::rhi
