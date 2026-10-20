#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/frontend/command_usage.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteInputsFixture final {
    using Inputs = rhi::OriginalCommandInputs;
    using Binding = rhi::OriginalCommandDescriptorBindingContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General Inputs6: %s\n", id);
        assert(!id);
    }

public:
    static void verifyContract()
    {
        using Contract = rhi::OriginalCommandPipelineBindingContract;
        const rhi::OriginalCommandInputsBudget request{6u, true, 1u, 1u, 0u, 1u};
        assert(!Contract::validateBudget(request));
        auto bad = request;
        bad.prototypeOptIn = false;
        assert(Contract::validateBudget(bad));
        bad = request;
        bad.maxDescriptorBindings = 0u;
        assert(Contract::validateBudget(bad));
        bad = request;
        bad.maxPipelineBindings = 0u;
        assert(Contract::validateBudget(bad));
        bad = request;
        bad.maxTableBindings = 1u;
        assert(Contract::validateBudget(bad));
        bad = request;
        bad.maxVertexBindings = 1u;
        assert(Contract::validateBudget(bad));
        bad = request;
        bad.maxIndexBindings = 1u;
        assert(Contract::validateBudget(bad));
        assert(!Binding::validateSetProfile(6u, 5u));
        for (const auto source : {0u, 1u, 2u, 3u, 4u, 6u, UINT32_MAX})
            assert(Binding::validateSetProfile(6u, source));
        for (const auto inputs : {0u, 1u, 2u, 3u, 4u, 5u, 7u, UINT32_MAX})
            assert(Binding::validateSetProfile(inputs, 5u));
        for (const auto inputs : {3u, 4u, 5u})
            for (const auto source : {2u, 3u, 4u})
                assert(!Binding::validateSetProfile(inputs, source));
        std::puts(
            "BINDLESS_RHI_GENERAL_INPUTS6_CONTRACT_OK mixed-Source5 exact-profile legacy-isolated table-geometry-refused execution-closed");
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static auto verifyActual(const std::shared_ptr<rhi::OriginalCommandResourceSources>& sources,
        const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>& descriptor,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota,
        bool dependencies = false)
    {
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        constexpr std::uint64_t bytes = 65536u;
        const char* id = nullptr;
        assert(descriptor->sourceContractVersion() == 5u
            && descriptor->frozenFiniteDeclarations().empty());
        const auto pipelineIndex = descriptor->frozenPipelineSourceIndex();
        const auto descriptorIndex = sources->originalLeaseIndex(descriptor);
        assert(pipelineIndex == 6u && descriptorIndex == 7u);
        const std::size_t commandCount = dependencies ? 9u : 6u;
        const std::size_t referenceCount = dependencies ? 5u : 2u;
        const std::size_t descriptorCount = dependencies ? 5u : 2u;
        const std::array<rhi::CommandUsage, 9u> commands{{{K::BeginComputePass, 0u, 0u},
            {K::BindComputePipeline, 0u, 1u},
            {K::BindComputeDescriptor, 1u, 1u},
            {K::Dispatch, 2u, 0u},
            {K::EndComputePass, 2u, 0u},
            {dependencies ? K::BufferMemoryBarrier : K::Finish, 2u, dependencies ? 1u : 0u},
            {K::TextureMemoryBarrier, 3u, 1u},
            {K::TextureMemoryBarrier, 4u, 1u},
            {K::Finish, 5u, 0u}}};
        const std::array<rhi::OriginalCommandUsage::Reference, 5u> refs{
            {{6u}, {7u}, {0u}, {1u}, {3u}}};
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(
            sources, commands.data(), commandCount, refs.data(), referenceCount, graphQuota, id);
        require(id);
        assert(usage);
        const bool dynamic = descriptor->frozenDefinition().bindings[0].dynamicOffset;
        const std::uint32_t displacement =
            descriptor->frozenElements()[0].declaration.dynamicOffset;
        const auto field = [&](std::size_t i) -> rhi::CommandVariableArguments::Input {
            const auto scalar = i == 2u
                ? Scalar::capture(commands[i].kind, Scalar::DescriptorBinding{0u})
                : i == 3u ? Scalar::capture(commands[i].kind, Scalar::Dispatch{1u, 1u, 1u})
                          : Scalar::capture(commands[i].kind, Scalar::None{});
            assert(scalar);
            return {*scalar, {}};
        };
        auto fields = std::array<rhi::CommandVariableArguments::Input, 9u>{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u),
            field(8u)}};
        fields[2].offsets = dynamic ? &displacement : nullptr;
        fields[2].offsetCount = dynamic ? 1u : 0u;
        auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
            commandCount,
            {1u, true, commandCount, dynamic ? 1u : 0u, 0u, 0u, bytes},
            graphQuota,
            id);
        require(id);
        assert(arguments);
        auto scalars =
            rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graphQuota, id);
        require(id);
        assert(scalars);
        const auto pipeline =
            std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                sources->sources()[pipelineIndex]);
        std::array<rhi::ResourceMemoryBarrierDesc, 3u> memory;
        for (std::size_t i = 0u; i < memory.size(); ++i) {
            memory[i].resource =
                rhi::OriginalCommandResourceSources::resource(sources->sources()[i == 0u ? 0u
                        : i == 1u                                                        ? 1u
                                                                                         : 3u]);
            memory[i].sourceStages = memory[i].destinationStages =
                rhi::PipelineStage::ComputeShader;
            memory[i].sourceAccess = memory[i].destinationAccess =
                i == 1u ? rhi::ResourceAccess::ShaderWrite : rhi::ResourceAccess::ShaderRead;
        }
        const std::array<Inputs::OriginalDescriptor, 5u> records{
            {{1u,
                 rhi::OriginalCommandPipelineBinding{K::BindComputePipeline,
                     pipelineIndex,
                     0u,
                     pipeline->originalObject()->originalResourceRegistration().generation()}},
                {2u,
                    rhi::OriginalCommandDescriptorBinding{K::BindComputeDescriptor,
                        descriptorIndex,
                        pipelineIndex,
                        0u,
                        descriptor->originalObject()->originalResourceRegistration().generation(),
                        descriptor->frozenContentRevision()}},
                {5u, *rhi::ResourceDependencyArguments::capture(K::BufferMemoryBarrier, memory[0])},
                {6u,
                    *rhi::ResourceDependencyArguments::capture(K::TextureMemoryBarrier, memory[1])},
                {7u,
                    *rhi::ResourceDependencyArguments::capture(
                        K::TextureMemoryBarrier, memory[2])}}};
        const rhi::OriginalCommandInputsBudget budget{6u, true, 1u, 1u, 0u, dynamic ? 1u : 0u};
        const auto make = [&] {
            return Inputs::prepareOriginal(
                scalars, records.data(), descriptorCount, budget, graphQuota, id);
        };
        for (std::uint32_t version = 1u; version <= 5u; ++version) {
            auto legacy = budget;
            legacy.contractVersion = version;
            assert(!Inputs::prepareOriginal(
                       scalars, records.data(), descriptorCount, legacy, graphQuota, id)
                && id);
        }
        for (std::uint32_t field = 0u; field < 6u; ++field) {
            auto hostile = records;
            auto& binding = std::get<rhi::OriginalCommandDescriptorBinding>(hostile[1].arguments);
            switch (field) {
            case 0u:
                binding.sourceIndex = 6u;
                break;
            case 1u:
                binding.pipelineSourceIndex = 5u;
                break;
            case 2u:
                binding.scopeBeginCommandIndex = 1u;
                break;
            case 3u:
                ++binding.registrationGeneration;
                break;
            case 4u:
                ++binding.contentRevision;
                break;
            case 5u:
                hostile[1].commandIndex = 3u;
                break;
            }
            assert(!Inputs::prepareOriginal(
                       scalars, hostile.data(), descriptorCount, budget, graphQuota, id)
                && id);
        }
        if (dynamic) {
            auto shortBudget = budget;
            shortBudget.maxFrozenDynamicOffsets = 0u;
            assert(!Inputs::prepareOriginal(
                scalars, records.data(), descriptorCount, shortBudget, graphQuota, id));
            // Build a genuine different immutable argument sequence. A fitting
            // aligned displacement cannot replace the one frozen in Source5.
            std::uint32_t wrong = displacement
                ? 0u
                : static_cast<std::uint32_t>(descriptor->frozenElements()[0]
                          .declaration.write.bufferBindingRange.offsetBytes);
            auto changed = fields;
            changed[2].offsets = &wrong;
            auto altered = rhi::CommandVariableArguments::prepareOriginal(changed.data(),
                commandCount,
                {1u, true, commandCount, 1u, 0u, 0u, bytes},
                graphQuota,
                id);
            require(id);
            assert(altered);
            auto alteredScalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, altered, graphQuota, id);
            require(id);
            assert(alteredScalars);
            assert(!Inputs::prepareOriginal(
                alteredScalars, records.data(), descriptorCount, budget, graphQuota, id));
            assert(core::StringView(id) == "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS");
        }
        {
            // Missing dynamic offsets and extra static offsets are independently
            // refused by the exact positional join, before any Inputs allocation.
            const std::uint32_t extra = 0u;
            auto changed = fields;
            changed[2].offsets = dynamic ? nullptr : &extra;
            changed[2].offsetCount = dynamic ? 0u : 1u;
            auto altered = rhi::CommandVariableArguments::prepareOriginal(changed.data(),
                commandCount,
                {1u, true, commandCount, 1u, 0u, 0u, bytes},
                graphQuota,
                id);
            require(id);
            assert(altered);
            auto alteredScalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, altered, graphQuota, id);
            require(id);
            assert(alteredScalars);
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{graphQuota}, [&]() noexcept -> const char* {
                    const auto& binding =
                        std::get<rhi::OriginalCommandDescriptorBinding>(records[1].arguments);
                    assert(core::StringView(Binding::validate(binding,
                               *alteredScalars,
                               2u,
                               pipelineIndex,
                               0u,
                               rhi::semantics::CommandRecordingOrder::Scope::Compute,
                               6u))
                        == "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-OFFSETS");
                    return nullptr;
                }));
        }
        CpuQuotaFactoryFixture pressure(graphQuota);
        const auto peak = pressure.measureRetained(make);
        pressure.verify(peak, make);
        auto inputs = make();
        require(id);
        assert(inputs && !inputs->nativeExecutionProven());
        auto forged = std::shared_ptr<const Inputs>(sources->originalMetadataQuota(), inputs.get());
        assert(Inputs::validateOriginal(forged));
        forged.reset();
        const Inputs::IncomingReference incomingSet{descriptor->originalObject()};
        Inputs::Incoming incoming{fields[2], {}, &incomingSet, 1u};
        require(inputs->compareOriginalInput(2u, incoming));
        const Inputs::IncomingReference wrongSet{pipeline->originalObject()};
        incoming.references = &wrongSet;
        assert(inputs->compareOriginalInput(2u, incoming));
        incoming.references = &incomingSet;
        if (dynamic) {
            const std::uint32_t wrong = displacement ^ 1u;
            incoming.scalar.offsets = &wrong;
            assert(inputs->compareOriginalInput(2u, incoming));
            incoming.scalar = fields[2];
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{graphQuota}, [&]() noexcept {
                    if (const auto* failure = Inputs::validateOriginal(inputs))
                        return failure;
                    return inputs->compareOriginalInput(2u, incoming);
                }));
        });
        std::printf(
            "BINDLESS_RHI_GENERAL_INPUTS6_OK %s displacement%u bytes%llu original-Sources5 positional-pipeline-set-revision mixed-frozen-offsets exact-less-exhausted hostile-refused hot0-query0 execution-closed\n",
            dynamic ? "dynamic" : "static",
            displacement,
            static_cast<unsigned long long>(peak));
        return inputs;
    }
#endif
};
} // namespace arp::test
