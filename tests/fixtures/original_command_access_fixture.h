#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/command_state.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
// Reusable genuine original access/plan recipe. All geometry, pitch, usage and
// initial states are authored explicitly; it creates no native GPU command.
class OriginalCommandAccessFixture final {
    static constexpr std::uint64_t bytes = 65536u;
    using Inputs = rhi::OriginalCommandInputs;
    using Sources = rhi::OriginalCommandResourceSources;
    using Usage = rhi::OriginalCommandUsage;
    using K = rhi::CommandUsageKind;
    using S = rhi::ResourceState;
    using Store = rhi::CommandVariableArguments;
    using Scalar = rhi::CommandScalarArguments;
    using Graph = rhi::OriginalCommandResourceStates;
    using Root = rhi::OriginalCommandStateTransaction;
    using Access = rhi::common::OriginalCommandAccessValidation;

    static std::shared_ptr<core::CpuMemoryQuota> quota()
    {
        return core::makeShared<core::CpuMemoryQuota>(bytes);
    }

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    class Context final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 6> quotas{
            quota(), quota(), quota(), quota(), quota(), quota()};
        core::UniquePtr<rhi::GpuDevice> device;
        std::shared_ptr<core::CpuMemoryQuota> nativeLossQuota;
        std::optional<rhi::OriginalDeviceNativeLossObservation> nativeLossObservation;
        std::array<Sources::Source, 5> leases;
        std::array<Inputs::OriginalDescriptor, 6> descriptors{
            {{0u, rhi::TransferCopyArguments::finish()},
                {0u, rhi::TransferCopyArguments::finish()},
                {0u, rhi::TransferCopyArguments::finish()},
                {0u, rhi::TransferCopyArguments::finish()},
                {0u, rhi::TransferCopyArguments::finish()},
                {0u, rhi::TransferCopyArguments::finish()}}};
        rhi::OriginalCommandAdmissionBudget budget;
        rhi::OriginalCommandStateAdmissionBudget graphBudget{1u, true, 1u, 5u, 8u, 6u, 29u, bytes};
        rhi::BufferToTextureCopyDesc upload;
        rhi::TextureToBufferCopyDesc read;
        rhi::TextureToTextureCopyDesc copy;
        rhi::RenderPassDesc pass;
        std::array<rhi::CommandUsage, 10> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::BufferCopy, 0u, 2u},
            {K::BufferToTextureCopy, 2u, 2u},
            {K::TextureToBufferCopy, 4u, 2u},
            {K::TextureToTextureCopy, 6u, 2u},
            {K::EndTransferPass, 8u, 0u},
            {K::TextureBarrier, 8u, 1u},
            {K::BeginRenderPass, 9u, 1u},
            {K::EndRenderPass, 10u, 0u},
            {K::Finish, 10u, 0u}}};
        std::array<Usage::Reference, 10> references{
            {{0u}, {1u}, {0u}, {3u}, {2u}, {1u}, {2u}, {3u}, {3u}, {4u}}};

        template <class Factory>
        explicit Context(Factory&& create,
            bool singleMip = false,
            std::uint32_t nativePayloads = 5u,
            bool nativeLoss = false)
            : device(create())
        {
            const char* diagnostic = nullptr;
            assert(device);
            assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, quotas[0]));
            if (nativeLoss) {
                nativeLossQuota = quota();
                assert(!device->prepareOriginalDeviceNativeLoss(
                    {{1u, true, 1u, 1u, bytes}, 1u}, nativeLossQuota));
                auto observation = device->observeOriginalDeviceNativeLoss(diagnostic);
                assert(observation && !diagnostic);
                nativeLossObservation.emplace(*observation);
            }
            assert(!device->prepareOriginalResourceRegistration(
                {1u, true, 2u, 2u, 1u, 13u, bytes}, quotas[1]));
            rhi::BufferDesc buffer;
            buffer.sizeBytes = 2048u;
            buffer.initialState = S::CopySrc;
            buffer.usage = rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
            auto src = device->createBuffer(buffer);
            buffer.initialState = S::CopyDst;
            auto dst = device->createBuffer(buffer);
            assert(src && dst);
            auto bs = rhi::OriginalCommandBufferLease::prepareOriginal(
                src, {1u, true, 2u, bytes}, quotas[2], diagnostic);
            auto bd = rhi::OriginalCommandBufferLease::prepareOriginal(
                dst, {1u, true, 2u, bytes}, quotas[2], diagnostic);
            assert(bs && bd);
            rhi::TextureDesc texture;
            texture.extent = {4u, 4u, 1u};
            texture.format = rhi::TextureFormat::Rgba8Unorm;
            texture.mipLevelCount = singleMip ? 1u : 2u;
            texture.arrayLayerCount = 1u;
            texture.initialState = S::CopySrc;
            texture.usage = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst
                | rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment;
            auto ts = device->createTexture(texture);
            texture.initialState = S::CopyDst;
            auto td = device->createTexture(texture);
            assert(ts && td);
            rhi::TextureViewDesc view;
            view.format = texture.format;
            view.usage = rhi::TextureUsage::RenderAttachment;
            auto target = device->createTextureView(*td, view);
            assert(target);
            auto sl = rhi::OriginalCommandTextureLease::prepareOriginal(
                ts, {1u, true, 2u, bytes}, quotas[2], diagnostic);
            auto dl = rhi::OriginalCommandTextureLease::prepareOriginal(
                td, {1u, true, 2u, bytes}, quotas[2], diagnostic);
            auto vl = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                target, {1u, true, 2u, bytes}, quotas[2], diagnostic);
            assert(sl && dl && vl);
            auto vs = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                vl, dl, {1u, true, 5u, bytes}, quotas[2], diagnostic);
            assert(vs);
            leases = {bs, bd, sl, dl, vs};
            budget.contractVersion = 1u;
            budget.prototypeOptIn = true;
            budget.maxCommands = 10u;
            budget.maxReferences = 10u;
            budget.maxGraphOwners = 20u;
            budget.maxInputDescriptors = 6u;
            budget.maxStateObservations = 8u;
            budget.maxTextureRanges = 6u;
            budget.maxStateTransactions = 1u;
            budget.maxNativePayloadObjects = nativePayloads;
            budget.maxMipMappings = 6u;
            budget.maxPassColorAttachments = 1u;
            budget.maxPassAttachmentReferences = 1u;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                2u;
            budget.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
                2u;
            budget.maxSourceOwners[static_cast<std::size_t>(
                rhi::CommandReferenceDomain::TextureView)] = 1u;
            budget.maxCpuBytes[0] = budget.maxCpuBytes[1] = budget.maxCpuBytes[2] = bytes;
            const auto range = rhi::defaultTextureSubresourceRange(texture);
            upload.sourceBuffer = bs->resource();
            upload.destinationTexture = dl->resource();
            upload.extent = texture.extent;
            upload.textureSubresources = range;
            upload.bytesPerRow = 256u;
            upload.rowsPerImage = 4u;
            upload.mipLayouts = {
                {0u, {4u, 4u, 1u}, 256u, 4u, 0u}, {1u, {2u, 2u, 1u}, 256u, 2u, 1024u}};
            read.sourceTexture = sl->resource();
            read.destinationBuffer = bd->resource();
            read.extent = texture.extent;
            read.textureSubresources = range;
            read.bytesPerRow = 256u;
            read.rowsPerImage = 4u;
            read.mipLayouts = {
                {0u, {4u, 4u, 1u}, 256u, 4u, 0u}, {1u, {2u, 2u, 1u}, 256u, 2u, 1024u}};
            copy.sourceTexture = sl->resource();
            copy.destinationTexture = dl->resource();
            copy.extent = texture.extent;
            copy.sourceSubresources = copy.destinationSubresources = range;
            copy.mipLayouts = {{0u, {4u, 4u, 1u}}, {1u, {2u, 2u, 1u}}};
            if (singleMip) {
                upload.mipLayouts.resize(1u);
                read.mipLayouts.resize(1u);
                copy.mipLayouts.resize(1u);
            }
            rhi::RenderPassColorAttachmentDesc color;
            color.target = vs->resource();
            color.format = texture.format;
            color.loadOp = rhi::LoadOp::Clear;
            pass.colorAttachments.push_back(color);
            rhi::ResourceBarrierDesc barrier{dl->resource(), range, S::CopyDst, S::ColorAttachment};
            barrier.sourceStages = rhi::PipelineStage::Transfer;
            barrier.sourceAccess = rhi::ResourceAccess::TransferWrite;
            barrier.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
            barrier.destinationAccess = rhi::ResourceAccess::ColorAttachmentRead
                | rhi::ResourceAccess::ColorAttachmentWrite;
            descriptors = {{{1u,
                                *rhi::TransferCopyArguments::capture(rhi::BufferCopyDesc{
                                    bs->resource(), bd->resource(), 0u, 0u, 64u})},
                {2u, freeze(upload)},
                {3u, freeze(read)},
                {4u, freeze(copy)},
                {6u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier)},
                {7u, freezePass()}}};
        }

        template <class Desc>
        std::shared_ptr<const rhi::TransferCopyInputSnapshot> freeze(const Desc& value)
        {
            const char* diagnostic = nullptr;
            auto frozen = rhi::TransferCopyInputSnapshot::prepareOriginal(
                value, {1u, true, 2u, bytes}, quotas[3], diagnostic);
            assert(frozen);
            return frozen;
        }

        std::shared_ptr<const rhi::RenderPassArguments> freezePass()
        {
            const char* diagnostic = nullptr;
            auto frozen = rhi::RenderPassArguments::prepareOriginal(
                pass, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, quotas[3], diagnostic);
            assert(frozen);
            return frozen;
        }

        template <class Commands, class References, class Descriptors>
        std::shared_ptr<const Graph> tryMake(const Commands& stream,
            const References& refs,
            const Descriptors& input,
            const char*& diagnostic)
        {
            diagnostic = nullptr;
            auto sources = Sources::prepareOriginal(
                leases.data(), leases.size(), budget, quotas[2], diagnostic);
            assert(sources);
            auto usage = Usage::prepareOriginal(sources,
                stream.data(),
                stream.size(),
                refs.data(),
                refs.size(),
                quotas[3],
                diagnostic);
            assert(usage);
            core::pmr::Vector<Store::Input> fields;
            for (const auto& c : stream)
                fields.push_back({*Scalar::capture(c.kind, Scalar::None{})});
            auto store = Store::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, budget.maxCommands, 0u, 0u, 0u, bytes},
                quotas[3],
                diagnostic);
            assert(store);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, store, quotas[3], diagnostic);
            assert(scalars);
            auto inputs =
                Inputs::prepareOriginal(scalars, input.data(), input.size(), quotas[3], diagnostic);
            assert(inputs);
            auto graph =
                Graph::prepareOriginal(inputs, *device, graphBudget, quotas[4], diagnostic);
            return graph;
        }

        template <class Commands, class References, class Descriptors>
        std::shared_ptr<const Graph> make(
            const Commands& stream, const References& refs, const Descriptors& input)
        {
            const char* diagnostic = nullptr;
            auto graph = tryMake(stream, refs, input, diagnostic);
            if (!graph)
                std::fprintf(stderr, "original access graph diagnostic: %s\n", diagnostic);
            return graph;
        }

        std::shared_ptr<const Graph> make() { return make(commands, references, descriptors); }

        std::shared_ptr<const Graph> makeCopies()
        {
            const std::array<rhi::CommandUsage, 7> stream{{commands[0],
                commands[1],
                commands[2],
                commands[3],
                commands[4],
                commands[5],
                {K::Finish, 8u, 0u}}};
            const std::array<Usage::Reference, 8> refs{{references[0],
                references[1],
                references[2],
                references[3],
                references[4],
                references[5],
                references[6],
                references[7]}};
            const std::array<Inputs::OriginalDescriptor, 4> fields{
                {descriptors[0], descriptors[1], descriptors[2], descriptors[3]}};
            return make(stream, refs, fields);
        }
    };

    static void verifyPlan(Context& c,
        std::shared_ptr<const Graph> graph,
        const rhi::OriginalCommandStateSequenceBudget& budget,
        std::size_t cells,
        std::size_t transitions)
    {
        c.quotas[5] = quota();
        const char* diagnostic = nullptr;
        const auto sources =
            graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto make = [&] {
            return Root::prepareOriginalOwnership(
                graph, *c.device, {1u, true, 1u, 5u, 8u, 6u, 30u, bytes}, c.quotas[5], diagnostic);
        };
        const auto planned = [&] {
            auto root = make();
            if (root && root->materializeOriginalPrototypePlan(budget))
                root.reset();
            return root;
        };
        CpuQuotaFactoryFixture pressure(c.quotas[5]);
        const auto peak = pressure.measure(planned);
        pressure.verify(peak, planned);
        auto root = make();
        assert(root && !root->hasOriginalPrototypePlan());
        {
            core::CpuMemoryQuotaLease full(c.quotas[5], bytes - c.quotas[5]->liveBytes());
            assert(full.charged());
            is(root->materializeOriginalPrototypePlan(budget), "RHI-ORIGINAL-PLAN-ALLOCATION");
            assert(!root->hasOriginalPrototypePlan() && root->journalEmpty() && !graph->validate());
        }
        auto limited = budget;
        --limited.maxSteps;
        is(root->materializeOriginalPrototypePlan(limited), "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
        limited = budget;
        --limited.maxStateCells;
        is(root->materializeOriginalPrototypePlan(limited), "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY");
        assert(!root->hasOriginalPrototypePlan() && root->journalEmpty() && !graph->validate());
        assert(!root->materializeOriginalPrototypePlan(budget)
            && root->plannedFinalCellCount() == cells
            && root->plannedAccessStepCount() == budget.maxSteps
            && root->plannedTransitionCount() == transitions);
        is(root->materializeOriginalPrototypePlan(budget), "RHI-ORIGINAL-PLAN-STATUS");
        is(root->beginOriginalPendingOwnership(),
            "RHI-ORIGINAL-PLAN-EXPLICIT-APPLICATION-REQUIRED");
        const auto ownerCount = root->retainedOwnerCount();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            if (const auto* id = root->applyOriginalPrototypePlan())
                return id;
            if (const auto* id = root->validate())
                return id;
            if (const auto* id = root->compareOriginalGraph(graph, *c.device))
                return id;
            if (!graph->validate() || root->retainedOwnerCount() != ownerCount
                || sources->originalStateTransactionCount() != 1u)
                return "TEST-ORIGINAL-PLAN-PENDING";
            root->abandonOriginalPendingOwnership();
            return root->journalEmpty() && root->status() == Root::Status::Abandoned
                ? nullptr
                : "TEST-ORIGINAL-PLAN-ROLLBACK";
        }));
        assert(graph->validate() && sources->originalStateTransactionCount() == 1u);
        root.reset();
        assert(!sources->originalStateTransactionCount());
        graph = transitions ? c.make() : c.makeCopies();
        assert(graph);
        root = Root::prepareOriginalOwnership(
            graph, *c.device, {1u, true, 1u, 5u, 8u, 6u, 30u, bytes}, c.quotas[5], diagnostic);
        assert(root);
        assert(!root->materializeOriginalPrototypePlan(budget));
        std::weak_ptr<const Graph> weak = graph;
        graph.reset();
        assert(!weak.expired());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            if (const auto* id = root->applyOriginalPrototypePlan())
                return id;
            if (const auto* id = root->commitOriginalPendingOwnership())
                return id;
            if (root->status() != Root::Status::Committed || !root->journalEmpty()
                || root->nativeExecutionProven())
                return "TEST-ORIGINAL-PLAN-COMMIT";
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU})
                if (!root->executionDiagnostic(backend)
                    || arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                        .executionAvailable)
                    return "TEST-ORIGINAL-PLAN-EXECUTION";
            return root->validate();
        }));
        if (transitions) {
            const auto* texture =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(c.leases[3])
                    ->originalObject();
            assert(texture->subresourceState(rhi::defaultTextureSubresourceRange(texture->desc()))
                == S::ColorAttachment);
            assert(root->originalGraph()->validate());
        }
        root.reset();
        assert(weak.expired() && !sources->originalStateTransactionCount());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_PLAN_OK cells%zu steps%llu transitions%zu cold%llu same-root-quota immutable-intent sealed-finals rollback-ABA commit-caller-drop exact-less-exhausted independent-bounds hot-allocation0 gates4-closed\n",
            cells,
            static_cast<unsigned long long>(budget.maxSteps),
            transitions,
            static_cast<unsigned long long>(peak));
    }

public:
    // Shared genuine original recipe for backend closure/recording tests. No
    // private native graph or substitute mutable control is constructed here.
    template <class Factory, class Visitor>
    static void withOriginalPlan(
        Factory&& create, Visitor&& visitor, std::uint32_t nativePayloads = 5u)
    {
        withConfiguredOriginalPlan(create, [](auto&) {}, visitor, nativePayloads);
    }

    template <class Factory, class Configure, class Visitor>
    static void withConfiguredOriginalPlan(Factory&& create,
        Configure&& configure,
        Visitor&& visitor,
        std::uint32_t nativePayloads = 5u,
        bool nativeLoss = false)
    {
        Context context(create, true, nativePayloads, nativeLoss);
        configure(context.budget);
        auto graph = context.make();
        assert(graph);
        const char* diagnostic = nullptr;
        auto root = prepareOriginalPlan(graph, *context.device, context.quotas[5], diagnostic);
        assert(root);
        visitor(context, root);
    }

    template <class Factory, class Visitor>
    static void withNativeLossOriginalPlan(Factory&& create, Visitor&& visitor)
    {
        withConfiguredOriginalPlan(create, [](auto&) {}, visitor, 5u, true);
    }

    // Reuse the same genuine recipe when testing independent admissions over
    // its original graph. No control/observation/source is reconstructed here.
    static std::shared_ptr<Root> prepareOriginalPlan(const std::shared_ptr<const Graph>& graph,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& recording,
        const char*& diagnostic,
        const rhi::OriginalCommandStateSequenceBudget& planBudget = {1u, true, 10u, 0u, 8u, 10u},
        const rhi::OriginalCommandStateAdmissionBudget& ownershipBudget = {
            1u, true, 1u, 5u, 8u, 6u, 30u, bytes})
    {
        auto root =
            Root::prepareOriginalOwnership(graph, device, ownershipBudget, recording, diagnostic);
        if (!root)
            return {};
        diagnostic = root->materializeOriginalPrototypePlan(planBudget);
        return diagnostic ? nullptr : root;
    }

    template <class Factory, class Configure, class Visitor>
    static void inspectConfiguredOriginalMipGraph(
        Factory&& create, Configure&& configure, Visitor&& visitor, bool nativeLoss = true)
    {
        Context context(create, false, 5u, nativeLoss);
        configure(context.budget);
        const char* diagnostic = nullptr;
        auto graph =
            context.tryMake(context.commands, context.references, context.descriptors, diagnostic);
        visitor(context, graph, diagnostic);
    }

    template <class Factory> static void verify(Factory&& create)
    {
        Context c(create);
        auto graph = c.makeCopies();
        assert(graph && graph->retainedOwnerCount() == 28u);
        rhi::OriginalCommandStateSequenceBudget budget{1u, true, 7u, 0u, 10u, 12u};
        auto result = Access::inspect(graph, budget);
        assert(!result.diagnostic && result.stateCells == 10u && result.steps == 12u);
        const char* diagnostic = nullptr;
        auto root = Root::prepareOriginalOwnership(
            graph, *c.device, {1u, true, 1u, 5u, 8u, 6u, 30u, bytes}, c.quotas[5], diagnostic);
        assert(root && !root->preflightOriginalAccesses(budget).diagnostic
            && root->retainedOwnerCount() == 29u && root->originalControlCount() == 4u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            if (const auto* id = root->preflightOriginalAccesses(budget).diagnostic)
                return id;
            return graph->validate();
        }));
        root.reset();
        const auto saved = c.descriptors;
        c.upload.mipLayouts[1].bufferOffsetBytes = 512u;
        c.descriptors[1].arguments = c.freeze(c.upload);
        auto invalid = c.makeCopies();
        assert(invalid);
        result = Access::inspect(invalid, budget);
        is(result.diagnostic, "RHI-ORIGINAL-ACCESS-COPY-MIP-OVERLAP");
        assert(result.commandIndex == 2u);
        c.upload.mipLayouts[1].bufferOffsetBytes = 1024u;
        c.descriptors = saved;
        c.upload.mipLayouts[1].bytesPerRow = 8u;
        c.descriptors[1].arguments = c.freeze(c.upload);
        invalid = c.makeCopies();
        assert(invalid);
        is(Access::inspect(invalid, budget).diagnostic, "RHI-ORIGINAL-ACCESS-COPY-TEXTURE-LAYOUT");
        c.upload.mipLayouts[1].bytesPerRow = 256u;
        c.descriptors = saved;
        c.copy.mipLayouts.pop_back();
        c.descriptors[3].arguments = c.freeze(c.copy);
        invalid = c.makeCopies();
        assert(invalid);
        is(Access::inspect(invalid, budget).diagnostic, "RHI-ORIGINAL-ACCESS-COPY-MIP-COUNT");
        c.copy.mipLayouts.push_back({1u, {2u, 2u, 1u}});
        c.descriptors = saved;
        c.copy.mipLayouts[1].extent = {3u, 2u, 1u};
        c.descriptors[3].arguments = c.freeze(c.copy);
        invalid = c.makeCopies();
        assert(invalid);
        is(Access::inspect(invalid, budget).diagnostic, "RHI-ORIGINAL-ACCESS-COPY-TEXTURE-EXTENT");
        c.copy.mipLayouts[1].extent = {2u, 2u, 1u};
        c.descriptors = saved;
        Context p(create, true);
        auto passGraph = p.make();
        assert(passGraph && passGraph->retainedOwnerCount() == 29u);
        const rhi::OriginalCommandStateSequenceBudget passBudget{1u, true, 10u, 0u, 8u, 10u};
        assert(!Access::inspect(passGraph, passBudget).diagnostic);
        p.pass.layeredRendering = true;
        p.descriptors[5].arguments = p.freezePass();
        auto invalidPass = p.make();
        assert(invalidPass);
        is(Access::inspect(invalidPass, passBudget).diagnostic,
            "RHI-ORIGINAL-ACCESS-LAYERED-PASS-UNPROVEN");
        p.pass.layeredRendering = false;
        p.descriptors[5].arguments = p.freezePass();
        const auto& bs =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(c.leases[0]);
        rhi::ResourceBarrierDesc write{bs->resource(), {}, S::CopySrc, S::ShaderWrite};
        write.sourceStages = rhi::PipelineStage::Transfer;
        write.sourceAccess = rhi::ResourceAccess::TransferRead;
        write.destinationStages = rhi::PipelineStage::ComputeShader;
        write.destinationAccess = rhi::ResourceAccess::ShaderWrite;
        const std::array<rhi::CommandUsage, 2> commands{
            {{K::BufferBarrier, 0u, 1u}, {K::Finish, 1u, 0u}}};
        const std::array<Usage::Reference, 1> references{{{0u}}};
        const std::array<Inputs::OriginalDescriptor, 1> descriptors{
            {{0u, *rhi::ResourceDependencyArguments::capture(K::BufferBarrier, write)}}};
        invalid = c.make(commands, references, descriptors);
        assert(invalid);
        is(Access::inspect(invalid, {1u, true, 2u, 0u, 2u, 1u}).diagnostic,
            "RHI-ORIGINAL-ACCESS-BUFFER-USAGE");
        assert(!graph->validate() && bs->originalObject()->state() == S::CopySrc);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend);
            assert(!decision.executionAvailable && !decision.originalIssuerAvailable);
        }
        verifyPlan(c, graph, budget, 6u, 0u);
        verifyPlan(p, passGraph, passBudget, 4u, 1u);
        for (const bool shutdown : {false, true}) {
            Context terminal(create, true);
            auto initial = terminal.make();
            assert(initial);
            auto pending = Root::prepareOriginalOwnership(initial,
                *terminal.device,
                {1u, true, 1u, 5u, 8u, 6u, 30u, bytes},
                terminal.quotas[5],
                diagnostic);
            assert(pending);
            assert(!pending->materializeOriginalPrototypePlan(passBudget)
                && !pending->applyOriginalPrototypePlan());
            initial.reset();
            if (shutdown)
                terminal.device.reset();
            else
                terminal.device->retireOriginalDeviceLifetime();
            const char* id = shutdown ? "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN"
                                      : "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED";
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(terminal.quotas, [&]() noexcept {
                is(pending->validate(), id);
                is(pending->commitOriginalPendingOwnership(), id);
                if (pending->journalEmpty())
                    return "TEST-ORIGINAL-PLAN-TERMINAL-RETENTION";
                pending->abandonOriginalPendingOwnership();
                is(pending->validate(), id);
                return pending->journalEmpty() ? nullptr : "TEST-ORIGINAL-PLAN-TERMINAL-ROLLBACK";
            }));
            pending.reset();
            const auto* image = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                terminal.leases[3])
                                    ->originalObject();
            assert(image->state() == S::CopyDst
                && !image->observeOriginalCommittedState(
                    rhi::defaultTextureSubresourceRange(image->desc())));
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_PLAN_TERMINAL_OK genuine-retire-shutdown pending-retained commit-rejected rollback-no-revival hot-allocation0 gates4-closed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ACCESS_OK original-device-source-state controls4 copies-cells10-steps12-owners28-root29 pass-cells8-steps10-owners29 copy-families4 explicit-mips full-padded-footprint one-layer-pass usage-binding overlap-pitch-count-extent-layered-reject no-mutation hot-allocation0 gates4-closed");
    }
};
} // namespace arp::test
