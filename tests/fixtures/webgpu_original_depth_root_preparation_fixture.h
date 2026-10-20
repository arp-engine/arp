#pragma once
#include "rhi/backends/webgpu/webgpu_bindless_preparation.h"
#include "rhi/backends/webgpu/webgpu_bindless_root.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include "cpu_quota_factory_fixture.h"
#include "original_bindless_depth_stream_fixture.h"
#include "original_bindless_root_assembly_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/command_state.h"
#include "webgpu_original_bindless_root_execution_fixture.h"

namespace arp::test {
// Real public pipeline, prepared binding and Root in one original inventory.
// Cold profiles check typed depth consumption and a cancellable CPU state claim;
// explicit execution5 reuses the public Root executor and genuine native drain.
class WebGpuOriginalDepthRootPreparationFixture final {
    static void verifyReadPlan(
        const std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootNativeClosure>& closure,
        rhi::GpuDevice& device)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        using Sources = OriginalCommandResourceSources;
        using Transaction = OriginalCommandStateTransaction;
        const auto check = [](const char* id) {
            if (id)
                std::fprintf(stderr, "depth read Plan11: %s\n", id);
            assert(!id);
        };
        const auto& inventory = closure->originalInventory();
        const auto& bindless = closure->originalRoot();
        const auto bytes = inventory->originalMetadataQuota()->limitBytes();
        auto inputsQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto txQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        {
            const auto rootIndex = static_cast<std::uint32_t>(inventory->sources().size() - 1u);
            const auto& parent = std::get<std::shared_ptr<const OriginalCommandTextureLease>>(
                inventory->sources()[0]);
            const auto& view = std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                inventory->sources()[1]);
            const std::array<CommandUsage, 6u> commands{{{K::TextureBarrier, 0u, 1u},
                {K::BeginComputePass, 1u, 0u},
                {K::BindPreparedComputeBinding, 1u, 1u},
                {K::Dispatch, 2u, 0u},
                {K::EndComputePass, 2u, 0u},
                {K::Finish, 2u, 0u}}};
            const std::array<OriginalCommandUsage::Reference, 2u> references{{{0u}, {rootIndex}}};
            core::pmr::Vector<CommandVariableArguments::Input> fields;
            for (std::size_t i = 0u; i < commands.size(); ++i)
                fields.push_back({*(commands[i].kind == K::Dispatch
                        ? CommandScalarArguments::capture(
                              K::Dispatch, CommandScalarArguments::Dispatch{1u, 1u, 1u})
                        : CommandScalarArguments::capture(
                              commands[i].kind, CommandScalarArguments::None{}))});
            ResourceBarrierDesc barrier{parent->resource(),
                {0u, 2u, 0u, 1u, TextureAspect::Depth},
                ResourceState::DepthAttachment,
                ResourceState::ShaderRead};
            barrier.sourceStages = PipelineStage::DepthStencil;
            barrier.sourceAccess =
                ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite;
            barrier.destinationStages = PipelineStage::ComputeShader;
            barrier.destinationAccess = ResourceAccess::ShaderRead;
            const auto makeInputs = [&](const auto& transition) {
                const OriginalCommandInputs::OriginalDescriptor descriptor{
                    0u, *ResourceDependencyArguments::capture(K::TextureBarrier, transition)};
                auto result = OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
                    commands.data(),
                    commands.size(),
                    references.data(),
                    references.size(),
                    fields.data(),
                    &descriptor,
                    1u,
                    {},
                    inputsQuota,
                    id);
                check(id);
                assert(result);
                return result;
            };
            auto inputs = makeInputs(barrier);
            std::uint32_t owners = 0u, registrations = 0u;
            check(Sources::registeredOwnerUnion(
                inventory, inputs->retainedOwnerCount() + 1u, 128u, 128u, owners, registrations));
            OriginalCommandStateAdmissionBudget state{
                1u, true, 1u, registrations, 16u, 16u, owners, bytes};
            state.bindlessRoots = {4u,
                true,
                1u,
                bindless->size(),
                bindless->size() * inventory->sources().size(),
                1u,
                1u};
            const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
                inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            check(projected.diagnostic);
            state.bindlessRoots.maxSourceVisits = projected.sourceVisits;
            auto graph = OriginalCommandResourceStates::prepareOriginal(
                inputs, device, state, graphQuota, id);
            check(id);
            assert(graph);
            state.maxRetainedOwners += 2u;
            auto tx = Transaction::prepareOriginalOwnership(graph, device, state, txQuota, id);
            check(id);
            assert(tx);
            OriginalCommandStateSequenceBudget sequence{1u, true, 6u, 0u, 16u, 32u};
            sequence.bindlessRootStateVersion = 4u;
            sequence.bindlessRootStateOptIn = true;
            sequence.maxImplicitAccesses = projected.accesses;
            OriginalTextureContentPlanBudget content{11u, true, 1u, 0u, 2u, 4u, 60u, 4096u};
            const auto inspection = tx->preflightOriginalContentCells(content);
            check(inspection.diagnostic);
            assert(inspection.controls == 1u && inspection.cells == 2u && inspection.steps == 4u
                && !inspection.nativeExecutionProven());
            content.maxContentControls = static_cast<std::uint32_t>(inspection.controls);
            content.maxContentCells = inspection.cells;
            content.maxContentSteps = inspection.steps;
            content.maxCommandVisits = inspection.commandVisits;
            content.maxProjectionVisits = inspection.projectionVisits;
            const auto refuse = [&](const auto& budget) {
                const auto before = txQuota->liveBytes();
                const auto result = tx->preflightOriginalContentCells(budget);
                assert(result.diagnostic && !result.controls && !result.cells && !result.steps
                    && !result.commandVisits && !result.projectionVisits);
                assert(txQuota->liveBytes() == before && !tx->hasOriginalPrototypePlan());
            };
            auto bad = content;
            --bad.maxContentCells;
            refuse(bad);
            bad = content;
            --bad.maxContentSteps;
            refuse(bad);
            bad = content;
            --bad.maxCommandVisits;
            refuse(bad);
            bad = content;
            --bad.maxProjectionVisits;
            refuse(bad);
            bad = content;
            bad.contractVersion = 2u;
            refuse(bad);
            bad = content;
            bad.maxReadbackCommands = 1u;
            refuse(bad);
            auto wrongBarrier = barrier;
            wrongBarrier.after = ResourceState::CopySrc;
            wrongBarrier.destinationStages = PipelineStage::Transfer;
            wrongBarrier.destinationAccess = ResourceAccess::TransferRead;
            tx.reset(); // Keep the declared one-transaction capacity during the negative case.
            {
                auto wrongInputs = makeInputs(wrongBarrier);
                auto wrongGraph = OriginalCommandResourceStates::prepareOriginal(
                    wrongInputs, device, state, graphQuota, id);
                check(id);
                assert(wrongGraph);
                auto wrong =
                    Transaction::prepareOriginalOwnership(wrongGraph, device, state, txQuota, id);
                check(id);
                assert(wrong);
                assert(wrong->materializeOriginalPrototypePlan(sequence, content));
            }
            tx = Transaction::prepareOriginalOwnership(graph, device, state, txQuota, id);
            check(id);
            assert(tx);
            const auto retainedBytes = txQuota->liveBytes();
            {
                core::CpuMemoryQuotaLease pressure(txQuota, txQuota->limitBytes() - retainedBytes);
                assert(
                    pressure.charged() && tx->materializeOriginalPrototypePlan(sequence, content));
                assert(!tx->hasOriginalPrototypePlan() && tx->journalEmpty());
            }
            assert(txQuota->liveBytes() == retainedBytes);
            check(tx->materializeOriginalPrototypePlan(sequence, content));
            assert(tx->plannedContentControlCount() == 1u && tx->plannedContentCellCount() == 2u);
            for (const auto backend :
                {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
                assert(tx->executionDiagnostic(backend));
            check(tx->applyOriginalPrototypePlan());
            assert(tx->status() == Transaction::Status::Pending && !tx->journalEmpty());
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            check(tx->validate());
            check(closure->validate());
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            assert(!parent->originalObject()->observeOriginalCommittedState(
                barrier.textureSubresources));
            check(tx->abandonOriginalPendingOwnership());
            assert(tx->journalEmpty());
            const auto restored = parent->originalObject()->observeOriginalCommittedState(
                barrier.textureSubresources);
            assert(restored && restored->state() == ResourceState::DepthAttachment);
            check(closure->validate());
        }
        assert(!inputsQuota->liveBytes() && !graphQuota->liveBytes() && !txQuota->liveBytes());
        std::puts(
            "COMPLETED_DEPTH_ROOT_READ_PLAN11_OK same-inventory journal ShaderRead Compute exact-cell-proof mixed-generations budgets hot0 cancel-preserved no-native-shader-execution");
    }

public:
    static std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootNativeClosure> prepare(
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& view,
        const std::shared_ptr<const rhi::OriginalCommandSamplerLease>& sampler,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& result,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const WebGpuOriginalBindlessDepthExecutionFixture* execution = nullptr,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& secondResult = {})
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using Sources = OriginalCommandResourceSources;
        using Root = OriginalCommandBindlessRootSource;
        using Closure = WebGpuBindlessRootNativeClosure;
        const auto check = [](const char* id) {
            if (id)
                std::fprintf(stderr, "depth Root5: %s\n", id);
            assert(!id);
        };
        const char* id = nullptr;
        core::pmr::String diagnostic;
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        auto authorization = extension->authorize(package, *snapshot, diagnostic);
        if (!authorization)
            std::fprintf(stderr, "depth authorization5: %s\n", diagnostic.c_str());
        assert(authorization);
        auto pipeline = core::shareUnique(extension->createComputePipeline(authorization,
            execution ? execution->fault ? "faultMain" : "publicCleanMain" : "cleanMain",
            diagnostic));
        if (!pipeline)
            std::fprintf(stderr, "depth pipeline5: %s\n", diagnostic.c_str());
        assert(pipeline);
        const auto retained = snapshot->plan().snapshotBytes + package->sourceBytes();
        BindlessComputeBindingPool bindings(
            *extension, {1u, retained + 4096u, retained + 4096u, 8u, 4096u});
        auto binding = bindings.prepare(
            {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()}, diagnostic);
        if (!binding)
            std::fprintf(stderr, "depth binding5: %s\n", diagnostic.c_str());
        assert(binding);
        core::pmr::Vector<Sources::Source> inputs{
            view->originalParentLease(), view, sampler, result};
        if (secondResult)
            inputs.push_back(secondResult);
        for (const auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                 BindlessSnapshot::OriginalBufferRole::FaultLatch,
                 BindlessSnapshot::OriginalBufferRole::FaultReadback})
            inputs.push_back(snapshot->originalBufferSource(role));
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = execution ? 20u : 6u;
        admission.maxReferences = execution ? 10u : 2u;
        admission.maxInputDescriptors = execution ? 6u : 1u;
        admission.maxGraphOwners = 256u;
        admission.maxStateTransactions = 1u;
        admission.maxStateObservations = 16u;
        admission.maxTextureRanges = 16u;
        if (execution) {
            admission.maxPendingSubmissions = 1u;
            admission.maxUnpublishedResultBytes = sizeof(std::uint32_t);
            WebGpuOriginalBindlessPreparationRequest{}.configureAdmission(admission);
        }
        admission.maxCpuBytes.fill(quota->limitBytes());
        for (const auto& input : inputs)
            std::visit(
                [&](const auto& source) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(source)>::element_type>;
                    ++admission.maxSourceOwners[static_cast<std::size_t>(T::domain)];
                },
                input);
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::BindlessRoot)] =
            1u;
        auto inventory = Sources::prepareColdOriginalTyped(
            inputs.data(), inputs.size(), admission, {6u, true, 1u}, quota, id);
        check(id);
        assert(inventory);
        auto root = OriginalBindlessRootAssemblyFixture::prepare(
            snapshot, binding, inventory, device, quota, id);
        check(id);
        assert(root);
        const auto count = static_cast<std::uint32_t>(root->elements().size() + 1u);
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(quota->limitBytes());
        OriginalBindlessRootNativeClosureBudget budget{
            execution ? 8u : 7u, true, count, count + 3u, 128u, nativeQuota->limitBytes()};
        budget.completedDepthInputs = {
            1u, true, 2u, 20u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        const auto factory = [&] {
            return Closure::prepareOriginal(root, inventory, device, budget, nativeQuota, id);
        };
        auto closure = factory();
        check(id);
        assert(closure && !closure->nativeExecutionProven());
        const auto physical = webGpuOriginalBindlessPhysicalGroup(binding);
        assert(physical && physical->completedTexturePublications.size() == 2u);
        auto& publication = physical->completedTexturePublications.front();
        const auto actual = publication.depthContent;
        publication.depthContent =
            std::shared_ptr<const OriginalCompletedDepthTextureSource>(nativeQuota, actual.get());
        assert(closure->validate());
        publication.depthContent = actual;
        check(closure->validate());
        const auto nativeView = physical->bindings.front().textureView;
        assert(nativeView);
        const auto parent = nativeView->parentTexture;
        nativeView->parentTexture =
            std::shared_ptr<WebGpuNativeTexturePayload>(nativeQuota, parent.get());
        assert(closure->validate());
        nativeView->parentTexture = parent;
        check(closure->validate());
        budget.maxNativeOwners = closure->nativeOwnerCount();
        budget.maxRetainedOwners = closure->retainedOwnerCount();
        {
            auto exact = factory();
            check(id);
            assert(exact);
        }
        --budget.completedDepthInputs.maxCells;
        assert(!factory() && id);
        ++budget.completedDepthInputs.maxCells;
        --budget.completedDepthInputs.maxTextureTexelBytes;
        assert(!factory() && id);
        ++budget.completedDepthInputs.maxTextureTexelBytes;
        --budget.completedDepthInputs.maxRetainedOwners;
        assert(!factory() && id);
        ++budget.completedDepthInputs.maxRetainedOwners;
        --budget.maxResourceEntries;
        assert(!factory() && id);
        ++budget.maxResourceEntries;
        --budget.maxNativeOwners;
        assert(!factory() && id);
        ++budget.maxNativeOwners;
        --budget.maxRetainedOwners;
        assert(!factory() && id);
        ++budget.maxRetainedOwners;
        const auto baseline = nativeQuota->liveBytes();
        {
            core::CpuMemoryQuotaLease pressure(nativeQuota, nativeQuota->limitBytes() - baseline);
            assert(pressure.charged() && !factory() && id);
        }
        assert(nativeQuota->liveBytes() == baseline);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        check(closure->validate());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        if (execution) {
            assert(OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                snapshot->plan(), 5u));
            OriginalBindlessDepthStreamFixture::execute(root, inventory, device, *execution);
        } else {
            assert(!OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                snapshot->plan(), 5u));
            verifyReadPlan(closure, device);
        }
        return closure;
    }
};
} // namespace arp::test
#endif
