#pragma once

#include "original_general_finite_state_stream_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteGraphicsStateStreamFixture final {
    using Graph = rhi::OriginalCommandResourceStates;
    using Program = rhi::OriginalCommandStreamProgram;
    using Base = OriginalGeneralFiniteStateStreamFixture;
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "finite Graphics State5/Stream5: %s\n", id);
        assert(!id);
    }

public:
    static auto stateBudget(bool dependencies = false, bool initialized = false) noexcept
    {
        auto result = Base::stateBudget();
        // One buffer, one attachment, two bind ranges and two draw ranges.
        result.maxStateObservations = (dependencies ? 9u : 6u) + (initialized ? 2u : 0u);
        result.maxTextureRanges = (dependencies ? 8u : 5u) + (initialized ? 2u : 0u);
        result.renderDescriptorUsage = OriginalGeneralFiniteConsumptionFixture::renderBudget(
            (dependencies ? 12u : 8u) + (initialized ? 2u : 0u));
        return result;
    }

    static auto streamBudget(bool dependencies = false, bool initialized = false) noexcept
    {
        auto result = Base::streamBudget();
        result.maxCommands = (dependencies ? 12u : 8u) + (initialized ? 2u : 0u);
        result.maxDescriptors = result.maxReferences =
            (dependencies ? 7u : 3u) + (initialized ? 2u : 0u);
        result.maxRenderPasses = result.maxAttachmentReferences = 1u;
        result.maxImplicitAccesses = 8u;
        result.renderDescriptorUsage = OriginalGeneralFiniteConsumptionFixture::renderBudget(
            (dependencies ? 12u : 8u) + (initialized ? 2u : 0u));
        // Texture observations contribute five steps; the buffer contributes
        // one at bind and one at draw. Samplers carry no memory step.
        result.stateSequence = {5u,
            true,
            static_cast<std::uint32_t>(result.maxCommands),
            0u,
            initialized ? (dependencies ? 15u : 11u) : (dependencies ? 9u : 6u),
            initialized ? (dependencies ? 17u : 12u) : (dependencies ? 11u : 7u),
            8u};
        return result;
    }

    static void verifyContract()
    {
        Base::verifyContract();
        require(rhi::OriginalCommandStateAdmissionContract::validateIntent(stateBudget()));
        require(rhi::OriginalCommandStreamContract::validateIntent(streamBudget()));
        require(rhi::OriginalCommandStateAdmissionContract::validateIntent(stateBudget(true)));
        require(rhi::OriginalCommandStreamContract::validateIntent(streamBudget(true)));
        for (bool dependencies : {false, true}) {
            require(rhi::OriginalCommandStateAdmissionContract::validateIntent(
                stateBudget(dependencies, true)));
            require(rhi::OriginalCommandStreamContract::validateIntent(
                streamBudget(dependencies, true)));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_STATE5_STREAM5_CONTRACT_OK exact-Inputs6-GeneralResources2 attachment1 bind4 draw4 native-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Retained = Base::Retained;

    static Retained verifyActual(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& streamQuota,
        bool dependencies = false,
        bool initialized = false)
    {
        using Sources = rhi::OriginalCommandResourceSources;
        using Origin = rhi::common::OriginalCommandTextureRanges::Point::Origin;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const std::size_t prefix = initialized ? 2u : 0u;
        const auto authoredState = stateBudget(dependencies, initialized);
        const auto authoredStream = streamBudget(dependencies, initialized);
        assert(inputs->originalBudget().contractVersion == 6u
            && sources->originalBudget().maxStateObservations >= authoredState.maxStateObservations
            && sources->originalBudget().maxTextureRanges >= authoredState.maxTextureRanges);
        const char* id = nullptr;
        auto state = authoredState;
        std::uint32_t owners = 0u, registrations = 0u;
        require(Sources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            sources->originalBudget().maxGraphOwners,
            sources->originalBudget().maxGraphOwners,
            owners,
            registrations));
        assert(registrations == 12u
            && owners
                == inputs->retainedOwnerCount() + 1u
                    + rhi::OriginalResourceRegistrationContract::fixedOwnerCount + registrations
            && owners + 1u <= sources->originalBudget().maxGraphOwners);
        state.maxRetainedOwners = owners;
        state.maxResourceRegistrations = registrations;
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
        }
        for (const auto& alias :
            std::array{sources->originalMetadataQuota(), inputs->originalMetadataQuota()})
            assert(!Graph::prepareOriginal(inputs, device, state, alias, id) && id);
        CpuQuotaFactoryFixture statePressure(stateQuota);
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
            && result.graph->textureEntries().size() == authoredState.maxTextureRanges
            && !result.graph->nativeExecutionProven());
        assert(result.graph->bufferEntries()[0].sourceIndex == 0u
            && result.graph->bufferEntries()[0].observation.state()
                == rhi::ResourceState::ShaderRead);
        const auto& attachment = result.graph->textureEntries()[prefix];
        assert(attachment.point.commandIndex == prefix && attachment.point.sourceIndex == 7u
            && attachment.point.origin == Origin::ExplicitOperand
            && attachment.point.descriptorSourceIndex == SIZE_MAX
            && attachment.observation.state()
                == (initialized ? rhi::ResourceState::Undefined
                                : rhi::ResourceState::ColorAttachment));
        const auto& attachmentSource =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                sources->sources()[7u]);
        const auto& attachmentLayout = attachmentSource->originalViewLease()->layout();
        const rhi::TextureSubresourceRange attachmentRange{attachmentLayout.baseMipLevel,
            attachmentLayout.mipLevelCount,
            attachmentLayout.baseArrayLayer,
            attachmentLayout.arrayLayerCount,
            attachmentLayout.aspect};
        assert(attachment.point.range == attachment.observation.range()
            && attachment.point.range == attachmentRange);
        for (std::size_t i = 0u; i < 4u; ++i) {
            const auto& entry =
                result.graph->textureEntries()[prefix + (dependencies ? 4u : 1u) + i];
            const auto sourceIndex = i % 2u == 0u ? 1u : 3u;
            assert(entry.point.commandIndex == prefix + (i < 2u ? 4u : 5u)
                && entry.point.sourceIndex == sourceIndex
                && entry.point.viewSourceIndex == sourceIndex + 1u
                && entry.point.descriptorSourceIndex == 9u
                && entry.point.origin
                    == (i < 2u ? Origin::RenderBinding : Origin::ShaderConsumption)
                && entry.point.stages == rhi::PipelineStage::FragmentShader
                && entry.point.range == entry.observation.range());
        }
        if (dependencies)
            for (std::size_t i = 0u; i < 3u; ++i) {
                const auto& entry = result.graph->textureEntries()[prefix + 1u + i];
                assert(entry.point.commandIndex == prefix + 8u + i
                    && entry.point.sourceIndex
                        == (i == 0u       ? 1u
                                : i == 1u ? 3u
                                          : 6u)
                    && entry.point.origin == Origin::ExplicitOperand
                    && entry.point.viewSourceIndex == SIZE_MAX
                    && entry.point.descriptorSourceIndex == SIZE_MAX);
            }
        if (initialized)
            for (std::size_t i = 0u; i < 2u; ++i) {
                const auto& entry = result.graph->textureEntries()[i];
                assert(entry.point.commandIndex == i
                    && entry.point.sourceIndex == (i == 0u ? 3u : 6u)
                    && entry.point.origin == Origin::ExplicitOperand
                    && entry.point.viewSourceIndex == SIZE_MAX
                    && entry.observation.state()
                        == (i == 0u ? rhi::ResourceState::CopyDst : rhi::ResourceState::Undefined)
                    && entry.point.range.mipLevelCount == (i == 0u ? 2u : 1u));
            }
        auto forged = std::shared_ptr<const Graph>(stateQuota, result.graph.get());
        assert(Graph::validateOriginal(forged));
        forged.reset();
        auto stream = authoredStream;
        stream.maxRetainedOwners = result.graph->retainedOwnerCount() + 1u;
        const auto sequence =
            rhi::common::OriginalCommandStateSequence::inspect(result.graph, stream.stateSequence);
        require(sequence.diagnostic);
        assert(sequence.stateCells == stream.stateSequence.maxStateCells
            && sequence.steps == stream.stateSequence.maxSteps);
        const auto factory = [&] {
            return Program::prepareOriginalTyped(result.graph, stream, streamQuota, id);
        };
        for (const auto& alias : std::array{
                 sources->originalMetadataQuota(), inputs->originalMetadataQuota(), stateQuota})
            assert(!Program::prepareOriginalTyped(result.graph, stream, alias, id) && id);
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
        assert(counts.commands == stream.maxCommands && counts.descriptors == stream.maxDescriptors
            && counts.references == stream.maxReferences && counts.pipelineBinds == 1u
            && counts.descriptorBinds == 1u && counts.consumptions == 1u
            && counts.implicitAccesses == 8u && counts.renderPasses == 1u
            && counts.attachmentReferences == 1u && !counts.geometryBinds);
        const auto& operations = result.program->operations();
        assert(operations[prefix].kind == rhi::CommandUsageKind::BeginRenderPass
            && operations[prefix].elementCount == 1u
            && operations[prefix + 4u].kind == rhi::CommandUsageKind::BindRenderDescriptor
            && operations[prefix + 4u].firstImplicitAccess == 0u
            && operations[prefix + 4u].implicitAccessCount == 4u
            && operations[prefix + 5u].kind == rhi::CommandUsageKind::Draw
            && operations[prefix + 5u].firstImplicitAccess == 4u
            && operations[prefix + 5u].implicitAccessCount == 4u);
        for (std::uint32_t field = 0u; field < 9u; ++field) {
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
            case 7u:
                --bad.maxRenderPasses;
                break;
            case 8u:
                --bad.maxAttachmentReferences;
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
            "BINDLESS_RHI_GENERAL_GRAPHICS_STATE5_STREAM5_OK observations%u ranges%u cells%u steps%u implicit8 registrations12 attachment1 bind4 draw4 exact-less-exhausted preflight-no-output hot0-query0 stateBytes%llu streamBytes%llu no-content-root-native\n",
            state.maxStateObservations,
            state.maxTextureRanges,
            static_cast<unsigned>(sequence.stateCells),
            static_cast<unsigned>(sequence.steps),
            static_cast<unsigned long long>(stateBytes),
            static_cast<unsigned long long>(streamBytes));
        if (initialized)
            std::printf(
                "BINDLESS_RHI_GENERAL_GRAPHICS_INITIALIZED_STREAM5_OK commands%u transitions2 scope2..8 whole-sampled-mips2 initial-CopyDst-Undefined dry-state-unchanged no-content-root-native\n",
                static_cast<unsigned>(counts.commands));
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
            "BINDLESS_RHI_GENERAL_GRAPHICS_STATE5_STREAM5_TERMINAL_OK original-attachment-bind-draw-graph-invalid preflight-no-output no-native");
    }
#endif
};
} // namespace arp::test
