#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "rhi/frontend/command_indirect_accesses.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_stream_program.h"

namespace arp::test {
class OriginalBindlessIndirectFixture final {
public:
    static void verify(const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice& device,
        core::pmr::Vector<rhi::CommandUsage> commands,
        core::pmr::Vector<rhi::OriginalCommandUsage::Reference> references,
        core::pmr::Vector<rhi::OriginalCommandInputs::OriginalDescriptor> descriptors,
        core::pmr::Vector<rhi::CommandVariableArguments::Input> fields,
        rhi::OriginalCommandStateAdmissionBudget state,
        rhi::OriginalCommandStreamBudget streamBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& inputQuota,
        std::size_t draw,
        std::uint32_t argumentSource)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        using Access = OriginalCommandIndirectAccesses;
        constexpr std::uint64_t bytes = 65536u;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> quotas;
        // The retained pass arguments belong to the original input domain.
        // Reuse that same quota when extending its stream; a fresh quota must
        // not relabel already frozen pass metadata.
        assert(inputQuota && inputQuota->limitBytes() == bytes);
        quotas[0u] = inputQuota;
        for (std::size_t i = 1u; i < quotas.size(); ++i)
            quotas[i] = core::makeShared<core::CpuMemoryQuota>(bytes);
        const auto require = [](const char* id) {
            if (id)
                std::fprintf(stderr, "Root Indirect6: %s\n", id);
            assert(!id);
        };
        const char* id = nullptr;
        const auto maximum = *fields[draw].scalar.get<CommandScalarArguments::DrawIndexed>();
        const auto reference = commands[draw].firstReference;
        commands[draw].kind = K::DrawIndexedIndirect;
        commands[draw].referenceCount = 1u;
        references.insert(references.begin() + reference, {argumentSource});
        for (std::size_t i = draw + 1u; i < commands.size(); ++i)
            ++commands[i].firstReference;
        fields[draw].scalar = *CommandScalarArguments::capture(
            K::DrawIndexedIndirect, CommandScalarArguments::DrawIndirect{4u, 2u, 24u});
        std::size_t resultCopy = SIZE_MAX, producerSource = SIZE_MAX, resultSource = SIZE_MAX;
        std::shared_ptr<const OriginalCommandBindlessRootSource> resultRoot;
        for (const auto& source : inventory->sources()) {
            if (const auto* lease =
                    std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source))
                if ((*lease)->originalObject()->desc().label == "root-produced-result")
                    resultSource = inventory->originalLeaseIndex(*lease);
            if (const auto* root =
                    std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        &source)) {
                resultRoot = *root;
                for (const auto& element : (*root)->elements())
                    if ((element.declaration.write.type == DescriptorType::StorageBuffer
                            || element.declaration.write.type
                                == DescriptorType::ReadOnlyStorageBuffer)
                        && element.declaration.bufferAccessBytes >= 8u) {
                        producerSource = element.declaration.sourceIndex;
                        break;
                    }
            }
        }
        if (producerSource != SIZE_MAX && resultSource != SIZE_MAX) {
            const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory->sources()[producerSource]);
            assert((lease->layout().usage & ResourceUsage::CopySrc) != ResourceUsage::None);
            assert(commands.back().kind == K::Finish);
            commands.pop_back();
            fields.pop_back();
            const auto append = [&](K kind, std::initializer_list<std::size_t> refs) {
                const auto ordinal = commands.size();
                commands.push_back({kind,
                    static_cast<std::uint32_t>(references.size()),
                    static_cast<std::uint32_t>(refs.size())});
                for (const auto source : refs)
                    references.push_back({static_cast<std::uint32_t>(source)});
                fields.push_back(
                    {*CommandScalarArguments::capture(kind, CommandScalarArguments::None{})});
                return ordinal;
            };
            const auto element = std::find_if(
                resultRoot->elements().begin(), resultRoot->elements().end(), [&](const auto& e) {
                    return e.declaration.sourceIndex == producerSource;
                });
            const auto& access = element->declaration;
            const auto before =
                (access.access & ResourceAccess::ShaderWrite) != ResourceAccess::None
                ? ResourceState::ShaderWrite
                : ResourceState::ShaderRead;
            ResourceBarrierDesc barrier{lease->resource(), {}, before, ResourceState::CopySrc};
            barrier.sourceStages = access.stages;
            barrier.sourceAccess = access.access;
            barrier.destinationStages = PipelineStage::Transfer;
            barrier.destinationAccess = ResourceAccess::TransferRead;
            descriptors.push_back({append(K::BufferBarrier, {producerSource}),
                *ResourceDependencyArguments::capture(K::BufferBarrier, barrier)});
            append(K::BeginTransferPass, {});
            const auto& target = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory->sources()[resultSource]);
            const BufferCopyDesc copy{
                lease->resource(), target->resource(), access.bufferAccessOffset, 8u, 8u};
            resultCopy = append(K::BufferCopy, {producerSource, resultSource});
            descriptors.push_back({resultCopy, *TransferCopyArguments::capture(copy)});
            append(K::EndTransferPass, {});
            std::swap(barrier.before, barrier.after);
            std::swap(barrier.sourceStages, barrier.destinationStages);
            std::swap(barrier.sourceAccess, barrier.destinationAccess);
            descriptors.push_back({append(K::BufferBarrier, {producerSource}),
                *ResourceDependencyArguments::capture(K::BufferBarrier, barrier)});
            append(K::Finish, {});
            streamBudget.maxCommands = commands.size();
            streamBudget.maxDescriptors = descriptors.size();
            ++streamBudget.maxCopyElements;
        }
        assert(streamBudget.maxPipelineBinds <= UINT32_MAX);
        OriginalCommandInputsBudget inputBudget{
            8u, true, static_cast<std::uint32_t>(streamBudget.maxPipelineBinds)};
        inputBudget.maxVertexBindings = inputBudget.maxIndexBindings = 1u;
        const auto assemble = [&](const auto& limits, const auto& data) {
            return OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                data.data(),
                descriptors.data(),
                descriptors.size(),
                limits,
                quotas[0],
                id);
        };
        auto inputs = assemble(inputBudget, fields);
        require(id);
        assert(inputs);
        state.bindlessRoots.contractVersion = 6u;
        state.bindlessRoots.maxIndirectCalls = 1u;
        state.bindlessRoots.maxIndirectRecords = 2u;
        state.geometryUsage.maxCommands = commands.size();
        // Each consumption searches the immutable sparse descriptor sequence,
        // including descriptors appended after the draw. Re-measure this stream
        // rather than reusing the original, shorter stream's exact scan count.
        assert(!state.geometryUsage.maxDrawAccesses
            || descriptors.size() <= UINT64_MAX / state.geometryUsage.maxDrawAccesses);
        state.geometryUsage.maxBindingScans =
            std::uint64_t(descriptors.size()) * state.geometryUsage.maxDrawAccesses;
        const auto geometry = inputs->visitGeometryAccesses(
            state.geometryUsage, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        require(geometry.diagnostic);
        state.geometryUsage.maxBindingScans = geometry.bindingScans;
        streamBudget.geometryUsage = state.geometryUsage;
        // Root6 authenticates both copy operands against every original
        // snapshot before classifying a copy as ordinary or fault. Charge one
        // complete inventory scan per actual copy, in addition to Root5's
        // already measured declaration/fault visits.
        const auto copies = std::count_if(commands.begin(), commands.end(), [](const auto& op) {
            return op.kind == K::BufferCopy;
        });
        state.bindlessRoots.maxSourceVisits += std::uint64_t(inventory->sources().size()) * copies;
        state.bindlessRoots.maxDependencyVisits = std::uint64_t(inventory->sources().size())
            * commands.size() * (streamBudget.maxImplicitAccesses + 5u);
        std::size_t emitted = 0u;
        const auto observe = [&](const Access::Point& point) noexcept -> const char* {
            assert(point.commandIndex == draw && point.sourceIndex == argumentSource
                && point.offsetBytes == 4u && point.sizeBytes == 44u && point.records == 2u
                && point.strideBytes == 24u && point.registrationGeneration
                && point.scope.state == ResourceState::ShaderRead
                && point.scope.stages == PipelineStage::DrawIndirect
                && point.scope.access == ResourceAccess::IndirectCommandRead);
            ++emitted;
            return nullptr;
        };
        const auto accesses = Access::visit(inputs, state.bindlessRoots, observe);
        require(accesses.diagnostic);
        assert(emitted == 1u && accesses.calls == 1u && accesses.records == 2u
            && !accesses.nativeExecutionProven());
        auto less = state.bindlessRoots;
        --less.maxIndirectRecords;
        emitted = 0u;
        assert(Access::visit(inputs, less, observe).diagnostic && !emitted);
        auto bad = fields;
        bad[draw].scalar = *CommandScalarArguments::capture(
            K::DrawIndexedIndirect, CommandScalarArguments::DrawIndirect{4u, 3u, 24u});
        auto outOfRange = assemble(inputBudget, bad);
        require(id);
        assert(outOfRange && Access::visit(outOfRange, state.bindlessRoots, observe).diagnostic
            && !emitted);
        auto legacy = state.bindlessRoots;
        legacy.contractVersion = 5u;
        assert(legacy.diagnostic());
        legacy.maxIndirectCalls = legacy.maxIndirectRecords = legacy.maxDependencyVisits = 0u;
        assert(common::OriginalCommandBindlessRootAccesses::visit(
            inputs, legacy, [](const auto&) noexcept -> const char* {
                return nullptr;
            }).diagnostic);
        auto graph =
            OriginalCommandResourceStates::prepareOriginal(inputs, device, state, quotas[1], id);
        require(id);
        assert(graph);
        auto sequence = streamBudget.stateSequence;
        sequence.bindlessRootStateVersion = 6u;
        ++sequence.maxSteps; // Original indirect buffer operand, not a second owner graph.
        if (resultCopy != SIZE_MAX)
            sequence.maxSteps += 4u; // Two transitions and the two actual copy operands.
        sequence.maxCommands = commands.size();
        const auto ordered = common::OriginalCommandAccessValidation::inspect(graph, sequence);
        require(ordered.diagnostic);
        assert(ordered.steps == sequence.maxSteps && ordered.dependencyVisits);
        {
            auto cut = state;
            cut.bindlessRoots.maxDependencyVisits = ordered.dependencyVisits - 1u;
            auto scratch = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto limited =
                OriginalCommandResourceStates::prepareOriginal(inputs, device, cut, scratch, id);
            require(id);
            assert(limited);
            const auto failure =
                common::OriginalCommandAccessValidation::inspect(limited, sequence);
            assert(failure.diagnostic
                && core::StringView(failure.diagnostic)
                    == "RHI-ORIGINAL-BINDLESS-DEPENDENCY-VISIT-CAPACITY");
        }
        // Keep a legal state transition but remove the shader destination
        // dependency after the real fault clear. State equality alone must
        // not authorize the subsequent original indirect draw's Root access.
        outOfRange.reset();
        bool checkedDestination = false;
        for (auto& descriptor : descriptors) {
            if (commands[descriptor.commandIndex].kind != K::BufferBarrier)
                continue;
            const auto saved = descriptor.arguments;
            auto* dependency = std::get_if<ResourceDependencyArguments>(&descriptor.arguments);
            ResourceBarrierDesc transition;
            if (!dependency || !dependency->write(K::BufferBarrier, transition)
                || transition.before != ResourceState::CopyDst
                || transition.after != ResourceState::ShaderWrite)
                continue;
            transition.destinationStages = PipelineStage::VertexShader;
            transition.destinationAccess = ResourceAccess::ShaderRead;
            descriptor.arguments =
                *ResourceDependencyArguments::capture(K::BufferBarrier, transition);
            auto incorrect = assemble(inputBudget, fields);
            descriptor.arguments = saved;
            require(id);
            assert(incorrect);
            auto scratch = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto invalid = OriginalCommandResourceStates::prepareOriginal(
                incorrect, device, state, scratch, id);
            require(id);
            assert(invalid);
            const auto failure =
                common::OriginalCommandAccessValidation::inspect(invalid, sequence);
            assert(failure.diagnostic
                && core::StringView(failure.diagnostic)
                    == "RHI-RECORDED-RESOURCE-DEPENDENCY-DESTINATION");
            checkedDestination = true;
            break;
        }
        assert(checkedDestination);
        streamBudget.bindlessRoots = state.bindlessRoots;
        streamBudget.stateSequence = sequence;
        streamBudget.maxReferences = references.size();
        auto stream =
            OriginalCommandStreamProgram::prepareOriginalTyped(graph, streamBudget, quotas[2], id);
        require(id);
        assert(stream && stream->counts().geometryDraws == 1u
            && stream->counts().geometryDrawAccesses == 2u && stream->counts().consumptions == 1u);
        {
            OriginalBindlessGraphicsPassBudget pass{2u, true, 9u, 11u, 64u, bytes};
            pass.maxCommandVisits = stream->operations().size();
            std::shared_ptr<const OriginalCommandBindlessRootSource> graphics;
            for (const auto& op : stream->operations()) {
                if (op.kind == K::BeginRenderPass)
                    pass.commandIndex = op.commandIndex;
                if (op.kind == K::BindPreparedGraphicsBinding)
                    graphics = std::get<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        inventory->sources()[references[op.firstReference].sourceIndex]);
            }
            assert(graphics);
            std::size_t selected = SIZE_MAX;
            require(OriginalBindlessGraphicsPassContract::selectPass(
                *stream, graphics, pass, selected));
            assert(selected == pass.commandIndex);
            auto cut = pass;
            --cut.maxCommandVisits;
            assert(
                OriginalBindlessGraphicsPassContract::selectPass(*stream, graphics, cut, selected));
            cut = pass;
            cut.commandIndex = draw;
            assert(
                OriginalBindlessGraphicsPassContract::selectPass(*stream, graphics, cut, selected));
            cut = pass;
            cut.contractVersion = 1u;
            assert(
                OriginalBindlessGraphicsPassContract::selectPass(*stream, graphics, cut, selected));
        }
        if (resultCopy != SIZE_MAX) {
            const auto& target = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory->sources()[resultSource]);
            auto requestQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto resultQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            auto request = BufferReadbackRequestDeclaration::prepareOriginal(target,
                device,
                {19u, 1u, resultRoot->originalSnapshot()->generation()},
                8u,
                8u,
                {1u, true, 8u, bytes},
                requestQuota,
                id);
            require(id);
            auto result = OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(
                request, resultQuota, id);
            require(id);
            OriginalCommandBufferReadbackBindingBudget bindingBudget{
                5u, true, 5u, commands.size() + resultRoot->elements().size(), bytes};
            const auto rootIndex = inventory->originalLeaseIndex(resultRoot);
            const auto prepareBinding = [&](const auto& limits) {
                return OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                    stream,
                    request,
                    result,
                    resultCopy,
                    rootIndex,
                    device,
                    limits,
                    requestQuota,
                    id);
            };
            auto binding = prepareBinding(bindingBudget);
            require(id);
            assert(binding && !binding->isOriginalSnapshotFaultReadback()
                && binding->originalCopySource()->resource() != target->resource()
                && core::SharedOwnerProjection::sameOwner(binding->originalReadbackSource(), target)
                && !binding->nativeExecutionProven() && !result->publishedBytes());
            core::pmr::String diagnostic;
            assert(!OriginalCommandBufferReadbackBinding::acquireOriginalFaultCustody(
                       binding, diagnostic)
                       .valid()
                && diagnostic == "RHI-ORIGINAL-BUFFER-READBACK-FAULT-BINDING-REQUIRED");
            auto old = bindingBudget;
            old.contractVersion = 4u;
            assert(!prepareBinding(old));
            old.contractVersion = 6u;
            assert(!prepareBinding(old) && id
                && core::StringView(id) == "RHI-ORIGINAL-BUFFER-READBACK-BINDING-PROFILE");
            old = bindingBudget;
            --old.maxOperationVisits;
            assert(!prepareBinding(old));
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{requestQuota, resultQuota}, [&]() noexcept {
                    return binding->validate();
                }));
            const auto& faultTarget = resultRoot->originalSnapshot()->originalBufferSource(
                BindlessSnapshot::OriginalBufferRole::FaultReadback);
            std::size_t faultCopy = SIZE_MAX;
            for (std::size_t i = 0u; i < commands.size(); ++i)
                if (commands[i].kind == K::BufferCopy && i != resultCopy)
                    faultCopy = i;
            assert(faultCopy != SIZE_MAX);
            auto faultRequest = BufferReadbackRequestDeclaration::prepareOriginal(faultTarget,
                device,
                {20u, 1u, resultRoot->originalSnapshot()->generation()},
                0u,
                4u,
                {1u, true, 4u, bytes},
                requestQuota,
                id);
            require(id);
            auto faultResult = OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(
                faultRequest, resultQuota, id);
            require(id);
            auto faultBinding = OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                stream,
                faultRequest,
                faultResult,
                faultCopy,
                rootIndex,
                device,
                bindingBudget,
                requestQuota,
                id);
            require(id);
            assert(faultBinding && faultBinding->isOriginalSnapshotFaultReadback());
            std::array bindings{faultBinding, binding};
            const OriginalCommandReadbackResultContract::Budget resultBudget{
                1u, true, 2u, 2u, 2u, 12u, 12u, bytes};
            const auto joined = OriginalCommandBufferReadbackResultContract::inspect(
                bindings.data(), bindings.size(), commands.size() + 32u, resultBudget);
            require(joined.diagnostic);
            require(OriginalCommandNativeLifecycleContract::validateRootReadbackMapCount(
                *stream->originalInputs()->originalScalars()->originalUsage(), 2u));
            assert(OriginalCommandNativeLifecycleContract::validateRootReadbackMapCount(
                *stream->originalInputs()->originalScalars()->originalUsage(), 1u));
            assert(OriginalCommandNativeLifecycleContract::validateRootReadbackMapCount(
                *stream->originalInputs()->originalScalars()->originalUsage(), 3u));
            assert(joined.required.readbackCommands == 2u
                && joined.required.uniqueResultBytes == 12u && !result->publishedBytes()
                && !faultResult->publishedBytes());
            {
                core::CpuMemoryQuotaScope scope(requestQuota);
                using Sequence = OriginalBindlessRootReadbackSequence;
                const Sequence::Input selected{
                    bindings.data(), bindings.size(), commands.size() + 32u, resultBudget};
                require(Sequence::inspectInput(faultBinding, selected));
                // Finite mapping preserves the original destination offset;
                // it never relabels a subrange as a whole-buffer request.
                require(Sequence::inspectMapDestinations(faultBinding, selected));
                assert(Sequence::inspectWholeDestinations(faultBinding, selected));
                assert(Sequence::inspectInput(binding, selected));
                auto malformed = selected;
                malformed.bindings = nullptr;
                assert(Sequence::inspectInput(faultBinding, malformed));
                auto forgedFault = std::shared_ptr<const OriginalCommandBufferReadbackBinding>(
                    requestQuota, faultBinding.get());
                assert(Sequence::inspectInput(forgedFault, selected));
                assert(Sequence::resultCount(selected) == 2u
                    && Sequence::additionalOwnerBound(selected)
                        == OriginalCommandBufferReadbackBinding::retainedOwnerCount + 2u
                    && Sequence::containsCopy(faultBinding, selected, resultCopy)
                    && Sequence::containsCopy(faultBinding, selected, faultCopy)
                    && !Sequence::containsCopy(faultBinding, selected, SIZE_MAX));
                OriginalBindlessRootReadbackSequence retained(faultBinding, selected);
                require(retained.diagnostic());
                assert(retained.bindings().size() == 2u
                    && core::SharedOwnerProjection::sameOwner(
                        retained.faultBinding(), faultBinding));
                // Caller metadata retires cold; custody keeps its own exact set.
                bindings[1u].reset();
                require(Sequence::inspectInput(faultBinding, retained.input()));
                assert(retained.bindings()[1u].get() == binding.get());
                bindings[1u] = binding;
                const auto noAllocation = core::defaultCpuAllocator().stats().allocationCount;
                require(Sequence::inspectInput(faultBinding, retained.input()));
                assert(core::defaultCpuAllocator().stats().allocationCount == noAllocation);
            }
            auto tight = resultBudget;
            --tight.maxUniqueResultBytes;
            const auto exhausted = OriginalCommandBufferReadbackResultContract::inspect(
                bindings.data(), bindings.size(), commands.size() + 32u, tight);
            assert(exhausted.diagnostic && !exhausted.required.resultDeclarations);
            bindings[1u] = faultBinding;
            const auto duplicate = OriginalCommandBufferReadbackResultContract::inspect(
                bindings.data(), bindings.size(), commands.size() + 32u, resultBudget);
            assert(duplicate.diagnostic && !duplicate.required.resultDeclarations);
        }
        // CPU-only maximum-fetch adapters exercise the actual indirect draw's
        // bound index/vertex/pipeline epoch. They confer no native authority.
        using Fetch = OriginalIndexedDrawFetchContract;
        Fetch::Budget fetch{3u, true, commands.size(), 2u, 2u, 256u, 2u * maximum.indices};
        std::array<std::uint8_t, 64u> initializedIndices{};
        bool refuseMaximum = false, refuseVertex = false;
        std::size_t produced = 0u;
        const auto bounds = [&](const Fetch::Point& point, auto& out) noexcept -> const char* {
            assert(point.commandIndex == draw && point.argumentSource == argumentSource
                && point.argumentRecord < 2u
                && point.argumentOffset == 4u + 24u * point.argumentRecord
                && point.pipelineBind < draw && point.pipelineSource < inventory->sources().size());
            out = maximum;
            return refuseMaximum && point.argumentRecord == 1u ? "fixture-produced-bounds-rejected"
                                                               : nullptr;
        };
        const auto analyzer =
            [&](const Fetch::Point& point, const auto& lease, auto& out) noexcept -> const char* {
            assert(inventory->originalLeaseIndex(lease) == point.indexSource);
            return IndexInputConsumptionContract::analyzeLittleEndianBytes(
                initializedIndices.data(),
                initializedIndices.size(),
                point.index.format,
                point.index.offset,
                point.draw.firstIndex,
                point.draw.indices,
                point.draw.vertexOffset,
                fetch.maxAnalyzedIndices,
                out);
        };
        const auto vertex = [&](const Fetch::Point& point,
                                const auto& binding,
                                const auto&,
                                const auto&,
                                const auto& lease) noexcept -> const char* {
            assert(binding.pipelineSourceIndex == point.pipelineSource
                && inventory->originalLeaseIndex(lease) == binding.sourceIndex
                && binding.pipelineBindCommandIndex == point.pipelineBind);
            return refuseVertex && point.argumentRecord == 1u ? "fixture-produced-vertex-rejected"
                                                              : nullptr;
        };
        const auto observeFetch = [&](const Fetch::Point& point) noexcept -> const char* {
            assert(point.vertexStreams == 1u && point.fetch.indexCount == maximum.indices
                && point.fetch.vertexCount == 1u && !point.nativeExecutionProven());
            ++produced;
            return nullptr;
        };
        const auto inspectFetch = [&](const auto& limits) {
            return stream->visitInitializedProducedDrawFetch(
                limits, bounds, analyzer, vertex, observeFetch);
        };
        const auto fetched = inspectFetch(fetch);
        require(fetched.diagnostic);
        assert(produced == 2u && fetched.draws == 2u && fetched.streams == 2u
            && fetched.analyzedIndices == 2u * maximum.indices);
        fetch.maxBindingScans = fetched.bindingScans;
        const auto rejectFetch = [&](const auto& limits, const char* expected) {
            produced = 0u;
            const auto failure = inspectFetch(limits);
            assert(failure.diagnostic && core::StringView(failure.diagnostic) == expected
                && !produced);
        };
        auto cutFetch = fetch;
        --cutFetch.maxDraws;
        rejectFetch(cutFetch, "RHI-ORIGINAL-INDEXED-FETCH-DRAW-CAPACITY");
        cutFetch = fetch;
        --cutFetch.maxAnalyzedIndices;
        rejectFetch(cutFetch, "RHI-ORIGINAL-INDEXED-FETCH-ANALYSIS-CAPACITY");
        cutFetch = fetch;
        --cutFetch.maxBindingScans;
        rejectFetch(cutFetch, "RHI-ORIGINAL-INDEXED-FETCH-SCAN-CAPACITY");
        refuseMaximum = true;
        rejectFetch(fetch, "fixture-produced-bounds-rejected");
        refuseMaximum = false;
        refuseVertex = true;
        rejectFetch(fetch, "fixture-produced-vertex-rejected");
        refuseVertex = false;
        produced = 0u;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas,
                   [&]() noexcept -> const char* {
                       return inspectFetch(fetch).diagnostic;
                   })
            && produced == 2u);
        assert(Fetch::visitInitialized(inputs, fetch, analyzer, vertex, observeFetch).diagnostic);
        auto transaction = OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, state, quotas[3], id);
        require(id);
        assert(transaction && transaction->journalEmpty() && !transaction->nativeExecutionProven());
        assert(transaction->hasOriginalBindlessProducedGeometryStateProfile()
            && !transaction->hasOriginalBindlessProducedGeometryProfile());
        transaction.reset();
        bool controlledAttachment = false;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source))
                controlledAttachment = controlledAttachment
                    || (((*view)->originalObject()->desc().usage == TextureUsage::RenderAttachment)
                        && (*view)
                            ->originalParentLease()
                            ->originalObject()
                            ->originalContentExclusive());
        if (controlledAttachment) {
            OriginalFiniteGraphicsContentPlanBudget content{
                9u, true, 0u, 1u, 1u, 0u, 2u, 4u * commands.size(), 8192u};
            content.bindlessGeometryContractVersion = 2u;
            content.bindlessGeometryOptIn = true;
            auto legacyContent = content;
            legacyContent.bindlessGeometryContractVersion = 1u;
            assert(!OriginalCommandStateTransaction::prepareOriginalBindlessGraphicsOwnership(
                graph, stream, device, state, sequence, legacyContent, quotas[3], id));
            transaction = OriginalCommandStateTransaction::prepareOriginalBindlessGraphicsOwnership(
                graph, stream, device, state, sequence, content, quotas[3], id);
            require(id);
            assert(transaction && transaction->hasOriginalBindlessProducedGeometryProfile()
                && transaction->originalContentPlanContractVersion() == 9u
                && transaction->plannedContentControlCount() == 1u
                && transaction->plannedContentCellCount() == 1u
                && transaction->plannedAccessStepCount() == sequence.maxSteps
                && transaction->journalEmpty() && !transaction->nativeExecutionProven());
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return transaction->compareOriginalBindlessGraphicsStream(stream);
            }));
            // Native admission has not authenticated a producer. CPU content
            // correspondence must never make an existing issuer accept Root6.
            const auto lifecycle = OriginalCommandNativeLifecycleBudget{9u};
            assert(OriginalCommandNativeLifecycleContract::inspectOriginalOwnership(
                transaction, lifecycle, {}, {}));
            const auto& sourceBudget = inventory->originalBudget();
            if (sourceBudget.maxPendingSubmissions) {
                using Life = OriginalCommandNativeLifecycleContract;
                using Domain = OriginalCommandAdmissionBudget::CpuDomain;
                const bool callbacks = sourceBudget.maxCallbackTickets != 0u;
                OriginalCommandNativeLifecycleBudget produced;
                produced.contractVersion = callbacks ? 22u : 21u;
                produced.prototypeOptIn = true;
                produced.maxPendingSubmissions = sourceBudget.maxPendingSubmissions;
                produced.maxRecordingOwners = produced.maxSubmissionOwners =
                    transaction->retainedOwnerCount();
                produced.maxRecordingCpuBytes = quotas[3]->limitBytes();
                produced.maxSubmissionCpuBytes =
                    sourceBudget.maxCpuBytes[static_cast<std::size_t>(Domain::Submission)];
                produced.maxCallbackCpuBytes =
                    sourceBudget.maxCpuBytes[static_cast<std::size_t>(Domain::Callback)];
                produced.maxCallbackTickets = sourceBudget.maxCallbackTickets;
                produced.maxCallbackRegistrations = sourceBudget.maxCallbackRegistrations;
                produced.maxCallbackOwners = callbacks ? transaction->retainedOwnerCount() : 0u;
                auto submission =
                    core::makeShared<core::CpuMemoryQuota>(produced.maxSubmissionCpuBytes);
                auto callback = callbacks
                    ? core::makeShared<core::CpuMemoryQuota>(produced.maxCallbackCpuBytes)
                    : std::shared_ptr<core::CpuMemoryQuota>{};
                require(
                    Life::inspectOriginalPreparation(transaction, produced, submission, callback));
                assert(transaction->journalEmpty() && !transaction->nativeExecutionProven());
                auto oldProfile = produced;
                oldProfile.contractVersion = callbacks ? 12u : 9u;
                assert(Life::inspectOriginalPreparation(
                    transaction, oldProfile, submission, callback));
                // Distinct domains and sufficient owners are still required;
                // a valid Geometry2 content plan does not bypass admission.
                assert(
                    Life::inspectOriginalPreparation(transaction, produced, quotas[3], callback));
                auto insufficient = produced;
                --insufficient.maxRecordingOwners;
                assert(Life::inspectOriginalPreparation(
                    transaction, insufficient, submission, callback));
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return Life::inspectOriginalOwnership(
                        transaction, produced, submission, callback);
                }));
            }
        }
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                if (const auto* failure =
                        Access::visit(inputs, state.bindlessRoots, observe).diagnostic)
                    return failure;
                return stream->validate();
            }));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto lowering =
                semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 6u);
            assert(lowering.projectionAvailable && !lowering.executionAvailable
                && !lowering.diagnostic && stream->executionDiagnostic(backend));
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ROOT_INDIRECT6 original-inputs8 ranges-budget-geometry-state-stream-journal hot0 values-and-native-execution-unproven");
    }
};
} // namespace arp::test
