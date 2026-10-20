#pragma once
#include "rhi/backends/webgpu/webgpu_texture_readback_cold_budget_lowering.h"
#include "texture_readback_source_lease_fixture.h"
#include "texture_readback_statistics_plan_fixture.h"

namespace arp::test {
class TextureReadbackColdBudgetFixture final {
public:
    using Plan = rhi::TextureReadbackColdBudgetPlan;
    using Budget = rhi::TextureReadbackColdBudget;
    using Lowering = rhi_webgpu::WebGpuTextureReadbackColdBudgetLowering;
    using Recipe = Lowering::Recipe;
    static constexpr std::uint64_t cpuBytes = 262144u;

    // Authored test ceilings; native facts are independently validated below.
    static Budget budget()
    {
        Budget b;
        b.contractVersion = 1u;
        b.prototypeOptIn = true;
        b.maxPendingRequests = 1u;
        b.maxResourceOwners = 6u;
        b.maxMipCopies = 2u;
        b.maxStatisticsViews = 4u;
        b.maxOriginalCommands = 5u;
        b.maxNativeCommands = 4u;
        b.maxEncoders = 1u;
        b.maxCommandBuffers = 1u;
        b.maxDebugScopes = 2u;
        b.maxDebugDepth = 1u;
        b.maxStateControls = 3u;
        b.maxStateTransitions = 2u;
        b.maxCreationCallbacks =
            rhi_webgpu::WebGpuOriginalTextureReadbackDestination::callbackCount;
        b.maxRecordingCallbacks = rhi_webgpu::WebGpuOrdinaryRecording::callbackCount;
        b.maxSubmissionCallbacks = rhi_webgpu::WebGpuOrdinarySubmission::callbackCount;
        b.maxMappingCallbacks = rhi_webgpu::WebGpuOriginalTextureReadbackMapping::callbackCount
            + rhi_webgpu::WebGpuOriginalTextureReadbackMapping::scopeCallbackCount;
        b.maxSourceAllocationBytes = 2896u;
        b.maxNativeBytesPerRequest = 4432u;
        b.maxContentBytesPerRequest = 1696u;
        b.maxStagingBytesPerRequest = 1536u;
        b.maxResultBytesPerRequest = 1536u;
        b.maxLabelBytesPerRequest = 128u;
        b.maxAnalyzedPixelsPerRequest = 20u;
        b.maxDeviceStagingBytes = b.maxStagingBytesPerRequest;
        b.maxDeviceRetainedNativeBytes = b.maxNativeBytesPerRequest;
        b.maxDeviceRetainedContentBytes = b.maxContentBytesPerRequest;
        b.maxDeviceRetainedResultBytes = b.maxResultBytesPerRequest;
        b.maxDeviceRetainedLabelBytes = b.maxLabelBytesPerRequest;
        b.maxCpuDeviceBytes = b.maxCpuRequestBytes = b.maxCpuResourceBytes = b.maxCpuResultBytes =
            cpuBytes;
        b.deviceLoss = {1u, true, 1u, 1u, cpuBytes};
        // Independent authored complete-graph ceilings; the template validates
        // positional minima. Pass label12 + request label128 = text140 bytes.
        b.graph = {1u, true, 1u, 9u, 3u, 3u, 2u, 1u, 1u, 3u, 2u, 1u, 3u, 140u, 12u, cpuBytes};
        return b;
    }

    static Plan::Quotas quotas(const Budget& b)
    {
        return {core::makeShared<core::CpuMemoryQuota>(b.maxCpuDeviceBytes),
            core::makeShared<core::CpuMemoryQuota>(b.maxCpuRequestBytes),
            core::makeShared<core::CpuMemoryQuota>(b.maxCpuResourceBytes),
            core::makeShared<core::CpuMemoryQuota>(b.maxCpuResultBytes)};
    }

    static Budget publicPairBudget()
    {
        // Authored two-request ceiling for two 2x2 RGBA8 padded snapshots.
        auto b = budget();
        b.maxPendingRequests = 2u;
        b.maxStagingBytesPerRequest = b.maxResultBytesPerRequest = 512u;
        b.maxDeviceStagingBytes = b.maxDeviceRetainedResultBytes = 1024u;
        b.maxDeviceRetainedNativeBytes = b.maxNativeBytesPerRequest * 2u;
        b.maxDeviceRetainedContentBytes = b.maxContentBytesPerRequest * 2u;
        b.maxDeviceRetainedLabelBytes = b.maxLabelBytesPerRequest * 2u;
        return b;
    }

    static Recipe prepare(const Budget& b = budget())
    {
        const char* diagnostic = nullptr;
        auto plan = Plan::prepareOriginal(b, quotas(b), diagnostic);
        assert(plan);
        Recipe recipe;
        const auto* id = Lowering::lowerOriginal(plan, recipe);
        if (id)
            std::printf("COLD_BUDGET_LOWERING %s\n", id);
        assert(!id);
        return recipe;
    }

    static void verify()
    {
        {
            auto pair = prepare(publicPairBudget());
            const auto domains = pair.originalPlan->quotas();
            assert(pair.publication.maxPendingResults == 2u
                && pair.originalPlan->budget().maxDeviceStagingBytes == 1024u);
            pair = {};
            for (const auto& domain : domains)
                assert(!domain->liveBytes());
        }
        const char* diagnostic = nullptr;
        auto b = budget();
        auto q = quotas(b);
        const auto factory = [&] {
            return Plan::prepareOriginal(b, q, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(q[0]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease full(q[0], cpuBytes);
            assert(full.charged() && !factory());
        }
        b.contractVersion = 0u;
        assert(!factory());
        b = budget();
        b.prototypeOptIn = false;
        assert(!factory());
        b = budget();
        for (auto* field : {&b.maxPendingRequests,
                 &b.maxResourceOwners,
                 &b.maxMipCopies,
                 &b.maxStatisticsViews,
                 &b.maxOriginalCommands,
                 &b.maxNativeCommands,
                 &b.maxEncoders,
                 &b.maxCommandBuffers,
                 &b.maxDebugScopes,
                 &b.maxStateControls,
                 &b.maxStateTransitions,
                 &b.maxCreationCallbacks,
                 &b.maxRecordingCallbacks,
                 &b.maxSubmissionCallbacks,
                 &b.maxMappingCallbacks}) {
            auto saved = *field;
            *field = 0u;
            assert(!factory());
            *field = saved;
        }
        for (auto* field : {&b.maxSourceAllocationBytes,
                 &b.maxNativeBytesPerRequest,
                 &b.maxContentBytesPerRequest,
                 &b.maxStagingBytesPerRequest,
                 &b.maxResultBytesPerRequest,
                 &b.maxAnalyzedPixelsPerRequest,
                 &b.maxDeviceStagingBytes,
                 &b.maxDeviceRetainedNativeBytes,
                 &b.maxDeviceRetainedContentBytes,
                 &b.maxDeviceRetainedResultBytes,
                 &b.maxCpuDeviceBytes,
                 &b.maxCpuRequestBytes,
                 &b.maxCpuResourceBytes,
                 &b.maxCpuResultBytes}) {
            auto saved = *field;
            *field = 0u;
            assert(!factory());
            *field = saved;
        }
        for (auto* field : {&b.maxDeviceStagingBytes,
                 &b.maxDeviceRetainedNativeBytes,
                 &b.maxDeviceRetainedContentBytes,
                 &b.maxDeviceRetainedResultBytes,
                 &b.maxDeviceRetainedLabelBytes}) {
            --*field;
            assert(!factory());
            ++*field;
        }
        for (std::size_t i = 0; i < q.size(); ++i) {
            auto saved = q[i];
            q[i].reset();
            assert(!factory());
            q[i] = core::makeShared<core::CpuMemoryQuota>(cpuBytes - 1u);
            assert(!factory());
            if (i) {
                q[i] = q[0];
                assert(!factory()
                    && std::string_view(diagnostic) == "RHI-READBACK-COLD-BUDGET-QUOTA-ALIAS");
            }
            q[i] = saved;
        }

        struct AliasDomains {
            core::CpuMemoryQuota first{cpuBytes}, second{cpuBytes};
        };

        {
            auto owners = core::makeShared<AliasDomains>();
            auto saved = q;
            q[0] = std::shared_ptr<core::CpuMemoryQuota>(owners, &owners->first);
            q[1] = std::shared_ptr<core::CpuMemoryQuota>(owners, &owners->second);
            assert(!factory()
                && std::string_view(diagnostic) == "RHI-READBACK-COLD-BUDGET-QUOTA-ALIAS");
            q = saved;
        }
        if (SIZE_MAX < UINT64_MAX) {
            b.maxCpuResultBytes = UINT64_MAX;
            assert(Plan::validateBudget(b));
            b = budget();
        }
        b.maxResultBytesPerRequest = b.maxCpuResultBytes + 1u;
        b.maxDeviceRetainedResultBytes = b.maxResultBytesPerRequest;
        assert(Plan::validateBudget(b));
        b = budget();
        auto plan = factory();
        assert(plan && !plan->nativeExecutionProven());
        b.maxPendingRequests = 2u;
        assert(plan->budget().maxPendingRequests == 1u);
        b = budget();
        verifyDeviceLossBudget(b, q);
        verifyGraphBudget(b, q);
        Recipe recipe;
        auto forged = std::shared_ptr<const Plan>(q[1], plan.get());
        assert(Lowering::lowerOriginal(forged, recipe) && !recipe.originalPlan);
        forged.reset();
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Lowering::lowerOriginal(plan, recipe) && !recipe.nativeExecutionProven());
        assert(Lowering::lowerOriginal(plan, recipe));
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::pmr::set_default_resource(previous);
        assert(recipe.input.maxCpuMetadataBytes == cpuBytes
            && recipe.destination.maxCpuResourceBytes == cpuBytes
            && recipe.destination.maxCpuResultBytes == cpuBytes);
        assert(recipe.publication.maxPendingResults == 1u
            && recipe.publication.maxMapCallbacks == b.maxMappingCallbacks);
        assert(recipe.mapping.maxCallbacks
            == rhi_webgpu::WebGpuOriginalTextureReadbackMapping::callbackCount);
        recipe = {};
        plan.reset();
        for (auto* field : {&b.maxCreationCallbacks,
                 &b.maxRecordingCallbacks,
                 &b.maxSubmissionCallbacks,
                 &b.maxMappingCallbacks}) {
            --*field;
            auto p = factory();
            Recipe rejected;
            assert(p && Lowering::lowerOriginal(p, rejected) && !rejected.originalPlan);
            ++*field;
        }
        b.maxPendingRequests = UINT32_MAX;
        {
            auto p = factory();
            Recipe rejected;
            assert(p
                && std::string_view(Lowering::lowerOriginal(p, rejected))
                    == "RHI-READBACK-COLD-BUDGET-AGGREGATE-COUNT");
        }
        b = budget();
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            assert(Plan::lowering(backend).cpuBudgetImplemented
                && Plan::lowering(backend).validatorOnly == (backend == rhi::Backend::Null));
            assert(Lowering::lowering(backend).privateBudgetMappingImplemented
                == (backend == rhi::Backend::WebGPU));
        }
        verifyRequest();
        for (const auto& domain : q)
            assert(!domain->liveBytes());
        std::printf(
            "BINDLESS_RHI_READBACK_COLD_BUDGET_OK %llu domains4 quota-alias exact-less-exhausted frozen callbacks aggregate request backends4 allocation0 recipe-only\n",
            static_cast<unsigned long long>(peak));
    }

private:
    static void verifyDeviceLossBudget(Budget b, const Plan::Quotas& q)
    {
        using Contract = rhi::DeviceLossRegistrationContract;
        const auto factory = [&] {
            const char* id = nullptr;
            return Plan::prepareOriginal(b, q, id);
        };
        for (auto* count : {&b.deviceLoss.maxRegistrations, &b.deviceLoss.maxCallbacks}) {
            auto saved = *count;
            *count = 0u;
            assert(!factory());
            *count = saved;
        }
        b.deviceLoss.contractVersion = 0u;
        assert(!factory());
        b = budget();
        b.deviceLoss.prototypeOptIn = false;
        assert(!factory());
        b = budget();
        b.deviceLoss.maxCpuMetadataBytes = cpuBytes - 1u;
        assert(!factory());
        b = budget();
        const auto a = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Contract::validateBudget(b.deviceLoss, cpuBytes));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::lowering(backend);
            assert(decision.diagnostic);
            assert(decision.nativePrototype == (backend == rhi::Backend::WebGPU)
                && decision.validatorOnly == (backend == rhi::Backend::Null));
        }
        assert(!Contract::lowering(static_cast<rhi::Backend>(99u)).nativePrototype);
        for (unsigned bits = 0u; bits < 8u; ++bits) {
            const bool pending = bits & 1u, quarantined = bits & 2u, lost = bits & 4u;
            assert(bool(Contract::validateDetachment(pending, quarantined, lost))
                == (pending || (quarantined && !lost)));
        }
        assert(a == core::defaultCpuAllocator().stats().allocationCount);
        {
            const char* id = nullptr;
            auto original = Plan::prepareOriginal(b, q, id);
            assert(original);
            auto registrations = Plan::originalLossRegistrationCapacity(original),
                 callbacks = Plan::originalLossCallbackCapacity(original);
            const auto before = core::defaultCpuAllocator().stats().allocationCount;
            core::AtomicCapacityLease registration(registrations, 1u), callback(callbacks, 1u);
            assert(registration.charged() && callback.charged());
            core::AtomicCapacityLease duplicateRegistration(registrations, 1u),
                duplicateCallback(callbacks, 1u);
            assert(!duplicateRegistration.charged() && !duplicateCallback.charged());
            registration.release();
            registration.release();
            callback.release();
            callback.release();
            assert(!registrations->used() && !callbacks->used()
                && before == core::defaultCpuAllocator().stats().allocationCount);
            auto forged = std::shared_ptr<const Plan>(q[1], original.get());
            assert(!Plan::originalLossRegistrationCapacity(forged)
                && !Plan::originalLossCallbackCapacity(forged));
        }
        std::puts(
            "BINDLESS_RHI_DEVICE_LOSS_REGISTRATION_CONTRACT_OK version-opt-in counts quota backends4 detach8 allocation0");
    }

    static void verifyGraphBudget(Budget b, const Plan::Quotas& quotas)
    {
        using Contract = rhi::TextureReadbackCommandGraphContract;
        const char* diagnostic = nullptr;
        const auto rejected = [&](const char* expected) {
            const auto a = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Plan::prepareOriginal(b, quotas, diagnostic)
                && std::string_view(diagnostic) == expected);
            assert(a == core::defaultCpuAllocator().stats().allocationCount);
        };
        b.graph.contractVersion = 0u;
        rejected("RHI-READBACK-GRAPH-BUDGET-VERSION");
        b = budget();
        b.graph.prototypeOptIn = false;
        rejected("RHI-READBACK-GRAPH-BUDGET-OPT-IN");
        b = budget();
        for (auto* field : {&b.graph.maxCompletedSourceGraphs,
                 &b.graph.maxProjectedOwners,
                 &b.graph.maxAllocationRecords,
                 &b.graph.maxReferences,
                 &b.graph.maxTextureReferences,
                 &b.graph.maxReadbackReferences,
                 &b.graph.maxStateReferences,
                 &b.graph.maxReferenceUsages,
                 &b.graph.maxDescriptorRecords,
                 &b.graph.maxReadbackDescriptors,
                 &b.graph.maxSourceReferences}) {
            const auto saved = *field;
            *field = 0u;
            rejected("RHI-READBACK-GRAPH-BUDGET-COUNT");
            *field = saved;
        }
        for (auto* field : {&b.graph.maxReferences,
                 &b.graph.maxTextureReferences,
                 &b.graph.maxReferenceUsages,
                 &b.graph.maxDescriptorRecords}) {
            --*field;
            rejected("RHI-READBACK-GRAPH-BUDGET-CAPACITY");
            ++*field;
        }
        --b.maxOriginalCommands;
        rejected("RHI-READBACK-GRAPH-BUDGET-COMMAND-CAPACITY");
        b = budget();
        --b.graph.maxCpuMetadataBytes;
        rejected("RHI-READBACK-GRAPH-BUDGET-QUOTA");
        b = budget();
        --b.graph.maxCommandTextBytes;
        rejected("RHI-READBACK-GRAPH-BUDGET-LABEL-RANGE");
        b = budget();
        b.graph.maxPassLabelBytes = UINT64_MAX;
        rejected("RHI-READBACK-GRAPH-BUDGET-LABEL-RANGE");
        b = budget();
        const auto plan = Plan::prepareOriginal(b, quotas, diagnostic);
        assert(plan);
        Recipe recipe;
        assert(!Lowering::lowerOriginal(plan, recipe));
        assert(recipe.pair.maxProjectedOwners == b.graph.maxProjectedOwners
            && recipe.projection.maxAllocationReferences == b.graph.maxAllocationRecords);
        assert(
            recipe.projection
                .maxDomainReferences[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)]
            == b.graph.maxTextureReferences);
        assert(recipe.usage.maxReferenceUsages == b.graph.maxReferenceUsages
            && recipe.variables.maxTextBytes == b.graph.maxCommandTextBytes);
        assert(!recipe.variables.maxDynamicOffsets && !recipe.variables.maxPushBytes);
        assert(recipe.inputs.maxDescriptorRecords == b.graph.maxDescriptorRecords
            && recipe.states.maxSourceReferences == b.graph.maxSourceReferences);
        assert(recipe.completion.maxPendingRecords == b.maxPendingRequests
            && recipe.completion.maxCpuMetadataBytes == b.maxCpuDeviceBytes);
        for (const auto bytes : {recipe.pair.maxCpuMetadataBytes,
                 recipe.projection.maxCpuMetadataBytes,
                 recipe.usage.maxCpuMetadataBytes,
                 recipe.variables.maxCpuMetadataBytes,
                 recipe.scalars.maxCpuMetadataBytes,
                 recipe.inputs.maxCpuMetadataBytes,
                 recipe.states.maxCpuMetadataBytes})
            assert(bytes == b.maxCpuRequestBytes);
        assert(Contract::commands.front() == rhi::CommandUsageKind::TextureBarrier
            && Contract::commands.back() == rhi::CommandUsageKind::Finish);
        std::puts(
            "BINDLESS_RHI_READBACK_GRAPH_BUDGET_CONTRACT_OK explicit-version-opt-in counts-domain-text quota overflow commands5 references3 descriptors2 Device-completion Request-graph backends4");
    }

    static void verifyRequest()
    {
        TextureReadbackLayoutFixture::Context c;
        const char* diagnostic = nullptr;
        auto layout = c.prepare(TextureReadbackLayoutFixture::descriptor(),
            TextureReadbackLayoutFixture::texture(),
            diagnostic);
        assert(layout);
        auto b = budget();
        b.maxResultBytesPerRequest = b.maxStagingBytesPerRequest = b.maxDeviceRetainedResultBytes =
            b.maxDeviceStagingBytes = 5120u;
        b.maxAnalyzedPixelsPerRequest = 40u;
        auto q = quotas(b);
        auto request = rhi::TextureReadbackRequestDeclaration::prepareOriginal(
            layout, {17u, 23u, 29u}, {1u, true, 2u, 128u, 5120u, cpuBytes}, q[1], diagnostic);
        assert(request);
        auto stats = rhi::TextureReadbackStatisticsPlan::prepareOriginal(
            layout, {1u, true, 4u, 40u, 5120u, cpuBytes}, q[1], diagnostic);
        assert(stats);
        b.maxLabelBytesPerRequest = layout->inputOwner()->label().size();
        auto plan = Plan::prepareOriginal(b, q, diagnostic);
        assert(plan && !Plan::validateRequestOriginal(plan, request, stats));
        for (auto* field : {&b.maxResultBytesPerRequest,
                 &b.maxStagingBytesPerRequest,
                 &b.maxAnalyzedPixelsPerRequest,
                 &b.maxLabelBytesPerRequest}) {
            --*field;
            auto constrained = Plan::prepareOriginal(b, q, diagnostic);
            assert(constrained && Plan::validateRequestOriginal(constrained, request, stats));
            ++*field;
        }
        for (auto* field : {&b.maxMipCopies, &b.maxStatisticsViews}) {
            --*field;
            auto constrained = Plan::prepareOriginal(b, q, diagnostic);
            assert(constrained && Plan::validateRequestOriginal(constrained, request, stats));
            ++*field;
        }
        auto foreign = c.prepare(TextureReadbackLayoutFixture::descriptor(),
            TextureReadbackLayoutFixture::texture(),
            diagnostic);
        auto foreignStats = rhi::TextureReadbackStatisticsPlan::prepareOriginal(
            foreign, {1u, true, 4u, 40u, 5120u, cpuBytes}, q[1], diagnostic);
        assert(foreignStats && Plan::validateRequestOriginal(plan, request, foreignStats));
        auto forged =
            std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>(q[0], request.get());
        assert(Plan::validateRequestOriginal(plan, forged, stats));
        forged.reset();
        foreignStats.reset();
        foreign.reset();
        plan.reset();
        request.reset();
        stats.reset();
        layout.reset();
        c.retired();
        for (const auto& domain : q)
            assert(!domain->liveBytes());
    }
};
} // namespace arp::test
