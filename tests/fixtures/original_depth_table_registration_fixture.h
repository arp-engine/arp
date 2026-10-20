#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_depth_table_publication_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"
#include "webgpu_foreign_device_fixture.h"
#include "webgpu_original_device_fixture.h"

namespace arp::test {
class OriginalDepthTableRegistrationFixture final {
    using Join = rhi::OriginalTypedNativeLossJoinContract;
    using Wrapper = rhi::OriginalAcceptedTableExpansionPipelineContract;
    using Definition = rhi_webgpu::WebGpuOriginalDepthTableExpansionPipelineDefinition;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static rhi::OriginalResourceRegistrationBudget budget(bool compute)
    {
        rhi::OriginalResourceRegistrationBudget b;
        b.contractVersion = 8u;
        b.prototypeOptIn = true;
        b.maxPipelineLayouts = b.maxDescriptorArenas = b.maxDescriptorTables = 1u;
        b.maxComputePipelines = compute ? 1u : 0u;
        b.maxGraphicsPipelines = compute ? 0u : 1u;
        b.maxRetainedOwners = 16u;
        b.maxCpuMetadataBytes = bytes;
        return b;
    }

    static void is(const char* id, const char* expected)
    {
        assert(id && core::StringView(id) == expected);
    }

public:
    static void verifyContract()
    {
        auto b = budget(true);
        assert(!Join::validate({2u, true}, b));
        is(Join::validate({1u, true}, b), "RHI-ORIGINAL-TYPED-LOSS-REGISTRATION-VERSION");
        is(Join::validate({2u, false}, b), "RHI-ORIGINAL-TYPED-LOSS-JOIN-OPT-IN");
        auto old = b;
        old.contractVersion = 7u;
        is(Join::validate({2u, true}, old), "RHI-ORIGINAL-TYPED-LOSS-REGISTRATION-VERSION");
        is(Join::validate({1u, true}, old), "RHI-ORIGINAL-TYPED-LOSS-DESCRIPTOR-TABLE-CLOSED");
        auto bad = b;
        bad.maxDescriptorTables = 0u;
        is(Join::validate({2u, true}, bad), "RHI-ORIGINAL-TABLE-REGISTRATION-DESCRIPTOR-BUDGET");
        bad = b;
        bad.maxDescriptorSets = 1u;
        is(Join::validate({2u, true}, bad), "RHI-ORIGINAL-TABLE-REGISTRATION-DESCRIPTOR-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d = Join::lowering(backend, 2u);
            assert(d.registrationAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            assert(!d.scopedGroupJoinAvailable && !d.recordingAvailable && !d.executionAvailable);
            assert(Wrapper::lowering(backend, true).factoryEligible
                == (backend == rhi::Backend::WebGPU));
            is(rhi::OriginalResourceRegistrationContract::lowering(backend, 8u).diagnostic,
                "RHI-ORIGINAL-TYPED-LOSS-EXPLICIT-REQUEST-REQUIRED");
        }
        assert(Wrapper::validateBudget({0u, true, bytes}));
        assert(Wrapper::validateBudget({1u, false, bytes}));
        assert(Wrapper::validateBudget({1u, true, 0u}));
        auto null = rhi::createNullDevice();
        auto metadata = quota();
        rhi::OriginalDeviceNativeLossObservation unissued;
        const auto count = core::defaultCpuAllocator().stats().allocationCount;
        is(null->prepareOriginalResourceRegistration(b, metadata),
            "RHI-ORIGINAL-TYPED-LOSS-EXPLICIT-REQUEST-REQUIRED");
        is(null->prepareOriginalTypedNativeLossRegistration({2u, true}, b, metadata, unissued),
            "RHI-ORIGINAL-TYPED-LOSS-NULL-VALIDATOR-ONLY");
        assert(
            !metadata->liveBytes() && count == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "ORIGINAL_TABLE_REGISTRATION_CONTRACT_OK join2 registry8 explicit-request four-backends ordinary-join1-closed group-recording-execution-closed");
    }

    static void verifyWriteIntent(const rhi::PipelineLayoutDesc& layout)
    {
        using Contract = rhi::PipelineShaderDeclarationContract;
        assert(layout.descriptorTables.size() == 1u);
        const auto& slot = layout.descriptorTables.front();
        rhi::DescriptorTableDesc table{slot.tableName,
            slot.type,
            slot.stages,
            slot.capacity,
            slot.textureViewDimensionConstrained,
            slot.textureViewDimension,
            slot.depthTexture};
        std::array<rhi::DescriptorTableWrite, 3u> writes;
        for (std::uint32_t index = 0u; index < writes.size(); ++index) {
            writes[index].index = index;
            writes[index].type = table.type;
            writes[index].resourceKind = rhi::DescriptorResourceKind::TextureView;
            writes[index].resource = rhi::allocateGpuResourceId();
        }
        const auto validate = [&](const auto& candidate, std::size_t count = 3u) {
            return Contract::validateDepthTableExpansionWrites(
                {1u, true}, layout, table, candidate.data(), count);
        };
        assert(!validate(writes));
        is(Contract::validateDepthTableExpansionWrites(
               {0u, true}, layout, table, writes.data(), writes.size()),
            "RHI-ORIGINAL-TABLE-WRITE-VERSION");
        is(Contract::validateDepthTableExpansionWrites(
               {1u, false}, layout, table, writes.data(), writes.size()),
            "RHI-ORIGINAL-TABLE-WRITE-OPT-IN");
        is(Contract::validateDepthTableExpansionWrites(
               {1u, true}, layout, table, nullptr, writes.size()),
            "RHI-ORIGINAL-TABLE-WRITE-COVERAGE");
        for (const auto count : {0u, 2u, 4u})
            is(validate(writes, count), "RHI-ORIGINAL-TABLE-WRITE-COVERAGE");
        for (std::size_t index = 0u; index < writes.size(); ++index)
            for (std::uint32_t mutation = 0u; mutation < 4u; ++mutation) {
                auto changed = writes;
                if (mutation == 0u)
                    changed[index].index = (index + 1u) % writes.size();
                if (mutation == 1u)
                    changed[index].type = rhi::DescriptorType::StorageTexture;
                if (mutation == 2u)
                    changed[index].resourceKind = rhi::DescriptorResourceKind::Sampler;
                if (mutation == 3u)
                    changed[index].resource = {};
                is(validate(changed), "RHI-ORIGINAL-TABLE-WRITE-ELEMENT");
            }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 0u>{}, [&]() noexcept {
                return validate(writes);
            }));
        std::puts(
            "ORIGINAL_TABLE_WRITE_INTENT_OK ordered3 exact-logical-slot missing-reordered-type-kind-id-reject hot0 no-resource-custody-group-or-execution-proof");
    }

    template <class Desc>
    static void verifyNative(const std::shared_ptr<const Definition::Space>& space,
        const rhi::PipelineLayoutDesc& layoutDesc,
        const Desc& original,
        const Definition::Preflight::Budget& preflight,
        bool retainForLoss = false,
        bool publication = false,
        bool contentCells = false,
        bool execution = false,
        bool readback = false)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        const bool geometry = OriginalDepthTableContentPlanFixture::geometryCompositionRequested;
        const bool geometryLoss =
            geometry && OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested;
        assert(!geometry
            || (!compute && publication && contentCells && readback && !execution
                && !retainForLoss));
        using Pipeline = std::conditional_t<compute, rhi::ComputePipeline, rhi::RenderPipeline>;
        using Actor = std::conditional_t<compute,
            rhi_webgpu::WebGpuOriginalArtifactComputeCreation,
            rhi_webgpu::WebGpuOriginalArtifactGraphicsCreation>;
        const char* id = nullptr;
        // Lifetime, registry, subscription, layout/arena wrappers, definition,
        // actor, native payload, callbacks and accepted wrapper are independent.
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> q;
        for (auto& domain : q)
            domain = quota();
        auto deviceDesc = WebGpuOriginalDeviceFixture::describe(
            "Original registered expanded depth table", 8u * 1024u * 1024u, 4096u, 1024u * 1024u);
        auto device = rhi_webgpu::createWebGpuRhiDevice(deviceDesc);
        assert(device);
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, q[0]));
        assert(!device->prepareOriginalDeviceNativeLoss(
            {{1u, true, 1u, 1u, bytes}, 1u}, {1u, true, 1u, 2u, bytes}, q[2]));
        auto loss = device->observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        auto b = budget(compute);
        if (publication) {
            b.maxTextures = b.maxTextureViews = compute ? 3u : 4u;
            if (contentCells) {
                assert(!compute);
                b.maxTextureViews += 3u;
            }
            if constexpr (compute)
                ++b.maxComputePipelines;
            else
                ++b.maxGraphicsPipelines;
            b.maxRetainedOwners += (b.maxTextures + b.maxTextureViews)
                    * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount
                + rhi::OriginalResourceRegistrationContract::ownerCount(compute
                        ? rhi::OriginalRegisteredResourceKind::ComputePipeline
                        : rhi::OriginalRegisteredResourceKind::GraphicsPipeline,
                    b.contractVersion);
        }
        if (execution && !compute) {
            // Independently authored CPU correspondence probe resources: one
            // real readback buffer, two simultaneously live texture owners and
            // one attachment view. Reserve them before registry admission.
            b.maxBuffers = 1u;
            b.maxTextures += 2u;
            ++b.maxTextureViews;
            b.maxRetainedOwners +=
                4u * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
        }
        if (readback) {
            // Geometry/readback drains loss in its same-root Execution5 harness;
            // the procedural path retains the external legacy loss fixture.
            assert(!compute && publication && contentCells && !execution
                && retainForLoss
                    == (!geometry
                        && OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested));
            ++b.maxBuffers;
            b.maxRetainedOwners += rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
        }
        if (geometry) {
            b.maxBuffers += 3u;
            b.maxRetainedOwners +=
                3u * rhi::OriginalResourceRegistrationContract::perResourceOwnerCount;
        }
        is(device->prepareOriginalTypedNativeLossRegistration({1u, true}, b, q[1], *loss),
            "RHI-ORIGINAL-TYPED-LOSS-REGISTRATION-VERSION");
        is(device->prepareOriginalTypedNativeLossRegistration({2u, true}, b, q[2], *loss),
            "RHI-ORIGINAL-TYPED-LOSS-QUOTA-DOMAIN");
        assert(!device->prepareOriginalTypedNativeLossRegistration({2u, true}, b, q[1], *loss));
        // The general original issuer intentionally excludes this selected registry.
        assert(device->validateOriginalTypedNativeLossJoin(*loss));
        std::shared_ptr<rhi::PipelineLayout> layout;
        {
            core::CpuMemoryQuotaScope scope(q[3]);
            layout = core::shareUnique(device->createOriginalPipelineLayout(layoutDesc));
        }
        assert(layout && !device->validateOriginalResourceRegistration(*layout));
        Desc desc = original;
        desc.layout = layout.get();
        Definition::Budget db{1u, true, bytes, 256u, preflight};
        auto definition =
            Definition::prepareOriginal(rhi::Backend::WebGPU, space, layout, desc, db, q[4], id);
        assert(definition && !id);
        rhi::OriginalDepthTableExpansionCreationContract::Budget cb{1u,
            true,
            Actor::callbackCount,
            256u,
            256u,
            bytes,
            bytes,
            {1u, true, definition->requirements().shaderSourceBytes, bytes}};
        auto actor = Actor::prepareTableExpansion(definition, cb, q[5], q[6], q[7], id);
        assert(actor && !id);
        std::shared_ptr<const std::conditional_t<compute,
            rhi::OriginalComputePipelineCreation,
            rhi::OriginalGraphicsPipelineCreation>>
            accepted = actor;
        const auto publish = [&]() {
            if constexpr (compute)
                return device->createOriginalTableExpansionComputePipeline(
                    accepted, {1u, true, bytes}, q[8]);
            else
                return device->createOriginalTableExpansionGraphicsPipeline(
                    accepted, {1u, true, bytes}, q[8]);
        };
        assert(!publish());
        assert(!Actor::initialize(actor));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return actor->scopesObserved();
        }));
        assert(!actor->validate());
        // Old publication requests may never reinterpret Input3 as ordinary.
        if constexpr (compute)
            assert(
                !device->createOriginalArtifactComputePipeline(accepted, {1u, true, bytes}, q[8]));
        else
            assert(
                !device->createOriginalArtifactGraphicsPipeline(accepted, {1u, true, bytes}, q[8]));
        // A genuinely Ready actor on a different GPUDevice cannot be published
        // in this facade, even though its CPU layout and all limits match.
        {
            WebGpuForeignDeviceFixture foreign;
            WebGpuForeignDeviceFixture::Scope scope(foreign);
            auto otherDefinition = Definition::prepareOriginal(
                rhi::Backend::WebGPU, space, layout, desc, db, q[4], id);
            assert(otherDefinition);
            auto other = Actor::prepareTableExpansion(otherDefinition, cb, q[5], q[6], q[7], id);
            assert(other && !Actor::initialize(other));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return other->scopesObserved();
            }));
            assert(!other->validate());
            const auto registryBytes = q[1]->liveBytes(), wrapperBytes = q[8]->liveBytes();
            if constexpr (compute)
                assert(!device->createOriginalTableExpansionComputePipeline(
                    other, {1u, true, bytes}, q[8]));
            else
                assert(!device->createOriginalTableExpansionGraphicsPipeline(
                    other, {1u, true, bytes}, q[8]));
            is(device->lastOriginalResourceRegistrationDiagnostic().data(),
                "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-NATIVE-SOURCE");
            assert(registryBytes == q[1]->liveBytes() && wrapperBytes == q[8]->liveBytes());
        }
        CpuQuotaFactoryFixture wrapperPressure(q[8]);
        const auto wrapperPeak = wrapperPressure.measureSingleUse([] {}, publish);
        wrapperPressure.verifySingleUse(wrapperPeak, [] {}, publish);
        for (const auto index : {1u, 8u}) {
            core::CpuMemoryQuotaLease exhausted(
                q[index], q[index]->limitBytes() - q[index]->liveBytes());
            assert(exhausted.charged() && !publish());
        }
        auto candidate = publish();
        if (!candidate)
            std::fprintf(stderr,
                "table registration publication: %.*s\n",
                int(device->lastOriginalResourceRegistrationDiagnostic().size()),
                device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(candidate);
        auto pipeline =
            rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(std::move(candidate), id);
        assert(pipeline && !id && !device->validateOriginalResourceRegistration(*pipeline));
        assert(pipeline->originalResourceRegistration().matchesParent(
            layout->originalResourceRegistration()));
        assert(!pipeline->originalFullDefinitionDiagnostic());
        auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
        assert(native && native->issuedContract());
        assert(native->issuedContract()->semantics().contractVersion == 2u);
        std::shared_ptr<Pipeline> equivalentPipeline;
        if (publication) {
            auto equalCandidate = publish();
            assert(equalCandidate);
            equivalentPipeline = rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(
                std::move(equalCandidate), id);
            assert(equivalentPipeline && !id && equivalentPipeline.get() != pipeline.get());
        }
        assert(!publish()); // Exact authored kind capacity consumed by live pipelines.
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
            q[2]->setLimitBytes(bytes - 1u);
            assert(device->validateOriginalResourceRegistration(*pipeline));
            q[2]->setLimitBytes(bytes);
            auto& package =
                const_cast<rhi::ShaderPackageDesc&>(*pipeline->originalShaderPackageMetadata());
            package.reflection.descriptorBindings[0].depthTexture = false;
            assert(device->validateOriginalResourceRegistration(*pipeline));
            package.reflection.descriptorBindings[0].depthTexture = true;
            return device->validateOriginalResourceRegistration(*pipeline);
        }));
        std::shared_ptr<rhi::DescriptorArena> arena;
        rhi::DescriptorArenaDesc ad;
        ad.maxSets = 1u;
        ad.maxDescriptors = 3u;
        {
            core::CpuMemoryQuotaScope scope(q[3]);
            arena = core::shareUnique(device->createOriginalDescriptorArena(ad));
        }
        assert(arena);
        const auto& slot = layoutDesc.descriptorTables.front();
        rhi::DescriptorTableDesc td{slot.tableName,
            slot.type,
            slot.stages,
            slot.capacity,
            slot.textureViewDimensionConstrained,
            slot.textureViewDimension,
            slot.depthTexture};
        auto invalid = td;
        invalid.depthTexture = false;
        assert(!device->allocateOriginalDescriptorTable(arena, invalid));
        is(device->lastOriginalResourceRegistrationDiagnostic().data(),
            "RHI-ORIGINAL-TABLE-REGISTRATION-SHAPE");
        core::UniquePtr<rhi::DescriptorTable> table;
        {
            core::CpuMemoryQuotaScope scope(q[3]);
            table = device->allocateOriginalDescriptorTable(arena, td);
        }
        assert(table && !device->validateOriginalResourceRegistration(*table));
        assert(table->originalResourceRegistration().matchesParent(
            arena->originalResourceRegistration()));
        assert(!table->stats().writtenDescriptors);
        OriginalDepthTablePublicationFixture::Publication published;
        if (publication)
            published = OriginalDepthTablePublicationFixture::verifyNative(*device,
                pipeline,
                arena,
                table,
                equivalentPipeline,
                retainForLoss || execution
                    || (readback && OriginalDepthTableContentPlanFixture::mixedCompletionRequested),
                contentCells,
                execution,
                readback,
                geometry);
        else
            arena->reset();
        if (readback && OriginalDepthTableContentPlanFixture::mixedCompletionRequested) {
            if (geometry)
                OriginalDepthTableContentPlanFixture::verifyGeometryExecution(
                    published.nativeCustody, *device);
            else
                OriginalDepthTableContentPlanFixture::verifyReadbackExecution(
                    published.nativeCustody, published.typedStream, *device);
            if (!OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested)
                arena->reset();
        }
        if (execution) {
            assert(publication && contentCells && !retainForLoss);
            OriginalDepthTableContentPlanFixture::verifyExecution(
                published.nativeCustody, published.typedStream);
            arena->reset();
        }
        if (!retainForLoss || !publication) {
            if (table)
                is(device->validateOriginalResourceRegistration(*table),
                    geometryLoss ? "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"
                                 : "RHI-ORIGINAL-REGISTRATION-ALLOCATION-STALE");
            published.nativeCustody = {};
            published.typedStream.transaction.reset();
            published.typedStream.graph.reset();
            published.typedStream.program.reset();
            published.states.reset();
            published.inputs.reset();
            published.sources.reset();
            table.reset();
            arena.reset();
            for (std::size_t i = 0u; i < 3u; ++i)
                if (published.quotas[i])
                    assert(!published.quotas[i]->liveBytes());
            // Resource registration reuses the device's existing map. Its bucket
            // capacity remains charged until facade destruction, as in the
            // original descriptor content fixture; it is not a live Table owner.
        }
        // Issuer device identity, not merely matching queried limits.
        {
            WebGpuForeignDeviceFixture foreign;
            WebGpuForeignDeviceFixture::Scope scope(foreign);
            auto peer = rhi_webgpu::createWebGpuRhiDevice(deviceDesc);
            assert(peer);
            assert(peer->validateOriginalResourceRegistration(*pipeline));
            if (published.nativeCustody.closure)
                is(peer->validateOriginalTypedNativeLossJoin(
                       *published.nativeCustody.closure->originalNativeLossObservation(),
                       {2u, true}),
                    "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
        }
        equivalentPipeline.reset();
        accepted.reset();
        actor.reset();
        definition.reset();
        layout.reset();
        if (geometryLoss)
            is(device->validateOriginalResourceRegistration(*pipeline),
                "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        else
            assert(!device->validateOriginalResourceRegistration(*pipeline));
        if (retainForLoss) {
            auto& retainedLoss = contentCells ? contentLoss_<Pipeline> : loss_<Pipeline>;
            retainedLoss = core::makeUnique<Retained<Pipeline>>();
            auto& state = *retainedLoss;
            state.quotas = q;
            state.device = std::move(device);
            state.pipeline = std::move(pipeline);
            state.table = std::move(table);
            state.published = std::move(published);
            return;
        }
        auto ticket = pipeline->originalResourceRegistration();
        device.reset();
        is(ticket.diagnostic(),
            geometryLoss ? "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"
                         : "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
        pipeline.reset();
        native.reset();
        ticket = {};
        loss.reset();
        for (const auto& domain : q)
            assert(!domain->liveBytes());
        for (const auto& domain : published.quotas)
            if (domain)
                assert(!domain->liveBytes());
        for (const auto& domain : published.geometryQuotas)
            if (domain)
                assert(!domain->liveBytes());
        if (geometry)
            std::puts(
                "ORIGINAL_TABLE_GEOMETRY_RETIRE_OK initialized-streams original-root result registry-all-quotas-zero public-closed");
        std::printf(
            "ORIGINAL_TABLE_REGISTRATION_NATIVE_OK %s original-issuer registry8 expanded-payload parent-layout arena-epoch copied-definition subscription-quota caller-drop shutdown hot0 group-recording-execution-closed\n",
            compute ? "Compute" : "Graphics");
#endif
    }

    template <class Desc> static void verifyActualLoss(bool contentCells = false)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        using Pipeline = std::conditional_t<compute, rhi::ComputePipeline, rhi::RenderPipeline>;
        auto& retainedLoss = contentCells ? contentLoss_<Pipeline> : loss_<Pipeline>;
        assert(retainedLoss);
        auto& state = *retainedLoss;
        assert(WebGpuAsyncWaitFixture::until([&] {
            return state.pipeline->originalResourceRegistration().diagnostic() != nullptr;
        }));
        if (contentCells && OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested)
            OriginalDepthTableContentPlanFixture::verifyReadbackLoss(
                state.published.nativeCustody, state.published.typedStream);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            state.quotas, [&]() noexcept -> const char* {
                is(state.device->validateOriginalResourceRegistration(*state.pipeline),
                    "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                is(state.device->originalDeviceNativeLossDiagnostic(),
                    "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                if (state.published.sources)
                    is(rhi::OriginalCommandResourceSources::validateRegisteredLifetime(
                           state.published.sources, *state.device),
                        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                if (state.published.inputs)
                    is(state.published.inputs->validate(),
                        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                if (state.published.inputs)
                    OriginalDepthTablePublicationFixture::verifyConsumptionUnavailable(
                        state.published.inputs, *state.device);
                if (state.published.states)
                    is(state.published.states->validate(),
                        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                if (state.published.typedStream.program)
                    is(state.published.typedStream.program->validate(),
                        "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                OriginalDepthTablePublicationFixture::verifyUsageUnavailable(
                    state.published.typedStream, *state.device);
                OriginalDepthTableContentPlanFixture::verifyUnavailable(
                    state.published.nativeCustody, "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
                return nullptr;
            }));
        const auto quotas = state.quotas;
        const auto publicationQuotas = state.published.quotas;
        const bool publication = bool(state.published.sources);
        retainedLoss.reset();
        for (const auto& domain : quotas)
            assert(!domain->liveBytes());
        for (const auto& domain : publicationQuotas)
            if (domain)
                assert(!domain->liveBytes());
        if (publication) {
            const auto domains = std::count_if(
                publicationQuotas.begin(), publicationQuotas.end(), [](const auto& domain) {
                    return bool(domain);
                });
            std::printf(
                "ORIGINAL_TABLE_PUBLICATION_PHYSICAL_LOSS_OK %s genuine-device-loss original-table-view-parent-native-group-inputs4-state3-and-%u-quota-domains-retired\n",
                compute ? "Compute" : "Graphics",
                static_cast<unsigned>(quotas.size() + domains));
            if (contentCells && !OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested)
                std::printf(
                    "ORIGINAL_TABLE_CONTENT_PLAN2_LOSS_OK Graphics canonical-controls4 original-table-output-views-stream-transaction native-device-loss domains%u retired execution-closed\n",
                    static_cast<unsigned>(quotas.size() + domains));
            if (contentCells && !OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested)
                std::printf(
                    "ORIGINAL_TABLE_NATIVE_CUSTODY_LOSS_OK Graphics closure3 freeze2 same-original-root real-device-loss domains%u-retired execution-closed\n",
                    static_cast<unsigned>(quotas.size() + domains));
            if (contentCells && !OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested)
                std::printf(
                    "ORIGINAL_TABLE_NATIVE_COLD2_LOSS_OK Graphics cold2 command2 same-original-root real-device-loss domains%u-retired execution-closed\n",
                    static_cast<unsigned>(quotas.size() + domains));
        }
        std::printf(
            "ORIGINAL_TABLE_REGISTRATION_PHYSICAL_LOSS_OK %s genuine-device-callback pipeline-and-subscription-refuse nine-domains-retired hot0\n",
            compute ? "Compute" : "Graphics");
#endif
    }

private:
    template <class Pipeline> struct Retained {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> quotas;
        core::UniquePtr<rhi::GpuDevice> device;
        std::shared_ptr<Pipeline> pipeline;
        core::UniquePtr<rhi::DescriptorTable> table;
        OriginalDepthTablePublicationFixture::Publication published;
    };
    template <class Pipeline> inline static core::UniquePtr<Retained<Pipeline>> loss_;
    template <class Pipeline> inline static core::UniquePtr<Retained<Pipeline>> contentLoss_;
};
} // namespace arp::test
