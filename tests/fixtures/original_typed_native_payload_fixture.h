#pragma once

#include "original_descriptor_native_identity_fixture.h"
#include "original_typed_native_cold_contract_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_native_payload.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalTypedNativePayloadFixture final {
    using C = rhi::OriginalTypedNativePayloadContract;
    using P = rhi_webgpu::WebGpuOriginalTypedNativePayload;
    using D = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
    using K = rhi::CommandUsageKind;
    using Work = C::Work;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        const auto work = OriginalTypedNativeColdContractFixture::required().work;
        const C::Request request{1u, true, work, 0u, 64u, 65536u};
        assert(!C::validateIntent(request));
        auto table = request;
        table.contractVersion = 2u;
        assert(!C::validateIntent(table));
        auto general = request;
        general.contractVersion = 6u;
        assert(!C::validateIntent(general) && C::profileMatches(general, 6u, 8u));
        for (std::uint32_t old = 1u; old <= 5u; ++old) {
            auto legacy = general;
            legacy.contractVersion = old;
            assert(!C::profileMatches(legacy, 6u, 8u));
            assert(!C::profileMatches(general, old, 8u));
        }
        for (const auto closure : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 9u})
            assert(!C::profileMatches(general, 6u, closure));
        auto generalBad = general;
        generalBad.prototypeOptIn = false;
        is(C::validateIntent(generalBad), "RHI-ORIGINAL-TYPED-PAYLOAD-OPT-IN");
        generalBad = general;
        generalBad.maxReadbackCopies = 1u;
        is(C::validateIntent(generalBad), "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-SCHEMA");
        generalBad = general;
        generalBad.maxWork.indexBinds = generalBad.maxWork.indexedDraws = 1u;
        is(C::validateIntent(generalBad), "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-SCHEMA");
        generalBad = general;
        generalBad.contractVersion = 8u;
        is(C::validateIntent(generalBad), "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE");
        auto geometry = table;
        geometry.contractVersion = 4u;
        geometry.maxWork = {};
        geometry.maxWork.renderPasses = 2u;
        geometry.maxWork.pipelineBinds = geometry.maxWork.descriptorBinds = 2u;
        geometry.maxWork.indexBinds = 2u;
        geometry.maxWork.indexedDraws = 3u;
        geometry.maxWork.dependencies = 2u;
        geometry.maxWork.implicitAccesses = 10u;
        geometry.maxWork.attachmentReferences = 2u;
        assert(!rhi::OriginalTypedNativeColdContract::inspectWorkTotals(
            geometry.maxWork, geometry.maxWork.commands, geometry.maxWork.nativeCalls));
        assert(geometry.maxWork.commands == 16u && geometry.maxWork.nativeCalls == 15u);
        assert(!C::validateIntent(geometry) && C::profileMatches(geometry, 4u, 6u));
        for (const auto freeze : {0u, 1u, 2u, 3u, 5u})
            assert(!C::profileMatches(geometry, freeze, 6u));
        for (const auto closure : {0u, 2u, 3u, 4u, 5u, 7u})
            assert(!C::profileMatches(geometry, 4u, closure));
        assert(!C::validateWork(geometry.maxWork, 0u, 0u, 0u, geometry));
        for (const auto member : {&Work::indexBinds, &Work::indexedDraws}) {
            auto less = geometry;
            --(less.maxWork.*member);
            is(C::validateWork(geometry.maxWork, 0u, 0u, 0u, less),
                "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        }
        auto vertex = geometry;
        vertex.maxWork.vertexBinds = 2u;
        assert(!rhi::OriginalTypedNativeColdContract::inspectWorkTotals(
            vertex.maxWork, vertex.maxWork.commands, vertex.maxWork.nativeCalls));
        assert(vertex.maxWork.commands == 18u && vertex.maxWork.nativeCalls == 17u);
        assert(!C::validateWork(vertex.maxWork, 0u, 0u, 0u, vertex));
        auto vertexLess = vertex;
        --vertexLess.maxWork.vertexBinds;
        is(C::validateWork(vertex.maxWork, 0u, 0u, 0u, vertexLess),
            "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        auto changed = vertex.maxWork;
        --changed.vertexBinds;
        assert(!vertex.maxWork.matches(changed));
        changed = vertex.maxWork;
        --changed.indexBinds;
        assert(!vertex.maxWork.matches(changed));
        changed = vertex.maxWork;
        --changed.indexedDraws;
        assert(!vertex.maxWork.matches(changed));
        for (const auto version : {1u, 2u, 3u}) {
            auto legacy = geometry;
            legacy.contractVersion = version;
            is(C::validateIntent(legacy), "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-SCHEMA");
            auto cold = OriginalTypedNativeColdContractFixture::request(
                OriginalTypedNativeColdContractFixture::required());
            cold.contractVersion = version;
            cold.maxWork = geometry.maxWork;
            is(rhi::OriginalTypedNativeColdContract::validateIntent(cold),
                "RHI-ORIGINAL-TYPED-COLD-GEOMETRY-SCHEMA");
        }
        auto geometryBad = geometry;
        geometryBad.maxWork.renderPasses = 0u;
        is(C::validateIntent(geometryBad), "RHI-ORIGINAL-TYPED-PAYLOAD-PASS-PROFILE");
        geometryBad = geometry;
        geometryBad.maxWork.indexBinds = 0u;
        is(C::validateIntent(geometryBad), "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-BUDGET");
        geometryBad = geometry;
        geometryBad.maxReadbackCopies = 1u;
        is(C::validateIntent(geometryBad), "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-SCHEMA");
        for (const auto member : {&Work::vertexBinds, &Work::indexBinds, &Work::indexedDraws}) {
            auto overflowing = geometry.maxWork;
            overflowing.*member = UINT64_MAX;
            std::uint64_t commands = 99u, calls = 99u;
            is(rhi::OriginalTypedNativeColdContract::inspectWorkTotals(
                   overflowing, commands, calls),
                "RHI-ORIGINAL-TYPED-COLD-COUNT-OVERFLOW");
            assert(!commands && !calls);
        }
        assert(C::profileMatches(request, 1u, 2u) && C::profileMatches(table, 2u, 3u));
        assert(!C::profileMatches(request, 2u, 3u) && !C::profileMatches(table, 1u, 2u));
        assert(!C::profileMatches(table, 2u, 2u) && !C::profileMatches(table, 1u, 3u));
        auto mixed = table;
        mixed.contractVersion = 3u;
        mixed.maxTransferPasses = 1u;
        mixed.maxReadbackCommands = 2u;
        mixed.maxReadbackCopies = 3u;
        mixed.maxWork.commands += 4u;
        mixed.maxWork.nativeCalls += 3u;
        assert(!C::validateIntent(mixed) && C::profileMatches(mixed, 3u, 4u));
        assert(!C::profileMatches(request, 3u, 4u) && !C::profileMatches(table, 3u, 4u)
            && !C::profileMatches(mixed, 2u, 3u) && !C::profileMatches(mixed, 3u, 3u));
        assert(!C::validateWork(mixed.maxWork, 1u, 2u, 3u, mixed));
        auto transferBad = mixed;
        --transferBad.maxReadbackCommands;
        is(C::validateWork(mixed.maxWork, 1u, 2u, 3u, transferBad),
            "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-CAPACITY");
        transferBad = mixed;
        --transferBad.maxReadbackCopies;
        is(C::validateWork(mixed.maxWork, 1u, 2u, 3u, transferBad),
            "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-CAPACITY");
        transferBad = mixed;
        --transferBad.maxTransferPasses;
        is(C::validateIntent(transferBad), "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-BUDGET");
        transferBad = mixed;
        transferBad.maxReadbackCommands = 0u;
        is(C::validateIntent(transferBad), "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-BUDGET");
        transferBad = mixed;
        transferBad.contractVersion = 2u;
        is(C::validateIntent(transferBad), "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-SCHEMA");
        is(C::validateWork(mixed.maxWork, 1u, 2u, 1u, mixed),
            "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-REQUIREMENTS");
        is(C::validateWork(work, 0u, 0u, 0u, mixed),
            "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-REQUIREMENTS");
        auto wrongTotals = mixed.maxWork;
        --wrongTotals.nativeCalls;
        is(C::validateWork(wrongTotals, 1u, 2u, 3u, mixed),
            "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-TOTALS");
        std::uint64_t totalCommands = 1u, totalCalls = 1u;
        is(C::inspectWorkTotals(work, 1u, UINT64_MAX, 1u, totalCommands, totalCalls),
            "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-OVERFLOW");
        assert(!totalCommands && !totalCalls);
        is(C::inspectWorkTotals(work, 1u, 1u, UINT64_MAX, totalCommands, totalCalls),
            "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-OVERFLOW");
        assert(!totalCommands && !totalCalls);
        is(C::inspectWorkTotals(work, UINT64_MAX, 1u, 1u, totalCommands, totalCalls),
            "RHI-ORIGINAL-TYPED-PAYLOAD-COUNT-OVERFLOW");
        assert(!totalCommands && !totalCalls);
        auto wrong = request;
        wrong.contractVersion = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-TYPED-PAYLOAD-VERSION");
        wrong = request;
        wrong.prototypeOptIn = false;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-TYPED-PAYLOAD-OPT-IN");
        wrong = request;
        wrong.maxCpuMetadataBytes = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-TYPED-PAYLOAD-OWNER-CPU-BUDGET");
        wrong = request;
        wrong.maxWork.nativeCalls = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-TYPED-PAYLOAD-WORK-BUDGET");
        wrong = request;
        wrong.maxWork.computePasses = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-TYPED-PAYLOAD-PASS-PROFILE");
        auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto generalDecision =
                        arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
                            backend, 6u);
                    assert(generalDecision.payloadAvailable == (backend == rhi::Backend::WebGPU));
                    assert(!generalDecision.recordingAvailable
                        && !generalDecision.executionAvailable && generalDecision.diagnostic);
                    assert((generalDecision.disposition
                               == arp::rhi::semantics::OriginalCommandNativeClosureLowering::
                                   Disposition::ValidatorOnly)
                        == (backend == rhi::Backend::Null));
                    const auto geometryDecision =
                        arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
                            backend, 4u);
                    assert(geometryDecision.payloadAvailable == (backend == rhi::Backend::WebGPU));
                    assert(!geometryDecision.recordingAvailable
                        && !geometryDecision.executionAvailable && geometryDecision.diagnostic);
                    assert((geometryDecision.disposition
                               == arp::rhi::semantics::OriginalCommandNativeClosureLowering::
                                   Disposition::ValidatorOnly)
                        == (backend == rhi::Backend::Null));
                    const auto mixedDecision =
                        arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
                            backend, 3u);
                    assert(mixedDecision.payloadAvailable == (backend == rhi::Backend::WebGPU));
                    assert(!mixedDecision.recordingAvailable && !mixedDecision.executionAvailable
                        && mixedDecision.diagnostic);
                    assert((mixedDecision.disposition
                               == arp::rhi::semantics::OriginalCommandNativeClosureLowering::
                                   Disposition::ValidatorOnly)
                        == (backend == rhi::Backend::Null));
                    const auto tableDecision =
                        arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
                            backend, 2u);
                    assert(tableDecision.payloadAvailable == (backend == rhi::Backend::WebGPU));
                    assert(!tableDecision.recordingAvailable && !tableDecision.executionAvailable
                        && tableDecision.diagnostic);
                    assert((tableDecision.disposition
                               == arp::rhi::semantics::OriginalCommandNativeClosureLowering::
                                   Disposition::ValidatorOnly)
                        == (backend == rhi::Backend::Null));
                    const auto d =
                        arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(backend);
                    assert(d.payloadAvailable == (backend == rhi::Backend::WebGPU));
                    assert(!d.recordingAvailable && !d.executionAvailable && d.diagnostic);
                    assert((d.disposition
                               == arp::rhi::semantics::OriginalCommandNativeClosureLowering::
                                   Disposition::ValidatorOnly)
                        == (backend == rhi::Backend::Null));
                }
                assert(arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
                    static_cast<rhi::Backend>(255u))
                        .diagnostic);
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto graphicsPayload =
                        arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
                            backend, 8u);
                    assert(graphicsPayload.payloadAvailable == (backend == rhi::Backend::WebGPU));
                    assert(!graphicsPayload.recordingAvailable
                        && !graphicsPayload.executionAvailable && graphicsPayload.diagnostic);
                }
                assert(!arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(
                    rhi::Backend::WebGPU, 9u)
                        .payloadAvailable);
                const auto refused = P::inspectOriginal({}, request);
                assert(refused.diagnostic && !refused.work.commands && !refused.colorAttachments);
                const char* diagnostic = nullptr;
                assert(!P::prepareOriginal({}, request, diagnostic) && diagnostic
                    && P::validateOriginal({}));
                auto huge = work;
                huge.dependencies = UINT64_MAX;
                std::uint64_t commands = 5u, calls = 5u;
                assert(
                    rhi::OriginalTypedNativeColdContract::inspectWorkTotals(huge, commands, calls)
                    && !commands && !calls);
                return nullptr;
            }));
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_CONTRACT_OK payload1 explicit-opt-in original-work-ceilings shared-overflow-validation output-clearing four-execution-gates-closed hot0");
    }
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
    static P::Request requestFor(
        const std::shared_ptr<const D>& decision, std::uint32_t version = 1u)
    {
        const auto& closure = decision->originalClosure();
        const auto& stream = *closure->originalStream();
        const auto& arguments = *stream.originalInputs()->originalScalars()->originalArguments();
        Work work;
        std::uint64_t colors = 0u, transfers = 0u, readbacks = 0u, copies = 0u;
        // Independently authored fixture ceilings from the full original stream,
        // never using runtime inspection to manufacture a production budget.
        for (const auto& operation : stream.operations()) {
            ++work.commands;
            work.implicitAccesses += operation.implicitAccessCount;
            switch (operation.kind) {
            case K::BeginComputePass:
                ++work.computePasses;
                break;
            case K::BeginTransferPass:
                ++transfers;
                break;
            case K::TextureReadback:
                ++readbacks;
                copies += operation.elementCount;
                break;
            case K::BeginRenderPass: {
                ++work.renderPasses;
                work.attachmentReferences += operation.referenceCount;
                colors += std::get<std::shared_ptr<const rhi::RenderPassArguments>>(
                    stream.originalInputs()->descriptors()[operation.descriptorIndex].arguments)
                              ->desc()
                              .colorAttachments.size();
                break;
            }
            case K::BindComputePipeline:
            case K::BindGraphicsPipeline:
                ++work.pipelineBinds;
                break;
            case K::BindComputeDescriptor:
            case K::BindRenderDescriptor:
            case K::BindComputeDescriptorTable:
            case K::BindRenderDescriptorTable:
                ++work.descriptorBinds;
                work.dynamicOffsets += arguments.records()[operation.commandIndex].offsetCount;
                break;
            case K::Draw:
                ++work.draws;
                break;
            case K::BindVertexBuffer:
                ++work.vertexBinds;
                break;
            case K::BindIndexBuffer:
                ++work.indexBinds;
                break;
            case K::DrawIndexed:
                ++work.indexedDraws;
                break;
            case K::Dispatch:
                ++work.dispatches;
                break;
            case K::SetViewport:
                ++work.viewports;
                break;
            case K::SetScissor:
                ++work.scissors;
                break;
            case K::BufferBarrier:
            case K::TextureBarrier:
            case K::BufferMemoryBarrier:
            case K::TextureMemoryBarrier:
                ++work.dependencies;
                break;
            default:
                break;
            }
        }
        std::uint64_t commands = 0u;
        assert(!C::inspectWorkTotals(work, transfers, readbacks, copies, commands, work.nativeCalls)
            && commands == work.commands);
        return {version,
            true,
            work,
            colors,
            decision->retainedOwnerCount() + C::additionalOwners,
            closure->originalMetadataQuota()->limitBytes(),
            transfers,
            readbacks,
            copies};
    }

    template <std::size_t Count>
    static std::shared_ptr<const P> verify(const std::shared_ptr<const D>& decision,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas,
        const char* label,
        std::uint32_t version = 1u,
        std::uint32_t authoredOwnerCeiling = 0u)
    {
        const auto& closure = decision->originalClosure();
        const auto& stream = *closure->originalStream();
        const auto& arguments = *stream.originalInputs()->originalScalars()->originalArguments();
        auto request = requestFor(decision, version);
        const auto ownerDemand = request.maxRetainedOwners;
        if (authoredOwnerCeiling)
            request.maxRetainedOwners = authoredOwnerCeiling;
        const auto& work = request.maxWork;
        const auto colors = request.maxColorAttachments;
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return P::prepareOriginal(decision, request, diagnostic);
        };
        auto payload = factory();
        if (!payload)
            std::fprintf(
                stderr, "typed payload %s: %s\n", label, diagnostic ? diagnostic : "unknown");
        assert(payload);
        assert(payload->records().size() == stream.operations().size()
            && payload->passes().size() == work.renderPasses);
        assert(!payload->nativeExecutionProven() && !payload->synchronizationProven());
        is(payload->executionDiagnostic(), "RHI-WEBGPU-TYPED-PAYLOAD-LIFECYCLE-EXECUTOR-MISSING");
        for (std::size_t i = 0u; i < payload->records().size(); ++i) {
            const auto& record = payload->records()[i];
            assert(record.original.matches(stream.operations()[i]));
            assert(record.scalar.matches(arguments.records()[i].scalar)
                && record.text == arguments.text(i));
            assert(record.offsets == arguments.offsets(i)
                && record.offsetCount == arguments.records()[i].offsetCount);
            if (record.original.kind == K::BindComputePipeline)
                assert(record.compute);
            if (record.original.kind == K::BindGraphicsPipeline)
                assert(record.graphics);
            if (record.original.kind == K::BindVertexBuffer
                || record.original.kind == K::BindIndexBuffer) {
                assert(rhi::OriginalTypedNativeColdContract::geometryProfile(version)
                    && record.geometry && record.buffer && record.binding->indexContent);
                assert(record.pipeline && record.pipeline->object.graphics);
                assert(record.geometry
                    == std::get_if<rhi::OriginalCommandGeometryBinding>(
                        stream.originalInputs()->descriptorAt(i)));
                const auto offset = record.original.kind == K::BindIndexBuffer
                    ? record.scalar.template get<rhi::CommandScalarArguments::IndexBinding>()
                          ->offset
                    : record.scalar.template get<rhi::CommandScalarArguments::VertexBinding>()
                          ->offset;
                assert(record.bufferBindingBytes
                    == record.binding->backing().bufferDesc.sizeBytes - offset);
            }
            if (record.original.kind == K::DrawIndexed)
                assert(rhi::OriginalTypedNativeColdContract::geometryProfile(version)
                    && record.pipeline && record.pipeline->object.graphics);
            if (record.original.kind == K::BindComputeDescriptor
                || record.original.kind == K::BindRenderDescriptor)
                assert(record.group);
            if (record.original.kind == K::BindComputeDescriptorTable
                || record.original.kind == K::BindRenderDescriptorTable) {
                std::uint32_t group = UINT32_MAX;
                assert(record.group && !P::originalPhysicalGroup(record, group));
                assert(group == 0u && record.binding->object.table);
            }
            if (record.dependency)
                assert(record.dependency->original.commandIndex == i
                    && (record.buffer || record.texture));
        }
        std::size_t passOrdinal = 0u;
        for (const auto& pass : payload->passes()) {
            const auto& desc = pass.pass->desc();
            const auto end = std::find_if(
                payload->records().begin(), payload->records().end(), [&](const auto& record) {
                    return record.original.kind == K::EndRenderPass
                        && record.passIndex == passOrdinal;
                });
            assert(end != payload->records().end() && end->pass == pass.pass
                && pass.maxDraw.maxDrawCount == end->passDrawCount);
            ++passOrdinal;
            assert(pass.renderPass.nextInChain == &pass.maxDraw.chain
                && pass.renderPass.colorAttachments == pass.colors.values().data());
            for (std::size_t i = 0u; i < desc.colorAttachments.size(); ++i) {
                const auto& original = desc.colorAttachments[i];
                const auto& native = pass.colors.values()[i];
                assert(native.loadOp
                    == rhi_webgpu::WebGpuAttachmentOperationLowering::load(original.loadOp));
                assert(native.storeOp
                    == rhi_webgpu::WebGpuAttachmentOperationLowering::store(original.storeOp));
            }
        }
        payload.reset();
        CpuQuotaFactoryFixture allocation(closure->originalMetadataQuota());
        const auto bytes = allocation.measureRetained(factory);
        allocation.verify(bytes, factory);
        {
            core::CpuMemoryQuotaLease pressure(closure->originalMetadataQuota(),
                closure->originalMetadataQuota()->limitBytes()
                    - closure->originalMetadataQuota()->liveBytes());
            assert(pressure.charged() && !factory());
        }
        const auto reject = [&](auto change) {
            const auto saved = request;
            change(request);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto inspection = P::inspectOriginal(decision, request);
            assert(
                inspection.diagnostic && !inspection.work.commands && !inspection.colorAttachments);
            assert(!factory() && diagnostic
                && allocations == core::defaultCpuAllocator().stats().allocationCount);
            request = saved;
        };
        reject([](auto& b) {
            b.contractVersion = 0u;
        });
        reject([](auto& b) {
            b.prototypeOptIn = false;
        });
        reject([&](auto& b) {
            b.maxRetainedOwners = ownerDemand - 1u;
        });
        reject([](auto& b) {
            --b.maxCpuMetadataBytes;
        });
        if (colors)
            reject([](auto& b) {
                --b.maxColorAttachments;
            });
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
            if (work.*member)
                reject([&](auto& b) {
                    --(b.maxWork.*member);
                });
        payload = factory();
        assert(payload);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged =
                        std::shared_ptr<const P>(closure->originalMetadataQuota(), payload.get());
                    is(P::validateOriginal(forged), "RHI-WEBGPU-TYPED-PAYLOAD-OWNER");
                    auto forgedDecision =
                        std::shared_ptr<const D>(closure->originalMetadataQuota(), decision.get());
                    assert(!P::prepareOriginal(forgedDecision, request, diagnostic));
                    for (const auto& source : stream.originalInputs()
                             ->originalScalars()
                             ->originalUsage()
                             ->originalSources()
                             ->sources())
                        std::visit(
                            [&](const auto& value) {
                                using T = std::remove_const_t<
                                    typename std::decay_t<decltype(value)>::element_type>;
                                if constexpr (std::is_same_v<T,
                                                  rhi::OriginalCommandDescriptorSetSource>) {
                                    const auto native = rhi_webgpu::webGpuNativePayloadFor(
                                        *value->originalObject());
                                    OriginalDescriptorNativeIdentityFixture::mutateActual(
                                        native, [&] {
                                            assert(payload->validate() && !factory());
                                        });
                                } else if constexpr (std::is_same_v<T,
                                                         rhi::OriginalCommandComputePipelineSource>
                                    || std::is_same_v<T,
                                        rhi::OriginalCommandGraphicsPipelineSource>) {
                                    const auto native = rhi_webgpu::webGpuNativePayloadFor(
                                        *value->originalPipeline());
                                    const auto handle = native->pipeline;
                                    native->pipeline = nullptr;
                                    assert(payload->validate() && !factory());
                                    native->pipeline = handle;
                                }
                            },
                            source);
                    for (const auto& pass : payload->passes()) {
                        auto* chain = const_cast<WGPURenderPassMaxDrawCount*>(&pass.maxDraw);
                        const auto bound = chain->maxDrawCount;
                        ++chain->maxDrawCount;
                        is(payload->validate(), "RHI-WEBGPU-TYPED-PAYLOAD-PASS-FIELDS");
                        chain->maxDrawCount = bound;
                    }
                    for (const auto& record : payload->records())
                        if (record.dependency) {
                            if (record.buffer) {
                                const auto native = record.binding->backing().buffer;
                                const auto original = native->buffer;
                                native->buffer = nullptr;
                                assert(payload->validate() && !factory());
                                native->buffer = original;
                            } else {
                                const auto native = record.binding->backing().texture;
                                const auto original = native->texture;
                                native->texture = nullptr;
                                assert(payload->validate() && !factory());
                                native->texture = original;
                            }
                        }
                    for (const auto& record : payload->records())
                        if (record.geometry) {
                            auto& mutableRecord = const_cast<P::Record&>(record);
                            const auto bytes = record.bufferBindingBytes;
                            --mutableRecord.bufferBindingBytes;
                            is(payload->validate(), "RHI-WEBGPU-TYPED-PAYLOAD-ORIGINAL-BINDING");
                            mutableRecord.bufferBindingBytes = bytes;
                            const auto* geometry = record.geometry;
                            mutableRecord.geometry = nullptr;
                            is(payload->validate(), "RHI-WEBGPU-TYPED-PAYLOAD-ORIGINAL-BINDING");
                            mutableRecord.geometry = geometry;
                            const auto native = record.binding->backing().buffer;
                            const auto buffer = native->buffer;
                            native->buffer = nullptr;
                            assert(payload->validate() && !factory());
                            native->buffer = buffer;
                        }
                    return P::validateOriginal(payload);
                }));
        });
        std::printf(
            "%s %s commands%llu calls%llu render%llu compute%llu draws%llu dispatches%llu dependencies%llu colors%llu bytes%llu original-native-bindings exact-pass-draw-bound exact-less-exhausted forged-native-mutated-reject hot0-query0 execution-closed\n",
            version == 4u ? "BINDLESS_RHI_GEOMETRY_NATIVE_PAYLOAD_FROZEN_OK"
                          : "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK",
            label,
            static_cast<unsigned long long>(work.commands),
            static_cast<unsigned long long>(work.nativeCalls),
            static_cast<unsigned long long>(work.renderPasses),
            static_cast<unsigned long long>(work.computePasses),
            static_cast<unsigned long long>(work.draws),
            static_cast<unsigned long long>(work.dispatches),
            static_cast<unsigned long long>(work.dependencies),
            static_cast<unsigned long long>(colors),
            static_cast<unsigned long long>(bytes));
        return payload;
    }

    static std::shared_ptr<const P> retainForActualLoss(
        const std::shared_ptr<const D>& decision, const char* label)
    {
        const char* diagnostic = nullptr;
        auto payload = P::prepareOriginal(decision, requestFor(decision), diagnostic);
        assert(payload && !diagnostic && !payload->validate());
        std::printf(
            "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_RETAINED_OK %s authentic-loss-closure before-pending-and-physical-loss execution-closed\n",
            label);
        return payload;
    }
#endif
    static void verifyUnavailable(const std::shared_ptr<const P>& payload, const char* reason)
    {
        if (!payload)
            return;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(payload->validate());
        if (core::StringView(reason) == "finite-physical-loss"
            || core::StringView(reason) == "whole-physical-loss")
            is(payload->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        const char* diagnostic = nullptr;
        assert(
            !P::prepareOriginal(payload->originalDecision(), payload->originalRequest(), diagnostic)
            && diagnostic);
        const auto refused =
            P::inspectOriginal(payload->originalDecision(), payload->originalRequest());
        assert(refused.diagnostic && !refused.work.commands && !refused.colorAttachments);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::printf(
            "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_REJECT_OK %s genuine-frozen-native-input original-root-stale counts-cleared hot0 execution-closed\n",
            reason);
    }
};
} // namespace arp::test
