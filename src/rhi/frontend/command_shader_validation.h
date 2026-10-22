#pragma once

#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/descriptor_validation.h"
#include "rhi/frontend/memory_budget_ledger.h"

#include <array>

// region original_typed_native_cold
namespace arp::rhi {
// Independent TypedNativeCold1/2/3/4/5/6/7/8 value schema, never a NativeCold1-5 adapter.
// These values are not provenance, an executable sync decision or a receipt.
struct OriginalTypedNativeColdWorkCounts {
    std::uint64_t commands = 0u, computePasses = 0u, renderPasses = 0u;
    std::uint64_t pipelineBinds = 0u, descriptorBinds = 0u, draws = 0u, dispatches = 0u;
    std::uint64_t dependencies = 0u, dynamicOffsets = 0u, implicitAccesses = 0u;
    std::uint64_t viewports = 0u, scissors = 0u, attachmentReferences = 0u, nativeCalls = 0u;
    // Independent Payload4 geometry calls. Draws counts non-indexed draws only.
    // Legacy Cold1/2/3 profiles must reject any nonzero geometry demand.
    std::uint64_t vertexBinds = 0u, indexBinds = 0u, indexedDraws = 0u;

    constexpr bool hasGeometry() const noexcept
    {
        return vertexBinds || indexBinds || indexedDraws;
    }

    constexpr bool completeGeometryCounts() const noexcept
    {
        return (vertexBinds || indexBinds) && (draws || indexedDraws)
            && (!indexedDraws || indexBinds);
    }

    // Shared shape only. Payload separately validates its color attachments;
    // cold admission separately authenticates the exact payload/root/lifecycle.
    constexpr bool completeGraphicsCounts() const noexcept
    {
        return !computePasses && !dispatches && renderPasses && draws && viewports && scissors
            && attachmentReferences;
    }

    bool matches(const OriginalTypedNativeColdWorkCounts& other) const noexcept
    {
        const std::array members{&OriginalTypedNativeColdWorkCounts::commands,
            &OriginalTypedNativeColdWorkCounts::computePasses,
            &OriginalTypedNativeColdWorkCounts::renderPasses,
            &OriginalTypedNativeColdWorkCounts::pipelineBinds,
            &OriginalTypedNativeColdWorkCounts::descriptorBinds,
            &OriginalTypedNativeColdWorkCounts::draws,
            &OriginalTypedNativeColdWorkCounts::dispatches,
            &OriginalTypedNativeColdWorkCounts::dependencies,
            &OriginalTypedNativeColdWorkCounts::dynamicOffsets,
            &OriginalTypedNativeColdWorkCounts::implicitAccesses,
            &OriginalTypedNativeColdWorkCounts::viewports,
            &OriginalTypedNativeColdWorkCounts::scissors,
            &OriginalTypedNativeColdWorkCounts::attachmentReferences,
            &OriginalTypedNativeColdWorkCounts::nativeCalls,
            &OriginalTypedNativeColdWorkCounts::vertexBinds,
            &OriginalTypedNativeColdWorkCounts::indexBinds,
            &OriginalTypedNativeColdWorkCounts::indexedDraws};
        for (const auto member : members)
            if (this->*member != other.*member)
                return false;
        return true;
    }
};

struct OriginalTypedNativeColdCallbackCounts {
    std::uint32_t recordingScopes = 0u, submissionScopes = 0u, queue = 0u;
    std::uint32_t runtimeLossCallbacks = 0u, runtimeLossRegistrations = 0u;
};

struct OriginalTypedNativeTransferCounts {
    std::uint64_t passes = 0u, readbacks = 0u, copies = 0u;

    constexpr bool empty() const noexcept { return !passes && !readbacks && !copies; }

    constexpr bool matches(const OriginalTypedNativeTransferCounts& other) const noexcept
    {
        return passes == other.passes && readbacks == other.readbacks && copies == other.copies;
    }
};
enum class OriginalTypedNativeColdSynchronization : std::uint32_t {
    Unspecified,
    OriginalUsageScopeBoundaries1
};

struct OriginalTypedNativeColdRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t synchronizationVersion = 0u;
    OriginalTypedNativeColdSynchronization synchronization =
        OriginalTypedNativeColdSynchronization::Unspecified;
    OriginalTypedNativeColdWorkCounts maxWork;
    OriginalTypedNativeColdCallbackCounts maxCallbacks;
    std::uint32_t maxEncoders = 0u, maxCommandBuffers = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    OriginalTypedNativeTransferCounts maxTransfers;
    // Independent mixed readback demand, authored before Source admission.
    // Empty preserves the recording-only eight-participant profile.
    OriginalCommandReadbackResultContract::Budget readback;
    OriginalCommandReadbackMapContract::Budget readbackMap;
};

struct OriginalTypedNativeColdRequirements {
    std::uint32_t contractVersion = 0u;
    OriginalTypedNativeColdWorkCounts work;
    OriginalTypedNativeColdCallbackCounts callbacks;
    std::uint32_t encoders = 0u, commandBuffers = 0u;
    std::uint32_t existingDeviceLossCallbacks = 0u, existingDeviceLossRegistrations = 0u;
    OriginalCommandLifecycleCapacityRequest capacity;
    std::uint32_t retainedOwners = 0u;
    std::uint64_t cpuMetadataBytes = 0u;
    OriginalTypedNativeTransferCounts transfers;
    OriginalCommandReadbackResultContract::Requirements readback;
    OriginalCommandReadbackMapContract::Requirements readbackMap;

    bool matches(const OriginalTypedNativeColdRequirements& other) const noexcept
    {
        return contractVersion == other.contractVersion && work.matches(other.work)
            && callbacks.recordingScopes == other.callbacks.recordingScopes
            && callbacks.submissionScopes == other.callbacks.submissionScopes
            && callbacks.queue == other.callbacks.queue
            && callbacks.runtimeLossCallbacks == other.callbacks.runtimeLossCallbacks
            && callbacks.runtimeLossRegistrations == other.callbacks.runtimeLossRegistrations
            && encoders == other.encoders && commandBuffers == other.commandBuffers
            && existingDeviceLossCallbacks == other.existingDeviceLossCallbacks
            && existingDeviceLossRegistrations == other.existingDeviceLossRegistrations
            && capacity.contractVersion == other.capacity.contractVersion
            && capacity.prototypeOptIn == other.capacity.prototypeOptIn
            && capacity.pendingSubmissions == other.capacity.pendingSubmissions
            && capacity.callbackTickets == other.capacity.callbackTickets
            && capacity.callbackRegistrations == other.capacity.callbackRegistrations
            && retainedOwners == other.retainedOwners && cpuMetadataBytes == other.cpuMetadataBytes
            && transfers.matches(other.transfers)
            && readback.readbackCommands == other.readback.readbackCommands
            && readback.nativeCopyCommands == other.readback.nativeCopyCommands
            && readback.resultDeclarations == other.readback.resultDeclarations
            && readback.declaredReadbackBytes == other.readback.declaredReadbackBytes
            && readback.uniqueResultBytes == other.readback.uniqueResultBytes
            && readback.cpuResultBytes == other.readback.cpuResultBytes
            && readbackMap.maps == other.readbackMap.maps
            && readbackMap.mapCallbacks == other.readbackMap.mapCallbacks
            && readbackMap.scopeCallbacks == other.readbackMap.scopeCallbacks
            && readbackMap.totalCallbackTickets == other.readbackMap.totalCallbackTickets
            && readbackMap.totalCallbackRegistrations
            == other.readbackMap.totalCallbackRegistrations;
    }
};

class OriginalTypedNativeColdContract final {
public:
    using Request = OriginalTypedNativeColdRequest;
    using Requirements = OriginalTypedNativeColdRequirements;
    using Work = OriginalTypedNativeColdWorkCounts;

    static constexpr bool generalProfile(std::uint32_t version) noexcept
    {
        return version == 6u || version == 7u || version == 8u;
    }

    static constexpr std::uint32_t generalLifecycleVersion(std::uint32_t version) noexcept
    {
        return version == 8u ? 5u : version == 7u ? 4u : version == 6u ? 3u : 0u;
    }

    static constexpr bool geometryProfile(std::uint32_t version) noexcept
    {
        return version == 4u || version == 5u;
    }

    static constexpr bool readbackProfile(std::uint32_t version) noexcept
    {
        return version == 3u || version == 5u;
    }

    // Exact typed result/mapper ABI; neither legacy nor unknown cold profiles
    // can borrow a composed Join3 or mixed Join2.
    static constexpr std::uint32_t readbackJoinVersion(std::uint32_t version) noexcept
    {
        return version == 3u ? 2u : version == 5u ? 3u : 0u;
    }

    static constexpr bool hasReadbackDemand(const Request& request) noexcept
    {
        const auto& b = request.readback;
        return b.contractVersion || b.prototypeOptIn || b.maxReadbackCommands
            || b.maxNativeCopyCommands || b.maxResultDeclarations || b.maxDeclaredReadbackBytes
            || b.maxUniqueResultBytes || b.maxCpuResultBytes
            || OriginalCommandReadbackMapContract::hasBudget(request.readbackMap);
    }

    static constexpr bool hasReadbackRequirements(const Requirements& required) noexcept
    {
        const auto& r = required.readback;
        return r.readbackCommands || r.nativeCopyCommands || r.resultDeclarations
            || r.declaredReadbackBytes || r.uniqueResultBytes || r.cpuResultBytes
            || OriginalCommandReadbackMapContract::hasRequirements(required.readbackMap);
    }

    static constexpr const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 8u)
            return "RHI-ORIGINAL-TYPED-COLD-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-TYPED-COLD-OPT-IN";
        if (!geometryProfile(request.contractVersion) && request.maxWork.hasGeometry())
            return "RHI-ORIGINAL-TYPED-COLD-GEOMETRY-SCHEMA";
        if (request.contractVersion == 8u && !request.maxWork.completeGraphicsCounts())
            return "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE";
        // /5 is a complete composition, never an optional upgrade of /3 or /4.
        if (request.contractVersion == 5u && !hasReadbackDemand(request))
            return "RHI-ORIGINAL-TYPED-COLD-COMPOSED-READBACK-REQUIRED";
        if (hasReadbackDemand(request)) {
            if (!readbackProfile(request.contractVersion))
                return "RHI-ORIGINAL-TYPED-COLD-READBACK-PROFILE";
            if (const auto* id =
                    OriginalCommandReadbackResultContract::validateProgramIntent(request.readback))
                return id;
            if (const auto* id =
                    OriginalCommandReadbackMapContract::validateIntent(request.readbackMap))
                return id;
        }
        if (!readbackProfile(request.contractVersion) && !request.maxTransfers.empty())
            return "RHI-ORIGINAL-TYPED-COLD-TRANSFER-SCHEMA";
        if (readbackProfile(request.contractVersion)
            && (!request.maxTransfers.passes || !request.maxTransfers.readbacks
                || !request.maxTransfers.copies))
            return "RHI-ORIGINAL-TYPED-COLD-TRANSFER-BUDGET";
        if (request.synchronizationVersion != 1u
            || request.synchronization
                != OriginalTypedNativeColdSynchronization::OriginalUsageScopeBoundaries1)
            return "RHI-ORIGINAL-TYPED-COLD-SYNCHRONIZATION-POLICY";
        const auto& work = request.maxWork;
        if (geometryProfile(request.contractVersion) && !work.completeGeometryCounts())
            return "RHI-ORIGINAL-TYPED-COLD-GEOMETRY-BUDGET";
        if (!work.commands || !work.pipelineBinds || !work.descriptorBinds || !work.implicitAccesses
            || !work.nativeCalls || (!work.draws && !work.indexedDraws && !work.dispatches))
            return "RHI-ORIGINAL-TYPED-COLD-WORK-BUDGET";
        if ((!work.computePasses && work.dispatches)
            || (!work.renderPasses
                && (work.draws || work.hasGeometry() || work.viewports || work.scissors
                    || work.attachmentReferences)))
            return "RHI-ORIGINAL-TYPED-COLD-PASS-PROFILE";
        const auto& callbacks = request.maxCallbacks;
        if (!callbacks.recordingScopes || !callbacks.submissionScopes || !callbacks.queue
            || !callbacks.runtimeLossCallbacks || !callbacks.runtimeLossRegistrations)
            return "RHI-ORIGINAL-TYPED-COLD-CALLBACK-BUDGET";
        if (!request.maxEncoders || !request.maxCommandBuffers || !request.maxRetainedOwners
            || !request.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-BUDGET";
        return nullptr;
    }

    // Pure selected-vocabulary count consistency, not a native lowering.
    // Every sum reuses core's overflow guard; failures clear both outputs.
    static const char* inspectWorkTotals(
        const Work& work, std::uint64_t& commands, std::uint64_t& nativeCalls) noexcept
    {
        commands = nativeCalls = 0u;
        std::uint64_t total = 1u; // The selected program has exactly one Finish.
        const std::array terms{work.computePasses,
            work.computePasses,
            work.renderPasses,
            work.renderPasses,
            work.pipelineBinds,
            work.descriptorBinds,
            work.draws,
            work.dispatches,
            work.dependencies,
            work.viewports,
            work.scissors,
            work.vertexBinds,
            work.indexBinds,
            work.indexedDraws};
        for (const auto term : terms) {
            if (!core::MemoryBudgetLedger::canAdd(total, term))
                return "RHI-ORIGINAL-TYPED-COLD-COUNT-OVERFLOW";
            total += term;
        }
        const auto calls = total - work.dependencies;
        // createCommandEncoder is independent of the Finish command's finish().
        if (!core::MemoryBudgetLedger::canAdd(calls, 1u))
            return "RHI-ORIGINAL-TYPED-COLD-COUNT-OVERFLOW";
        commands = total;
        nativeCalls = calls + 1u;
        return nullptr;
    }

    // Shared selected-work validation for cold admission and immutable payload
    // preparation. It grants no callback, lifecycle or execution authority.
    static const char* validateWork(
        const Work& work, const Work& maximum, bool geometry = false) noexcept
    {
        if (!geometry && (work.hasGeometry() || maximum.hasGeometry()))
            return "RHI-ORIGINAL-TYPED-COLD-GEOMETRY-SCHEMA";
        if (geometry && !work.completeGeometryCounts())
            return "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-REQUIREMENTS";
        if (!work.pipelineBinds || !work.descriptorBinds || !work.implicitAccesses
            || (!work.draws && !work.indexedDraws && !work.dispatches)
            || (!work.computePasses && work.dispatches)
            || (!work.renderPasses
                && (work.draws || work.hasGeometry() || work.viewports || work.scissors
                    || work.attachmentReferences)))
            return "RHI-ORIGINAL-TYPED-COLD-WORK-REQUIREMENTS";
        std::uint64_t commands = 0u, calls = 0u;
        if (const auto* id = inspectWorkTotals(work, commands, calls))
            return id;
        if (work.commands != commands || work.nativeCalls != calls)
            return "RHI-ORIGINAL-TYPED-COLD-COUNT-TOTALS";
        constexpr std::array members{&Work::commands,
            &Work::computePasses,
            &Work::renderPasses,
            &Work::pipelineBinds,
            &Work::descriptorBinds,
            &Work::draws,
            &Work::dispatches,
            &Work::dependencies,
            &Work::dynamicOffsets,
            &Work::implicitAccesses,
            &Work::viewports,
            &Work::scissors,
            &Work::attachmentReferences,
            &Work::nativeCalls,
            &Work::vertexBinds,
            &Work::indexBinds,
            &Work::indexedDraws};
        for (const auto member : members)
            if (work.*member > maximum.*member)
                return "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY";
        return nullptr;
    }

    // Shared pure mixed-work arithmetic; neither values nor counts grant a
    // native pass/copy authority. Legacy work validation remains unchanged.
    static const char* inspectMixedWorkTotals(const Work& work,
        const OriginalTypedNativeTransferCounts& transfers,
        std::uint64_t& commands,
        std::uint64_t& calls) noexcept
    {
        commands = calls = 0u;
        std::uint64_t shaderCommands = 0u, shaderCalls = 0u;
        if (const auto* id = inspectWorkTotals(work, shaderCommands, shaderCalls))
            return id;
        for (const auto value : {transfers.passes, transfers.passes, transfers.readbacks}) {
            if (!core::MemoryBudgetLedger::canAdd(shaderCommands, value))
                return "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-OVERFLOW";
            shaderCommands += value;
        }
        if (!core::MemoryBudgetLedger::canAdd(shaderCalls, transfers.copies))
            return "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-OVERFLOW";
        commands = shaderCommands;
        calls = shaderCalls + transfers.copies;
        return nullptr;
    }

    static const char* validateMixedWork(const Work& work,
        const OriginalTypedNativeTransferCounts& transfers,
        const Work& maximum,
        bool geometry = false) noexcept
    {
        if (!transfers.passes || !transfers.readbacks || !transfers.copies
            || transfers.copies < transfers.readbacks)
            return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-REQUIREMENTS";
        std::uint64_t commands = 0u, calls = 0u;
        if (const auto* id = inspectMixedWorkTotals(work, transfers, commands, calls))
            return id;
        if (work.commands != commands || work.nativeCalls != calls)
            return "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-TOTALS";
        if (commands > maximum.commands || calls > maximum.nativeCalls)
            return "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY";
        auto shader = work;
        shader.commands -= transfers.passes + transfers.passes + transfers.readbacks;
        shader.nativeCalls -= transfers.copies;
        return validateWork(shader, maximum, geometry);
    }

    static constexpr const char* validatePayloadProfile(
        const Request& request, std::uint32_t payloadVersion) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        return payloadVersion == request.contractVersion ? nullptr
                                                         : "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE";
    }

    static constexpr const char* validateLifecycleProfile(
        const Request& request, std::uint32_t lifecycleVersion) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        return lifecycleVersion
                == (generalProfile(request.contractVersion)
                        ? generalLifecycleVersion(request.contractVersion)
                        : readbackProfile(request.contractVersion) ? 2u
                                                                   : 1u)
            ? nullptr
            : "RHI-WEBGPU-TYPED-COLD-LIFECYCLE-PROFILE";
    }

    static const char* validateRequirements(
        const Requirements& required, const Request& request) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        if (required.contractVersion != request.contractVersion)
            return "RHI-ORIGINAL-TYPED-COLD-REQUIREMENTS-VERSION";
        if (request.contractVersion == 8u && !required.work.completeGraphicsCounts())
            return "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE";
        if (readbackProfile(request.contractVersion)) {
            if (required.transfers.passes > request.maxTransfers.passes
                || required.transfers.readbacks > request.maxTransfers.readbacks
                || required.transfers.copies > request.maxTransfers.copies)
                return "RHI-ORIGINAL-TYPED-COLD-TRANSFER-CAPACITY";
            if (const auto* id = validateMixedWork(required.work,
                    required.transfers,
                    request.maxWork,
                    geometryProfile(request.contractVersion)))
                return id;
        } else {
            if (!required.transfers.empty())
                return "RHI-ORIGINAL-TYPED-COLD-TRANSFER-SCHEMA";
            if (const auto* id =
                    validateWork(required.work, request.maxWork, request.contractVersion == 4u))
                return id;
        }
        if (required.encoders != 1u || required.commandBuffers != 1u
            || required.existingDeviceLossCallbacks
                != OriginalDeviceNativeLossContract::callbackCount
            || required.existingDeviceLossRegistrations
                != OriginalDeviceNativeLossContract::registrationCount)
            return "RHI-ORIGINAL-TYPED-COLD-NATIVE-OBJECT-REQUIREMENTS";
        if (required.encoders > request.maxEncoders
            || required.commandBuffers > request.maxCommandBuffers)
            return "RHI-ORIGINAL-TYPED-COLD-NATIVE-OBJECT-CAPACITY";
        const auto& callbacks = required.callbacks;
        if (!callbacks.recordingScopes || !callbacks.submissionScopes || callbacks.queue != 1u
            || callbacks.runtimeLossCallbacks != OriginalDeviceNativeLossContract::callbackCount
            || callbacks.runtimeLossRegistrations
                != OriginalDeviceNativeLossContract::registrationCount)
            return "RHI-ORIGINAL-TYPED-COLD-CALLBACK-REQUIREMENTS";
        using C = OriginalTypedNativeColdCallbackCounts;
        constexpr std::array callbackMembers{&C::recordingScopes,
            &C::submissionScopes,
            &C::queue,
            &C::runtimeLossCallbacks,
            &C::runtimeLossRegistrations};
        for (const auto member : callbackMembers)
            if (callbacks.*member > request.maxCallbacks.*member)
                return "RHI-ORIGINAL-TYPED-COLD-CALLBACK-CAPACITY";
        const auto scopes =
            std::uint64_t(callbacks.recordingScopes) + callbacks.submissionScopes + callbacks.queue;
        const auto baseTickets = scopes + callbacks.runtimeLossCallbacks;
        const auto baseRegistrations = scopes + callbacks.runtimeLossRegistrations;
        auto tickets = baseTickets, registrations = baseRegistrations;
        if (hasReadbackDemand(request)) {
            if (const auto* id = OriginalCommandReadbackResultContract::validateProgramRequirements(
                    required.readback, request.readback))
                return id;
            if (required.readback.readbackCommands != required.transfers.readbacks
                || required.readback.nativeCopyCommands != required.transfers.copies)
                return "RHI-ORIGINAL-TYPED-COLD-READBACK-COVERAGE";
            if (const auto* id =
                    OriginalCommandReadbackMapContract::validateRequirements(required.readback,
                        baseTickets,
                        baseRegistrations,
                        request.readbackMap,
                        required.readbackMap))
                return id;
            tickets = required.readbackMap.totalCallbackTickets;
            registrations = required.readbackMap.totalCallbackRegistrations;
        } else if (hasReadbackRequirements(required))
            return "RHI-ORIGINAL-TYPED-COLD-READBACK-PROFILE";
        const auto& capacity = required.capacity;
        if (tickets > UINT32_MAX || registrations > UINT32_MAX || capacity.contractVersion != 1u
            || !capacity.prototypeOptIn || capacity.pendingSubmissions != 1u
            || capacity.callbackTickets != tickets
            || capacity.callbackRegistrations != registrations)
            return "RHI-ORIGINAL-TYPED-COLD-CALLBACK-RESERVATION";
        if (!required.retainedOwners || !required.cpuMetadataBytes)
            return "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-REQUIREMENTS";
        if (required.retainedOwners > request.maxRetainedOwners
            || required.cpuMetadataBytes > request.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-CAPACITY";
        return nullptr;
    }

    // Actual retained resource payloads plus independently required command
    // objects consume the authored Source ceiling. Zero is no native authority.
    static const char* validateSourceAuthority(const Requirements& required,
        std::uint64_t retainedPayloads,
        const OriginalCommandAdmissionBudget& source) noexcept
    {
        if (const auto* id = OriginalCommandAdmissionContract::validateIntent(source))
            return id;
        const auto objects = std::uint64_t(required.encoders) + required.commandBuffers;
        if (!core::MemoryBudgetLedger::canAdd(retainedPayloads, objects))
            return "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-OVERFLOW";
        if (retainedPayloads + objects > source.maxNativePayloadObjects)
            return "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY";
        if (required.work.nativeCalls > source.maxTypedNativeCommandCalls)
            return "RHI-ORIGINAL-TYPED-COLD-SOURCE-NATIVE-CALL-CAPACITY";
        if (required.capacity.pendingSubmissions > source.maxPendingSubmissions)
            return "RHI-ORIGINAL-TYPED-COLD-SOURCE-SUBMISSION-CAPACITY";
        if (required.capacity.callbackTickets > source.maxCallbackTickets)
            return "RHI-ORIGINAL-TYPED-COLD-SOURCE-CALLBACK-CAPACITY";
        if (required.capacity.callbackRegistrations > source.maxCallbackRegistrations)
            return "RHI-ORIGINAL-TYPED-COLD-SOURCE-REGISTRATION-CAPACITY";
        return nullptr;
    }

    // General6/7 and Graphics8 have two independent owner domains. The immutable payload's
    // authored native ceiling bounds initializer custody; Sources bounds only
    // its CPU root. No caller can expand either ceiling during cold admission.
    static constexpr const char* validateOwnerCapacity(const Request& request,
        std::uint32_t payloadOwners,
        std::uint32_t payloadCeiling,
        std::uint32_t additionalOwners,
        std::uint32_t rootOwners,
        std::uint32_t sourceCeiling) noexcept
    {
        const auto demand = std::uint64_t(payloadOwners) + additionalOwners;
        const auto ceiling = generalProfile(request.contractVersion)
            ? std::uint64_t(payloadCeiling) + additionalOwners
            : sourceCeiling;
        return demand > UINT32_MAX || ceiling > UINT32_MAX || demand > request.maxRetainedOwners
                || request.maxRetainedOwners > ceiling
                || (generalProfile(request.contractVersion) && rootOwners > sourceCeiling)
            ? "RHI-WEBGPU-TYPED-COLD-OWNER-CAPACITY"
            : nullptr;
    }
};

// endregion original_typed_native_cold

// region original_typed_usage_projection

class OriginalTypedUsageProjectionContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        OriginalCommandDescriptorConsumptionContract::Budget shaderConsumption;
        // Zero disables render binding work in a compute-only stream.
        OriginalRenderDescriptorUsageContract::Budget renderBindings;
        std::uint64_t maxCommands = 0u, maxAttachmentReferences = 0u, maxContributions = 0u;
        // /2 independently bounds sampled contributions and all texture cells.
        std::uint64_t maxTextureAccesses = 0u, maxTextureCells = 0u;
        // /3 adds complete retained readback operands independently of /2's
        // unchanged shader/descriptor footprint schema. Per-command totals
        // include repeated copies and authored destination padding.
        std::uint64_t maxReadbacks = 0u, maxReadbackMips = 0u, maxReadbackDeclaredBytes = 0u;
        // /4 consumes exact State4/Stream4 geometry bindings in the same complete
        // projection. Retained readback intent is optional, but all-or-nothing.
        OriginalGeometryUsageBudget geometryUsage;
    };

    static constexpr std::uint32_t programVersion(const Budget& b) noexcept
    {
        return b.contractVersion == 6u ? 5u
            : b.contractVersion == 5u  ? (b.geometryUsage.empty() ? 3u : 4u)
            : b.contractVersion == 4u  ? 4u
            : b.contractVersion == 1u  ? 2u
                                       : 3u;
    }

    static constexpr std::uint32_t scopeVersion(const Budget& b) noexcept
    {
        // Scope5 already validates finite intervals and whole-backing hazards.
        // The independent Usage6 composition schema does not create Scope6.
        return b.contractVersion == 6u ? 5u : b.contractVersion;
    }

    static constexpr bool isGeneral(const Budget& b) noexcept
    {
        return b.contractVersion == 5u || b.contractVersion == 6u;
    }

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u && b.contractVersion != 3u
            && b.contractVersion != 4u && b.contractVersion != 5u && b.contractVersion != 6u)
            return "RHI-ORIGINAL-USAGE-PROJECTION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-USAGE-PROJECTION-OPT-IN";
        if (b.contractVersion == 4u || (b.contractVersion == 5u && !b.geometryUsage.empty())) {
            if (const auto* id = b.geometryUsage.diagnostic())
                return id;
        } else if (!b.geometryUsage.empty())
            return "RHI-ORIGINAL-USAGE-PROJECTION-GEOMETRY-SCHEMA";
        if (b.contractVersion == 1u && (b.maxTextureAccesses || b.maxTextureCells))
            return "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-SCHEMA";
        if (b.contractVersion != 1u && (!b.maxTextureAccesses || !b.maxTextureCells))
            return "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-BUDGET";
        const bool readbackIntent =
            b.maxReadbacks || b.maxReadbackMips || b.maxReadbackDeclaredBytes;
        if (b.contractVersion != 3u && b.contractVersion != 4u && b.contractVersion != 5u
            && readbackIntent)
            return "RHI-ORIGINAL-USAGE-PROJECTION-READBACK-SCHEMA";
        if ((b.contractVersion == 3u
                || ((b.contractVersion == 4u || b.contractVersion == 5u) && readbackIntent))
            && (!b.maxReadbacks || !b.maxReadbackMips || !b.maxReadbackDeclaredBytes))
            return "RHI-ORIGINAL-USAGE-PROJECTION-READBACK-BUDGET";
        if (const auto* id =
                OriginalCommandDescriptorConsumptionContract::validateBudget(b.shaderConsumption))
            return id;
        const auto footprintVersion = isGeneral(b) ? programVersion(b) - 1u
            : b.contractVersion == 4u              ? 3u
            : b.contractVersion == 3u              ? 2u
                                                   : b.contractVersion;
        if (b.shaderConsumption.contractVersion != footprintVersion)
            return "RHI-ORIGINAL-USAGE-PROJECTION-CONSUMPTION-PROFILE";
        const bool general = isGeneral(b);
        const auto generalVersion = b.contractVersion == 6u ? 2u : general ? 1u : 0u;
        if (general != b.shaderConsumption.generalShaderResourcesOptIn
            || b.shaderConsumption.generalShaderResourcesVersion != generalVersion)
            return "RHI-ORIGINAL-USAGE-PROJECTION-GENERAL-PROFILE";
        if (b.renderBindings.contractVersion) {
            if (const auto* id =
                    OriginalRenderDescriptorUsageContract::validateBudget(b.renderBindings))
                return id;
            if (b.renderBindings.contractVersion != footprintVersion)
                return "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-PROFILE";
            if (general != b.renderBindings.generalShaderResourcesOptIn
                || b.renderBindings.generalShaderResourcesVersion != generalVersion)
                return "RHI-ORIGINAL-USAGE-PROJECTION-GENERAL-PROFILE";
        } else if (b.renderBindings.prototypeOptIn || b.renderBindings.finiteFootprintVersion
            || b.renderBindings.wholeFootprintVersion || b.renderBindings.finiteFullBoundViewOptIn
            || b.renderBindings.wholeFullBoundViewOptIn || b.renderBindings.maxCommands
            || b.renderBindings.maxBindings || b.renderBindings.maxLogicalAccesses
            || b.renderBindings.depthTableFootprintVersion
            || b.renderBindings.depthTableFullBoundViewOptIn || b.renderBindings.maxTextureAccesses
            || b.renderBindings.generalShaderResourcesVersion
            || b.renderBindings.generalShaderResourcesOptIn)
            return "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-INTENT";
        return b.maxCommands && b.maxContributions ? nullptr
                                                   : "RHI-ORIGINAL-USAGE-PROJECTION-BUDGET";
    }
};

// endregion original_typed_usage_projection

// region original_typed_state_bound_usage

// Independent explicit state-bound CPU projection prerequisite for immutable
// typed synchronization decisions. No raw device lifetime or execution grant.
class OriginalTypedStateBoundUsageContract final {
public:
    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        OriginalTypedUsageProjectionContract::Budget projection;
    };

    static constexpr const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion != 1u && request.contractVersion != 2u
            && request.contractVersion != 3u && request.contractVersion != 4u
            && request.contractVersion != 5u && request.contractVersion != 6u)
            return "RHI-ORIGINAL-STATE-BOUND-USAGE-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-STATE-BOUND-USAGE-OPT-IN";
        if (request.projection.contractVersion != request.contractVersion)
            return "RHI-ORIGINAL-STATE-BOUND-USAGE-PROFILE";
        return OriginalTypedUsageProjectionContract::validateBudget(request.projection);
    }
};

// endregion original_typed_state_bound_usage

// region original_typed_dependency_projection

// OriginalDependencyProjection/1 freezes CPU positional clauses only. An exact
// clause is a prerequisite for a separately admitted native operation payload,
// not an ordering/visibility/completion or NativeSync receipt.
class OriginalTypedDependencyProjectionContract final {
public:
    struct Request {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        OriginalTypedStateBoundUsageContract::Request usages;
        std::uint64_t maxDependencies = 0u, maxProjectionPasses = 0u, maxContributionVisits = 0u;
        std::uint64_t maxPairChecks = 0u, maxEndpointContributions = 0u, maxTextureCells = 0u;
    };

    struct FreezeRequest {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        Request projection;
        std::uint32_t maxRetainedOwners = 0u;
        std::uint64_t maxCpuMetadataBytes = 0u;
    };

    static const char* validateFreezeIntent(const FreezeRequest& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 8u)
            return "RHI-ORIGINAL-DEPENDENCY-FREEZE-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-DEPENDENCY-FREEZE-OPT-IN";
        if (request.contractVersion == 8u) {
            if (request.projection.contractVersion != 6u)
                return "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE";
        } else if (request.projection.contractVersion
            != (request.contractVersion == 7u       ? 6u
                    : request.contractVersion == 6u ? 5u
                    : request.contractVersion == 5u ? 4u
                                                    : request.contractVersion))
            return "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE";
        if (const auto* id = validateIntent(request.projection))
            return id;
        if (request.contractVersion == 5u) {
            const auto& projection = request.projection.usages.projection;
            if (projection.geometryUsage.drawConsumptionEmpty())
                return "RHI-ORIGINAL-DEPENDENCY-FREEZE-COMPOSED-GEOMETRY-REQUIRED";
            if (!projection.maxReadbacks || !projection.maxReadbackMips
                || !projection.maxReadbackDeclaredBytes)
                return "RHI-ORIGINAL-DEPENDENCY-FREEZE-COMPOSED-READBACK-REQUIRED";
        }
        if (request.contractVersion == 8u) {
            const auto& projection = request.projection.usages.projection;
            if (!projection.maxAttachmentReferences)
                return "RHI-ORIGINAL-GRAPHICS-FREEZE-ATTACHMENT-REQUIRED";
            if (!projection.geometryUsage.drawConsumptionEmpty() || projection.maxReadbacks
                || projection.maxReadbackMips || projection.maxReadbackDeclaredBytes)
                return "RHI-ORIGINAL-GRAPHICS-FREEZE-COMPOSITION";
        }
        return request.maxRetainedOwners && request.maxCpuMetadataBytes
            ? nullptr
            : "RHI-ORIGINAL-DEPENDENCY-FREEZE-BUDGET";
    }

    // Geometry custody is independently versioned. Closure5 owns index bytes
    // only; Freeze4 requires Closure6's original draw-content join.
    static constexpr bool closureProfileMatches(
        std::uint32_t freezeVersion, std::uint32_t closureVersion) noexcept
    {
        return freezeVersion >= 1u && freezeVersion <= 3u ? closureVersion == freezeVersion + 1u
            : freezeVersion == 4u                         ? closureVersion == 6u
            : freezeVersion == 5u                         ? closureVersion == 7u
                                  : (freezeVersion == 6u && closureVersion == 8u)
                || (freezeVersion == 7u && closureVersion == 9u)
                || (freezeVersion == 8u && closureVersion == 10u);
    }

    static const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion != 1u && request.contractVersion != 2u
            && request.contractVersion != 3u && request.contractVersion != 4u
            && request.contractVersion != 5u && request.contractVersion != 6u)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-OPT-IN";
        if (request.usages.contractVersion != request.contractVersion)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PROFILE";
        if (const auto* id = OriginalTypedStateBoundUsageContract::validateIntent(request.usages))
            return id;
        if (request.contractVersion != 1u && !request.maxTextureCells)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-BUDGET";
        if (!request.maxDependencies || !request.maxProjectionPasses
            || !request.maxContributionVisits || !request.maxEndpointContributions)
            return "RHI-ORIGINAL-DEPENDENCY-PROJECTION-BUDGET";
        return nullptr;
    }
};

// endregion original_typed_dependency_projection

// region original_typed_native_payload

// Independent immutable native-input projection. No seal, reservation, callback,
// encoder or receipt is created by this request. The executor remains closed.
struct OriginalTypedNativePayloadRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    OriginalTypedNativeColdWorkCounts maxWork;
    std::uint64_t maxColorAttachments = 0u;
    std::uint32_t maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    // Payload3 counts original readback commands separately from expanded
    // native mip copies. Legacy profiles require both ceilings to be zero.
    std::uint64_t maxTransferPasses = 0u, maxReadbackCommands = 0u, maxReadbackCopies = 0u;
};

class OriginalTypedNativePayloadContract final {
public:
    using Request = OriginalTypedNativePayloadRequest;
    using Work = OriginalTypedNativeColdWorkCounts;
    static constexpr std::uint32_t additionalOwners = 1u;

    // Payload8 is the independent finite Graphics composition. It prepares
    // immutable native inputs only; geometry, readback and execution stay gated.
    static const char* validateGraphicsProfile(
        const Work& work, std::uint64_t colorAttachments) noexcept
    {
        if (!work.completeGraphicsCounts() || !colorAttachments)
            return "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE";
        return nullptr;
    }

    static const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 8u)
            return "RHI-ORIGINAL-TYPED-PAYLOAD-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-TYPED-PAYLOAD-OPT-IN";
        if (request.contractVersion == 8u)
            if (const auto* id =
                    validateGraphicsProfile(request.maxWork, request.maxColorAttachments))
                return id;
        if (!OriginalTypedNativeColdContract::geometryProfile(request.contractVersion)
            && request.maxWork.hasGeometry())
            return "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-SCHEMA";
        if (!request.maxRetainedOwners || !request.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TYPED-PAYLOAD-OWNER-CPU-BUDGET";
        if (!OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
            && (request.maxTransferPasses || request.maxReadbackCommands
                || request.maxReadbackCopies))
            return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-SCHEMA";
        if (OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
            && (!request.maxTransferPasses || !request.maxReadbackCommands
                || !request.maxReadbackCopies))
            return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-BUDGET";
        const auto& work = request.maxWork;
        if (OriginalTypedNativeColdContract::geometryProfile(request.contractVersion)
            && !work.completeGeometryCounts())
            return "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-BUDGET";
        if (!work.commands || !work.pipelineBinds || !work.descriptorBinds || !work.implicitAccesses
            || !work.nativeCalls || (!work.draws && !work.indexedDraws && !work.dispatches))
            return "RHI-ORIGINAL-TYPED-PAYLOAD-WORK-BUDGET";
        if ((!work.computePasses && work.dispatches)
            || (!work.renderPasses
                && (work.draws || work.hasGeometry() || work.viewports || work.scissors
                    || work.attachmentReferences)))
            return "RHI-ORIGINAL-TYPED-PAYLOAD-PASS-PROFILE";
        return nullptr;
    }

    // Exact independent tuples; no profile grants recording or completion.
    static constexpr bool profileMatches(
        const Request& request, std::uint32_t freezeVersion, std::uint32_t closureVersion) noexcept
    {
        return request.contractVersion == 1u ? freezeVersion == 1u && closureVersion == 2u
            : request.contractVersion == 2u  ? freezeVersion == 2u && closureVersion == 3u
            : request.contractVersion == 3u  ? freezeVersion == 3u && closureVersion == 4u
            : request.contractVersion == 4u  ? freezeVersion == 4u && closureVersion == 6u
            : request.contractVersion == 5u
            ? freezeVersion == 5u && closureVersion == 7u
            : (request.contractVersion == 6u && freezeVersion == 6u && closureVersion == 8u)
                || (request.contractVersion == 7u && freezeVersion == 7u && closureVersion == 9u)
                || (request.contractVersion == 8u && freezeVersion == 8u && closureVersion == 10u);
    }

    static const char* inspectWorkTotals(const Work& work,
        std::uint64_t transfers,
        std::uint64_t readbacks,
        std::uint64_t copies,
        std::uint64_t& commands,
        std::uint64_t& calls) noexcept
    {
        return OriginalTypedNativeColdContract::inspectMixedWorkTotals(
            work, {transfers, readbacks, copies}, commands, calls);
    }

    static const char* validateWork(const Work& work,
        std::uint64_t transfers,
        std::uint64_t readbacks,
        std::uint64_t copies,
        const Request& request) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        if (request.contractVersion == 8u)
            if (const auto* id = validateGraphicsProfile(work, request.maxColorAttachments))
                return id;
        if (!OriginalTypedNativeColdContract::geometryProfile(request.contractVersion)
            && work.hasGeometry())
            return "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-SCHEMA";
        if (!OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
            && (transfers || readbacks || copies))
            return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-SCHEMA";
        if ((OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
                && (!transfers || !readbacks || !copies))
            || copies < readbacks)
            return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-REQUIREMENTS";
        if (transfers > request.maxTransferPasses || readbacks > request.maxReadbackCommands
            || copies > request.maxReadbackCopies)
            return "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-CAPACITY";
        return OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
            ? OriginalTypedNativeColdContract::validateMixedWork(work,
                  {transfers, readbacks, copies},
                  request.maxWork,
                  OriginalTypedNativeColdContract::geometryProfile(request.contractVersion))
            : OriginalTypedNativeColdContract::validateWork(
                  work, request.maxWork, request.contractVersion == 4u);
    }
};

// endregion original_typed_native_payload

// region original_typed_native_cold_command

struct OriginalTypedNativeColdCommandRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommandPayloads = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// Cold2 pairs only with Payload2/Freeze2/Closure3. Neither profile grants
// recording, submission, initialized content or completion authority.
// Independent TypedNativeColdCommand1/2/3/4/5/6/7/8, metadata and connection only.
// Graphics8 requires exact Cold8/Lifecycle5 and the predeclared native ceiling.
// Geometry4 pairs only Cold4 and never borrows a legacy execution profile.
class OriginalTypedNativeColdCommandContract final {
public:
    using Request = OriginalTypedNativeColdCommandRequest;
    static constexpr std::uint32_t additionalOwners = 3u; // wrapper, shared command, owned Impl

    static constexpr const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 8u)
            return "RHI-ORIGINAL-TYPED-COLD-COMMAND-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-TYPED-COLD-COMMAND-OPT-IN";
        if (request.maxCommandPayloads != 1u)
            return "RHI-ORIGINAL-TYPED-COLD-COMMAND-COUNT";
        if (!request.maxRetainedOwners || !request.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TYPED-COLD-COMMAND-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateProfile(
        const Request& request, std::uint32_t admissionVersion) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        return request.contractVersion == admissionVersion
            ? nullptr
            : "RHI-ORIGINAL-TYPED-COLD-COMMAND-ADMISSION-PROFILE";
    }

    static constexpr const char* validateOwners(std::uint32_t admissionOwners,
        const Request& request,
        const OriginalCommandAdmissionBudget& source,
        const OriginalCommandNativeLifecycleBudget& lifecycle,
        std::uint32_t admissionCeiling = 0u,
        std::uint32_t rootOwners = 0u) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        if (admissionOwners > UINT32_MAX - additionalOwners)
            return "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-OVERFLOW";
        const auto owners = admissionOwners + additionalOwners;
        // General initializer actors belong to the native custody domain. The
        // same immutable Cold6/7/8 ceiling carries downstream demand; connection
        // cannot grow it. Sources still bounds the independent CPU root.
        if (OriginalTypedNativeColdContract::generalProfile(request.contractVersion)) {
            if (lifecycle.contractVersion
                    != OriginalTypedNativeColdContract::generalLifecycleVersion(
                        request.contractVersion)
                || !lifecycle.prototypeOptIn)
                return "RHI-ORIGINAL-TYPED-COLD-COMMAND-LIFECYCLE-PROFILE";
            if (!rootOwners || rootOwners > source.maxGraphOwners || !admissionCeiling
                || request.maxRetainedOwners > admissionCeiling)
                return "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY";
        }
        return owners > request.maxRetainedOwners
                || (!OriginalTypedNativeColdContract::generalProfile(request.contractVersion)
                    && owners > source.maxGraphOwners)
                || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
                || owners > lifecycle.maxCallbackOwners
            ? "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY"
            : nullptr;
    }
};

// endregion original_typed_native_cold_command

// region original_typed_callback_custody

enum class OriginalTypedLossObserverPolicy : std::uint32_t {
    Unspecified,
    RetainPhysicalLossUntilLocalDrain1
};

struct OriginalTypedCallbackCustodyRequest {
    struct ReadbackMapIntent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    OriginalTypedLossObserverPolicy lossPolicy = OriginalTypedLossObserverPolicy::Unspecified;
    std::uint32_t maxPendingRecords = 0u, maxCallbackSlots = 0u, maxLocalLossObservers = 0u,
                  maxRetainedOwners = 0u;
    std::uint64_t maxCpuSubmissionBytes = 0u, maxCpuCallbackBytes = 0u;
    ReadbackMapIntent readbackMap;
};

// Explicit independently paired Callback1/2/3/4/5/6/7/8 participant classification.
// Geometry4 reuses the existing eight participants; no mixed transfers/maps.
// Composed5 requires genuine result/map custody as well as geometry counts.
// Original handler/callback are paid once by the original binding; this graph
// reserves seven scope/queue participants and one local observer participant.
class OriginalTypedCallbackCustodyContract final {
public:
    using Request = OriginalTypedCallbackCustodyRequest;

    static constexpr std::uint32_t additionalOwners(const Request& request) noexcept
    {
        return request.maxCallbackSlots <= UINT32_MAX - 3u ? 3u + request.maxCallbackSlots : 0u;
    } // wrapper/state/graph + slots

    struct Participants {
        std::uint32_t sharedNativeLossRegistrations = 0u, sharedNativeLossCallbacks = 0u;
        std::uint32_t commandNativeRegistrations = 0u, commandNativeCallbacks = 0u;
        std::uint32_t localObserverRegistrations = 0u, localObserverCallbacks = 0u;
        std::uint32_t reservedRegistrations = 0u, reservedCallbacks = 0u;
    };

    static constexpr const char* inspect(const OriginalTypedNativeColdRequirements& cold,
        const Request& request,
        Participants& output) noexcept
    {
        output = {};
        if (request.contractVersion < 1u || request.contractVersion > 8u)
            return "RHI-TYPED-CALLBACK-CUSTODY-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-TYPED-CALLBACK-CUSTODY-OPT-IN";
        if (request.lossPolicy
            != OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1)
            return "RHI-TYPED-CALLBACK-CUSTODY-LOSS-POLICY";
        if (cold.contractVersion != request.contractVersion
            || cold.existingDeviceLossCallbacks != 1u || cold.existingDeviceLossRegistrations != 1u
            || cold.callbacks.runtimeLossCallbacks != 1u
            || cold.callbacks.runtimeLossRegistrations != 1u)
            return "RHI-TYPED-CALLBACK-CUSTODY-ORIGINAL-SUBSCRIPTION";
        if (request.contractVersion == 8u && !cold.work.completeGraphicsCounts())
            return "RHI-TYPED-CALLBACK-CUSTODY-GRAPHICS-PROFILE";
        if (OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
                ? (!cold.transfers.passes || !cold.transfers.readbacks || !cold.transfers.copies
                      || cold.transfers.copies < cold.transfers.readbacks)
                : !cold.transfers.empty())
            return "RHI-TYPED-CALLBACK-CUSTODY-TRANSFER-PROFILE";
        if (OriginalTypedNativeColdContract::geometryProfile(request.contractVersion)
                ? !cold.work.completeGeometryCounts()
                : cold.work.hasGeometry())
            return "RHI-TYPED-CALLBACK-CUSTODY-GEOMETRY-PROFILE";
        // Independent receipt custody intent. A larger Source reservation alone
        // cannot authorize mapping or submission of the original program.
        const bool maps = OriginalTypedNativeColdContract::hasReadbackRequirements(cold);
        if (request.contractVersion == 5u && !maps)
            return "RHI-TYPED-CALLBACK-CUSTODY-COMPOSED-READBACK-REQUIRED";
        if (maps) {
            if (request.readbackMap.contractVersion != 1u)
                return "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-VERSION";
            if (!request.readbackMap.prototypeOptIn)
                return "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-OPT-IN";
            const auto& map = cold.readbackMap;
            if (!OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)
                || !cold.readback.resultDeclarations || map.maps != cold.readback.resultDeclarations
                || map.maps > UINT32_MAX / OriginalCommandReadbackMapContract::participantsPerMap
                || map.mapCallbacks != map.maps
                || map.scopeCallbacks
                    != map.maps * OriginalCommandReadbackMapContract::scopeCallbacksPerMap)
                return "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-REQUIREMENTS";
        } else if (request.readbackMap.contractVersion || request.readbackMap.prototypeOptIn)
            return "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-PROFILE";
        const auto baseNative = std::uint64_t(cold.callbacks.recordingScopes)
            + cold.callbacks.submissionScopes + cold.callbacks.queue;
        const auto native = baseNative
            + (maps ? std::uint64_t(cold.readbackMap.mapCallbacks) + cold.readbackMap.scopeCallbacks
                    : 0u);
        if (!cold.callbacks.recordingScopes || !cold.callbacks.submissionScopes
            || cold.callbacks.queue != 1u || native >= UINT32_MAX)
            return "RHI-TYPED-CALLBACK-CUSTODY-NATIVE-PARTICIPANTS";
        const auto total = static_cast<std::uint32_t>(native + 1u);
        if (maps
            && (cold.readbackMap.totalCallbackTickets != total
                || cold.readbackMap.totalCallbackRegistrations != total))
            return "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-REQUIREMENTS";
        if (cold.capacity.contractVersion != 1u || !cold.capacity.prototypeOptIn
            || cold.capacity.pendingSubmissions != 1u || cold.capacity.callbackTickets != total
            || cold.capacity.callbackRegistrations != total || request.maxPendingRecords != 1u
            || request.maxCallbackSlots != total || request.maxLocalLossObservers != 1u)
            return "RHI-TYPED-CALLBACK-CUSTODY-PARTICIPANTS";
        if (!request.maxRetainedOwners || !request.maxCpuSubmissionBytes
            || !request.maxCpuCallbackBytes)
            return "RHI-TYPED-CALLBACK-CUSTODY-BUDGET";
        if (!additionalOwners(request))
            return "RHI-TYPED-CALLBACK-CUSTODY-OWNER-OVERFLOW";
        output = {1u,
            1u,
            static_cast<std::uint32_t>(native),
            static_cast<std::uint32_t>(native),
            1u,
            1u,
            total,
            total};
        return nullptr;
    }

    static constexpr const char* validateOwners(std::uint32_t commandOwners,
        std::uint32_t resultOwners,
        const Request& request,
        std::uint32_t commandCeiling,
        std::uint32_t admissionCeiling,
        const OriginalCommandAdmissionBudget& source,
        const OriginalCommandNativeLifecycleBudget& lifecycle,
        std::uint32_t rootOwners) noexcept
    {
        const auto extra = additionalOwners(request);
        const auto owners = std::uint64_t(commandOwners) + extra + resultOwners;
        if (!extra || owners > UINT32_MAX)
            return "RHI-WEBGPU-TYPED-CALLBACK-OWNER-OVERFLOW";
        if (OriginalTypedNativeColdContract::generalProfile(request.contractVersion)) {
            if (lifecycle.contractVersion
                    != OriginalTypedNativeColdContract::generalLifecycleVersion(
                        request.contractVersion)
                || !lifecycle.prototypeOptIn)
                return "RHI-WEBGPU-TYPED-CALLBACK-LIFECYCLE-PROFILE";
            if (resultOwners || !rootOwners || rootOwners > source.maxGraphOwners
                || request.maxRetainedOwners > commandCeiling
                || request.maxRetainedOwners > admissionCeiling)
                return "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY";
        }
        return owners > request.maxRetainedOwners
                || (!OriginalTypedNativeColdContract::generalProfile(request.contractVersion)
                    && request.maxRetainedOwners > source.maxGraphOwners)
                || owners > commandCeiling || owners > admissionCeiling
                || owners > lifecycle.maxRecordingOwners || owners > lifecycle.maxSubmissionOwners
                || owners > lifecycle.maxCallbackOwners
            ? "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY"
            : nullptr;
    }
};

// endregion original_typed_callback_custody

// region original_typed_issuer_preparation

// Independent TypedIssuerPreparation1/2 admits existing issuer objects and seven
// prepared native receipt slots only. Geometry4 reuses these same slots.
// No native recording/submission is granted by preparation.
struct OriginalTypedIssuerPreparationRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRecordingIssuers = 0u, maxSubmissionIssuers = 0u, maxNativeReceiptSlots = 0u,
                  maxRetainedOwners = 0u;
    std::uint64_t maxRecordingCpuBytes = 0u, maxSubmissionCpuBytes = 0u, maxCallbackCpuBytes = 0u;
};

class OriginalTypedIssuerPreparationContract final {
public:
    using Request = OriginalTypedIssuerPreparationRequest;
    static constexpr std::uint32_t additionalOwners =
        3u; // wrapper + existing Recording + Submission

    static constexpr const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 8u)
            return "RHI-TYPED-ISSUER-PREPARATION-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-TYPED-ISSUER-PREPARATION-OPT-IN";
        if (request.maxRecordingIssuers != 1u || request.maxSubmissionIssuers != 1u
            || request.maxNativeReceiptSlots != 7u)
            return "RHI-TYPED-ISSUER-PREPARATION-PARTICIPANTS";
        if (!request.maxRetainedOwners || !request.maxRecordingCpuBytes
            || !request.maxSubmissionCpuBytes || !request.maxCallbackCpuBytes)
            return "RHI-TYPED-ISSUER-PREPARATION-BUDGET";
        return nullptr;
    }

    static constexpr const char* validateProfile(
        const Request& request, std::uint32_t callbackVersion) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        return request.contractVersion == callbackVersion
            ? nullptr
            : "RHI-TYPED-ISSUER-PREPARATION-CALLBACK-PROFILE";
    }

    static constexpr const char* validateOwners(std::uint32_t callbackOwners,
        const Request& request,
        std::uint32_t callbackCeiling,
        std::uint32_t commandCeiling,
        std::uint32_t admissionCeiling,
        const OriginalCommandAdmissionBudget& source,
        const OriginalCommandNativeLifecycleBudget& lifecycle,
        std::uint32_t rootOwners) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        if (callbackOwners > UINT32_MAX - additionalOwners)
            return "RHI-WEBGPU-TYPED-ISSUER-OWNER-OVERFLOW";
        const auto total = callbackOwners + additionalOwners;
        if (OriginalTypedNativeColdContract::generalProfile(request.contractVersion)) {
            if (lifecycle.contractVersion
                    != OriginalTypedNativeColdContract::generalLifecycleVersion(
                        request.contractVersion)
                || !lifecycle.prototypeOptIn)
                return "RHI-WEBGPU-TYPED-ISSUER-LIFECYCLE-PROFILE";
            if (!rootOwners || rootOwners > source.maxGraphOwners
                || request.maxRetainedOwners > callbackCeiling
                || request.maxRetainedOwners > commandCeiling
                || request.maxRetainedOwners > admissionCeiling)
                return "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY";
        }
        return total > request.maxRetainedOwners
                || (!OriginalTypedNativeColdContract::generalProfile(request.contractVersion)
                    && request.maxRetainedOwners > source.maxGraphOwners)
                || total > callbackCeiling || total > commandCeiling || total > admissionCeiling
                || total > lifecycle.maxRecordingOwners || total > lifecycle.maxSubmissionOwners
                || total > lifecycle.maxCallbackOwners
            ? "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY"
            : nullptr;
    }
};

// endregion original_typed_issuer_preparation

// region original_typed_native_execution

enum class OriginalTypedSynchronizationPolicy { Unspecified, FrozenOriginalScopeVisibility1 };

struct OriginalTypedNativeExecutionRequest {
    struct ReadbackCompletionIntent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    struct GeneralSubmissionIntent {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
    };

    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    OriginalTypedSynchronizationPolicy synchronizationPolicy =
        OriginalTypedSynchronizationPolicy::Unspecified;
    ReadbackCompletionIntent readbackCompletion;
    GeneralSubmissionIntent generalSubmission;
};

class OriginalTypedNativeExecutionContract final {
public:
    using Request = OriginalTypedNativeExecutionRequest;
    // WebGPU supported-limits table, guaranteed minimum, Prototype1/2 ceiling.
    static constexpr std::uint32_t guaranteedDispatchDimension = 65535u;

    static constexpr const char* validateIntent(const Request& request) noexcept
    {
        if (request.contractVersion < 1u || request.contractVersion > 8u)
            return "RHI-TYPED-NATIVE-EXECUTION-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-TYPED-NATIVE-EXECUTION-OPT-IN";
        if (request.generalSubmission.contractVersion || request.generalSubmission.prototypeOptIn) {
            if (request.contractVersion != 6u && request.contractVersion != 7u
                && request.contractVersion != 8u)
                return "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-PROFILE";
            if (request.generalSubmission.contractVersion
                != (request.contractVersion == 8u       ? 3u
                        : request.contractVersion == 7u ? 2u
                                                        : 1u))
                return "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-VERSION";
            if (!request.generalSubmission.prototypeOptIn)
                return "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-OPT-IN";
        }
        if (request.contractVersion == 5u && !request.readbackCompletion.contractVersion
            && !request.readbackCompletion.prototypeOptIn)
            return "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED";
        if (request.readbackCompletion.contractVersion
            || request.readbackCompletion.prototypeOptIn) {
            if (!OriginalTypedNativeColdContract::readbackProfile(request.contractVersion))
                return "RHI-TYPED-NATIVE-READBACK-COMPLETION-PROFILE";
            if (request.readbackCompletion.contractVersion != 1u)
                return "RHI-TYPED-NATIVE-READBACK-COMPLETION-VERSION";
            if (!request.readbackCompletion.prototypeOptIn)
                return "RHI-TYPED-NATIVE-READBACK-COMPLETION-OPT-IN";
        }
        return request.synchronizationPolicy
                == OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1
            ? nullptr
            : "RHI-TYPED-NATIVE-EXECUTION-SYNCHRONIZATION-POLICY";
    }

    // Both record and submit consume the exact independently opted-in issuer
    // profile. Execution2 cannot borrow a descriptor-set-only Execution1 chain.
    static constexpr const char* validateProfile(
        const Request& request, std::uint32_t issuerVersion) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        return request.contractVersion == issuerVersion
            ? nullptr
            : "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE";
    }

    static constexpr const char* validateReadbackProfile(const Request& request,
        const OriginalTypedNativeColdRequirements& cold,
        const OriginalTypedCallbackCustodyRequest& custody) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        const bool demand = OriginalTypedNativeColdContract::hasReadbackRequirements(cold);
        if (!request.readbackCompletion.contractVersion)
            return demand ? "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED" : nullptr;
        OriginalTypedCallbackCustodyContract::Participants participants;
        if (const auto* id =
                OriginalTypedCallbackCustodyContract::inspect(cold, custody, participants))
            return id;
        return cold.contractVersion == request.contractVersion && demand
                && custody.readbackMap.contractVersion == 1u && custody.readbackMap.prototypeOptIn
            ? nullptr
            : "RHI-TYPED-NATIVE-READBACK-COMPLETION-BINDING";
    }

    static constexpr const char* validateRecordedGeneralSubmission(
        const Request& request, std::uint32_t recordedVersion) noexcept
    {
        if (const auto* id = validateIntent(request))
            return id;
        if (recordedVersion == UINT32_MAX)
            return "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-UNRECORDED";
        return recordedVersion == request.generalSubmission.contractVersion
            ? nullptr
            : "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-RECORDED-PROFILE";
    }

    static constexpr const char* validateDispatch(
        std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
    {
        return x <= guaranteedDispatchDimension && y <= guaranteedDispatchDimension
                && z <= guaranteedDispatchDimension
            ? nullptr
            : "RHI-TYPED-NATIVE-EXECUTION-DISPATCH-LIMIT";
    }
};
} // namespace arp::rhi

// endregion original_typed_native_execution
