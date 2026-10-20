#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/frontend/command_recording.h"

#include <cstdio>

namespace arp::test {
// Reuses the genuine State2/Inputs3 root. No actor, scope or state machine.
class OriginalCommandTypedStreamFixture final {
    using Program = rhi::OriginalCommandStreamProgram;
    using Graph = rhi::OriginalCommandResourceStates;
    using Budget = rhi::OriginalCommandStreamBudget;
    using K = rhi::CommandUsageKind;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "typed stream expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    struct Retained {
        std::shared_ptr<const Program> program;
        std::shared_ptr<core::CpuMemoryQuota> quota;
        std::shared_ptr<core::CpuMemoryQuota> stateQuota;
        std::shared_ptr<const Graph> graph;
        std::shared_ptr<rhi::OriginalCommandStateTransaction> transaction;
        std::shared_ptr<core::CpuMemoryQuota> recordingQuota;
    };

    static Budget budget(const std::shared_ptr<const Graph>& graph,
        const rhi::OriginalCommandStateSequenceBudget& sequence,
        const rhi::OriginalCommandDescriptorConsumptionContract::Budget& footprint,
        std::uint64_t descriptorBinds)
    {
        const auto& inputs = *graph->originalInputs();
        const auto& usage = *inputs.originalScalars()->originalUsage();
        Budget b{2u,
            true,
            usage.commands().size(),
            inputs.descriptors().size(),
            usage.references().size(),
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            0u,
            graph->retainedOwnerCount() + 1u,
            inputs.originalMetadataQuota()->limitBytes()};
        for (const auto& command : usage.commands())
            if (command.kind == K::BindComputePipeline || command.kind == K::BindGraphicsPipeline)
                ++b.maxPipelineBinds;
        b.maxDescriptorBinds = descriptorBinds;
        b.maxConsumptions = footprint.maxConsumptions;
        b.maxImplicitAccesses = footprint.maxLogicalAccesses;
        b.descriptorConsumption = footprint;
        b.stateSequence = sequence;
        // The accepted graphics fixture authors two passes, each referencing
        // one original attachment View. Parent textures are graph dependencies.
        if (!graph->textureEntries().empty()) {
            b.maxRenderPasses = 2u;
            b.maxAttachmentReferences = 2u;
        }
        return b;
    }

    static void verifyRejected(
        const std::shared_ptr<const Graph>& graph, const Budget& b, const char* expected)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuMetadataBytes);
        const char* id = nullptr;
        std::size_t emitted = 0u;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Program::prepareOriginalTyped(graph, b, quota, id));
        is(id, expected);
        is(Program::visitOriginalTyped(graph,
               b,
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               }),
            expected);
        assert(!emitted && !quota->liveBytes()
            && allocations == core::defaultCpuAllocator().stats().allocationCount);
    }

    static Retained prepare(const std::shared_ptr<const Graph>& graph, const Budget& b)
    {
        Retained r;
        r.quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuMetadataBytes);
        const char* id = nullptr;
        r.program = Program::prepareOriginalTyped(graph, b, r.quota, id);
        if (!r.program)
            std::fprintf(stderr, "typed stream prepare failed %s\n", id ? id : "unknown");
        assert(r.program);
        return r;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static Retained verifyTextures(const std::shared_ptr<const Graph>& graph,
        const rhi::OriginalCommandStateSequenceBudget& sequence,
        const rhi::OriginalCommandDescriptorConsumptionContract::Budget& footprint,
        const rhi::OriginalRenderDescriptorUsageBudget& render)
    {
        auto b = budget(graph, sequence, footprint, 2u);
        b.contractVersion = 3u;
        b.renderDescriptorUsage = render;
        b.maxImplicitAccesses = footprint.maxLogicalAccesses + render.maxLogicalAccesses;
        b.maxRenderPasses = b.maxAttachmentReferences = 1u;
        Program::Counts expected{11u, 8u, 8u, 0u, 0u, 0u, 0u, 1u, 1u};
        expected.pipelineBinds = 1u;
        expected.descriptorBinds = 2u;
        expected.consumptions = 1u;
        expected.implicitAccesses = 9u;
        const auto inspected = Program::inspectTyped(graph, b);
        if (inspected.diagnostic)
            std::fprintf(stderr, "texture stream inspect: %s\n", inspected.diagnostic);
        assert(!inspected.diagnostic && inspected.counts.matches(expected));
        Retained retained;
        retained.quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuMetadataBytes);
        retained.stateQuota = graph->originalMetadataQuota();
        const char* id = nullptr;
        retained.graph = graph;
        const auto factory = [&] {
            return Program::prepareOriginalTyped(graph, b, retained.quota, id);
        };
        CpuQuotaFactoryFixture probe(retained.quota);
        const auto peak = probe.measureRetained(factory);
        probe.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(retained.quota, retained.quota->limitBytes());
            assert(pressure.charged() && !factory());
            is(id, "RHI-ORIGINAL-STREAM-ALLOCATION");
        }
        retained.program = factory();
        assert(retained.program && !id && !retained.program->validate());
        const auto& inputs = graph->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const auto hot = [&]() noexcept -> const char* {
            if (const auto* diagnostic = retained.program->validate())
                return diagnostic;
            assert(!retained.program->compareOriginalStates(graph)
                && !retained.program->compareOriginalInputs(inputs));
            std::size_t visited = 0u;
            std::uint64_t implicit = 0u;
            const auto* diagnostic = Program::visitOriginalTyped(
                graph, b, [&](const Program::Operation& operation) noexcept -> const char* {
                    assert(operation.matches(retained.program->operations()[visited++]));
                    if (operation.kind == K::BindRenderDescriptorTable
                        || operation.kind == K::Draw) {
                        assert(operation.firstImplicitAccess == implicit
                            && operation.implicitAccessCount == 3u);
                        implicit += 3u;
                    } else
                        assert(!operation.implicitAccessCount);
                    return nullptr;
                });
            assert(!diagnostic && visited == 11u && implicit == 9u
                && !retained.program->nativeExecutionProven());
            for (const auto backend : std::array{rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                assert(retained.program->executionDiagnostic(backend));
            return nullptr;
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    graph->originalMetadataQuota(),
                    retained.quota},
                hot));
        });
        auto bad = b;
        --bad.maxImplicitAccesses;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-IMPLICIT-CAPACITY");
        bad = b;
        --bad.renderDescriptorUsage.maxCommands;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-RENDER-FOOTPRINT-MISMATCH");
        bad = b;
        --bad.descriptorConsumption.maxTextureAccesses;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-FOOTPRINT-MISMATCH");
        bad = b;
        --bad.maxRetainedOwners;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-OWNER-CAPACITY");
        bad = b;
        --bad.maxReferences;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-REFERENCE-CAPACITY");
        bad = b;
        --bad.maxDescriptors;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-DESCRIPTOR-CAPACITY");
        bad = b;
        --bad.stateSequence.maxImplicitAccesses;
        verifyRejected(graph, bad, "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY");
        bad = b;
        bad.contractVersion = 2u;
        bad.renderDescriptorUsage = {};
        bad.descriptorConsumption = {1u, true, 1u, 1u, true, true, 1u, 3u};
        bad.stateSequence.contractVersion = 2u;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-STATE-PROFILE");
        is(Program::inspect(inputs, b).diagnostic, "RHI-ORIGINAL-STREAM-TYPED-STATE-REQUIRED");
        auto forged = std::shared_ptr<const Graph>(retained.quota, graph.get());
        verifyRejected(forged, b, "RHI-ORIGINAL-RESOURCE-STATES-OWNER");
        for (const auto& alias : std::array{sources->originalMetadataQuota(),
                 inputs->originalMetadataQuota(),
                 graph->originalMetadataQuota()}) {
            assert(!Program::prepareOriginalTyped(graph, b, alias, id));
            is(id, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
        }
        std::printf(
            "ORIGINAL_TABLE_STREAM_OK Graphics stream3 commands11 descriptors8 references8 binds2 draw1 implicit9 exact-less-exhausted shadowed-bind-ordinals preflight-emits0 retained-original hot0 query0 quota%llu native-closed\n",
            static_cast<unsigned long long>(peak));
        return retained;
    }
#endif
    static Retained verify(
        const std::shared_ptr<const Graph>& graph, const Budget& b, const char* profile)
    {
        const auto& inputs = graph->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        Program::Counts expected{b.maxCommands,
            b.maxDescriptors,
            b.maxReferences,
            0u,
            0u,
            0u,
            0u,
            b.maxRenderPasses,
            b.maxAttachmentReferences};
        expected.pipelineBinds = 3u;
        expected.descriptorBinds = b.maxDescriptorBinds;
        expected.consumptions = 3u;
        expected.implicitAccesses = 6u;
        const auto inspection = Program::inspectTyped(graph, b);
        assert(!inspection.diagnostic && inspection.counts.matches(expected));
        auto r = prepare(graph, b);
        r.program.reset();
        const char* id = nullptr;
        const auto make = [&] {
            return Program::prepareOriginalTyped(graph, b, r.quota, id);
        };
        CpuQuotaFactoryFixture pressure(r.quota);
        const auto peak = pressure.measure(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(r.quota, b.maxCpuMetadataBytes);
            assert(full.charged());
            assert(!make());
            is(id, "RHI-ORIGINAL-STREAM-ALLOCATION");
        }
        r.program = make();
        assert(r.program && !r.program->validate() && r.program->counts().matches(expected));
        assert(r.program->retainedOwnerCount() == b.maxRetainedOwners
            && r.program->streamContractVersion() == 2u);
        assert(
            !r.program->compareOriginalStates(graph) && !r.program->compareOriginalInputs(inputs));
        is(r.program->compareOriginalStates({}), "RHI-ORIGINAL-STREAM-STATE-IDENTITY");
        auto forged = std::shared_ptr<const Graph>(r.quota, graph.get());
        assert(!Program::prepareOriginalTyped(forged, b, r.quota, id));
        is(id, "RHI-ORIGINAL-RESOURCE-STATES-OWNER");
        is(r.program->compareOriginalStates(forged), "RHI-ORIGINAL-STREAM-STATE-IDENTITY");
        auto forgedProgram = std::shared_ptr<const Program>(r.quota, r.program.get());
        is(Program::validateOriginal(forgedProgram), "RHI-ORIGINAL-STREAM-OWNER");
        auto bad = b;
        bad.contractVersion = 1u;
        bad.maxPipelineBinds = bad.maxDescriptorBinds = bad.maxConsumptions =
            bad.maxImplicitAccesses = 0u;
        bad.descriptorConsumption = {};
        bad.stateSequence = {};
        is(Program::inspect(inputs, bad).diagnostic, "RHI-ORIGINAL-STREAM-TYPED-PROFILE");
        is(Program::inspect(inputs, b).diagnostic, "RHI-ORIGINAL-STREAM-TYPED-STATE-REQUIRED");
        bad = b;
        --bad.descriptorConsumption.maxLogicalAccesses;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-FOOTPRINT-MISMATCH");
        bad = b;
        --bad.stateSequence.maxImplicitAccesses;
        verifyRejected(graph, bad, "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY");
        bad = b;
        --bad.maxRetainedOwners;
        verifyRejected(graph, bad, "RHI-ORIGINAL-STREAM-OWNER-CAPACITY");

        struct Limit {
            std::uint64_t Budget::* member;
            const char* diagnostic;
        };

        const std::array<Limit, 9u> limits{
            {{&Budget::maxCommands, "RHI-ORIGINAL-STREAM-COMMAND-CAPACITY"},
                {&Budget::maxDescriptors, "RHI-ORIGINAL-STREAM-DESCRIPTOR-CAPACITY"},
                {&Budget::maxReferences, "RHI-ORIGINAL-STREAM-REFERENCE-CAPACITY"},
                {&Budget::maxPipelineBinds, "RHI-ORIGINAL-STREAM-PIPELINE-BIND-CAPACITY"},
                {&Budget::maxDescriptorBinds, "RHI-ORIGINAL-STREAM-DESCRIPTOR-BIND-CAPACITY"},
                {&Budget::maxConsumptions, "RHI-ORIGINAL-STREAM-CONSUMPTION-CAPACITY"},
                {&Budget::maxImplicitAccesses, "RHI-ORIGINAL-STREAM-IMPLICIT-CAPACITY"},
                {&Budget::maxRenderPasses, "RHI-ORIGINAL-STREAM-PASS-CAPACITY"},
                {&Budget::maxAttachmentReferences, "RHI-ORIGINAL-STREAM-ATTACHMENT-CAPACITY"}}};
        for (const auto& limit : limits)
            if (b.*limit.member) {
                bad = b;
                --(bad.*limit.member);
                verifyRejected(graph, bad, limit.diagnostic);
            }
        for (const auto& alias : std::array{inputs->originalMetadataQuota(),
                 sources->originalMetadataQuota(),
                 graph->originalMetadataQuota()}) {
            assert(!Program::prepareOriginalTyped(graph, b, alias, id));
            is(id, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
            auto object = std::shared_ptr<core::CpuMemoryQuota>(r.quota, alias.get());
            assert(!Program::prepareOriginalTyped(graph, b, object, id));
            is(id, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
            auto owner = std::shared_ptr<core::CpuMemoryQuota>(alias, r.quota.get());
            assert(!Program::prepareOriginalTyped(graph, b, owner, id));
            is(id, "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN");
        }
        const auto hot = [&]() noexcept -> const char* {
            assert(!r.program->validate() && !Program::inspectTyped(graph, b).diagnostic);
            std::size_t visited = 0u, records = 0u;
            std::uint64_t access = 0u, pipelines = 0u, sets = 0u, consumptions = 0u;
            assert(!Program::visitOriginalTyped(
                graph, b, [&](const Program::Operation& op) noexcept -> const char* {
                    assert(op.matches(r.program->operations()[visited++]));
                    const auto* positional = inputs->descriptorAt(op.commandIndex);
                    assert(bool(positional) == (op.descriptorIndex != Program::noDescriptor));
                    if (positional) {
                        assert(positional == &inputs->descriptors()[records].arguments
                            && op.descriptorIndex == records);
                        ++records;
                    }
                    if (op.kind == K::BindGraphicsPipeline || op.kind == K::BindComputePipeline) {
                        assert(op.firstElement == pipelines++ && op.elementCount == 1u);
                    }
                    if (op.kind == K::BindRenderDescriptor || op.kind == K::BindComputeDescriptor) {
                        assert(op.firstElement == sets++ && op.elementCount == 1u);
                    }
                    if (op.kind == K::Draw || op.kind == K::Dispatch) {
                        assert(op.firstElement == consumptions++ && op.elementCount == 1u
                            && op.firstImplicitAccess == access && op.implicitAccessCount == 2u);
                        access += op.implicitAccessCount;
                    } else
                        assert(!op.implicitAccessCount);
                    return nullptr;
                }));
            assert(visited == expected.commands && records == expected.descriptors
                && pipelines == 3u && sets == b.maxDescriptorBinds && consumptions == 3u
                && access == 6u);
            for (const auto backend : std::array{rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                assert(
                    r.program->executionDiagnostic(backend) && !r.program->nativeExecutionProven());
            return nullptr;
        };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
#endif
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    graph->originalMetadataQuota(),
                    r.quota},
                hot));
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        });
#endif
        const auto limit = r.quota->limitBytes();
        r.quota->setLimitBytes(limit - 1u);
        is(r.program->validate(), "RHI-ORIGINAL-STREAM-QUOTA");
        r.quota->setLimitBytes(limit);
        std::printf(
            "BINDLESS_RHI_TYPED_STREAM_OK %s commands%llu descriptors%llu pipeline-binds3 descriptor-binds%llu consumptions3 implicit6 cold%llu exact-less-exhausted positional-scalar-records preflight-emits0 quotas4 hot0 query0 native-closed\n",
            profile,
            static_cast<unsigned long long>(expected.commands),
            static_cast<unsigned long long>(expected.descriptors),
            static_cast<unsigned long long>(b.maxDescriptorBinds),
            static_cast<unsigned long long>(peak));
        return r;
    }
};
} // namespace arp::test
