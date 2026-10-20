#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_command_inputs.h"
#include "webgpu_original_resource_states_fixture.h"
#include "webgpu_original_transfer_state_validation_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalCommandInputsFixture final {
    using Plan = rhi_webgpu::WebGpuOriginalCommandInputs;
    using K = rhi::CommandUsageKind;
    using A = rhi::CommandScalarArguments;
    using Store = rhi::CommandVariableArguments;

public:
    static void verifyContract()
    {
        using namespace rhi_webgpu;
        WebGpuOriginalResourceStatesFixture::verifyControlDomain();
        WebGpuOriginalCommandInputsBudget budget;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-ORIGINAL-INPUTS-VERSION");
        budget.contractVersion = 1u;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-ORIGINAL-INPUTS-OPT-IN");
        budget.prototypeOptIn = true;
        assert(
            std::string_view(Plan::validateBudget(budget)) == "RHI-WEBGPU-ORIGINAL-INPUTS-BUDGET");
        std::array<std::size_t, 8> counts{};
        for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(K::Count); ++i)
            ++counts[static_cast<std::size_t>(Plan::family(static_cast<K>(i)))];
        assert((counts == std::array<std::size_t, 8>{46u, 1u, 4u, 5u, 1u, 1u, 1u, 1u}));
        // Metadata family is scalar. Execution remains independently gated.
        assert(Plan::family(K::BindPreparedComputeBinding) == Plan::Family::Scalar);
        assert(Plan::family(K::BindPreparedGraphicsBinding) == Plan::Family::Scalar);
        assert(Plan::family(static_cast<K>(UINT32_MAX)) == Plan::Family::Unsupported);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const char* diagnostic = nullptr;
        auto graph = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare({},
                nullptr,
                0u,
                {1u, true, 0u, 0u, 0u, 0u, 0u, 0u, {}, graphQuota->limitBytes(), 0u},
                graphQuota,
                diagnostic));
        const rhi::CommandUsage command{K::Finish};
        auto makeUsage = [&] {
            return WebGpuGenericCommandUsage::prepareOriginal(graph,
                &command,
                1u,
                {1u, true, 1u, 0u, sourceQuota->limitBytes()},
                sourceQuota,
                diagnostic);
        };
        auto usage = makeUsage();
        const Store::Input input{*A::capture(K::Finish, A::None{})};
        auto arguments = Store::prepareOriginal(&input,
            1u,
            {1u, true, 1u, 0u, 0u, 0u, sourceQuota->limitBytes()},
            sourceQuota,
            diagnostic);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(usage,
            arguments,
            {1u, true, 1u, 0u, sourceQuota->limitBytes()},
            sourceQuota,
            diagnostic);
        auto passes = WebGpuFrozenRenderPassBinding::prepareOriginal(
            usage, nullptr, 0u, {1u, true, 0u, sourceQuota->limitBytes()}, sourceQuota, diagnostic);
        assert(scalars && passes);
        budget = {1u, true, 0u, quota->limitBytes()};
        const auto factory = [&] {
            return Plan::prepareOriginal(scalars, passes, nullptr, 0u, budget, quota, diagnostic);
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto expect = [&](const char* id) {
            assert(!factory() && std::string_view(diagnostic) == id && !quota->liveBytes());
        };
        auto originalScalars = scalars;
        auto originalPasses = passes;
        scalars.reset();
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-SCALARS");
        scalars = originalScalars;
        scalars = std::shared_ptr<const WebGpuFrozenCommandScalars>(quota, originalScalars.get());
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-SCALARS");
        scalars = originalScalars;
        passes = std::shared_ptr<const WebGpuFrozenRenderPassBinding>(quota, originalPasses.get());
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-PASSES");
        passes = originalPasses;
        auto foreignUsage = makeUsage();
        passes = WebGpuFrozenRenderPassBinding::prepareOriginal(foreignUsage,
            nullptr,
            0u,
            {1u, true, 0u, sourceQuota->limitBytes()},
            sourceQuota,
            diagnostic);
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-USAGE");
        passes = originalPasses;
        --budget.maxCpuMetadataBytes;
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-QUOTA");
        ++budget.maxCpuMetadataBytes;
        budget.maxDescriptorRecords = UINT32_MAX;
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-ALLOCATION");
        budget.maxDescriptorRecords = 0u;
        const Plan::OriginalDescriptor extra{0u, rhi::TransferCopyArguments::finish()};
        assert(!Plan::prepareOriginal(scalars, passes, &extra, 1u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-INPUTS-CAPACITY");
        budget.maxDescriptorRecords = 1u;
        assert(!Plan::prepareOriginal(scalars, passes, &extra, 1u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-INPUTS-ORDER");
        assert(!Plan::prepareOriginal(scalars, passes, nullptr, 1u, budget, quota, diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-INPUTS-CAPACITY");
        budget.maxDescriptorRecords = 0u;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto plan = factory();
        std::pmr::set_default_resource(previous);
        assert(plan && !plan->validate());
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        Plan::RawInput raw{input, {}};
        assert(!plan->compareRaw(0u, raw, nullptr, 0u));
        raw.descriptor = rhi::TransferCopyArguments::finish();
        assert(std::string_view(plan->compareRaw(0u, raw, nullptr, 0u))
            == "RHI-WEBGPU-ORIGINAL-INPUTS-PAYLOAD");
        assert(plan->compareRaw(1u, raw, nullptr, 0u));
        assert(
            std::string_view(plan->executionDiagnostic()) == webGpuGenericRecordingUsageDiagnostic);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto states = WebGpuOriginalResourceStatesFixture::verify(plan, stateQuota, "empty");
        plan.reset();
        assert(quota->liveBytes());
        states.reset();
        assert(!stateQuota->liveBytes() && !quota->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_INPUTS_CONTRACT_OK %llu vocabulary same-usage coverage quotas allocation0\n",
            static_cast<unsigned long long>(peak));
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    struct LossProbe {
        std::shared_ptr<const Plan> plan;
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> states;
        std::shared_ptr<core::CpuMemoryQuota> stateQuota;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 8> quotas;
        const char* source;
        std::shared_ptr<rhi_webgpu::WebGpuRhiAllocator> allocator;

        bool retiredAfterActualLoss()
        {
            assert(plan && rhi_webgpu::webGpuNativeDeviceLost());
            assert(
                std::string_view(plan->validate()) == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
            assert(states
                && std::string_view(states->validate())
                    == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
            states.reset();
            assert(!stateQuota->liveBytes());
            std::printf("BINDLESS_RHI_ORIGINAL_STATES_LOSS_OK %s source-reject retire\n", source);
            plan.reset();
            for (const auto& quota : quotas)
                assert(!quota->liveBytes());
            if (allocator)
                assert(!allocator->memoryStats().allocatedBytes);
            std::printf("BINDLESS_RHI_ORIGINAL_INPUTS_LOSS_OK %s source-reject retire\n", source);
            return true;
        }
    };

    static LossProbe verifyPass(
        const std::shared_ptr<const rhi_webgpu::WebGpuFrozenRenderPassBinding>& passes)
    {
        using namespace rhi_webgpu;
        LossProbe probe;
        probe.source = "render-pass";
        probe.stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        for (auto& quota : probe.quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const auto& usage = passes->usage();
        const char* diagnostic = nullptr;
        assert(usage->commands().size() == 5u);
        const std::array<Store::Input, 5> input{{{*A::capture(K::BeginRenderPass, A::None{})},
            {*A::capture(K::EndRenderPass, A::None{})},
            {*A::capture(K::BeginRenderPass, A::None{})},
            {*A::capture(K::EndRenderPass, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        auto arguments = Store::prepareOriginal(input.data(),
            input.size(),
            {1u, true, 5u, 0u, 0u, 0u, probe.quotas[2]->limitBytes()},
            probe.quotas[2],
            diagnostic);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(usage,
            arguments,
            {1u, true, 5u, 0u, probe.quotas[3]->limitBytes()},
            probe.quotas[3],
            diagnostic);
        assert(scalars);
        const WebGpuOriginalCommandInputsBudget budget{1u, true, 0u, probe.quotas[4]->limitBytes()};
        const auto factory = [&] {
            return Plan::prepareOriginal(
                scalars, passes, nullptr, 0u, budget, probe.quotas[4], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(probe.quotas[4]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        assert(!Plan::prepareOriginal(scalars, {}, nullptr, 0u, budget, probe.quotas[4], diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-INPUTS-PASSES");
        probe.plan = factory();
        assert(probe.plan && !probe.plan->validate());
        std::array<WebGpuGenericCommandUsage::RawReference, 3> raw;
        const auto& refs = usage->projection()->references();
        for (std::size_t i = 0u; i < raw.size(); ++i)
            raw[i] = {refs[i].domain,
                usage->projection()->resources()->find(refs[i].resource)->view().get()};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t i = 0u; i < input.size(); ++i) {
            Plan::RawInput incoming{input[i], {}};
            for (const auto& pass : passes->passes())
                if (pass.commandIndex == i)
                    incoming.descriptor = &pass.arguments->desc();
            const auto& command = usage->commands()[i];
            assert(!probe.plan->compareRaw(i,
                incoming,
                command.referenceCount ? raw.data() : nullptr,
                command.referenceCount));
            if (command.referenceCount) {
                incoming.descriptor = static_cast<const rhi::RenderPassDesc*>(nullptr);
                assert(probe.plan->compareRaw(i, incoming, raw.data(), raw.size()));
            }
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::printf("BINDLESS_RHI_ORIGINAL_INPUTS_PASS_OK %llu 5 2 complete-pass allocation0\n",
            static_cast<unsigned long long>(peak));
        probe.states =
            WebGpuOriginalResourceStatesFixture::verify(probe.plan, probe.stateQuota, probe.source);
        return probe;
    }

    static void verifyStateInventory(
        const std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection>& graph)
    {
        using namespace rhi_webgpu;
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const char* diagnostic = nullptr;
        const rhi::CommandUsage command{K::Finish};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(
            graph, &command, 1u, {1u, true, 1u, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(usage);
        const Store::Input input{*A::capture(K::Finish, A::None{})};
        auto arguments = Store::prepareOriginal(
            &input, 1u, {1u, true, 1u, 0u, 0u, 0u, quota->limitBytes()}, quota, diagnostic);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 1u, 0u, quota->limitBytes()}, quota, diagnostic);
        auto inputs = Plan::prepareOriginal(
            scalars, {}, nullptr, 0u, {1u, true, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(inputs);
        auto states = WebGpuOriginalResourceStatesFixture::verify(inputs, stateQuota, "inventory");
        // One Transfer buffer + same ordinary buffer + two views of one parent
        // + explicit state-free sampler: five sources, exactly two controls.
        assert(states->sourceReferences() == 5u && states->states().size() == 2u);
        for (const auto& resource : graph->resources()->resources()) {
            if (resource.sampler())
                assert(!states->find(resource.id()));
            else
                assert(states->find(resource.backing().id));
        }
        states.reset();
        assert(!stateQuota->liveBytes());
    }

    static void verifyGraphics(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryPhysicalGroups>& original)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        LossProbe probe;
        probe.source = "graphics";
        probe.stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        for (auto& quota : probe.quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto pipeline = original->pipeline(), groupPipeline = pipeline;
        auto groups = original;
        const auto& graphics = pipeline->contract().graphics();
        assert(pipeline->graphics() && !graphics.vertexBufferCount
            && graphics.colorFormats.size() == 1u
            && graphics.colorFormats[0] == TextureFormat::Rgba8Unorm && graphics.sampleCount == 1u);
        assert(original->observedSets().size() == 1u);
        auto registry = core::makeShared<WebGpuRhiResourceRegistry>();
        TextureDesc desc{"full original graphics input target",
            {2u, 2u, 1u},
            graphics.colorFormats[0],
            TextureUsage::RenderAttachment | TextureUsage::CopySrc};
        desc.sampleCount = graphics.sampleCount;
        const auto bytes = estimateWebGpuRhiTextureBytes(desc);
        probe.allocator = core::makeShared<WebGpuRhiAllocator>(
            WebGpuRhiAllocatorDesc{"full original graphics input ledger", bytes, false});
        auto allocation = probe.allocator->allocate({desc.label, bytes, desc.memory});
        assert(allocation.success);
        auto texture = createWebGpuRhiTexture(
            allocateGpuResourceId(), desc, allocation, {true, true}, registry);
        assert(texture);
        registry->registerTexture(*texture);
        auto view = createWebGpuRhiTextureView(allocateGpuResourceId(),
            texture->id(),
            *texture,
            {"full original graphics input view", desc.format, TextureUsage::RenderAttachment},
            {true, true},
            registry);
        assert(view);
        registry->registerTextureView(*view);
        const auto viewId = view->id();
        const char* diagnostic = nullptr;
        WebGpuOrdinaryResourceBudget resourceBudget{1u,
            true,
            {kCommandRetentionBudgetContractVersion,
                true,
                3u,
                0u,
                0u,
                0u,
                probe.quotas[0]->limitBytes()},
            1u,
            1u,
            0u,
            bytes};
        auto resources = WebGpuOrdinaryResourceProjection::shareOriginal(
            WebGpuOrdinaryResourceProjection::prepare(
                *registry, &viewId, 1u, resourceBudget, probe.quotas[0], diagnostic));
        assert(resources);
        const std::array<WebGpuCommandReference, 3> references{
            {{CommandReferenceDomain::TextureView, WebGpuCommandReferenceSource::Resource, viewId},
                {CommandReferenceDomain::Pipeline, WebGpuCommandReferenceSource::Pipeline},
                {CommandReferenceDomain::Descriptor,
                    WebGpuCommandReferenceSource::PhysicalGroups}}};
        WebGpuGenericCommandProjectionBudget graphBudget{1u,
            true,
            0u,
            1u,
            1u,
            1u,
            3u,
            2u,
            {},
            probe.quotas[1]->limitBytes(),
            bytes + original->retainedAllocationBytes()};
        for (const auto& ref : references)
            ++graphBudget.maxDomainReferences[static_cast<std::size_t>(ref.domain)];
        WebGpuGenericCommandProjection::Sources sources;
        sources.resources = resources;
        sources.pipelines = &pipeline;
        sources.pipelineCount = 1u;
        sources.groups = &groups;
        sources.groupCount = 1u;
        auto graph = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare(std::move(sources),
                references.data(),
                references.size(),
                graphBudget,
                probe.quotas[1],
                diagnostic));
        assert(graph);
        const std::array<CommandUsage, 8> commands{{{K::BeginRenderPass, 0u, 1u},
            {K::BindGraphicsPipeline, 1u, 1u},
            {K::SetViewport, 2u, 0u},
            {K::SetScissor, 2u, 0u},
            {K::BindRenderDescriptor, 2u, 1u},
            {K::Draw, 3u, 0u},
            {K::EndRenderPass, 3u, 0u},
            {K::Finish, 3u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 8u, 3u, probe.quotas[2]->limitBytes()},
            probe.quotas[2],
            diagnostic);
        assert(usage);
        const auto& set = original->observedSets()[0];
        const auto& offsets = set.observation->offsets();
        const std::array<Store::Input, 8> inputs{{{*A::capture(K::BeginRenderPass, A::None{})},
            {*A::capture(K::BindGraphicsPipeline, A::None{})},
            {*A::capture(K::SetViewport, graphics.viewport)},
            {*A::capture(K::SetScissor, graphics.scissor)},
            {*A::capture(K::BindRenderDescriptor, A::DescriptorBinding{set.logicalSet}),
                {},
                offsets.data(),
                static_cast<std::uint32_t>(offsets.size())},
            {*A::capture(K::Draw, A::Draw{3u, 1u, 0u, 0u})},
            {*A::capture(K::EndRenderPass, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        auto arguments = Store::prepareOriginal(inputs.data(),
            inputs.size(),
            {1u,
                true,
                8u,
                static_cast<std::uint32_t>(offsets.size()),
                0u,
                0u,
                probe.quotas[3]->limitBytes()},
            probe.quotas[3],
            diagnostic);
        assert(arguments);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(usage,
            arguments,
            {1u, true, 8u, 0u, probe.quotas[4]->limitBytes()},
            probe.quotas[4],
            diagnostic);
        assert(scalars);
        RenderPassDesc pass;
        pass.colorAttachments.push_back({"full original graphics input color",
            viewId,
            desc.format,
            LoadOp::Clear,
            StoreOp::Store,
            false,
            {},
            {0.125f, 0.375f, 0.875f, 1.0f}});
        auto passArguments = RenderPassArguments::prepareOriginal(pass,
            {1u,
                true,
                1u,
                0u,
                0u,
                0u,
                pass.colorAttachments[0].label.size(),
                probe.quotas[5]->limitBytes()},
            probe.quotas[5],
            diagnostic);
        assert(passArguments);
        const WebGpuFrozenRenderPassBinding::OriginalPass originalPass{0u, passArguments};
        auto passes = WebGpuFrozenRenderPassBinding::prepareOriginal(usage,
            &originalPass,
            1u,
            {1u, true, 1u, probe.quotas[6]->limitBytes()},
            probe.quotas[6],
            diagnostic);
        assert(passes);
        const WebGpuOriginalCommandInputsBudget budget{1u, true, 0u, probe.quotas[7]->limitBytes()};
        const auto factory = [&] {
            return Plan::prepareOriginal(
                scalars, passes, nullptr, 0u, budget, probe.quotas[7], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(probe.quotas[7]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        assert(!Plan::prepareOriginal(scalars, {}, nullptr, 0u, budget, probe.quotas[7], diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-INPUTS-PASSES");
        probe.plan = factory();
        assert(probe.plan && !probe.plan->validate());
        const std::array<WebGpuGenericCommandUsage::RawReference, 3> raw{
            {{CommandReferenceDomain::TextureView, webGpuNativePayloadFor(*view).get()},
                {CommandReferenceDomain::Pipeline, pipeline->graphics().get()},
                {CommandReferenceDomain::Descriptor, groups.get()}}};
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::size_t i = 0u; i < inputs.size(); ++i) {
            Plan::RawInput input{inputs[i], {}};
            if (!i)
                input.descriptor = &pass;
            const auto& command = commands[i];
            assert(!probe.plan->compareRaw(i,
                input,
                command.referenceCount ? raw.data() + command.firstReference : nullptr,
                command.referenceCount));
        }
        Plan::RawInput passInput{inputs[0], &pass};
        pass.colorAttachments[0].clearColor.g = 0.5f;
        assert(probe.plan->compareRaw(0u, passInput, raw.data(), 1u));
        pass.colorAttachments[0].clearColor.g = 0.375f;
        const auto nativeView = webGpuNativePayloadFor(*view);
        ++nativeView->creationKey.baseMipLevel;
        assert(probe.plan->validate());
        --nativeView->creationKey.baseMipLevel;
        assert(!probe.plan->validate());
        view.reset();
        texture.reset();
        allocation.releaseToken.reset();
        resources.reset();
        graph.reset();
        usage.reset();
        arguments.reset();
        scalars.reset();
        passes.reset();
        passArguments.reset();
        pipeline.reset();
        groupPipeline.reset();
        groups.reset();
        assert(!probe.plan->validate() && probe.allocator->memoryStats().allocatedBytes == bytes);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        probe.states =
            WebGpuOriginalResourceStatesFixture::verify(probe.plan, probe.stateQuota, probe.source);
        assert(!graphicsLoss_.plan);
        graphicsLoss_ = std::move(probe);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_INPUTS_GRAPHICS_OK %llu 8 1 full-pass-pipeline-set-draw drop allocation0\n",
            static_cast<unsigned long long>(peak));
    }

    static const auto& originalGraphicsInputs() noexcept { return graphicsLoss_.plan; }

    static void verifyGraphicsRetainedDiagnostic(const char* id)
    {
        assert(graphicsLoss_.plan && std::string_view(graphicsLoss_.plan->validate()) == id);
    }

    static void verifyGraphicsDeviceLoss()
    {
        assert(graphicsLoss_.plan && graphicsLoss_.retiredAfterActualLoss());
        graphicsLoss_ = {};
    }

    static LossProbe verifyTransfer(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryTransferRetention>& transfer,
        const std::array<rhi::TransferCopyArguments, 6>& copies,
        bool retainStreamLoss = false)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Domain = WebGpuCommandReferenceDomain;
        LossProbe probe;
        probe.source = "transfer";
        probe.stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        for (auto& quota : probe.quotas)
            quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const auto b = copies[0].source(), t = copies[2].destination(), d = copies[0].destination();
        const ResourceBarrierDesc buffer{b,
            {},
            ResourceState::CopySrc,
            ResourceState::CopyDst,
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead,
            ResourceAccess::TransferWrite};
        const ResourceBarrierDesc texture{t,
            {},
            ResourceState::CopyDst,
            ResourceState::CopySrc,
            PipelineStage::Transfer,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            ResourceAccess::TransferRead};
        const ResourceMemoryBarrierDesc bufferMemory{b,
            PipelineStage::Transfer,
            PipelineStage::ComputeShader,
            ResourceAccess::TransferWrite,
            ResourceAccess::ShaderRead};
        const ResourceMemoryBarrierDesc textureMemory{t,
            PipelineStage::Transfer,
            PipelineStage::FragmentShader,
            ResourceAccess::TransferWrite,
            ResourceAccess::ShaderRead};
        AliasingBarrierDesc alias;
        alias.beforeResource = b;
        alias.afterResource = d;
        alias.aliasGroup = {19u};
        const std::array<ResourceDependencyArguments, 5> dependencies{
            {*ResourceDependencyArguments::capture(K::BufferBarrier, buffer),
                *ResourceDependencyArguments::capture(K::TextureBarrier, texture),
                *ResourceDependencyArguments::capture(K::BufferMemoryBarrier, bufferMemory),
                *ResourceDependencyArguments::capture(K::TextureMemoryBarrier, textureMemory),
                ResourceDependencyArguments::capture(alias)}};
        std::array<WebGpuCommandReference, 16> references;
        references[0] = {Domain::Buffer, WebGpuCommandReferenceSource::Transfer, b};
        references[1] = {Domain::Texture, WebGpuCommandReferenceSource::Transfer, t};
        references[2] = references[0];
        references[3] = references[1];
        references[4] = references[0];
        references[5] = {Domain::Buffer, WebGpuCommandReferenceSource::Transfer, d};
        std::array<CommandUsage, 13> commands;
        for (std::size_t i = 0u; i < 5u; ++i)
            commands[i] = {
                dependencies[i].kind(), static_cast<std::uint32_t>(i), i == 4u ? 2u : 1u};
        commands[5] = {K::BeginTransferPass, 6u, 0u};
        for (std::size_t i = 0u; i < 5u; ++i) {
            commands[6u + i] = {copies[i].kind(), static_cast<std::uint32_t>(6u + 2u * i), 2u};
            const auto* source = transfer->find(copies[i].source());
            const auto* destination = transfer->find(copies[i].destination());
            assert(source && destination);
            references[6u + 2u * i] = {source->buffer ? Domain::Buffer : Domain::Texture,
                WebGpuCommandReferenceSource::Transfer,
                copies[i].source()};
            references[7u + 2u * i] = {destination->buffer ? Domain::Buffer : Domain::Texture,
                WebGpuCommandReferenceSource::Transfer,
                copies[i].destination()};
        }
        commands[11] = {K::EndTransferPass, 16u, 0u};
        commands[12] = {K::Finish, 16u, 0u};
        const char* diagnostic = nullptr;
        WebGpuGenericCommandProjectionBudget graphBudget{1u,
            true,
            1u,
            0u,
            0u,
            0u,
            16u,
            5u,
            {},
            probe.quotas[0]->limitBytes(),
            transfer->retainedBytes()};
        for (const auto& ref : references)
            ++graphBudget.maxDomainReferences[static_cast<std::size_t>(ref.domain)];
        WebGpuGenericCommandProjection::Sources sources;
        sources.transfer = transfer;
        auto graph = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare(std::move(sources),
                references.data(),
                references.size(),
                graphBudget,
                probe.quotas[0],
                diagnostic));
        assert(graph);
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 13u, 16u, probe.quotas[1]->limitBytes()},
            probe.quotas[1],
            diagnostic);
        assert(usage);
        auto inputs = std::array<Store::Input, 13>{{{*A::capture(K::BufferBarrier, A::None{})},
            {*A::capture(K::TextureBarrier, A::None{})},
            {*A::capture(K::BufferMemoryBarrier, A::None{})},
            {*A::capture(K::TextureMemoryBarrier, A::None{})},
            {*A::capture(K::AliasBarrier, A::None{})},
            {*A::capture(K::BeginTransferPass, A::None{}), "transfer"},
            {*A::capture(K::BufferCopy, A::None{})},
            {*A::capture(K::BufferCopy, A::None{})},
            {*A::capture(K::BufferToTextureCopy, A::None{})},
            {*A::capture(K::TextureToTextureCopy, A::None{})},
            {*A::capture(K::TextureToBufferCopy, A::None{})},
            {*A::capture(K::EndTransferPass, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        auto arguments = Store::prepareOriginal(inputs.data(),
            inputs.size(),
            {1u, true, 13u, 0u, 8u, 0u, probe.quotas[2]->limitBytes()},
            probe.quotas[2],
            diagnostic);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(usage,
            arguments,
            {1u, true, 13u, 0u, probe.quotas[3]->limitBytes()},
            probe.quotas[3],
            diagnostic);
        assert(scalars);
        std::array<Plan::OriginalDescriptor, 10> records{{{0u, dependencies[0]},
            {1u, dependencies[1]},
            {2u, dependencies[2]},
            {3u, dependencies[3]},
            {4u, dependencies[4]},
            {6u, copies[0]},
            {7u, copies[1]},
            {8u, copies[2]},
            {9u, copies[3]},
            {10u, copies[4]}}};
        WebGpuOriginalCommandInputsBudget budget{1u, true, 10u, probe.quotas[4]->limitBytes()};
        const auto factory = [&] {
            return Plan::prepareOriginal(
                scalars, {}, records.data(), records.size(), budget, probe.quotas[4], diagnostic);
        };
        CpuQuotaFactoryFixture pressure(probe.quotas[4]);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        const auto expect = [&](const char* id) {
            assert(
                !factory() && std::string_view(diagnostic) == id && !probe.quotas[4]->liveBytes());
        };
        --budget.maxDescriptorRecords;
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-CAPACITY");
        ++budget.maxDescriptorRecords;
        auto original = records;
        std::swap(records[0], records[1]);
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-ORDER");
        records = original;
        records[0].arguments = copies[0];
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-KIND");
        records = original;
        auto changed = buffer;
        changed.resource = d;
        records[0].arguments = *ResourceDependencyArguments::capture(K::BufferBarrier, changed);
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-RESOURCE");
        records = original;
        records[5].arguments = copies[1];
        expect("RHI-WEBGPU-ORIGINAL-INPUTS-RESOURCE");
        records = original;
        BufferCopyDesc wrong;
        assert(copies[0].write(wrong));
        wrong.sizeBytes = 0u;
        records[5].arguments = *TransferCopyArguments::capture(wrong);
        expect("RHI-WEBGPU-TRANSFER-BUFFER-COPY-CONTRACT");
        records = original;
        assert(!Plan::prepareOriginal(
                   scalars, {}, records.data(), 9u, budget, probe.quotas[4], diagnostic)
            && std::string_view(diagnostic) == "RHI-WEBGPU-ORIGINAL-INPUTS-ORDER");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        probe.plan = factory();
        std::pmr::set_default_resource(previous);
        assert(probe.plan);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        std::array<WebGpuGenericCommandUsage::RawReference, 16> raw;
        for (std::size_t i = 0u; i < references.size(); ++i) {
            const auto* r = transfer->find(references[i].resource);
            raw[i] = {references[i].domain,
                r->buffer ? static_cast<const void*>(r->buffer.get()) : r->texture.get()};
        }
        for (std::size_t i = 0u; i < inputs.size(); ++i) {
            Plan::RawInput input{inputs[i], {}};
            for (const auto& record : records)
                if (record.commandIndex == i)
                    std::visit(
                        [&](const auto& value) {
                            input.descriptor = value;
                        },
                        record.arguments);
            const auto& command = commands[i];
            assert(!probe.plan->compareRaw(i,
                input,
                command.referenceCount ? raw.data() + command.firstReference : nullptr,
                command.referenceCount));
            if (command.referenceCount) {
                input.descriptor = {};
                assert(probe.plan->compareRaw(
                    i, input, raw.data() + command.firstReference, command.referenceCount));
            }
        }
        auto& observation = const_cast<WebGpuTransferResourceObservation&>(*transfer->find(b));
        const auto initial = observation.bufferDesc.initialState;
        observation.bufferDesc.initialState =
            initial == ResourceState::CopySrc ? ResourceState::CopyDst : ResourceState::CopySrc;
        assert(!observation.matches() && probe.plan->validate());
        observation.bufferDesc.initialState = initial;
        const auto originalOwner = observation.buffer;
        observation.buffer =
            std::shared_ptr<WebGpuNativeBufferPayload>(probe.quotas[5], originalOwner.get());
        assert(!observation.matches() && probe.plan->validate());
        observation.buffer = originalOwner;
        const auto source = transfer->find(b)->buffer;
        const auto size = source->sizeBytes;
        ++source->sizeBytes;
        assert(probe.plan->validate());
        source->sizeBytes = size;
        assert(!probe.plan->validate()
            && std::string_view(probe.plan->executionDiagnostic())
                == webGpuGenericRecordingUsageDiagnostic);
        records = original;
        records[0].commandIndex = 99u;
        records[5].arguments = TransferCopyArguments::finish();
        assert(!probe.plan->validate() && probe.plan->descriptors()[0].commandIndex == 0u);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_INPUTS_TRANSFER_OK %llu 13 10 full-copy5 dependency5 allocation0\n",
            static_cast<unsigned long long>(peak));
        WebGpuOriginalTransferStateValidationFixture::verify(transfer, copies, retainStreamLoss);
        probe.states =
            WebGpuOriginalResourceStatesFixture::verify(probe.plan, probe.stateQuota, probe.source);
        if (retainStreamLoss)
            WebGpuOriginalStateTransactionFixture::activateLossProbe();
        return probe;
    }

private:
    inline static LossProbe graphicsLoss_{};
#endif
};
} // namespace arp::test
