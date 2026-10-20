#pragma once

#include "original_general_descriptor_source_fixture.h"
#include "original_general_descriptor_write_fixture.h"

namespace arp::test {
class OriginalGeneralDescriptorSourceBrowserFixture final {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Set = rhi::OriginalCommandDescriptorSetSource;
    using Sources = rhi::OriginalCommandResourceSources;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General Source/5: %s\n", id);
        assert(!id);
    }

public:
    template <class Pipeline, std::size_t Count>
    static auto verifyActual(rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<Sources>& sources,
        const std::shared_ptr<rhi::DescriptorArena>& arena,
        core::UniquePtr<rhi::DescriptorSet>& object,
        const std::array<rhi::OriginalCommandDescriptorSetElement, Count>& elements,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota,
        std::uint64_t allocationBytes,
        std::size_t pipelineSourceIndex = 6u)
    {
        constexpr std::uint64_t bytes = 65536u;
        using Write = rhi::OriginalGeneralDescriptorWriteContract;
        using Scopes = WebGpuErrorScopeHandoffFixture<1015u>;
        const auto quota = sources->originalMetadataQuota();
        const std::shared_ptr<const Pipeline> accepted = pipeline;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> writeQuotas;
        for (auto& q : writeQuotas)
            q = core::makeShared<core::CpuMemoryQuota>(bytes);
        const rhi::OriginalGeneralDescriptorWriteRequest write{2u,
            true,
            Count,
            static_cast<std::uint32_t>(Write::ownerSlots(Count)),
            Write::callbackCount,
            {bytes, bytes, bytes},
            allocationBytes};
        const rhi::OriginalCommandDescriptorSourceBudget budget{5u,
            true,
            Count,
            Count,
            Set::finiteExclusiveRetainedOwnerCount,
            bytes,
            pipelineSourceIndex,
            0u,
            2u,
            rhi::dynamicOffsetBindingCount(
                pipeline->originalLayoutOwner()->desc().descriptorLayouts[0]),
            true};
        const char* id = nullptr;
        const auto begin = [&] {
            return device.beginOriginalGeneralDescriptorWrite(
                *object, pipeline, sources, 0u, elements.data(), Count, write, writeQuotas);
        };
        const auto drain = [&] {
            assert(WebGpuAsyncWaitFixture::until([&] {
                return object->originalGeneralWriteScopesDrained();
            }));
        };
        const auto fresh = [&] {
            object.reset();
            arena->reset();
            {
                core::CpuMemoryQuotaScope scope(resourceQuota);
                rhi::DescriptorSetDesc desc;
                desc.layout = rhi::DescriptorStateCopy::copy(
                    pipeline->originalLayoutOwner()->desc().descriptorLayouts[0]);
                object = device.allocateOriginalDescriptorSet(arena, desc);
                assert(object);
            }
            const auto* uncommitted = object.get();
            assert(!Set::prepareOriginal(
                object, arena, sources, elements.data(), Count, budget, quota, device, id));
            assert(object.get() == uncommitted
                && core::StringView(id) == "RHI-ORIGINAL-GENERAL-SOURCE-WRITE-PROFILE");
            require(begin());
            drain();
            require(object->finishOriginalGeneralWrite());
        };
        fresh();
        static_assert(Count == 4u);
        const auto capture = [&](std::size_t i) -> Set::FrozenElement {
            auto observation =
                object->observeSuccessfulBindingWrite(elements[i].write.binding, 0u, id);
            require(id);
            assert(observation);
            auto owner = rhi::OriginalCommandDescriptorDependencies::capture(
                *sources, elements[i].sourceIndex);
            assert(owner);
            return {elements[i], *observation, *owner};
        };
        const std::array<Set::FrozenElement, Count> frozen{
            {capture(0u), capture(1u), capture(2u), capture(3u)}};
        auto request = rhi::OriginalGeneralDescriptorSourceDiagnosticRequest{
            1u, true, object->contentRevision(), bytes};
        const auto diagnose = [&](const auto& sequence) {
            return object->diagnoseOriginalGeneralSource(
                request, accepted, sources, 0u, sequence.data(), sequence.size(), quota);
        };
        require(diagnose(frozen));
        assert(diagnose(
            std::array<Set::FrozenElement, Count>{{frozen[0], frozen[2], frozen[1], frozen[3]}}));
        assert(diagnose(std::array<Set::FrozenElement, Count>{
            {{frozen[0].declaration, frozen[1].observation, frozen[0].resourceOwner},
                frozen[1],
                frozen[2],
                frozen[3]}}));
        for (std::uint32_t field = 0u; field < 13u; ++field) {
            auto bad = frozen;
            switch (field) {
            case 0u:
                bad[0].declaration.sourceIndex = 2u;
                break;
            case 1u:
                ++bad[0].declaration.registrationGeneration;
                break;
            case 2u:
                ++bad[0].declaration.write.bufferBindingRange.offsetBytes;
                break;
            case 3u:
                ++bad[0].declaration.write.bufferBindingRange.sizeBytes;
                break;
            case 4u:
                ++bad[0].declaration.dynamicOffset;
                break;
            case 5u:
                ++bad[0].declaration.bufferAccessOffset;
                break;
            case 6u:
                --bad[0].declaration.bufferAccessBytes;
                break;
            case 7u:
                bad[0].declaration.stages = elements[0].stages == rhi::PipelineStage::ComputeShader
                    ? rhi::PipelineStage::FragmentShader
                    : rhi::PipelineStage::ComputeShader;
                break;
            case 8u:
                bad[0].declaration.access = rhi::ResourceAccess::ShaderWrite;
                break;
            case 9u:
                bad[1].declaration.write.binding = 2u;
                break;
            case 10u:
                bad[0].declaration.write.resource = frozen[1].declaration.write.resource;
                break;
            case 11u:
                bad[0].resourceOwner = frozen[1].resourceOwner;
                break;
            case 12u: {
                auto owner = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    frozen[0].resourceOwner);
                bad[0].resourceOwner = std::shared_ptr<const rhi::OriginalCommandBufferLease>(
                    writeQuotas[0], owner.get());
                break;
            }
            }
            assert(diagnose(bad));
            require(diagnose(frozen));
        }
        ++request.expectedContentRevision;
        assert(diagnose(frozen));
        --request.expectedContentRevision;
        assert(core::StringView(object->diagnoseOriginalGeneralSource(request,
                   std::shared_ptr<const rhi::ComputePipeline>{},
                   sources,
                   0u,
                   frozen.data(),
                   Count,
                   quota))
            == "RHI-ORIGINAL-GENERAL-SOURCE-PIPELINE");
        assert(core::StringView(object->diagnoseOriginalGeneralSource(request,
                   std::shared_ptr<const rhi::RenderPipeline>{},
                   sources,
                   0u,
                   frozen.data(),
                   Count,
                   quota))
            == "RHI-ORIGINAL-GENERAL-SOURCE-PIPELINE");
        assert(object->diagnoseOriginalGeneralSource(
            request, accepted, sources, 1u, frozen.data(), Count, quota));
        assert(object->diagnoseOriginalGeneralSource(request,
            std::shared_ptr<const Pipeline>(writeQuotas[0], pipeline.get()),
            sources,
            0u,
            frozen.data(),
            Count,
            quota));
        assert(object->diagnoseOriginalGeneralSource(request,
            accepted,
            std::shared_ptr<const Sources>(writeQuotas[0], sources.get()),
            0u,
            frozen.data(),
            Count,
            quota));
        assert(object->diagnoseOriginalGeneralSource(
            request, accepted, sources, 0u, frozen.data(), Count - 1u, quota));
        assert(object->diagnoseOriginalGeneralSource(
            request, accepted, sources, 0u, frozen.data(), Count, writeQuotas[0]));
        // Pending and genuinely failed replacement cannot authorize Source custody.
        Scopes::holdScopes();
        require(begin());
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(diagnose(frozen));
        assert(!Set::prepareOriginal(
            object, arena, sources, elements.data(), Count, budget, quota, device, id));
        Scopes::releaseScopes();
        drain();
        require(object->cancelOriginalGeneralWrite());
        require(diagnose(frozen));
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpGeneralSourceCreate = d.createBindGroup;
                d.createBindGroup = function(desc) {
                    return Module.arpGeneralSourceCreate.call(this, {...desc, entries : []});
                };
            // clang-format on
        });
        require(begin());
        drain();
        assert(object->finishOriginalGeneralWrite());
        assert(diagnose(frozen));
        require(object->cancelOriginalGeneralWrite());
        EM_ASM({
            // clang-format off
                Module.preinitializedWebGPUDevice.createBindGroup = Module.arpGeneralSourceCreate;
                delete Module.arpGeneralSourceCreate;
            // clang-format on
        });
        require(diagnose(frozen));
        for (std::uint32_t version = 1u; version <= 4u; ++version) {
            auto old = budget;
            old.contractVersion = version;
            if (version <= 2u) {
                old.finitePipelineSourceIndex = SIZE_MAX;
                old.finiteFootprintContractVersion = 0u;
                old.maxFrozenDynamicOffsets = 0u;
                old.finiteFullBoundViewOptIn = false;
            } else
                old.finiteFootprintContractVersion = 1u;
            assert(!Set::prepareOriginal(
                object, arena, sources, elements.data(), Count, old, quota, device, id));
            assert(core::StringView(id) == "RHI-ORIGINAL-GENERAL-SOURCE-LEGACY-PROFILE");
        }
        const auto freeze = [&](const auto& authored) {
            const auto* candidate = object.get();
            auto result = Set::prepareOriginal(
                object, arena, sources, elements.data(), Count, authored, quota, device, id);
            assert(result ? !object : object.get() == candidate);
            return result;
        };
        auto badBudget = budget;
        --badBudget.maxExclusiveRetainedOwners;
        assert(!freeze(badBudget));
        if (budget.maxFrozenDynamicOffsets) {
            badBudget = budget;
            badBudget.maxFrozenDynamicOffsets = 0u;
            assert(!freeze(badBudget));
        }
        badBudget = budget;
        badBudget.finitePipelineSourceIndex = 0u;
        assert(!freeze(badBudget));
        badBudget = budget;
        badBudget.finiteLogicalSet = 1u;
        assert(!freeze(badBudget));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
                    return diagnose(frozen);
                }));
        });
        if (!budget.maxFrozenDynamicOffsets) {
            auto whole = elements;
            whole[0].write.bufferBindingRange = {};
            whole[0].dynamicOffset = 0u;
            whole[0].bufferAccessOffset = 0u;
            whole[0].bufferAccessBytes =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    frozen[0].resourceOwner)
                    ->layout()
                    .sizeBytes;
            auto legacyWrite = write;
            legacyWrite.contractVersion = 1u;
            require(device.beginOriginalGeneralDescriptorWrite(
                *object, pipeline, sources, 0u, whole.data(), Count, legacyWrite, writeQuotas));
            drain();
            require(object->finishOriginalGeneralWrite());
            assert(!Set::prepareOriginal(
                object, arena, sources, whole.data(), Count, budget, quota, device, id));
            assert(object && core::StringView(id) == "RHI-ORIGINAL-GENERAL-SOURCE-WRITE-PROFILE");
            require(device.beginOriginalGeneralDescriptorWrite(
                *object, pipeline, sources, 0u, whole.data(), Count, write, writeQuotas));
            drain();
            require(object->finishOriginalGeneralWrite());
            for (std::uint32_t version = 1u; version <= 4u; ++version) {
                auto old = budget;
                old.contractVersion = version;
                if (version <= 2u) {
                    old.finitePipelineSourceIndex = SIZE_MAX;
                    old.finiteFootprintContractVersion = 0u;
                    old.maxFrozenDynamicOffsets = 0u;
                    old.finiteFullBoundViewOptIn = false;
                } else
                    old.finiteFootprintContractVersion = 1u;
                assert(!Set::prepareOriginal(
                    object, arena, sources, whole.data(), Count, old, quota, device, id));
                assert(core::StringView(id) == "RHI-ORIGINAL-GENERAL-SOURCE-LEGACY-PROFILE");
            }
            auto wholeSource = Set::prepareOriginal(
                object, arena, sources, whole.data(), Count, budget, quota, device, id);
            require(id);
            assert(wholeSource && !object && wholeSource->frozenFiniteDeclarations().empty());
            assert(!wholeSource->nativeExecutionProven());
            require(Set::validateOriginal(wholeSource, device));
            std::puts(
                "BINDLESS_RHI_GENERAL_SOURCE5_WHOLE2_OK all-whole-GeneralWrite2 accepted5 legacy1-4-refused no-profile-inference");
        }
        fresh();
        CpuQuotaFactoryFixture pressure(quota);
        const auto factory = [&] {
            return freeze(budget);
        };
        // The hot exhausted probe has already raised this domain's historical
        // peak to its limit. Measure genuine one-shot admission independently
        // of that history, retaining the original quota and candidate lifecycle.
        const auto peak = pressure.measureSingleUse(fresh, factory);
        pressure.verifySingleUse(peak, fresh, factory);
        fresh();
        auto result = factory();
        require(id);
        assert(result);
        assert(result->sourceContractVersion() == 5u && result->frozenFiniteDeclarations().empty()
            && result->frozenElements().size() == Count && !result->originalResourceSources()
            && !result->nativeExecutionProven());
        require(Sources::appendOriginalTyped(sources, {result}, device));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(
                !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
                    if (const auto* error = Sources::validateRegisteredLifetime(sources, device))
                        return error;
                    return Set::validateOriginal(result, device);
                }));
        });
        std::uint32_t owners = 0u;
        require(sources->cpuOwnerCount(1u, UINT32_MAX, owners));
        assert(owners && sources->cpuOwnerCount(1u, owners - 1u, owners));
        std::printf(
            "BINDLESS_RHI_GENERAL_SOURCE5_OK %s bytes%llu frozen-sequence same-pipeline-set-revision native-owners receipts exact-less-exhausted pending-failed-replacements legacy-refused hot0-query0 execution-closed\n",
            budget.maxFrozenDynamicOffsets ? "mixed-finite" : "mixed-whole",
            static_cast<unsigned long long>(peak));
        return std::make_pair(std::move(result), std::move(writeQuotas));
    }
#endif
};
} // namespace arp::test
