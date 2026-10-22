#pragma once
#include "bindless_scene_conformance_contract.h"
#include "bindless_scene_geometry_data_fixture.h"
#include "original_bindless_graphics_stream_fixture.h"

namespace arp::test {
using namespace renderer;

// Actual immutable package/pipeline/Root ownership on Null. This checks public
// scene-input selection, CPU readback correspondence and retirement; buffers
// contain no native content proof. No native geometry is recorded or submitted.
class BindlessSceneRootInputsFixture final {
    struct Data : BindlessSceneGeometryDataFixture {
        static constexpr bool sceneStream = true;
        renderer::BindlessPbrGeometryDraw16 draw;
        std::uint32_t pass = 0u;
        std::shared_ptr<std::size_t> preparedCount = core::makeShared<std::size_t>(0u);
        std::shared_ptr<std::size_t> readbackCount = core::makeShared<std::size_t>(0u);

        template <class Root, class Inventory, class Inputs>
        void preparedSceneInputs(const Root& root,
            const Inventory& inventory,
            const Inputs& inputs,
            const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
            const char* refusal,
            const rhi::OriginalBindlessRootStateBudget& budget,
            rhi::GpuDevice& device) const
        {
            using namespace rhi;
            assert(device.backend() == Backend::Null && !initializeNative);
            assert(!OriginalCommandInputs::validateOriginal(inputs)
                && inputs->originalBudget().contractVersion == 7u);
            std::fprintf(stderr,
                "Scene Null boundary %s: %s\n",
                vertex->entry.c_str(),
                refusal ? refusal : "available");
            assert(refusal
                && core::StringView(refusal)
                    == (artifact->execution == BindlessExecution::Native
                            ? "RHI-NULL-MEMBER-MEMORY-NATIVE-TARGET-UNAVAILABLE"
                            : "RHI-ORIGINAL-PUSH-RECORDING-NULL-VALIDATOR"));
            // Null refuses the independently requested recording intent before
            // allocating the graph. Reflection projection alone grants no graph.
            assert(!graph);
            if (graph)
                assert(!OriginalCommandResourceStates::validateOriginal(graph)
                    && core::SharedOwnerProjection::sameOwner(graph->originalInputs(), inputs));
            assert(core::SharedOwnerProjection::sameOwner(
                inventory, inputs->originalScalars()->originalUsage()->originalSources()));
            const auto& arguments = *inputs->originalScalars()->originalArguments();
            const auto& commands = inputs->originalScalars()->originalUsage()->commands();
            const auto& usage = *inputs->originalScalars()->originalUsage();
            bool pushed = false, drawn = false;
            std::size_t copies = 0u, numericCopies = 0u;
            for (std::size_t ordinal = 0u; ordinal < commands.size(); ++ordinal) {
                const auto& op = commands[ordinal];
                const auto& record = arguments.records()[ordinal];
                if (op.kind == CommandUsageKind::PushConstants) {
                    const auto* fields = record.scalar.template get<CommandScalarArguments::Push>();
                    assert(fields && fields->stages == ShaderStage::Vertex && !fields->offset
                        && fields->size == sizeof(draw) && record.byteCount == sizeof(draw));
                    assert(!std::memcmp(arguments.bytes(ordinal), &draw, sizeof(draw)));
                    pushed = true;
                } else if (op.kind == CommandUsageKind::DrawIndexed) {
                    const auto* fields =
                        record.scalar.template get<CommandScalarArguments::DrawIndexed>();
                    assert(pushed && fields && fields->indices == 3u && fields->instances == 1u
                        && !fields->firstIndex && !fields->vertexOffset && !fields->firstInstance);
                    drawn = true;
                } else if (op.kind == CommandUsageKind::BufferCopy) {
                    ++copies;
                    const auto destination = usage.references()[op.firstReference + 1u].sourceIndex;
                    if (destination != inventory->originalLeaseIndex(geometry->resultReadback))
                        continue;
                    assert(drawn && op.referenceCount == 2u);
                    const auto* descriptor = inputs->descriptorAt(ordinal);
                    const auto* copy =
                        descriptor ? std::get_if<TransferCopyArguments>(descriptor) : nullptr;
                    BufferCopyDesc fields;
                    assert(copy && copy->write(fields) && !fields.sourceOffsetBytes
                        && !fields.destinationOffsetBytes && fields.sizeBytes == resultBytes
                        && fields.destinationBuffer == geometry->resultReadback->resource());
                    const auto sourceIndex = usage.references()[op.firstReference].sourceIndex;
                    const auto& source =
                        std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                            inventory->sources()[sourceIndex]);
                    assert(fields.sourceBuffer == source->resource()
                        && source->layout().sizeBytes == resultBytes);
                    ++numericCopies;
                }
            }
            assert(numericCopies == (pass == 0u ? 1u : 0u) && copies == 1u + numericCopies);
            if (artifact->execution == BindlessExecution::Emulated) {
                const auto inspect = [&](const auto& limits) {
                    return common::OriginalCommandBindlessRootAccesses::visit(
                        inputs, limits, [](const auto&) noexcept -> const char* {
                            return nullptr;
                        });
                };
                const auto projected = inspect(budget);
                assert(!projected.diagnostic && projected.faultCopies == 1u
                    && projected.directResultCopies == numericCopies);
                if (numericCopies) {
                    auto denied = budget;
                    denied.directResultCopies = {};
                    assert(inspect(denied).diagnostic);
                    denied = budget;
                    denied.directResultCopies.maxCopies = 0u;
                    assert(inspect(denied).diagnostic);
                    denied = budget;
                    denied.maxSourceVisits = projected.sourceVisits - 1u;
                    assert(inspect(denied).diagnostic);
                }
            }
            assert(pushed && drawn && geometry->resultReadback
                && geometry->leases[0u]->layout().sizeBytes == vertexBytes().size()
                && geometry->leases[1u]->layout().sizeBytes == indexBytes().size());
            for (const auto& actor : geometry->actors)
                assert(!actor);
            assert(state->observedGeneration == root->originalSnapshot()->generation());
            ++*preparedCount;
        }

        template <class Root, class Inventory, class Binding, class Transaction>
        void operator()(const Root& root,
            const Inventory& inventory,
            const Binding& binding,
            const Transaction& transaction,
            rhi::GpuDevice& device,
            const rhi::OriginalBindlessRootReadbackSequence::Input& results) const
        {
            using namespace rhi;
            using Readback = OriginalCommandBufferReadbackBinding;
            assert(device.backend() == Backend::Null && !initializeNative && !transaction
                && artifact->execution == BindlessExecution::Emulated
                && !inventory->originalStateTransactionCount());
            assert(!Readback::validateOriginal(binding) && !binding->nativeExecutionProven()
                && binding->supportsOriginalPendingCustody()
                && binding->isOriginalSnapshotFaultReadback());
            const auto& program = binding->originalProgram();
            assert(!program->nativeExecutionProven()
                && !program->originalBudget().bindlessRoots.pushConsumption.recordingOptIn
                && !program->originalBudget().bindlessRoots.pushConsumption.recordingContractVersion
                && program->originalBudget().bindlessRoots.contractVersion == 5u
                && program->originalInputs()->originalBudget().contractVersion == 7u);
            assert(!OriginalBindlessRootReadbackSequence::inspectInput(binding, results)
                && results.count == (pass == 0u ? 2u : 1u));
            for (std::size_t i = 0u; i < results.count; ++i) {
                const auto& result = results.bindings[i];
                assert(!Readback::validateOriginal(result)
                    && !result->originalResult()->publishedBytes()
                    && result->matchesOriginalSequence(program->originalBudget().stateSequence)
                    && core::SharedOwnerProjection::sameOwner(program, result->originalProgram())
                    && result->originalRequest()->identity().resourceGeneration
                        == root->originalSnapshot()->generation());
                const auto baseline = result->originalMetadataQuota()->liveBytes();
                const char* id = nullptr;
                assert(!Readback::prepareOriginal(result->originalGraph(),
                           program,
                           result->originalRequest(),
                           result->originalResult(),
                           result->commandIndex(),
                           result->rootSourceIndex(),
                           device,
                           {5u,
                               true,
                               Readback::retainedOwnerCount,
                               4096u,
                               result->originalMetadataQuota()->limitBytes()},
                           result->originalMetadataQuota(),
                           id)
                    && id && core::StringView(id) == "RHI-ORIGINAL-BUFFER-READBACK-BINDING-PROFILE"
                    && result->originalMetadataQuota()->liveBytes() == baseline);
            }
            if (results.count == 2u) {
                const auto& numeric = results.bindings[1u];
                assert(!numeric->isOriginalSnapshotFaultReadback()
                    && numeric->originalRequest()->byteCapacity() == resultBytes
                    && core::SharedOwnerProjection::sameOwner(
                        numeric->originalReadbackSource(), geometry->resultReadback));
                auto incomplete = results;
                incomplete.count = 1u;
                assert(OriginalBindlessRootReadbackSequence::inspectInput(binding, incomplete));
                const std::array duplicate{binding, binding};
                incomplete = results;
                incomplete.bindings = duplicate.data();
                assert(OriginalBindlessRootReadbackSequence::inspectInput(binding, incomplete));
            }
            ++*readbackCount;
        }
    };

public:
    static void verify(const std::shared_ptr<const rhi::BindlessShaderPackage>& package)
    {
        using Contract = arp::test::BindlessSceneConformanceContract;
        using Input = arp::test::BindlessSceneConformanceInputs;
        std::size_t checked = 0u;
        for (const auto& vertex : package->modules()) {
            if (vertex.push.stages != rhi::ShaderStage::Vertex)
                continue;
            const rhi::BindlessPackageModule* fragment = nullptr;
            Contract::EntrySelection selection;
            for (const auto& candidate : package->modules()) {
                const auto value = Contract::selectEntries(vertex.entry, candidate.entry);
                if (!value.recognized || value.diagnostic)
                    continue;
                assert(!fragment);
                fragment = &candidate;
                selection = value;
            }
            assert(fragment);
            Data data;
            data.vertex = &vertex;
            data.family = selection.family;
            data.pass = selection.pass;
            data.artifact = &package->artifact();
            data.draw.material = data.terrain() ? 0u : 1u;
            bool observed = false;
            const auto observer = [&](const auto& root,
                                      const auto& inventory,
                                      rhi::GpuDevice* device) {
                if (!device) {
                    assert(!root && !inventory);
                    data.geometry->leases = {};
                    data.geometry->actors = {};
                    data.geometry->resultReadback.reset();
                    return;
                }
                assert(!observed);
                observed = true;
                assert(data.state->observedGeneration == root->originalSnapshot()->generation());
                assert(data.state->tableFactoryCalls == (data.terrain() ? 4u : 2u));
                assert(data.state->ordinaryFactoryCalls && data.state->shaderActors.empty());
                assert(root->originalShaderEntryAccessVersion() == 4u);
                for (const auto stage : {rhi::ShaderStage::Vertex,
                         rhi::ShaderStage::Fragment,
                         rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment}) {
                    for (std::size_t i = 0u; i < root->size(); ++i) {
                        rhi::BindlessResourceAccess access;
                        std::size_t declaration = SIZE_MAX;
                        bool active = false;
                        const auto* id = root->readOriginalShaderDeclaration(
                            i, stage, access, declaration, active, 4u);
                        if (id)
                            std::fprintf(stderr,
                                "Scene EntryAccess4 %s index%zu: %s\n",
                                vertex.entry.c_str(),
                                i,
                                id);
                        assert(!id);
                        if (active)
                            assert(declaration < inventory->sources().size()
                                && access.stages != rhi::ShaderStage::None
                                && (access.stages & stage) == access.stages);
                        else
                            assert(
                                declaration == SIZE_MAX && access.stages == rhi::ShaderStage::None);
                    }
                }
                {
                    rhi::BindlessResourceAccess access;
                    std::size_t declaration = SIZE_MAX;
                    bool active = true;
                    assert(root->readOriginalShaderDeclaration(
                               0u, rhi::ShaderStage::Compute, access, declaration, active, 4u)
                        && !active && declaration == SIZE_MAX);
                    assert(root->readOriginalShaderDeclaration(
                               0u, rhi::ShaderStage::Vertex, access, declaration, active, 5u)
                        && !active && declaration == SIZE_MAX);
                }
                rhi::OriginalBindlessInitializedGraphicsInputs generic{2u, true, 4096u, 4096u};
                using Generic = rhi::OriginalBindlessInitializedGraphicsContract;
                assert(!Generic::validateRootInput(root, generic));
                assert(Generic::validateRootInput(root, {}));
                generic.contractVersion = 1u;
                assert(Generic::validateRootInput(root, generic));
                Input input{1u, true, selection.family, 4096u, 4096u, 4096u};
                assert(!Contract::validateRootInput(root, input));
                const auto* absent = Contract::validateRootInput(root, {});
                assert(absent
                    && core::StringView(absent)
                        == "RHI-BINDLESS-INITIALIZED-GRAPHICS-INPUTS-REQUIRED");
                input.family = static_cast<Input::Family>(
                    (static_cast<std::uint32_t>(selection.family) + 1u) % 5u);
                assert(Contract::validateRootInput(root, input));
                input.family = selection.family;
                input.maxDescriptorVisits = 0u;
                assert(Contract::validateRootInput(root, input));
                input.maxDescriptorVisits = 4096u;
                OriginalBindlessGraphicsStreamFixture::execute(root, inventory, *device, data);
                assert(*data.preparedCount == 1u);
                assert(*data.readbackCount
                    == (package->artifact().execution == rhi::BindlessExecution::Emulated ? 1u
                                                                                          : 0u));
                auto forged = std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>(
                    inventory, root.get());
                assert(Contract::validateRootInput(forged, input));
            };
            validateOriginalBindlessRootSource(package,
                {},
                "unused",
                false,
                observer,
                data,
                false,
                fragment->entry,
                vertex.entry,
                selection.pass != 0u,
                OrdinaryRootMutation::None,
                rhi::TextureFormat::Rgba16Float);
            assert(observed);
            ++checked;
        }
        assert(checked == 3u || checked == 12u);
        std::printf(
            "BINDLESS_SCENE_ROOT_INPUTS_OK entries%zu authentic-Null-owners same-generation-material-bytes original-buffer-factories reflection-packed-geometry vertex-Push16 typed-Inputs7 native-memory-push-refused direct-Root5-result-copy CPU-readback-correspondence required-inputs family-budget-refusal no-native-content-grant\n",
            checked);
    }
};
} // namespace arp::test
