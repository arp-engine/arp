#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalTypedNativeColdContractFixture final {
    using Contract = rhi::OriginalTypedNativeColdContract;
    using Request = Contract::Request;
    using Required = Contract::Requirements;
    using Work = Contract::Work;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static Required required(bool graphics = false)
    {
        Required r;
        r.contractVersion = 1u;
        r.work.computePasses = graphics ? 0u : 2u;
        r.work.renderPasses = graphics ? 2u : 0u;
        r.work.pipelineBinds = r.work.descriptorBinds = 3u;
        r.work.draws = graphics ? 3u : 0u;
        r.work.dispatches = graphics ? 0u : 3u;
        r.work.dependencies = 6u;
        r.work.dynamicOffsets = 3u;
        r.work.implicitAccesses = 6u;
        r.work.viewports = r.work.scissors = graphics ? 2u : 0u;
        r.work.attachmentReferences = graphics ? 2u : 0u;
        assert(!Contract::inspectWorkTotals(r.work, r.work.commands, r.work.nativeCalls));
        r.encoders = r.commandBuffers = 1u;
        r.callbacks = {3u, 3u, 1u, 1u, 1u};
        r.existingDeviceLossCallbacks = r.existingDeviceLossRegistrations = 1u;
        r.capacity = {1u, true, 1u, 8u, 8u};
        r.retainedOwners = 64u;
        r.cpuMetadataBytes = 65536u;
        return r;
    }

    static Required graphics8Required()
    {
        auto r = required(true);
        r.contractVersion = 8u;
        r.work = {};
        auto& work = r.work;
        work.renderPasses = work.pipelineBinds = work.descriptorBinds = work.draws = 1u;
        work.viewports = work.scissors = work.attachmentReferences = 1u;
        work.dependencies = 6u;
        work.implicitAccesses = 8u;
        assert(!Contract::inspectWorkTotals(work, work.commands, work.nativeCalls));
        return r;
    }

    static Request request(const Required& r)
    {
        Request b;
        b.contractVersion = r.contractVersion;
        b.prototypeOptIn = true;
        b.synchronizationVersion = 1u;
        b.synchronization =
            rhi::OriginalTypedNativeColdSynchronization::OriginalUsageScopeBoundaries1;
        b.maxWork = r.work;
        b.maxCallbacks = r.callbacks;
        b.maxEncoders = b.maxCommandBuffers = 1u;
        b.maxRetainedOwners = 64u;
        b.maxCpuMetadataBytes = 65536u;
        return b;
    }

    static void verifyContract()
    {
        is(Contract::validateIntent({}), "RHI-ORIGINAL-TYPED-COLD-VERSION");
        for (const auto version : {1u, 2u})
            for (const bool graphics : {false, true}) {
                auto r = required(graphics);
                r.contractVersion = version;
                auto b = request(r);
                assert(!Contract::validateRequirements(r, b));
                assert(r.work.commands == (graphics ? 24u : 20u)
                    && r.work.nativeCalls == (graphics ? 19u : 15u));
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
                    &Work::nativeCalls};
                for (const auto member : members) {
                    if (!(r.work.*member))
                        continue;
                    auto less = b;
                    --(less.maxWork.*member);
                    assert(Contract::validateRequirements(r, less));
                    less.maxWork.*member = 0u;
                    assert(Contract::validateRequirements(r, less));
                }
                auto invalid = b;
                invalid.contractVersion = 9u;
                is(Contract::validateIntent(invalid), "RHI-ORIGINAL-TYPED-COLD-VERSION");
                if (!graphics) {
                    invalid = b;
                    invalid.contractVersion = 8u;
                    is(Contract::validateIntent(invalid), "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE");
                }
                auto foreign = r;
                foreign.contractVersion = version == 1u ? 2u : 1u;
                is(Contract::validateRequirements(foreign, b),
                    "RHI-ORIGINAL-TYPED-COLD-REQUIREMENTS-VERSION");
                assert(!Contract::validatePayloadProfile(b, version));
                assert(!Contract::validateLifecycleProfile(b, 1u));
                for (const auto wrong : {0u, 2u, 3u})
                    is(Contract::validateLifecycleProfile(b, wrong),
                        "RHI-WEBGPU-TYPED-COLD-LIFECYCLE-PROFILE");
                is(Contract::validatePayloadProfile(b, foreign.contractVersion),
                    "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE");
                invalid = b;
                invalid.prototypeOptIn = false;
                is(Contract::validateIntent(invalid), "RHI-ORIGINAL-TYPED-COLD-OPT-IN");
                invalid = b;
                invalid.synchronizationVersion = 0u;
                is(Contract::validateIntent(invalid),
                    "RHI-ORIGINAL-TYPED-COLD-SYNCHRONIZATION-POLICY");
                invalid = b;
                invalid.synchronization =
                    static_cast<rhi::OriginalTypedNativeColdSynchronization>(255u);
                is(Contract::validateIntent(invalid),
                    "RHI-ORIGINAL-TYPED-COLD-SYNCHRONIZATION-POLICY");
                invalid = b;
                invalid.maxWork.computePasses = invalid.maxWork.renderPasses = 0u;
                is(Contract::validateIntent(invalid), "RHI-ORIGINAL-TYPED-COLD-PASS-PROFILE");
                invalid = b;
                invalid.maxCpuMetadataBytes = 0u;
                is(Contract::validateIntent(invalid), "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-BUDGET");
                auto inconsistent = r;
                ++inconsistent.work.commands;
                is(Contract::validateRequirements(inconsistent, b),
                    "RHI-ORIGINAL-TYPED-COLD-COUNT-TOTALS");
                inconsistent = r;
                ++inconsistent.work.nativeCalls;
                is(Contract::validateRequirements(inconsistent, b),
                    "RHI-ORIGINAL-TYPED-COLD-COUNT-TOTALS");
                inconsistent = r;
                inconsistent.work.dependencies = UINT64_MAX;
                is(Contract::validateRequirements(inconsistent, b),
                    "RHI-ORIGINAL-TYPED-COLD-COUNT-OVERFLOW");
                std::uint64_t commands = 99u, calls = 99u;
                assert(Contract::inspectWorkTotals(inconsistent.work, commands, calls) && !commands
                    && !calls);
                auto maximum = Work{};
                maximum.dependencies = UINT64_MAX - 1u;
                assert(!Contract::inspectWorkTotals(maximum, commands, calls)
                    && commands == UINT64_MAX && calls == 2u);
                maximum.dependencies = 0u;
                maximum.draws = UINT64_MAX - 1u;
                commands = calls = 99u;
                assert(
                    Contract::inspectWorkTotals(maximum, commands, calls) && !commands && !calls);
                for (auto member : {&rhi::OriginalTypedNativeColdCallbackCounts::recordingScopes,
                         &rhi::OriginalTypedNativeColdCallbackCounts::submissionScopes}) {
                    invalid = b;
                    --(invalid.maxCallbacks.*member);
                    is(Contract::validateRequirements(r, invalid),
                        "RHI-ORIGINAL-TYPED-COLD-CALLBACK-CAPACITY");
                }
                inconsistent = r;
                --inconsistent.capacity.callbackTickets;
                is(Contract::validateRequirements(inconsistent, b),
                    "RHI-ORIGINAL-TYPED-COLD-CALLBACK-RESERVATION");
                inconsistent = r;
                inconsistent.callbacks.recordingScopes = UINT32_MAX;
                inconsistent.callbacks.submissionScopes = UINT32_MAX;
                auto huge = b;
                huge.maxCallbacks = inconsistent.callbacks;
                is(Contract::validateRequirements(inconsistent, huge),
                    "RHI-ORIGINAL-TYPED-COLD-CALLBACK-RESERVATION");
                inconsistent = r;
                --inconsistent.existingDeviceLossRegistrations;
                is(Contract::validateRequirements(inconsistent, b),
                    "RHI-ORIGINAL-TYPED-COLD-NATIVE-OBJECT-REQUIREMENTS");
                invalid = b;
                --invalid.maxRetainedOwners;
                is(Contract::validateRequirements(r, invalid),
                    "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-CAPACITY");
                invalid = b;
                --invalid.maxCpuMetadataBytes;
                is(Contract::validateRequirements(r, invalid),
                    "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-CAPACITY");
                inconsistent = r;
                inconsistent.retainedOwners = 0u;
                is(Contract::validateRequirements(inconsistent, b),
                    "RHI-ORIGINAL-TYPED-COLD-OWNER-CPU-REQUIREMENTS");
            }
        // General6 binds exactly Payload6/Lifecycle3; it cannot borrow geometry,
        // readback, or a legacy lifecycle profile even with valid count values.
        auto general = required();
        general.contractVersion = 6u;
        auto generalRequest = request(general);
        assert(!Contract::validateRequirements(general, generalRequest));
        assert(!Contract::validatePayloadProfile(generalRequest, 6u));
        assert(!Contract::validateLifecycleProfile(generalRequest, 3u));
        // This value fixture authors native218 + seal/reservation/admission5
        // independently of its CPU root46/ceiling48. They cannot substitute.
        generalRequest.maxRetainedOwners = 223u;
        assert(!Contract::validateOwnerCapacity(generalRequest, 218u, 218u, 5u, 46u, 48u));
        auto shortGeneral = generalRequest;
        --shortGeneral.maxRetainedOwners;
        assert(Contract::validateOwnerCapacity(shortGeneral, 218u, 218u, 5u, 46u, 48u));
        auto expandedGeneral = generalRequest;
        ++expandedGeneral.maxRetainedOwners;
        assert(Contract::validateOwnerCapacity(expandedGeneral, 218u, 218u, 5u, 46u, 48u));
        assert(Contract::validateOwnerCapacity(generalRequest, 218u, 218u, 5u, 49u, 48u));
        assert(
            Contract::validateOwnerCapacity(generalRequest, UINT32_MAX, UINT32_MAX, 5u, 46u, 48u));
        assert(Contract::validateOwnerCapacity(generalRequest, 218u, UINT32_MAX, 5u, 46u, 48u));
        auto legacyOwners = generalRequest;
        legacyOwners.contractVersion = 2u;
        assert(Contract::validateOwnerCapacity(legacyOwners, 218u, 218u, 5u, 46u, 48u));
        for (const auto old : {0u, 1u, 2u, 3u, 4u, 5u, 7u})
            assert(Contract::validatePayloadProfile(generalRequest, old));
        for (const auto old : {0u, 1u, 2u, 4u})
            assert(Contract::validateLifecycleProfile(generalRequest, old));
        auto invalidGeneral = generalRequest;
        invalidGeneral.maxWork.vertexBinds = 1u;
        assert(Contract::validateIntent(invalidGeneral));
        invalidGeneral = generalRequest;
        invalidGeneral.maxTransfers.passes = 1u;
        assert(Contract::validateIntent(invalidGeneral));
        invalidGeneral = generalRequest;
        invalidGeneral.readback.contractVersion = 1u;
        assert(Contract::validateIntent(invalidGeneral));
        auto geometry = required(true);
        geometry.contractVersion = 4u;
        geometry.work = {};
        geometry.work.renderPasses = geometry.work.pipelineBinds = geometry.work.descriptorBinds =
            2u;
        geometry.work.indexBinds = 2u;
        geometry.work.indexedDraws = 3u;
        geometry.work.dependencies = 2u;
        geometry.work.implicitAccesses = 10u;
        geometry.work.attachmentReferences = 2u;
        geometry.work.viewports = geometry.work.scissors = 2u;
        assert(!Contract::inspectWorkTotals(
            geometry.work, geometry.work.commands, geometry.work.nativeCalls));
        assert(geometry.work.commands == 20u && geometry.work.nativeCalls == 19u);
        auto geometryRequest = request(geometry);
        assert(!Contract::validateRequirements(geometry, geometryRequest));
        assert(!Contract::validatePayloadProfile(geometryRequest, 4u));
        assert(!Contract::validateLifecycleProfile(geometryRequest, 1u));
        for (const auto version : {0u, 1u, 2u, 3u, 5u})
            is(Contract::validatePayloadProfile(geometryRequest, version),
                "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE");
        for (const auto version : {0u, 2u, 3u})
            is(Contract::validateLifecycleProfile(geometryRequest, version),
                "RHI-WEBGPU-TYPED-COLD-LIFECYCLE-PROFILE");
        for (const auto member : {&Work::indexBinds,
                 &Work::indexedDraws,
                 &Work::viewports,
                 &Work::scissors,
                 &Work::nativeCalls}) {
            auto less = geometryRequest;
            --(less.maxWork.*member);
            is(Contract::validateRequirements(geometry, less),
                "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        }
        auto geometryBad = geometryRequest;
        geometryBad.maxWork.indexBinds = 0u;
        is(Contract::validateIntent(geometryBad), "RHI-ORIGINAL-TYPED-COLD-GEOMETRY-BUDGET");
        geometryBad = geometryRequest;
        geometryBad.maxWork.renderPasses = 0u;
        is(Contract::validateIntent(geometryBad), "RHI-ORIGINAL-TYPED-COLD-PASS-PROFILE");
        geometryBad = geometryRequest;
        geometryBad.maxTransfers.passes = 1u;
        is(Contract::validateIntent(geometryBad), "RHI-ORIGINAL-TYPED-COLD-TRANSFER-SCHEMA");
        for (const auto version : {1u, 2u, 3u}) {
            geometryBad = geometryRequest;
            geometryBad.contractVersion = version;
            is(Contract::validateIntent(geometryBad), "RHI-ORIGINAL-TYPED-COLD-GEOMETRY-SCHEMA");
        }
        auto mixed = required(true);
        mixed.contractVersion = 3u;
        mixed.transfers = {2u, 3u, 4u};
        mixed.work.commands += 7u;
        mixed.work.nativeCalls += 4u;
        auto mixedRequest = request(mixed);
        mixedRequest.maxTransfers = mixed.transfers;
        assert(!Contract::validateRequirements(mixed, mixedRequest));
        assert(!Contract::validatePayloadProfile(mixedRequest, 3u));
        assert(!Contract::validateLifecycleProfile(mixedRequest, 2u));
        for (const auto wrong : {0u, 1u, 3u})
            is(Contract::validateLifecycleProfile(mixedRequest, wrong),
                "RHI-WEBGPU-TYPED-COLD-LIFECYCLE-PROFILE");
        for (const auto wrong : {1u, 2u, 4u})
            is(Contract::validatePayloadProfile(mixedRequest, wrong),
                "RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE");
        auto overflowing = mixed.work;
        std::uint64_t mixedCommands = 99u, mixedCalls = 99u;
        is(Contract::inspectMixedWorkTotals(
               overflowing, {UINT64_MAX, 1u, 1u}, mixedCommands, mixedCalls),
            "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-OVERFLOW");
        assert(!mixedCommands && !mixedCalls);
        mixedCommands = mixedCalls = 99u;
        is(Contract::inspectMixedWorkTotals(
               overflowing, {1u, 1u, UINT64_MAX}, mixedCommands, mixedCalls),
            "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-OVERFLOW");
        assert(!mixedCommands && !mixedCalls);
        for (const auto member : {&rhi::OriginalTypedNativeTransferCounts::passes,
                 &rhi::OriginalTypedNativeTransferCounts::readbacks,
                 &rhi::OriginalTypedNativeTransferCounts::copies}) {
            auto shortBudget = mixedRequest;
            --(shortBudget.maxTransfers.*member);
            is(Contract::validateRequirements(mixed, shortBudget),
                "RHI-ORIGINAL-TYPED-COLD-TRANSFER-CAPACITY");
            shortBudget = mixedRequest;
            shortBudget.maxTransfers.*member = 0u;
            is(Contract::validateIntent(shortBudget), "RHI-ORIGINAL-TYPED-COLD-TRANSFER-BUDGET");
        }
        auto legacyTransfer = mixedRequest;
        legacyTransfer.contractVersion = 2u;
        is(Contract::validateIntent(legacyTransfer), "RHI-ORIGINAL-TYPED-COLD-TRANSFER-SCHEMA");
        auto emptyTransfer = mixed;
        emptyTransfer.transfers = {};
        is(Contract::validateRequirements(emptyTransfer, mixedRequest),
            "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-REQUIREMENTS");
        auto inconsistentMixed = mixed;
        --inconsistentMixed.work.commands;
        is(Contract::validateRequirements(inconsistentMixed, mixedRequest),
            "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-TOTALS");
        auto mapped = mixed;
        auto mappedRequest = mixedRequest;
        mapped.readback = {3u, 4u, 2u, 1536u, 1024u, 65536u};
        mapped.readbackMap = {2u, 2u, 6u, 16u, 16u};
        mapped.capacity.callbackTickets = mapped.capacity.callbackRegistrations = 16u;
        mappedRequest.readback = {2u, true, 3u, 4u, 2u, 1536u, 1024u, 65536u};
        mappedRequest.readbackMap = {1u, true, 2u, 2u, 6u, 16u, 16u};
        assert(!Contract::validateRequirements(mapped, mappedRequest));
        assert(mapped.matches(mapped) && !mapped.matches(mixed));
        auto shortMap = mappedRequest;
        --shortMap.readbackMap.maxTotalCallbackTickets;
        is(Contract::validateRequirements(mapped, shortMap),
            "RHI-ORIGINAL-READBACK-MAP-CALLBACK-CAPACITY");
        shortMap = mappedRequest;
        shortMap.readbackMap.prototypeOptIn = false;
        is(Contract::validateIntent(shortMap), "RHI-ORIGINAL-READBACK-MAP-OPT-IN");
        shortMap = mappedRequest;
        shortMap.readback = {};
        is(Contract::validateIntent(shortMap), "RHI-ORIGINAL-MIXED-READBACK-RESULT-VERSION");
        shortMap = mappedRequest;
        shortMap.readbackMap = {};
        is(Contract::validateIntent(shortMap), "RHI-ORIGINAL-READBACK-MAP-VERSION");
        shortMap = mappedRequest;
        shortMap.contractVersion = 2u;
        is(Contract::validateIntent(shortMap), "RHI-ORIGINAL-TYPED-COLD-READBACK-PROFILE");
        auto forgedMap = mapped;
        forgedMap.capacity.callbackTickets = 8u;
        is(Contract::validateRequirements(forgedMap, mappedRequest),
            "RHI-ORIGINAL-TYPED-COLD-CALLBACK-RESERVATION");
        forgedMap = mapped;
        --forgedMap.readback.readbackCommands;
        is(Contract::validateRequirements(forgedMap, mappedRequest),
            "RHI-ORIGINAL-TYPED-COLD-READBACK-COVERAGE");
        forgedMap = mapped;
        --forgedMap.readbackMap.totalCallbackRegistrations;
        is(Contract::validateRequirements(forgedMap, mappedRequest),
            "RHI-ORIGINAL-READBACK-MAP-REQUIREMENTS");
        is(Contract::validateRequirements(mapped, mixedRequest),
            "RHI-ORIGINAL-TYPED-COLD-READBACK-PROFILE");
        rhi::OriginalTypedCallbackCustodyContract::Participants mapParticipants;
        is(rhi::OriginalTypedCallbackCustodyContract::inspect(mapped,
               {3u,
                   true,
                   rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                   1u,
                   16u,
                   1u,
                   128u,
                   65536u,
                   65536u},
               mapParticipants),
            "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-VERSION");
        assert(!mapParticipants.reservedCallbacks);
        assert(!rhi::OriginalTypedCallbackCustodyContract::inspect(mapped,
            {3u,
                true,
                rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                1u,
                16u,
                1u,
                128u,
                65536u,
                65536u,
                {1u, true}},
            mapParticipants));
        assert(mapParticipants.reservedCallbacks == 16u
            && mapParticipants.commandNativeCallbacks == 15u);
        const rhi::OriginalCommandReadbackResultStorageContract::Budget typedStorage{
            2u, true, 2u, 1024u, 65536u, 68u};
        assert(!rhi::OriginalCommandReadbackResultStorageContract::validateTypedRequirements(
            mapped.readback, 64u, typedStorage));
        is(rhi::OriginalCommandReadbackResultStorageContract::validateIntent(typedStorage),
            "RHI-ORIGINAL-READBACK-STORAGE-VERSION");
        const auto r = required();
        const auto b = request(r);
        auto quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuMetadataBytes);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto storage =
                        rhi::semantics::OriginalCommandReadbackResultStorageLowering::typedLowering(
                            backend, 2u);
                    assert(storage.coldStorageAvailable
                        == (backend == rhi::Backend::Null || backend == rhi::Backend::WebGPU));
                    assert(!storage.mapAvailable && !storage.publicationAvailable
                        && storage.diagnostic);
                    assert(!rhi::semantics::OriginalCommandReadbackResultStorageLowering::
                            typedLowering(backend, 1u)
                                .coldStorageAvailable);
                    const auto d =
                        arp::rhi::semantics::OriginalTypedNativeColdLowering::lowering(backend);
                    assert(!d.coldJoinAvailable && !d.recordingAvailable && !d.executionAvailable
                        && d.diagnostic);
                    assert((d.disposition
                               == arp::rhi::semantics::OriginalCommandNativeClosureLowering::
                                   Disposition::ValidatorOnly)
                        == (backend == rhi::Backend::Null));
                    const auto legacy =
                        arp::rhi::semantics::OriginalCommandNativeColdLowering::typedLowering(
                            backend);
                    is(d.diagnostic, legacy.diagnostic);
                }
                assert(arp::rhi::semantics::OriginalTypedNativeColdLowering::lowering(
                    static_cast<rhi::Backend>(255u))
                        .diagnostic);
                return Contract::validateRequirements(r, b);
            }));
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_COLD_VALUE_CONTRACT_OK request1 request2 paired-requirements explicit-sync-policy compute-and-render independent-counts callbacks owners CPU exact-less-zero overflow-cleared full-pressure hot0 four-native-lowerings-closed no-provenance-or-sync-proof");
    }
};
} // namespace arp::test
