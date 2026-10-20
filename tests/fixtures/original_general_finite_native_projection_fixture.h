#pragma once
#include "original_general_finite_graphics_usage_fixture.h"
#include "original_general_finite_usage_fixture.h"
#include "original_typed_native_cold_contract_fixture.h"
#include "original_typed_native_payload_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_native_payload.h"

namespace arp::test {
class OriginalGeneralFiniteNativeProjectionFixture final {
    using F = rhi::OriginalTypedDependencyProjectionContract;
    using P = rhi::OriginalTypedNativePayloadContract;

public:
    static void verifyContract()
    {
        const auto projection = OriginalGeneralFiniteUsageFixture::dependencyBudget();
        const F::FreezeRequest frozen{7u, true, projection, 128u, 65536u};
        assert(!F::validateFreezeIntent(frozen) && F::closureProfileMatches(7u, 9u));
        assert(!F::closureProfileMatches(7u, 8u) && !F::closureProfileMatches(6u, 9u));
        for (std::uint32_t old = 1u; old <= 6u; ++old) {
            assert(!F::closureProfileMatches(old, 9u) && !F::closureProfileMatches(7u, old));
            auto wrong = frozen;
            wrong.contractVersion = old;
            assert(F::validateFreezeIntent(wrong));
            wrong = frozen;
            wrong.projection.contractVersion = old;
            if (old != 6u)
                assert(F::validateFreezeIntent(wrong));
        }
        auto wrong = frozen;
        wrong.prototypeOptIn = false;
        assert(F::validateFreezeIntent(wrong));
        wrong = frozen;
        wrong.maxRetainedOwners = 0u;
        assert(F::validateFreezeIntent(wrong));
        wrong = frozen;
        wrong.maxCpuMetadataBytes = 0u;
        assert(F::validateFreezeIntent(wrong));
        wrong = frozen;
        wrong.contractVersion = 8u;
        assert(F::validateFreezeIntent(wrong));
        wrong = frozen;
        wrong.projection.usages.projection.shaderConsumption.generalShaderResourcesVersion = 1u;
        assert(F::validateFreezeIntent(wrong));

        const auto work = OriginalTypedNativeColdContractFixture::required().work;
        const P::Request payload{7u, true, work, 0u, 128u, 65536u};
        assert(!P::validateIntent(payload) && P::profileMatches(payload, 7u, 9u));
        assert(!P::profileMatches(payload, 6u, 8u) && !P::profileMatches(payload, 7u, 8u));
        for (std::uint32_t old = 0u; old <= 6u; ++old) {
            assert(!P::profileMatches(payload, old, 9u) && !P::profileMatches(payload, 7u, old));
            auto legacy = payload;
            legacy.contractVersion = old;
            assert(!P::profileMatches(legacy, 7u, 9u));
        }
        auto bad = payload;
        bad.prototypeOptIn = false;
        assert(P::validateIntent(bad));
        bad = payload;
        bad.maxReadbackCopies = 1u;
        assert(P::validateIntent(bad));
        bad = payload;
        bad.maxWork.indexBinds = 1u;
        assert(P::validateIntent(bad));
        bad = payload;
        bad.contractVersion = 8u;
        assert(P::validateIntent(bad));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto freeze =
                rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                    backend, 7u);
            const auto native =
                rhi::semantics::OriginalTypedNativePayloadLowering::lowering(backend, 7u);
            assert(freeze.projectionAvailable == (backend == rhi::Backend::WebGPU)
                && !freeze.executionAvailable);
            assert(native.payloadAvailable == (backend == rhi::Backend::WebGPU)
                && !native.recordingAvailable && !native.executionAvailable);
            assert(rhi::semantics::OriginalTypedNativeColdLowering::admissionLowering(backend, 7u)
                       .admissionAvailable
                == (backend == rhi::Backend::WebGPU));
            const auto execution =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 7u);
            assert(execution.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && !execution.submissionAvailable);
            assert(rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                       backend, 8u)
                       .projectionAvailable
                == (backend == rhi::Backend::WebGPU));
            assert(rhi::semantics::OriginalTypedNativePayloadLowering::lowering(backend, 8u)
                       .payloadAvailable
                == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_FROZEN7_CONTRACT_OK Projection6 Closure9 Freeze7 Payload7 exact-opt-in legacy-isolated four-backends no-execution-authority");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static auto verifyGraphicsFreezeActual(
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalNativeClosure>& closure,
        std::uint32_t closureOwnerCeiling)
    {
        using Freeze = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
        using Dependencies = rhi::common::OriginalTypedDependencyProjection;
        const auto& root = closure->originalRoot();
        const auto quota = closure->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        const auto projection =
            OriginalGeneralFiniteGraphicsUsageFixture::dependencyBudget(true, true);
        const F::FreezeRequest request{
            8u, true, projection, closureOwnerCeiling + 1u, quota->limitBytes()};
        const char* id = nullptr;
        const auto make = [&](const F::FreezeRequest& value) {
            return Freeze::prepareOriginal(closure, value, id);
        };
        const auto create = [&] {
            return make(request);
        };
        const auto refuse = [&](const F::FreezeRequest& value) {
            assert(!make(value) && id && quota->liveBytes() == baseline && root->journalEmpty());
            assert(!root->validate());
        };
        for (std::uint32_t old = 1u; old <= 7u; ++old) {
            auto bad = request;
            bad.contractVersion = old;
            refuse(bad);
        }
        auto bad = request;
        bad.prototypeOptIn = false;
        refuse(bad);
        bad = request;
        bad.maxRetainedOwners = closure->retainedOwnerCount();
        refuse(bad);
        bad = request;
        bad.maxRetainedOwners = closureOwnerCeiling + 2u;
        refuse(bad);
        bad = request;
        --bad.maxCpuMetadataBytes;
        refuse(bad);
        bad = request;
        bad.projection.contractVersion = 5u;
        refuse(bad);
        bad = request;
        bad.projection.usages.projection.maxAttachmentReferences = 0u;
        refuse(bad);
        for (const auto member : {&F::Request::maxDependencies,
                 &F::Request::maxProjectionPasses,
                 &F::Request::maxContributionVisits,
                 &F::Request::maxPairChecks,
                 &F::Request::maxEndpointContributions,
                 &F::Request::maxTextureCells}) {
            bad = request;
            --(bad.projection.*member);
            refuse(bad);
        }
        auto forgedClosure =
            std::shared_ptr<const rhi_webgpu::WebGpuCanonicalNativeClosure>(quota, closure.get());
        assert(!Freeze::prepareOriginal(forgedClosure, request, id) && id
            && quota->liveBytes() == baseline);
        forgedClosure.reset();
        auto admitted = create();
        if (id)
            std::fprintf(stderr, "Graphics Freeze8 admission: %s\n", id);
        assert(admitted && !id);
        admitted.reset();
        CpuQuotaFactoryFixture pressure(quota);
        const auto exact = pressure.measureSingleUse([] {}, create);
        pressure.verify(exact, create);
        {
            core::CpuMemoryQuotaLease full(quota, quota->limitBytes() - baseline);
            assert(full.charged() && !create() && id);
        }
        auto frozen = create();
        assert(frozen && !id);
        const auto& counts = frozen->counts();
        assert(counts.dependencies == 6u && counts.initial == 2u && counts.final == 4u
            && !counts.between && counts.endpointContributions == 14u && counts.textureCells == 7u
            && counts.projectionPasses == 24u && counts.contributionVisits == 168u
            && counts.pairChecks == 21u && frozen->records().size() == 6u
            && frozen->retainedOwnerCount() == closure->retainedOwnerCount() + 1u
            && !frozen->nativeExecutionProven() && !frozen->synchronizationProven()
            && frozen->executionDiagnostic());
        auto forged = std::shared_ptr<const Freeze>(quota, frozen.get());
        assert(Freeze::validateOriginal(forged));
        forged.reset();
        for (std::size_t index = 0u; index < frozen->records().size(); ++index) {
            auto& record = const_cast<Freeze::Record&>(frozen->records()[index]);
            const auto original = record;
            for (std::uint32_t field = 0u; field < 5u; ++field) {
                if (field == 0u)
                    ++record.original.registrationGeneration;
                if (field == 1u)
                    ++record.original.descriptorIndex;
                if (field == 2u)
                    record.original.projectionVersion = 5u;
                if (field == 3u)
                    ++(record.original.preceding ? record.original.preceding
                                                 : record.original.following)
                          ->contributions;
                if (field == 4u)
                    record.candidate = Freeze::CandidateOperation::InterScopeCommandListBoundary;
                assert(frozen->validate());
                record = original;
                assert(!frozen->validate());
            }
            Dependencies::Counts binding;
            assert(!Dependencies::validateFrozenBinding(closure->originalStream(),
                root->originalGraph(),
                projection,
                record.original,
                binding));
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota,
                    closure->originalStream()->originalMetadataQuota(),
                    root->originalGraph()->originalMetadataQuota(),
                    root->originalGraph()->originalInputs()->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    return frozen->validate();
                }));
        });
        assert(root->journalEmpty()
            && root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared);
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_OK Closure10 ContentPlan6 commands14 dependencies6 initial2 final4 endpoints14 cells7 passes24 visits168 pairs21 freezeBytes%llu owners%u exact-less-exhausted hostile-clauses-refused same-Prepared-root hot0-query0 payload-native-closed\n",
            static_cast<unsigned long long>(exact),
            frozen->retainedOwnerCount());
        return frozen;
    }

    template <std::size_t Count>
    static auto verifyGraphicsPayloadActual(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedDependencyDecision>& frozen,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas)
    {
        using Payload = rhi_webgpu::WebGpuOriginalTypedNativePayload;
        const auto& root = frozen->originalClosure()->originalRoot();
        const auto ownerCeiling = frozen->originalRequest().maxRetainedOwners + P::additionalOwners;
        auto payload = OriginalTypedNativePayloadFixture::verify(
            frozen, quotas, "general-finite-graphics10", 8u, ownerCeiling);
        const auto& work = payload->work();
        assert(work.commands == 14u && work.nativeCalls == 9u && work.renderPasses == 1u
            && !work.computePasses && work.pipelineBinds == 1u && work.descriptorBinds == 1u
            && work.draws == 1u && !work.dispatches && work.dependencies == 6u
            && work.implicitAccesses == 8u && work.viewports == 1u && work.scissors == 1u
            && work.attachmentReferences == 1u && payload->passes().size() == 1u
            && !work.hasGeometry() && !payload->transferPassCount()
            && !payload->readbackCommandCount() && !payload->readbackCopyCount());
        auto request = payload->originalRequest();
        const auto live = root->originalMetadataQuota()->liveBytes();
        const char* id = nullptr;
        for (std::uint32_t old = 1u; old <= 7u; ++old) {
            auto legacy = request;
            legacy.contractVersion = old;
            assert(!Payload::prepareOriginal(frozen, legacy, id) && id
                && root->originalMetadataQuota()->liveBytes() == live);
        }
        auto tooMany = request;
        ++tooMany.maxRetainedOwners;
        assert(!Payload::prepareOriginal(frozen, tooMany, id)
            && core::StringView(id) == "RHI-WEBGPU-TYPED-PAYLOAD-OWNER-CAPACITY"
            && root->originalMetadataQuota()->liveBytes() == live);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    for (const auto& record : payload->records()) {
                        auto& changed = const_cast<Payload::Record&>(record);
                        const auto original = record;
                        if (record.original.kind == rhi::CommandUsageKind::BindRenderDescriptor) {
                            changed.offsetCount = original.offsetCount + 1u;
                            assert(payload->validate());
                            changed = original;
                        }
                        if (record.original.kind == rhi::CommandUsageKind::EndRenderPass) {
                            changed.passDrawCount = original.passDrawCount + 1u;
                            assert(payload->validate());
                            changed = original;
                        }
                    }
                    for (const auto& pass : payload->passes()) {
                        auto& color =
                            const_cast<WGPURenderPassColorAttachment&>(pass.colors.values()[0]);
                        const auto original = color;
                        color.view = nullptr;
                        assert(payload->validate());
                        color = original;
                        color.loadOp = WGPULoadOp_Undefined;
                        assert(payload->validate());
                        color = original;
                        color.storeOp = WGPUStoreOp_Undefined;
                        assert(payload->validate());
                        color = original;
                    }
                    return payload->validate();
                }));
        });
        assert(root->journalEmpty()
            && root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared);
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_OK Freeze8 Closure10 ContentPlan6 commands14 calls9 render1 compute0 pipeline1 group1 draw1 dispatch0 dependencies6 implicit8 viewport1 scissor1 attachment1 dynamicOffsets%llu owners%u exact-less-exhausted original-pass-group-attachment-mutated same-Prepared-root hot0-query0 cold-record-execution-closed\n",
            static_cast<unsigned long long>(work.dynamicOffsets),
            payload->retainedOwnerCount());
        return payload;
    }

    template <std::size_t Count>
    static auto verifyActual(
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalNativeClosure>& closure,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas,
        std::uint32_t closureOwnerCeiling)
    {
        using Freeze = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
        using Payload = rhi_webgpu::WebGpuOriginalTypedNativePayload;
        const auto& root = closure->originalRoot();
        const auto& program = closure->originalStream();
        auto usage = OriginalGeneralFiniteUsageFixture::budget();
        usage.maxCommands = 10u;
        usage.renderBindings = OriginalGeneralFiniteConsumptionFixture::renderBudget(10u);
        usage.maxTextureCells = 3u;
        // Same ten-command completed-upload program: three contributions,
        // four dependency clauses and five dependency texture cells.
        const F::Request projection{6u, true, {6u, true, usage}, 4u, 16u, 48u, 3u, 6u, 5u};
        const auto inspected = rhi::common::OriginalTypedDependencyProjection::inspect(
            program, root->originalGraph(), projection);
        if (inspected.diagnostic)
            std::fprintf(stderr, "General frozen7 projection: %s\n", inspected.diagnostic);
        assert(!inspected.diagnostic && inspected.counts.dependencies == 4u
            && inspected.counts.initial == 1u && inspected.counts.final == 3u);
        const auto recording = closure->originalMetadataQuota();
        const F::FreezeRequest request{
            7u, true, projection, closureOwnerCeiling + 1u, recording->limitBytes()};
        const auto baseline = recording->liveBytes();
        const char* id = nullptr;
        const auto make = [&](const F::FreezeRequest& b) {
            return Freeze::prepareOriginal(closure, b, id);
        };
        const auto create = [&] {
            return make(request);
        };
        const auto refuse = [&](const F::FreezeRequest& b) {
            assert(!make(b) && id && recording->liveBytes() == baseline && root->journalEmpty());
        };
        for (std::uint32_t old = 1u; old <= 6u; ++old) {
            auto bad = request;
            bad.contractVersion = old;
            refuse(bad);
        }
        auto bad = request;
        bad.prototypeOptIn = false;
        refuse(bad);
        bad = request;
        bad.maxRetainedOwners = closure->retainedOwnerCount();
        refuse(bad);
        bad = request;
        ++bad.maxRetainedOwners;
        refuse(bad);
        bad = request;
        --bad.maxCpuMetadataBytes;
        refuse(bad);
        CpuQuotaFactoryFixture pressure(recording);
        const auto exact = pressure.measureSingleUse([] {}, create);
        pressure.verify(exact, create);
        {
            core::CpuMemoryQuotaLease full(recording, recording->limitBytes() - baseline);
            assert(full.charged() && !create() && id);
        }
        auto frozen = create();
        assert(frozen && !id && frozen->counts().dependencies == 4u);
        auto forged = std::shared_ptr<const Freeze>(recording, frozen.get());
        assert(Freeze::validateOriginal(forged));
        forged.reset();
        auto payload = OriginalTypedNativePayloadFixture::verify(frozen,
            quotas,
            "general-finite-original9",
            7u,
            request.maxRetainedOwners + P::additionalOwners);
        assert(payload && payload->records().size() == 10u && payload->passes().empty()
            && !payload->nativeExecutionProven() && !payload->synchronizationProven());
        const auto live = recording->liveBytes();
        auto payloadRequest = OriginalTypedNativePayloadFixture::requestFor(frozen, 7u);
        for (std::uint32_t old = 1u; old <= 6u; ++old) {
            auto legacy = payloadRequest;
            legacy.contractVersion = old;
            assert(!Payload::prepareOriginal(frozen, legacy, id) && id
                && recording->liveBytes() == live);
        }
        auto cold = OriginalTypedNativeColdContractFixture::request(
            OriginalTypedNativeColdContractFixture::required());
        cold.contractVersion = 7u;
        cold.maxWork = payloadRequest.maxWork;
        assert(!rhi::OriginalTypedNativeColdContract::validateIntent(cold));
        assert(!rhi::OriginalTypedNativeColdContract::validateLifecycleProfile(cold, 4u));
        assert(rhi::OriginalTypedNativeColdContract::validateLifecycleProfile(cold, 3u));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    if (const auto* error = frozen->validate())
                        return error;
                    return payload->validate();
                }));
        });
        assert(root->journalEmpty() && !root->validate());
        std::printf(
            "BINDLESS_RHI_GENERAL_FROZEN7_OK Closure9 Freeze7 Payload7 commands10 dependencies4 initial1 final3 complete-original-offsets freezeBytes%llu owners%u exact-less-exhausted original-root-journal hot0-query0 no-execution-authority\n",
            static_cast<unsigned long long>(exact),
            payload->retainedOwnerCount());
        return payload;
    }
#endif
};
} // namespace arp::test
