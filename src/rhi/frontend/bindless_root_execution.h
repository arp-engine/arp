#pragma once
#include "rhi/frontend/bindless_initialized_graphics.h"
#include "rhi/frontend/bindless_mixed_entry_flow.h"
#include "rhi/frontend/bindless_produced_geometry.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/bindless_root_sequence.h"
#include "rhi/frontend/command_geometry_content.h"

namespace arp::rhi {
// Public cold join over the existing Root, inventory, stream and journal.
// All capacities remain caller-authored typed contracts, never backend defaults.
struct OriginalBindlessRootExecutionInputs {
    std::shared_ptr<const OriginalCommandBindlessRootSource> root;
    std::shared_ptr<const OriginalCommandResourceSources> inventory;
    std::shared_ptr<OriginalCommandStateTransaction> transaction;
    std::shared_ptr<const OriginalCommandBufferReadbackBinding> readback;
    std::shared_ptr<core::CpuMemoryQuota> nativeQuota, submissionQuota, callbackQuota;
    // Execution10 alone composes two authenticated entries over this same
    // snapshot, authorization, inventory, graph and callback completion.
    std::shared_ptr<const OriginalCommandBindlessRootSource> nextRoot;
    std::shared_ptr<const OriginalCommandBindlessRootSource> finalRoot;
    const std::shared_ptr<const OriginalInitializedGeometrySource>* geometry = nullptr;
    std::size_t geometryCount = 0u;
    const std::shared_ptr<const OriginalInitializedGeometrySource>* initializedShader = nullptr;
    std::size_t initializedShaderCount = 0u;
    OriginalBindlessRootReadbackSequence::Input readbacks;
    OriginalBindlessRootSequenceContract::Input roots;
    OriginalBindlessProducedGeometryInputs producedGeometry;
    OriginalBindlessInitializedGraphicsInputs initializedGraphics;
};

struct OriginalBindlessRootExecutionBudget {
    BindlessRequirements::OriginalExecution intent;
    OriginalBindlessRootNativeClosureBudget closure;
    OriginalBindlessRootCallbackColdBudget admission;
    OriginalCommandNativeLifecycleBudget lifecycle;
    OriginalCommandReadbackResultStorageContract::Budget result;
    OriginalBindlessRootColdCommandRequest command;
    OriginalBindlessRootCallbackCustodyRequest custody;
    OriginalBindlessRootIssuerRequest issuers;
    OriginalBindlessMixedEntryFlowContract::Budget mixedEntries;
    // Historical scoped profiles can remain unreserved. A caller requesting
    // complete in-flight accounting must opt in before any transaction exists.
    bool requireWorkloadRetention = false;

    const char* validate() const noexcept
    {
        using Execution = OriginalBindlessRootNativeExecutionContract;
        if (const auto* id = OriginalBindlessRootNativeExecutionContract::validateIntent(intent))
            return id;
        if (intent.contractVersion >= 6u && intent.contractVersion != 10u
            && intent.contractVersion != 12u && intent.contractVersion != 13u
            && intent.contractVersion != 14u && intent.contractVersion != 16u)
            return "RHI-ORIGINAL-ROOT-EXECUTION-FENCE-BUDGET-REQUIRED";
        if (intent.contractVersion == 12u) {
            if (const auto* id =
                    OriginalBindlessMixedEntryFlowContract::validateIntent(mixedEntries))
                return id;
        } else if (mixedEntries.contractVersion || mixedEntries.prototypeOptIn
            || mixedEntries.maxDeclarationVisits)
            return "RHI-BINDLESS-ROOT-EXECUTION-MIXED-SCHEMA";
        if (const auto* id = OriginalBindlessRootNativeClosureContract::validateBudget(closure))
            return id;
        if (const auto* id = OriginalBindlessRootCallbackColdContract::validateIntent(admission))
            return id;
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateIntent(lifecycle))
            return id;
        if (lifecycle.contractVersion != Execution::lifecycleVersion(intent.contractVersion))
            return "RHI-ORIGINAL-ROOT-EXECUTION-LIFECYCLE";
        if (admission.contractVersion != Execution::admissionVersion(intent.contractVersion)
            || closure.contractVersion != Execution::closureVersion(intent.contractVersion))
            return "RHI-ORIGINAL-ROOT-EXECUTION-PROFILE";
        if (const auto* id =
                OriginalCommandReadbackResultStorageContract::validateBufferIntent(result))
            return id;
        if (const auto* id = OriginalBindlessRootColdCommandContract::validateIntent(command))
            return id;
        if (command.contractVersion != 2u)
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-REQUIRED";
        if (command.program.contractVersion != Execution::admissionVersion(intent.contractVersion))
            return "RHI-ORIGINAL-ROOT-EXECUTION-PROFILE";
        if (const auto* id = OriginalBindlessRootPushUploadContract::validateCorrespondence(
                admission.pushUpload, command.program.pushUpload))
            return id;
        const bool geometry =
            OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(closure);
        const bool produced = intent.contractVersion == 16u;
        if ((!produced
                && (geometry != bool(admission.initializedGeometry.contractVersion)
                    || geometry != bool(command.program.initializedGeometryContractVersion)))
            || (geometry && !produced
                && (intent.contractVersion < 2u || intent.contractVersion > 4u)))
            return "RHI-BINDLESS-ROOT-EXECUTION-GEOMETRY-CORRESPONDENCE";
        if (produced
            && (!requireWorkloadRetention || admission.initializedGeometry.contractVersion != 3u
                || command.program.initializedGeometryContractVersion != 2u))
            return "RHI-BINDLESS-PRODUCED-EXECUTION-RETENTION-OR-FETCH";
        if (const auto* id = OriginalBindlessRootCallbackCustodyContract::validateIntent(custody))
            return id;
        if (const auto* id = OriginalBindlessRootIssuerContract::validateIntent(issuers))
            return id;
        if (command.maxCpuMetadataBytes != lifecycle.maxRecordingCpuBytes
            || issuers.maxRecordingCpuBytes != lifecycle.maxRecordingCpuBytes
            || issuers.maxSubmissionCpuBytes != lifecycle.maxSubmissionCpuBytes
            || issuers.maxCallbackCpuBytes != lifecycle.maxCallbackCpuBytes
            || custody.maxCpuSubmissionBytes != lifecycle.maxSubmissionCpuBytes
            || custody.maxCpuCallbackBytes != lifecycle.maxCallbackCpuBytes)
            return "RHI-BINDLESS-ROOT-EXECUTION-QUOTA";
        return nullptr;
    }
};

// A backend-issued view of the existing issuer, not a new execution owner,
// resource graph, result store, submission pool or callback-delivery surface.
class ARP_API OriginalBindlessRootExecution : public core::QuotaBoundSharedObjectIdentity {
public:
    virtual ~OriginalBindlessRootExecution() = default;
    using Request = BindlessRequirements::OriginalExecution;

    static const char* record(const std::shared_ptr<const OriginalBindlessRootExecution>& self,
        const Request& request) noexcept
    {
        if (const auto* id = OriginalBindlessRootNativeExecutionContract::validateIntent(request))
            return id;
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-BINDLESS-ROOT-EXECUTION-OWNER";
        return self->recordExecution(request);
    }

    static const char* submit(const std::shared_ptr<const OriginalBindlessRootExecution>& self,
        const Request& request) noexcept
    {
        if (const auto* id = OriginalBindlessRootNativeExecutionContract::validateIntent(request))
            return id;
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-BINDLESS-ROOT-EXECUTION-OWNER";
        return self->submitExecution(request);
    }

    virtual const char* cancelExecution() const noexcept = 0;
    virtual bool recordingProven() const noexcept = 0;
    virtual bool submissionCompleted() const noexcept = 0;

    // Callback backends already advance from genuine events. Fence backends
    // override with a nonblocking query of the original submitted command.
    virtual bool pollCompletion() const noexcept { return submissionCompleted(); }

    virtual const char* executionDiagnostic() const noexcept = 0;
    virtual std::uint32_t pendingRecords() const noexcept = 0;

protected:
    virtual const char* recordExecution(const Request&) const noexcept = 0;
    virtual const char* submitExecution(const Request&) const noexcept = 0;
};
} // namespace arp::rhi
