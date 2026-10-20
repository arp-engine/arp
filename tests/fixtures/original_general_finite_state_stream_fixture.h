#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_general_finite_consumption_fixture.h"
#include "rhi/frontend/command_stream_program.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteStateStreamFixture final {
    using Graph = rhi::OriginalCommandResourceStates;
    using Program = rhi::OriginalCommandStreamProgram;
    using State = rhi::OriginalCommandStateAdmissionBudget;
    using Stream = rhi::OriginalCommandStreamBudget;
    using Footprint = OriginalGeneralFiniteConsumptionFixture;
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General State5/Stream5: %s\n", id);
        assert(!id);
    }

public:
    static State stateBudget(bool dependencies = false) noexcept
    {
        State value{5u, true, 1u, 128u, 3u, 2u, 128u, bytes};
        value.descriptorConsumption = Footprint::budget();
        value.renderDescriptorUsage = Footprint::renderBudget(dependencies ? 9u : 6u);
        if (dependencies) {
            value.maxStateObservations = 5u;
            value.maxTextureRanges = 4u;
        }
        return value;
    }

    static Stream streamBudget(bool dependencies = false) noexcept
    {
        Stream value;
        value.contractVersion = 5u;
        value.prototypeOptIn = true;
        value.maxCommands = dependencies ? 9u : 6u;
        value.maxDescriptors = value.maxReferences = dependencies ? 5u : 2u;
        value.maxRetainedOwners = 129u;
        value.maxCpuMetadataBytes = bytes;
        value.maxPipelineBinds = value.maxDescriptorBinds = value.maxConsumptions = 1u;
        value.maxImplicitAccesses = 4u;
        value.descriptorConsumption = Footprint::budget();
        value.renderDescriptorUsage = Footprint::renderBudget(dependencies ? 9u : 6u);
        value.stateSequence = {5u,
            true,
            dependencies ? 9u : 6u,
            dependencies ? 3u : 0u,
            dependencies ? 5u : 3u,
            dependencies ? 6u : 3u,
            4u};
        return value;
    }

    static void verifyContract()
    {
        const auto state = stateBudget();
        const auto stream = streamBudget();
        assert(!rhi::OriginalCommandStateAdmissionContract::validateIntent(state));
        assert(!rhi::OriginalCommandStreamContract::validateIntent(stream));
        for (std::uint32_t version = 0u; version <= 6u; ++version) {
            auto s = state;
            s.contractVersion = version;
            auto p = stream;
            p.contractVersion = version;
            assert(bool(rhi::OriginalCommandStateAdmissionContract::validateIntent(s))
                == (version != 5u));
            assert(bool(rhi::OriginalCommandStreamContract::validateIntent(p)) == (version != 5u));
        }
        for (std::uint32_t field = 0u; field < 7u; ++field) {
            auto bad = state;
            switch (field) {
            case 0u:
                bad.prototypeOptIn = false;
                break;
            case 1u:
                bad.descriptorConsumption = {};
                break;
            case 2u:
                bad.renderDescriptorUsage = {};
                break;
            case 3u:
                bad.descriptorConsumption.generalShaderResourcesVersion = 1u;
                break;
            case 4u:
                bad.renderDescriptorUsage.generalShaderResourcesVersion = 1u;
                break;
            case 5u:
                bad.geometryUsage.contractVersion = 1u;
                break;
            case 6u:
                bad.retainedReadbackStateVersion = 1u;
                bad.retainedReadbackStateOptIn = true;
                break;
            }
            assert(rhi::OriginalCommandStateAdmissionContract::validateIntent(bad));
        }
        auto bad = stream;
        bad.stateSequence.contractVersion = 4u;
        assert(rhi::OriginalCommandStreamContract::validateIntent(bad));
        bad = stream;
        bad.geometryUsage.contractVersion = 1u;
        assert(rhi::OriginalCommandStreamContract::validateIntent(bad));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto captured =
                rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(backend, 5u);
            assert(captured.contractVersion == 5u && !captured.executionAvailable
                && captured.stateAvailable == (backend == rhi::Backend::WebGPU)
                && captured.projectionAvailable == captured.stateAvailable
                && captured.validatorOnly == (backend == rhi::Backend::Null));
            const auto compiled =
                rhi::semantics::OriginalCommandStreamLowering::typedLowering(backend, 5u);
            assert(compiled.contractVersion == 5u && !compiled.nativeExecutionAvailable
                && compiled.diagnostic);
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_STATE5_STREAM5_CONTRACT_OK exact-Inputs6-GeneralResources2-footprint4 sequence5 table-geometry-readback-refused native-closed");
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    struct Retained {
        std::shared_ptr<const Graph> graph;
        std::shared_ptr<const Program> program;
    };

    static Retained verifyActual(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& streamQuota,
        bool dependencies = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        assert(inputs->originalBudget().contractVersion == 6u
            && sources->originalBudget().maxStateObservations == (dependencies ? 5u : 3u)
            && sources->originalBudget().maxTextureRanges == (dependencies ? 4u : 2u));
        const char* id = nullptr;
        auto state = stateBudget(dependencies);
        std::uint32_t owners = 0u, registrations = 0u;
        require(Sources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            sources->originalBudget().maxGraphOwners,
            sources->originalBudget().maxGraphOwners,
            owners,
            registrations));
        assert(registrations == 10u
            && owners
                == inputs->retainedOwnerCount() + 1u
                    + rhi::OriginalResourceRegistrationContract::fixedOwnerCount + registrations
            && owners + 1u <= sources->originalBudget().maxGraphOwners);
        state.maxRetainedOwners = owners;
        state.maxResourceRegistrations = registrations;
        std::printf(
            "BINDLESS_RHI_GENERAL_STATE5_OWNER_INVENTORY_OK cpu%u registrations%u state%u ceiling%u exact-owner-registration-budgets\n",
            inputs->retainedOwnerCount() + 1u,
            registrations,
            owners,
            sources->originalBudget().maxGraphOwners);
        const auto make = [&] {
            return Graph::prepareOriginal(inputs, device, state, stateQuota, id);
        };
        for (std::uint32_t field = 0u; field < 4u; ++field) {
            auto bad = state;
            if (field == 0u)
                --bad.maxStateObservations;
            if (field == 1u)
                --bad.maxTextureRanges;
            if (field == 2u)
                --bad.maxRetainedOwners;
            if (field == 3u)
                --bad.maxResourceRegistrations;
            assert(!Graph::prepareOriginal(inputs, device, bad, stateQuota, id) && id
                && !stateQuota->liveBytes());
            if (field == 2u)
                assert(core::StringView(id) == "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
            if (field == 3u)
                assert(core::StringView(id) == "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
        }
        for (const auto& alias :
            std::array{sources->originalMetadataQuota(), inputs->originalMetadataQuota()})
            assert(!Graph::prepareOriginal(inputs, device, state, alias, id) && id);
        CpuQuotaFactoryFixture statePressure(stateQuota);
        // The same quota is reused for both authored displacements after full
        // exhaustion/hot probes. Its historical peak is already the limit on
        // the next visit; measure admission without a peak-based marker.
        const auto stateBytes = statePressure.measureSingleUse([] {}, make);
        statePressure.verify(stateBytes, make);
        {
            core::CpuMemoryQuotaLease full(stateQuota, bytes);
            assert(full.charged() && !make() && id);
        }
        Retained result;
        result.graph = make();
        require(id);
        assert(result.graph);
        assert(result.graph->stateContractVersion() == 5u
            && result.graph->bufferEntries().size() == 1u
            && result.graph->textureEntries().size() == (dependencies ? 4u : 2u)
            && !result.graph->nativeExecutionProven());
        assert(result.graph->bufferEntries()[0].sourceIndex == 0u
            && result.graph->bufferEntries()[0].observation.state()
                == rhi::ResourceState::ShaderRead);
        for (std::size_t i = 0u; i < 2u; ++i) {
            const auto& entry = result.graph->textureEntries()[i + (dependencies ? 2u : 0u)];
            assert(entry.point.commandIndex == 3u && entry.point.sourceIndex == (i == 0u ? 1u : 3u)
                && entry.point.viewSourceIndex == (i == 0u ? 2u : 4u)
                && entry.point.descriptorSourceIndex == 7u);
        }
        if (dependencies)
            for (std::size_t i = 0u; i < 2u; ++i) {
                const auto& entry = result.graph->textureEntries()[i];
                assert(entry.point.commandIndex == 6u + i
                    && entry.point.sourceIndex == (i == 0u ? 1u : 3u)
                    && entry.point.viewSourceIndex == SIZE_MAX
                    && entry.point.descriptorSourceIndex == SIZE_MAX);
            }
        auto forged = std::shared_ptr<const Graph>(stateQuota, result.graph.get());
        assert(Graph::validateOriginal(forged));
        forged.reset();
        auto stream = streamBudget(dependencies);
        stream.maxRetainedOwners = result.graph->retainedOwnerCount() + 1u;
        const auto sequence =
            rhi::common::OriginalCommandStateSequence::inspect(result.graph, stream.stateSequence);
        require(sequence.diagnostic);
        assert(sequence.stateCells == (dependencies ? 5u : 3u)
            && sequence.steps == (dependencies ? 6u : 3u));
        const auto factory = [&] {
            return Program::prepareOriginalTyped(result.graph, stream, streamQuota, id);
        };
        CpuQuotaFactoryFixture streamPressure(streamQuota);
        const auto streamBytes = streamPressure.measureSingleUse([] {}, factory);
        streamPressure.verify(streamBytes, factory);
        {
            core::CpuMemoryQuotaLease full(streamQuota, bytes);
            assert(full.charged() && !factory() && id);
        }
        result.program = factory();
        require(id);
        assert(result.program);
        const auto& counts = result.program->counts();
        assert(counts.commands == (dependencies ? 9u : 6u)
            && counts.descriptors == (dependencies ? 5u : 2u)
            && counts.references == (dependencies ? 5u : 2u) && counts.pipelineBinds == 1u
            && counts.descriptorBinds == 1u && counts.consumptions == 1u
            && counts.implicitAccesses == 4u && !counts.geometryBinds && !counts.renderPasses);
        const auto& operations = result.program->operations();
        assert(operations[3].kind == rhi::CommandUsageKind::Dispatch
            && operations[3].firstImplicitAccess == 0u && operations[3].implicitAccessCount == 4u);
        for (std::uint32_t field = 0u; field < 7u; ++field) {
            auto bad = stream;
            switch (field) {
            case 0u:
                --bad.maxImplicitAccesses;
                break;
            case 1u:
                --bad.maxDescriptors;
                break;
            case 2u:
                --bad.maxReferences;
                break;
            case 3u:
                --bad.maxRetainedOwners;
                break;
            case 4u:
                --bad.stateSequence.maxStateCells;
                break;
            case 5u:
                --bad.stateSequence.maxSteps;
                break;
            case 6u:
                --bad.stateSequence.maxImplicitAccesses;
                break;
            }
            std::uint32_t emitted = 0u;
            assert(Program::visitOriginalTyped(result.graph,
                       bad,
                       [&](const auto&) noexcept -> const char* {
                           ++emitted;
                           return nullptr;
                       })
                && !emitted);
        }
        auto forgedProgram = std::shared_ptr<const Program>(streamQuota, result.program.get());
        assert(Program::validateOriginal(forgedProgram));
        forgedProgram.reset();
        assert(!rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
            result.graph, device, state, streamQuota, id));
        assert(core::StringView(id)
            == "RHI-ORIGINAL-FINITE-TRANSACTION-CONTENT-PROFILE-UNIMPLEMENTED");
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    stateQuota,
                    streamQuota},
                [&]() noexcept -> const char* {
                    if (const auto* failure = Graph::validateOriginal(result.graph))
                        return failure;
                    return Program::validateOriginal(result.program);
                }));
        });
        std::printf(
            "BINDLESS_RHI_GENERAL_STATE5_STREAM5_OK cells%u steps%u implicit4 original-inputs-graph exact-less-exhausted preflight-no-output hot0-query0 stateBytes%llu streamBytes%llu no-content-root-native\n",
            dependencies ? 5u : 3u,
            dependencies ? 6u : 3u,
            static_cast<unsigned long long>(stateBytes),
            static_cast<unsigned long long>(streamBytes));
        return result;
    }

    static void verifyInvalidated(const Retained& retained)
    {
        assert(
            Graph::validateOriginal(retained.graph) && Program::validateOriginal(retained.program));
        std::uint32_t emitted = 0u;
        assert(Program::visitOriginalTyped(retained.graph,
                   retained.program->originalBudget(),
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   })
            && !emitted);
        std::puts(
            "BINDLESS_RHI_GENERAL_STATE5_STREAM5_TERMINAL_OK retained-original-invalid preflight-no-output no-native");
    }
#endif
};
} // namespace arp::test
