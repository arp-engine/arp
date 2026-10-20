#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "webgpu_original_command_inputs_fixture.h"

#include <cstdio>

namespace arp::test {
class WebGpuFrozenRenderPassBindingFixture final {
public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        using Plan = WebGpuFrozenRenderPassBinding;
        WebGpuFrozenRenderPassBindingBudget budget;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-PASS-ARGUMENTS-VERSION");
        budget.contractVersion = 1u;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-PASS-ARGUMENTS-OPT-IN");
        budget.prototypeOptIn = true;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-PASS-ARGUMENTS-BUDGET");
        budget.maxCpuMetadataBytes = metadataBytes;
        auto quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const char* diagnostic = nullptr;
        assert(!Plan::prepareOriginal({}, nullptr, 0u, budget, quota, diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-PASS-ARGUMENTS-ORIGINAL-USAGE");
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto usageQuota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        const WebGpuGenericCommandProjectionBudget graphBudget{
            1u, true, 0u, 0u, 0u, 0u, 0u, 0u, {}, metadataBytes, 0u};
        auto graph =
            WebGpuGenericCommandProjection::shareOriginal(WebGpuGenericCommandProjection::prepare(
                {}, nullptr, 0u, graphBudget, graphQuota, diagnostic));
        assert(graph);
        const rhi::CommandUsage finish{rhi::CommandUsageKind::Finish, 0u, 0u};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(
            graph, &finish, 1u, {1u, true, 1u, 0u, metadataBytes}, usageQuota, diagnostic);
        assert(usage);
        const auto factory = [&] {
            return Plan::prepareOriginal(usage, nullptr, 0u, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(!quota->liveBytes());
        auto empty = factory();
        assert(empty && !empty->validate() && empty->passes().empty());
        assert(std::string_view(empty->executionDiagnostic())
            == webGpuGenericRecordingUsageDiagnostic);
        assert(std::string_view(empty->compareRaw(0u, {}, nullptr, 0u))
            == "RHI-WEBGPU-PASS-ARGUMENTS-ORDER");
        empty.reset();
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged() && !factory()
                && std::string_view(diagnostic) == "RHI-WEBGPU-PASS-ARGUMENTS-ALLOCATION");
        }
        budget.maxPassRecords = UINT32_MAX;
        assert(
            !factory() && std::string_view(diagnostic) == "RHI-WEBGPU-PASS-ARGUMENTS-ALLOCATION");
        usage.reset();
        graph.reset();
        assert(!quota->liveBytes() && !usageQuota->liveBytes() && !graphQuota->liveBytes());
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class LossProbe final {
    public:
        std::shared_ptr<const rhi_webgpu::WebGpuFrozenRenderPassBinding> plan;
        std::shared_ptr<rhi_webgpu::WebGpuRhiAllocator> allocator;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5> quotas;
        WebGpuOriginalCommandInputsFixture::LossProbe originalInputs;

        void verifyDroppedSources() const
        {
            assert(plan && !plan->validate()
                && allocator->memoryStats().allocatedBytes
                    == plan->usage()->projection()->retainedAllocationBytes());
            for (const auto& quota : quotas)
                assert(quota->liveBytes());
            assert(originalInputs.plan && !originalInputs.plan->validate());
            std::puts("BINDLESS_RHI_FROZEN_PASS_BINDING_SOURCE_DROP_OK");
        }

        bool retiredAfterActualLoss()
        {
            assert(plan && rhi_webgpu::webGpuNativeDeviceLost());
            assert(
                std::string_view(plan->validate()) == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
            originalInputs.retiredAfterActualLoss();
            plan.reset();
            assert(!allocator->memoryStats().allocatedBytes
                && !allocator->memoryStats().liveResourceCount);
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            std::puts("BINDLESS_RHI_FROZEN_PASS_BINDING_LOSS_OK");
            return true;
        }
    };

    static LossProbe verifyActual()
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Plan = WebGpuFrozenRenderPassBinding;
        using Domain = CommandReferenceDomain;
        LossProbe loss;
        const char* diagnostic = nullptr;
        for (auto& quota : loss.quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(metadataBytes);
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        std::array<TextureDesc, 3> descs{
            {{"bound pass multisample color",
                 {2u, 2u, 1u},
                 TextureFormat::Rgba8Unorm,
                 TextureUsage::RenderAttachment | TextureUsage::Sampled},
                {"bound pass resolve",
                    {2u, 2u, 1u},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::RenderAttachment},
                {"bound pass depth",
                    {2u, 2u, 1u},
                    TextureFormat::D32Float,
                    TextureUsage::RenderAttachment}}};
        descs[0].sampleCount = descs[2].sampleCount = 4u;
        std::uint64_t bytes = 0u;
        for (const auto& desc : descs)
            bytes += estimateWebGpuRhiTextureBytes(desc);
        loss.allocator = core::makeShared<WebGpuRhiAllocator>(
            WebGpuRhiAllocatorDesc{"bound pass original ledger", bytes, false});
        std::array<core::UniquePtr<GpuTexture>, 3> textures;
        std::array<core::UniquePtr<GpuTextureView>, 4> views;
        for (std::size_t i = 0u; i < textures.size(); ++i) {
            auto allocation = loss.allocator->allocate(
                {descs[i].label, estimateWebGpuRhiTextureBytes(descs[i]), descs[i].memory});
            assert(allocation.success);
            textures[i] = createWebGpuRhiTexture(
                allocateGpuResourceId(), descs[i], allocation, {true, true}, registry);
            assert(textures[i]);
            registry->registerTexture(*textures[i]);
            TextureViewDesc view{
                "bound pass original view", descs[i].format, TextureUsage::RenderAttachment};
            view.aspect = i == 2u ? TextureAspect::Depth : TextureAspect::Color;
            views[i] = createWebGpuRhiTextureView(allocateGpuResourceId(),
                textures[i]->id(),
                *textures[i],
                view,
                {true, true},
                registry);
            assert(views[i]);
            registry->registerTextureView(*views[i]);
        }
        views[3] = createWebGpuRhiTextureView(allocateGpuResourceId(),
            textures[0]->id(),
            *textures[0],
            {"bound pass sampled only negative view", descs[0].format, TextureUsage::Sampled},
            {true, true},
            registry);
        assert(views[3]);
        registry->registerTextureView(*views[3]);
        const std::array ids{views[0]->id(), views[1]->id(), views[2]->id(), views[3]->id()};
        const WebGpuOrdinaryResourceBudget resourceBudget{1u,
            true,
            {kCommandRetentionBudgetContractVersion, true, 12u, 0u, 0u, 0u, metadataBytes},
            4u,
            4u,
            0u,
            bytes};
        auto resources = WebGpuOrdinaryResourceProjection::shareOriginal(
            WebGpuOrdinaryResourceProjection::prepare(
                *registry, ids.data(), ids.size(), resourceBudget, loss.quotas[0], diagnostic));
        assert(resources);
        std::array<WebGpuCommandReference, 6> references;
        for (std::size_t i = 0u; i < references.size(); ++i)
            references[i] = {
                Domain::TextureView, WebGpuCommandReferenceSource::Resource, ids[i % 3u]};
        WebGpuGenericCommandProjectionBudget graphBudget{
            1u, true, 0u, 1u, 0u, 0u, 6u, 4u, {}, metadataBytes, bytes};
        graphBudget.maxDomainReferences[static_cast<std::size_t>(Domain::TextureView)] = 6u;
        const std::array<CommandUsage, 5> commands{{{CommandUsageKind::BeginRenderPass, 0u, 3u},
            {CommandUsageKind::EndRenderPass, 3u, 0u},
            {CommandUsageKind::BeginRenderPass, 3u, 3u},
            {CommandUsageKind::EndRenderPass, 6u, 0u},
            {CommandUsageKind::Finish, 6u, 0u}}};
        const auto makeUsage = [&] {
            WebGpuGenericCommandProjection::Sources sources;
            sources.resources = resources;
            auto graph = WebGpuGenericCommandProjection::shareOriginal(
                WebGpuGenericCommandProjection::prepare(std::move(sources),
                    references.data(),
                    references.size(),
                    graphBudget,
                    loss.quotas[1],
                    diagnostic));
            assert(graph);
            return WebGpuGenericCommandUsage::prepareOriginal(graph,
                commands.data(),
                commands.size(),
                {1u, true, 5u, 6u, metadataBytes},
                loss.quotas[2],
                diagnostic);
        };
        auto usage = makeUsage();
        assert(usage);
        RenderPassDesc source;
        source.colorAttachments.push_back({"",
            ids[0],
            descs[0].format,
            LoadOp::Clear,
            StoreOp::Store,
            true,
            ids[1],
            {0.125f, 0.375f, 0.875f, 1.0f}});
        source.hasDepthStencilAttachment = true;
        source.depthStencilAttachment.target = ids[2];
        source.depthStencilAttachment.format = descs[2].format;
        source.depthStencilAttachment.depthClearValue = 0.375f;
        const RenderPassArgumentsBudget snapshotBudget{1u, true, 1u, 0u, 0u, 0u, 0u, metadataBytes};
        const auto snapshot = [&] {
            return RenderPassArguments::prepareOriginal(
                source, snapshotBudget, loss.quotas[3], diagnostic);
        };
        std::array<Plan::OriginalPass, 2> passes{{{0u, snapshot()}, {2u, snapshot()}}};
        assert(passes[0].arguments && passes[1].arguments);
        WebGpuFrozenRenderPassBindingBudget budget{1u, true, 2u, metadataBytes};
        const auto factory = [&] {
            return Plan::prepareOriginal(
                usage, passes.data(), passes.size(), budget, loss.quotas[4], diagnostic);
        };
        CpuQuotaFactoryFixture admission(loss.quotas[4]);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        const auto expect = [&](const char* id) {
            assert(
                !factory() && std::string_view(diagnostic) == id && !loss.quotas[4]->liveBytes());
        };
        --budget.maxPassRecords;
        expect("RHI-WEBGPU-PASS-ARGUMENTS-CAPACITY");
        ++budget.maxPassRecords;
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-PASS-ARGUMENTS-QUOTA");
        ++budget.maxCpuMetadataBytes;
        assert(
            !Plan::prepareOriginal(usage, passes.data(), 1u, budget, loss.quotas[4], diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-PASS-ARGUMENTS-ORDER");
        assert(!Plan::prepareOriginal(usage, nullptr, 2u, budget, loss.quotas[4], diagnostic));
        assert(std::string_view(diagnostic) == "RHI-WEBGPU-PASS-ARGUMENTS-CAPACITY");
        passes[0].commandIndex = 1u;
        expect("RHI-WEBGPU-PASS-ARGUMENTS-ORDER");
        passes[0].commandIndex = 0u;
        std::swap(passes[0], passes[1]);
        expect("RHI-WEBGPU-PASS-ARGUMENTS-ORDER");
        std::swap(passes[0], passes[1]);
        const auto original = passes[0].arguments;
        passes[0].arguments.reset();
        expect("RHI-WEBGPU-PASS-ARGUMENTS-ORIGINAL-SNAPSHOT");
        passes[0].arguments =
            std::shared_ptr<const RenderPassArguments>(loss.quotas[3], original.get());
        expect("RHI-WEBGPU-PASS-ARGUMENTS-ORIGINAL-SNAPSHOT");
        passes[0].arguments = original;
        source.colorAttachments[0].target = ids[1];
        passes[0].arguments = snapshot();
        expect("RHI-WEBGPU-PASS-ARGUMENTS-REFERENCE");
        source.colorAttachments[0].target = ids[0];
        source.depthStencilAttachment.format = TextureFormat::Rgba8Unorm;
        passes[0].arguments = snapshot();
        expect("RHI-WEBGPU-PASS-ARGUMENTS-VIEW");
        source.depthStencilAttachment.format = descs[2].format;
        passes[0].arguments = original;
        auto originalUsage = usage;
        usage =
            std::shared_ptr<const WebGpuGenericCommandUsage>(loss.quotas[2], originalUsage.get());
        expect("RHI-WEBGPU-PASS-ARGUMENTS-ORIGINAL-USAGE");
        usage = originalUsage;
        source.colorAttachments[0].target = ids[3];
        passes[0].arguments = snapshot();
        references[0].resource = ids[3];
        usage = makeUsage();
        assert(usage);
        expect("RHI-WEBGPU-PASS-ARGUMENTS-VIEW");
        source.colorAttachments[0].target = ids[0];
        passes[0].arguments = original;
        references[0].resource = ids[0];
        usage = originalUsage;
        loss.plan = factory();
        assert(loss.plan && !loss.plan->validate());
        std::array<WebGpuGenericCommandUsage::RawReference, 3> raw;
        for (std::size_t i = 0u; i < raw.size(); ++i)
            raw[i] = {Domain::TextureView, webGpuNativePayloadFor(*views[i]).get()};
        auto nativeView = webGpuNativePayloadFor(*views[0]);
        const auto key = nativeView->creationKey;
        const auto before = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(loss.quotas[4]);
            core::CpuMemoryQuotaLease pressure(
                loss.quotas[4], metadataBytes - loss.quotas[4]->liveBytes());
            assert(pressure.charged());
            assert(!loss.plan->compareRaw(0u, source, raw.data(), raw.size()));
            assert(!loss.plan->compareRaw(2u, source, raw.data(), raw.size()));
            source.depthStencilAttachment.depthClearValue = 0.875f;
            assert(std::string_view(loss.plan->compareRaw(0u, source, raw.data(), raw.size()))
                == "RHI-WEBGPU-PASS-ARGUMENTS-INPUT");
            source.depthStencilAttachment.depthClearValue = 0.375f;
            std::swap(raw[0], raw[1]);
            assert(loss.plan->compareRaw(0u, source, raw.data(), raw.size()));
            std::swap(raw[0], raw[1]);
            raw[0].domain = Domain::Texture;
            assert(loss.plan->compareRaw(0u, source, raw.data(), raw.size()));
            raw[0].domain = Domain::TextureView;
            assert(loss.plan->compareRaw(0u, source, raw.data(), 2u));
            ++nativeView->creationKey.baseMipLevel;
            assert(loss.plan->validate());
            nativeView->creationKey = key;
            const auto parent = nativeView->parentTexture;
            nativeView->parentTexture =
                std::shared_ptr<WebGpuNativeTexturePayload>(loss.quotas[0], parent.get());
            assert(loss.plan->validate());
            nativeView->parentTexture = parent;
            const auto device = parent->ownerDevice;
            parent->ownerDevice = nullptr;
            assert(loss.plan->validate());
            parent->ownerDevice = device;
            assert(!loss.plan->validate()
                && core::defaultCpuAllocator().stats().allocationCount == before);
        }
        std::printf("BINDLESS_RHI_FROZEN_PASS_BINDING_OK %llu 2 6 allocation0\n",
            static_cast<unsigned long long>(peak));
        loss.originalInputs = WebGpuOriginalCommandInputsFixture::verifyPass(loss.plan);
        // Local public textures/views/usage/snapshots disappear on return; only
        // the binding graph owns their original native payloads/charges.
        return loss;
    }
#endif
private:
    static constexpr std::uint64_t metadataBytes = 64u * 1024u;
};
} // namespace arp::test
