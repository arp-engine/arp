#pragma once
#include "rhi/frontend/command_buffer_readback.h"

namespace arp::rhi {
// Entry roles are derived from authenticated per-entry shader accesses.
// A prepared binding or table-wide retained declaration alone is insufficient.
class OriginalBindlessStorageExecutionContract final {
public:
    static const char* validatePair(const OriginalCommandBindlessRootSource& first,
        const OriginalCommandBindlessRootSource& next) noexcept
    {
        if (&first == &next || !first.ordinaryDomains().empty() || !next.ordinaryDomains().empty()
            || !core::SharedOwnerProjection::sameOwner(
                first.originalSnapshot(), next.originalSnapshot())
            || !core::SharedOwnerProjection::sameOwner(first.authorization(), next.authorization()))
            return "RHI-BINDLESS-ROOT-STORAGE-CORRESPONDENCE";
        if (const auto* id = validateEntry(first, true))
            return id;
        return validateEntry(next, false);
    }

    static const char* validateEntry(
        const OriginalCommandBindlessRootSource& root, bool write) noexcept
    {
        if (root.originalShaderEntryAccessVersion() != 1u)
            return "RHI-BINDLESS-ROOT-STORAGE-ENTRY-ACCESS";
        bool texture = false;
        for (std::size_t i = 0u; i < root.size(); ++i) {
            BindlessResourceAccess access;
            std::size_t declaration = SIZE_MAX;
            bool active = false;
            if (const auto* id = root.readOriginalShaderDeclaration(
                    i, ShaderStage::Compute, access, declaration, active))
                return id;
            if (!active || access.kind != BindlessAccessKind::Texture)
                continue;
            texture = true;
            if (access.state != (write ? ResourceState::ShaderWrite : ResourceState::ShaderRead)
                || access.access
                    != (write ? ResourceAccess::ShaderWrite : ResourceAccess::ShaderRead))
                return "RHI-BINDLESS-ROOT-STORAGE-ENTRY-ROLE";
        }
        return texture ? nullptr : "RHI-BINDLESS-ROOT-STORAGE-ENTRY-TEXTURE";
    }
};

// Independent Upload/1 joins the existing Graphics/2 program. Empty intent
// preserves every old refusal; a version number alone never enables push.
class OriginalBindlessRootPushUploadContract final {
public:
    using Budget = OriginalCommandPushUploadBudget;

    static constexpr const char* validateIntent(
        std::uint32_t programVersion, std::uint64_t maxCommands, const Budget& upload) noexcept
    {
        if (upload.empty())
            return nullptr;
        if (const auto* id = upload.diagnostic())
            return id;
        if (programVersion != 2u && programVersion != 7u)
            return "RHI-ORIGINAL-ROOT-PUSH-UPLOAD-PROFILE";
        return upload.maxUpdates <= maxCommands ? nullptr
                                                : "RHI-ORIGINAL-ROOT-PUSH-UPLOAD-COMMAND-CAPACITY";
    }

    static constexpr const char* validateCorrespondence(
        const Budget& admission, const Budget& program) noexcept
    {
        return admission.matches(program) ? nullptr
                                          : "RHI-ORIGINAL-ROOT-PUSH-UPLOAD-CORRESPONDENCE";
    }

    struct Footprint {
        std::uint32_t nativePayloadObjects = 0u, retainedOwners = 0u;
    };

    // Actual additional counts, not the ceilings, are added once to the full
    // original command footprint. The caller includes its shell/encoder and
    // callback custody in that original footprint before this cold preflight.
    static constexpr const char* validateFootprint(const Budget& upload,
        Footprint original,
        Footprint added,
        const OriginalCommandAdmissionBudget& source,
        const OriginalCommandNativeLifecycleBudget& lifecycle,
        std::uint32_t maxRetainedOwners,
        Footprint& output) noexcept
    {
        output = {};
        if (const auto* id = upload.diagnostic())
            return id;
        if (!added.nativePayloadObjects
            || added.nativePayloadObjects > upload.maxNativePayloadObjects)
            return "RHI-ORIGINAL-PUSH-UPLOAD-NATIVE-CAPACITY";
        if (!added.retainedOwners || added.retainedOwners > upload.maxRetainedOwners)
            return "RHI-ORIGINAL-PUSH-UPLOAD-OWNER-CAPACITY";
        const auto native =
            std::uint64_t(original.nativePayloadObjects) + added.nativePayloadObjects;
        const auto owners = std::uint64_t(original.retainedOwners) + added.retainedOwners;
        if (native > UINT32_MAX || native > source.maxNativePayloadObjects)
            return "RHI-ORIGINAL-ROOT-PUSH-UPLOAD-NATIVE-CAPACITY";
        if (owners > UINT32_MAX || owners > maxRetainedOwners || owners > source.maxGraphOwners
            || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
            || owners > lifecycle.maxCallbackOwners)
            return "RHI-ORIGINAL-ROOT-PUSH-UPLOAD-OWNER-CAPACITY";
        output = {static_cast<std::uint32_t>(native), static_cast<std::uint32_t>(owners)};
        return nullptr;
    }
};

// RootCallbackCold /1 reserves the same Root /4 buffer stream, journal and
// request/result. It is preparation only, independent of execution capability.
struct OriginalBindlessRootCallbackColdBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCommands = 0u, maxOperationVisits = 0u;
    std::uint32_t maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    OriginalCommandReadbackResultContract::Budget readback;
    OriginalCommandReadbackMapContract::Budget readbackMap;
    OriginalCommandPushUploadBudget pushUpload;
    // Independent Geometry/1 over Graphics/2; exact initialized-index fetch
    // validation is cold and the native program retains only frozen handles.
    OriginalIndexedDrawFetchContract::Budget initializedGeometry;
};

class OriginalBindlessRootCallbackColdContract final {
public:
    using Budget = OriginalBindlessRootCallbackColdBudget;

    static constexpr const char* validateIntent(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u
            && b.contractVersion != 4u && b.contractVersion != 5u && b.contractVersion != 6u
            && b.contractVersion != 7u)
            return "RHI-ORIGINAL-ROOT-CALLBACK-COLD-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-ROOT-CALLBACK-COLD-OPT-IN";
        if (!b.maxCommands || !b.maxOperationVisits || !b.maxRetainedOwners
            || !b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-ROOT-CALLBACK-COLD-BUDGET";
        if (const auto* id = OriginalBindlessRootPushUploadContract::validateIntent(
                b.contractVersion, b.maxCommands, b.pushUpload))
            return id;
        if (b.initializedGeometry.contractVersion) {
            if ((b.contractVersion != 2u || b.initializedGeometry.contractVersion != 2u)
                && (b.contractVersion != 7u || b.initializedGeometry.contractVersion != 3u))
                return "RHI-ORIGINAL-ROOT-GEOMETRY-CALLBACK-PROFILE";
            if (const auto* id = b.initializedGeometry.diagnostic())
                return id;
        } else if (b.initializedGeometry.prototypeOptIn || b.initializedGeometry.maxCommands
            || b.initializedGeometry.maxDraws || b.initializedGeometry.maxStreams
            || b.initializedGeometry.maxBindingScans || b.initializedGeometry.maxAnalyzedIndices)
            return "RHI-ORIGINAL-ROOT-GEOMETRY-CALLBACK-SCHEMA";
        if (b.contractVersion == 7u && b.initializedGeometry.contractVersion != 3u)
            return "RHI-BINDLESS-PRODUCED-GEOMETRY-CALLBACK-FETCH-REQUIRED";
        if (const auto* id = OriginalCommandReadbackResultContract::validateIntent(b.readback))
            return id;
        return OriginalCommandReadbackMapContract::validateIntent(b.readbackMap);
    }

    static constexpr bool matchesCompletionProfile(
        std::uint32_t lifecycle, std::uint32_t admission, std::uint32_t content) noexcept
    {
        return (lifecycle == 11u && admission == 1u && content == 0u)
            || (lifecycle == 12u && admission == 2u && content == 9u)
            || (lifecycle == 13u && admission == 3u && content == 11u)
            || (lifecycle == 16u && admission == 4u && content == 14u)
            || (lifecycle == 18u && admission == 5u && content == 16u)
            || (lifecycle == 20u && admission == 6u && content == 19u)
            || (lifecycle == 22u && admission == 7u && content == 9u);
    }

    static constexpr bool isCallbackLifecycle(std::uint32_t version) noexcept
    {
        return version == 10u || version == 11u || version == 12u || version == 13u
            || version == 16u || version == 18u || version == 20u || version == 22u;
    }

    static const char* validateCorrespondence(
        const std::shared_ptr<const OriginalCommandStateTransaction>& transaction,
        const std::shared_ptr<const OriginalCommandBufferReadbackBinding>& binding,
        std::uint32_t version = 1u) noexcept
    {
        if (const auto* id = OriginalCommandStateTransaction::validateOriginal(transaction))
            return id;
        if (const auto* id = OriginalCommandBufferReadbackBinding::validateOriginal(binding))
            return id;
        if (transaction->status() != OriginalCommandStateTransaction::Status::Prepared
            || !transaction->hasOriginalPrototypePlan() || !transaction->journalEmpty())
            return "RHI-ORIGINAL-ROOT-CALLBACK-COLD-PLAN-STATUS";
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateContentProfile(
                {version == 7u          ? 22u
                        : version == 6u ? 20u
                        : version == 5u ? 18u
                        : version == 4u ? 16u
                        : version == 3u ? 13u
                        : version == 2u ? 12u
                                        : 10u},
                transaction->originalContentPlanContractVersion()))
            return id;
        if (version == 7u && !transaction->hasOriginalBindlessProducedGeometryProfile())
            return "RHI-BINDLESS-PRODUCED-GEOMETRY-CALLBACK-ROOT";
        if (!core::SharedOwnerProjection::sameOwner(
                transaction->originalGraph(), binding->originalGraph()))
            return "RHI-ORIGINAL-ROOT-CALLBACK-COLD-GRAPH";
        const auto* sequence = transaction->originalPlannedSequenceBudget();
        if (!sequence || !binding->matchesOriginalSequence(*sequence))
            return "RHI-ORIGINAL-ROOT-CALLBACK-COLD-SEQUENCE";
        return binding->supportsOriginalPendingCustody()
            ? nullptr
            : "RHI-ORIGINAL-BUFFER-READBACK-PENDING-VERSION";
    }
};

// Native shell preparation is separate from recording and submission. The
// shell is exclusively claimed through the existing recording seal.
struct OriginalBindlessRootProgramRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCommands = 0u, maxOperationVisits = 0u;
    OriginalCommandPushUploadBudget pushUpload;
    std::uint32_t initializedGeometryContractVersion = 0u;
    bool initializedGeometryOptIn = false;

    constexpr bool empty() const noexcept
    {
        return !contractVersion && !prototypeOptIn && !maxCommands && !maxOperationVisits
            && pushUpload.empty() && !initializedGeometryContractVersion
            && !initializedGeometryOptIn;
    }
};

class OriginalBindlessRootProgramContract final {
public:
    using Request = OriginalBindlessRootProgramRequest;

    static constexpr const char* validateIntent(const Request& r) noexcept
    {
        if (r.contractVersion != 1u && r.contractVersion != 2u && r.contractVersion != 3u
            && r.contractVersion != 4u && r.contractVersion != 5u && r.contractVersion != 6u
            && r.contractVersion != 7u)
            return "RHI-ORIGINAL-ROOT-PROGRAM-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-ROOT-PROGRAM-OPT-IN";
        if ((r.initializedGeometryContractVersion || r.initializedGeometryOptIn)
            && ((!((r.contractVersion == 2u && r.initializedGeometryContractVersion == 1u)
                    || (r.contractVersion == 7u && r.initializedGeometryContractVersion == 2u)))
                || !r.initializedGeometryOptIn))
            return "RHI-ORIGINAL-ROOT-PROGRAM-GEOMETRY-PROFILE";
        if (r.contractVersion == 7u
            && (r.initializedGeometryContractVersion != 2u || !r.initializedGeometryOptIn))
            return "RHI-BINDLESS-PRODUCED-GEOMETRY-PROGRAM-FETCH-REQUIRED";
        if (const auto* id = OriginalBindlessRootPushUploadContract::validateIntent(
                r.contractVersion, r.maxCommands, r.pushUpload))
            return id;
        return r.maxCommands && r.maxOperationVisits ? nullptr : "RHI-ORIGINAL-ROOT-PROGRAM-BUDGET";
    }

    static constexpr const char* validateCounts(
        std::uint64_t commands, std::uint64_t visits, const Request& r) noexcept
    {
        if (const auto* id = validateIntent(r))
            return id;
        if (!commands || commands > r.maxCommands)
            return "RHI-ORIGINAL-ROOT-PROGRAM-COMMAND-CAPACITY";
        return visits <= r.maxOperationVisits ? nullptr
                                              : "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY";
    }

    // Buffer-only Compute plus its authentic latch clear/readback. Resource,
    // reference and range correspondence stays in the original RootState/4
    // graph and BufferReadbackBinding. No graphics, upload or indirect intent.
    static constexpr const char* validateOperation(
        CommandUsageKind kind, std::uint32_t version = 1u) noexcept
    {
        using K = CommandUsageKind;
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u && version != 7u)
            return "RHI-ORIGINAL-ROOT-PROGRAM-VERSION";
        // ProducedGeometry/7 joins the same bounded compute, graphics and
        // transfer stream. Geometry2, four producer actors and Fetch3 remain
        // separate cold requirements; an operation allowlist grants no native
        // issuer or permission to use direct/uninitialized draws.
        if (version == 7u) {
            switch (kind) {
            case K::BufferBarrier:
            case K::TextureBarrier:
            case K::ClearSnapshotFault:
            case K::BeginComputePass:
            case K::BindPreparedComputeBinding:
            case K::Dispatch:
            case K::EndComputePass:
            case K::BeginRenderPass:
            case K::BindPreparedGraphicsBinding:
            case K::SetViewport:
            case K::SetScissor:
            case K::EndRenderPass:
            case K::BeginTransferPass:
            case K::BufferCopy:
            case K::EndTransferPass:
            case K::Finish:
                return nullptr;
            default:
                return "RHI-ORIGINAL-ROOT-PROGRAM-OPERATION";
            }
        }
        if ((version == 3u || version == 4u || version == 5u) && kind == K::TextureBarrier)
            return nullptr;
        if ((version == 4u || version == 5u)
            && (kind == K::BeginRenderPass || kind == K::EndRenderPass))
            return nullptr;
        if (version == 2u || version == 6u) {
            switch (kind) {
            case K::BufferBarrier:
            case K::TextureBarrier:
            case K::ClearSnapshotFault:
            case K::BeginRenderPass:
            case K::BindPreparedGraphicsBinding:
            case K::SetViewport:
            case K::SetScissor:
            case K::Draw:
            case K::EndRenderPass:
            case K::BeginTransferPass:
            case K::BufferCopy:
            case K::EndTransferPass:
            case K::Finish:
                return nullptr;
            default:
                return "RHI-ORIGINAL-ROOT-PROGRAM-OPERATION";
            }
        }
        switch (kind) {
        case K::BufferBarrier:
        case K::ClearSnapshotFault:
        case K::BeginComputePass:
        case K::BindPreparedComputeBinding:
        case K::Dispatch:
        case K::EndComputePass:
        case K::BeginTransferPass:
        case K::BufferCopy:
        case K::EndTransferPass:
        case K::Finish:
            return nullptr;
        default:
            return "RHI-ORIGINAL-ROOT-PROGRAM-OPERATION";
        }
    }

    static constexpr const char* validateOperation(
        CommandUsageKind kind, const Request& request) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        if (kind == CommandUsageKind::PushConstants && !request.pushUpload.empty())
            return nullptr;
        if (request.initializedGeometryContractVersion == 2u
            && (kind == CommandUsageKind::BindVertexBuffer
                || kind == CommandUsageKind::BindIndexBuffer
                || kind == CommandUsageKind::DrawIndexedIndirect))
            return nullptr;
        if (request.initializedGeometryContractVersion == 1u
            && (kind == CommandUsageKind::RetainIndexedSnapshot
                || kind == CommandUsageKind::BindVertexBuffer
                || kind == CommandUsageKind::BindIndexBuffer
                || kind == CommandUsageKind::DrawIndexed))
            return nullptr;
        if (request.initializedGeometryContractVersion == 1u && kind == CommandUsageKind::Draw)
            return "RHI-ORIGINAL-ROOT-PROGRAM-GEOMETRY-NONINDEXED";
        return validateOperation(kind, request.contractVersion);
    }

    // The actual device query supplies this limit. Zero is never a default.
    static constexpr const char* validateDispatch(
        std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t queriedLimit) noexcept
    {
        if (!queriedLimit)
            return "RHI-ORIGINAL-ROOT-PROGRAM-DISPATCH-LIMIT-UNAVAILABLE";
        return x <= queriedLimit && y <= queriedLimit && z <= queriedLimit
            ? nullptr
            : "RHI-ORIGINAL-ROOT-PROGRAM-DISPATCH-LIMIT";
    }
};

struct OriginalBindlessRootColdCommandRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    // /2 freezes the native program in this same original shell. /1 remains
    // shell-only. Neither profile grants encoder/submit/completion authority.
    OriginalBindlessRootProgramRequest program;
};

class OriginalBindlessRootColdCommandContract final {
public:
    using Request = OriginalBindlessRootColdCommandRequest;
    static constexpr std::uint32_t additionalOwners = 2u; // Shell and wrapper.

    static constexpr const char* validateIntent(const Request& r) noexcept
    {
        if (r.contractVersion != 1u && r.contractVersion != 2u)
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-OPT-IN";
        if (r.contractVersion == 1u && !r.program.empty())
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE";
        if (r.contractVersion == 2u)
            if (const auto* id = OriginalBindlessRootProgramContract::validateIntent(r.program))
                return id;
        return r.maxRetainedOwners && r.maxCpuMetadataBytes
            ? nullptr
            : "RHI-ORIGINAL-ROOT-COLD-COMMAND-BUDGET";
    }

    static constexpr const char* validateOwners(std::uint32_t retained,
        const Request& r,
        const OriginalCommandAdmissionBudget& source,
        const OriginalCommandNativeLifecycleBudget& lifecycle) noexcept
    {
        if (const auto* id = validateIntent(r))
            return id;
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateIntent(lifecycle))
            return id;
        if (!OriginalBindlessRootCallbackColdContract::isCallbackLifecycle(
                lifecycle.contractVersion))
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-LIFECYCLE";
        if (lifecycle.contractVersion != 10u && r.contractVersion != 2u)
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-REQUIRED";
        if (r.contractVersion == 2u
            && (r.program.contractVersion == 2u) != (lifecycle.contractVersion == 12u))
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE";
        if (r.contractVersion == 2u
            && (r.program.contractVersion == 3u) != (lifecycle.contractVersion == 13u))
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE";
        if (r.contractVersion == 2u
            && (r.program.contractVersion == 4u) != (lifecycle.contractVersion == 16u))
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE";
        if (r.contractVersion == 2u
            && (r.program.contractVersion == 5u) != (lifecycle.contractVersion == 18u))
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE";
        if (r.contractVersion == 2u
            && (r.program.contractVersion == 6u) != (lifecycle.contractVersion == 20u))
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-PROGRAM-PROFILE";
        if (!source.maxNativePayloadObjects)
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-NATIVE-CAPACITY";
        const auto owners = std::uint64_t(retained) + additionalOwners;
        if (owners > r.maxRetainedOwners || owners > source.maxGraphOwners
            || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
            || owners > lifecycle.maxCallbackOwners)
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-OWNER-CAPACITY";
        if (r.maxCpuMetadataBytes != lifecycle.maxRecordingCpuBytes)
            return "RHI-ORIGINAL-ROOT-COLD-COMMAND-QUOTA";
        return nullptr;
    }
};

// The original callback pool retains the cold Root and its buffer result.
// Only native loss observation is armed; queue/scope/map issuance stays closed.
struct OriginalBindlessRootCallbackCustodyRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingRecords = 0u, maxCallbackSlots = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuSubmissionBytes = 0u, maxCpuCallbackBytes = 0u;
};

class OriginalBindlessRootCallbackCustodyContract final {
public:
    using Request = OriginalBindlessRootCallbackCustodyRequest;

    static constexpr const char* validateIntent(const Request& r) noexcept
    {
        if (r.contractVersion != 1u)
            return "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-OPT-IN";
        // Reuse the canonical map-pool contract for count and owner derivation.
        return OriginalCommandCallbackCustodyContract::validateIntent(canonicalBudget(r));
    }

    static constexpr OriginalCommandCallbackCustodyBudget canonicalBudget(const Request& r) noexcept
    {
        return {2u,
            r.prototypeOptIn,
            r.maxPendingRecords,
            r.maxCallbackSlots,
            r.maxRetainedOwners,
            r.maxCpuSubmissionBytes,
            r.maxCpuCallbackBytes};
    }

    static constexpr std::uint32_t additionalOwners(const Request& r) noexcept
    {
        return OriginalCommandCallbackCustodyContract::additionalOwners(canonicalBudget(r));
    }

    static constexpr const char* validateRequirements(std::uint32_t retained,
        const OriginalCommandLifecycleCapacityRequest& capacity,
        const OriginalCommandAdmissionBudget& source,
        const OriginalCommandNativeLifecycleBudget& lifecycle,
        const Request& r) noexcept
    {
        if (const auto* id = validateIntent(r))
            return id;
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateIntent(lifecycle))
            return id;
        if (!OriginalBindlessRootCallbackColdContract::isCallbackLifecycle(
                lifecycle.contractVersion)
            || capacity.contractVersion != 6u)
            return "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-LIFECYCLE";
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateCapacityRequest(
                capacity, lifecycle))
            return id;
        if (r.maxPendingRecords != capacity.pendingSubmissions
            || r.maxCallbackSlots != capacity.callbackTickets
            || r.maxCallbackSlots != capacity.callbackRegistrations)
            return "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-CAPACITY";
        if (r.maxCpuSubmissionBytes != lifecycle.maxSubmissionCpuBytes
            || r.maxCpuCallbackBytes != lifecycle.maxCallbackCpuBytes)
            return "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-QUOTA";
        const auto owners = std::uint64_t(retained) + additionalOwners(r);
        return owners <= r.maxRetainedOwners && owners <= source.maxGraphOwners
                && owners <= lifecycle.maxRecordingOwners && owners <= lifecycle.maxSubmissionOwners
                && owners <= lifecycle.maxCallbackOwners
            ? nullptr
            : "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-OWNER-CAPACITY";
    }
};

struct OriginalBindlessRootIssuerRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRecordingIssuers = 0u, maxSubmissionIssuers = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxRecordingCpuBytes = 0u, maxSubmissionCpuBytes = 0u, maxCallbackCpuBytes = 0u;
};

class OriginalBindlessRootIssuerContract final {
public:
    using Request = OriginalBindlessRootIssuerRequest;
    static constexpr std::uint32_t additionalOwners =
        OriginalCommandIssuerPreparationContract::additionalOwnerCount;

    static constexpr const char* validateIntent(const Request& r) noexcept
    {
        if (r.contractVersion != 1u)
            return "RHI-ORIGINAL-ROOT-ISSUER-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-ORIGINAL-ROOT-ISSUER-OPT-IN";
        if (r.maxRecordingIssuers != 1u || r.maxSubmissionIssuers != 1u)
            return "RHI-ORIGINAL-ROOT-ISSUER-COUNT";
        return r.maxRetainedOwners && r.maxRecordingCpuBytes && r.maxSubmissionCpuBytes
                && r.maxCallbackCpuBytes
            ? nullptr
            : "RHI-ORIGINAL-ROOT-ISSUER-BUDGET";
    }

    static constexpr const char* validateRequirements(std::uint32_t retained,
        const OriginalCommandAdmissionBudget& source,
        const OriginalCommandNativeLifecycleBudget& lifecycle,
        const Request& r) noexcept
    {
        if (const auto* id = validateIntent(r))
            return id;
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateIntent(lifecycle))
            return id;
        if (!OriginalBindlessRootCallbackColdContract::isCallbackLifecycle(
                lifecycle.contractVersion))
            return "RHI-ORIGINAL-ROOT-ISSUER-LIFECYCLE";
        if (r.maxRecordingCpuBytes != lifecycle.maxRecordingCpuBytes
            || r.maxSubmissionCpuBytes != lifecycle.maxSubmissionCpuBytes
            || r.maxCallbackCpuBytes != lifecycle.maxCallbackCpuBytes)
            return "RHI-ORIGINAL-ROOT-ISSUER-QUOTA";
        const auto owners = std::uint64_t(retained) + additionalOwners;
        return owners <= r.maxRetainedOwners && owners <= source.maxGraphOwners
                && owners <= lifecycle.maxRecordingOwners && owners <= lifecycle.maxSubmissionOwners
                && owners <= lifecycle.maxCallbackOwners
            ? nullptr
            : "RHI-ORIGINAL-ROOT-ISSUER-OWNER-CAPACITY";
    }
};

// Execution is a distinct typed intent over an already-issued executable plan.
// Cold program/issuer construction cannot promote OriginalPreparation/1.
class OriginalBindlessRootNativeExecutionContract final {
public:
    using Request = BindlessRequirements::OriginalExecution;

    static constexpr bool produced(std::uint32_t version) noexcept
    {
        return version == 15u || version == 16u;
    }

    static constexpr bool native(std::uint32_t version) noexcept
    {
        return (version >= 6u && version <= 9u) || version == 11u || version == 15u;
    }

    static constexpr std::uint32_t lifecycleVersion(std::uint32_t version) noexcept
    {
        return version == 16u                ? 22u
            : version == 15u                 ? 21u
            : version == 14u                 ? 20u
            : version == 13u                 ? 12u
            : version == 12u                 ? 18u
            : version == 11u                 ? 17u
            : version == 10u                 ? 16u
            : version == 9u                  ? 15u
            : version == 8u                  ? 8u
            : version == 7u                  ? 6u
            : version == 6u                  ? 14u
            : version == 5u                  ? 13u
            : version >= 2u && version <= 4u ? 12u
                                             : 11u;
    }

    static constexpr std::uint32_t admissionVersion(std::uint32_t version) noexcept
    {
        return produced(version)             ? 7u
            : version == 14u                 ? 6u
            : version == 13u                 ? 2u
            : version == 12u                 ? 5u
            : version == 10u                 ? 4u
            : version == 5u                  ? 3u
            : version >= 2u && version <= 4u ? 2u
                                             : 1u;
    }

    static constexpr std::uint32_t closureVersion(std::uint32_t version) noexcept
    {
        return produced(version) ? 21u
            : version == 14u     ? 20u
            : version == 13u     ? 18u
            : version == 12u     ? 15u
            : version == 11u     ? 14u
            : version == 10u     ? 12u
            : version == 9u      ? 11u
            : version == 6u      ? 9u
            : version == 5u      ? 8u
            : version == 4u      ? 6u
            : version == 3u      ? 5u
            : version == 2u      ? 4u
                                 : 1u;
    }

    static constexpr bool hasExecutionPlan(
        const BindlessLoweringPlan& plan, std::uint32_t version = 1u) noexcept
    {
        return plan.success && plan.executable && !plan.originalPreparationContractVersion
            && (version >= 1u && version <= 16u) && plan.originalExecutionContractVersion == version
            && plan.execution
            == (native(version) ? BindlessExecution::Native : BindlessExecution::Emulated);
    }

    static constexpr bool hasPreparedPlan(
        const BindlessLoweringPlan& plan, std::uint32_t version = 1u) noexcept
    {
        return hasExecutionPlan(plan, version)
            || (plan.success && !plan.executable
                && (version == 1u || version == 2u || version == 3u || version == 5u)
                && plan.originalPreparationContractVersion == version
                && !plan.originalExecutionContractVersion
                && plan.execution == BindlessExecution::Emulated);
    }

    static constexpr const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 16u)
            return "RHI-ORIGINAL-ROOT-EXECUTION-VERSION";
        return request.prototypeOptIn ? nullptr : "RHI-ORIGINAL-ROOT-EXECUTION-OPT-IN";
    }

    static const char* validateProfile(const Request& request,
        const BindlessLoweringPlan& plan,
        const OriginalCommandNativeLifecycleBudget& lifecycle) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        if (const auto* id = OriginalCommandNativeLifecycleContract::validateIntent(lifecycle))
            return id;
        if (lifecycle.contractVersion != lifecycleVersion(request.contractVersion))
            return "RHI-ORIGINAL-ROOT-EXECUTION-LIFECYCLE";
        return hasExecutionPlan(plan, request.contractVersion) ? nullptr
                                                               : "RHI-ORIGINAL-ROOT-EXECUTION-PLAN";
    }
};
} // namespace arp::rhi
