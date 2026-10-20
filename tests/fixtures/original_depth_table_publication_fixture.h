#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_command_typed_stream_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_depth_table_content_plan_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_usage_projection.h"
#include "rhi/frontend/descriptor_table_publication.h"
#include "webgpu_async_wait_fixture.h"

namespace arp::test {
class OriginalDepthTablePublicationFixture final {
    using Contract = rhi::OriginalDepthTablePublicationContract;
    using Request = rhi::OriginalDepthTablePublicationRequest;
    using StreamRetained = OriginalCommandTypedStreamFixture::Retained;
    static constexpr std::uint64_t bytes = 64u * 1024u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "depth-table expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

    class CandidateProbe final {
    public:
        CandidateProbe(rhi::DescriptorTable& table, const char* diagnostic)
            : table_(diagnostic ? nullptr : &table)
        {}

        CandidateProbe(const CandidateProbe&) = delete;
        CandidateProbe& operator=(const CandidateProbe&) = delete;

        explicit operator bool() const noexcept { return table_ != nullptr; }

        ~CandidateProbe()
        {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            if (table_) {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return table_->originalDepthTablePublicationScopesDrained();
                }));
                assert(!table_->cancelOriginalDepthTablePublication());
            }
#endif
        }

    private:
        rhi::DescriptorTable* table_;
    };

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static std::shared_ptr<const rhi::OriginalCommandResourceStates> verifyStates(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t bindings)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Point = rhi::common::OriginalCommandTextureRanges::Point;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const auto ranges = 1u + 3u * bindings + 3u;
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            UINT32_MAX,
            UINT32_MAX,
            owners,
            registrations));
        rhi::OriginalCommandStateAdmissionBudget budget{3u,
            true,
            1u,
            registrations,
            ranges,
            ranges,
            owners,
            bytes,
            {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u},
            {2u, true, 0u, 0u, false, false, 8u, bindings, 3u * bindings, 1u, true, 3u * bindings}};
        const char* id = nullptr;
        const auto factory = [&] {
            return Graph::prepareOriginal(inputs, device, budget, quota, id);
        };
        auto graph = factory();
        if (!graph)
            std::fprintf(stderr, "depth table State3: %s\n", id ? id : "missing");
        assert(graph && !id && !graph->validate() && graph->stateContractVersion() == 3u
            && graph->bufferEntries().empty() && graph->textureEntries().size() == ranges
            && graph->retainedOwnerCount() == owners && !graph->nativeExecutionProven());
        const auto verify = [&]() noexcept -> const char* {
            if (const auto* diagnostic = graph->compareOriginalInputs(inputs, device))
                return diagnostic;
            const auto& explicitPoint = graph->textureEntries()[0u].point;
            assert(explicitPoint.origin == Point::Origin::ExplicitOperand
                && explicitPoint.commandIndex == 0u && explicitPoint.sourceIndex == 7u
                && explicitPoint.referenceIndex == 0u && explicitPoint.viewSourceIndex == SIZE_MAX);
            for (std::size_t i = 1u; i < ranges; ++i) {
                const auto& entry = graph->textureEntries()[i];
                const auto& point = entry.point;
                const auto element = (i - 1u) % 3u;
                const bool render = i <= 3u * bindings;
                assert(point.origin
                        == (render ? Point::Origin::RenderBinding
                                   : Point::Origin::ShaderConsumption)
                    && point.commandIndex == (render ? 2u + (i - 1u) / 3u : 2u + bindings)
                    && point.referenceIndex == SIZE_MAX && point.sourceIndex == 2u * element
                    && point.viewSourceIndex == 2u * element + 1u
                    && point.descriptorSourceIndex == sources->sources().size() - 1u
                    && point.tableElementIndex == element
                    && point.state == rhi::ResourceState::ShaderRead
                    && point.stages == rhi::PipelineStage::FragmentShader
                    && point.access == rhi::ResourceAccess::ShaderRead);
                assert(point.range.baseMipLevel == element && point.range.baseArrayLayer == element
                    && point.range.mipLevelCount == 1u && point.range.arrayLayerCount == 1u
                    && point.range.aspect == rhi::TextureAspect::Depth
                    && entry.observation.range() == point.range
                    && entry.observation.state() == rhi::ResourceState::Undefined);
                const auto& parent =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                        sources->sources()[point.sourceIndex]);
                assert(entry.observation.resourceGeneration()
                    == parent->originalObject()->originalResourceRegistration().generation());
            }
            return nullptr;
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{
                    sources->originalMetadataQuota(), inputs->originalMetadataQuota(), quota},
                verify));
        });
        const auto baseline = quota->liveBytes();
        const auto negative = [&](auto bad, const char* expected) {
            const auto saved = budget;
            budget = bad;
            assert(!factory());
            is(id, expected);
            assert(quota->liveBytes() == baseline && !graph->validate());
            budget = saved;
        };
        auto bad = budget;
        --bad.maxStateObservations;
        negative(bad, "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY");
        bad = budget;
        --bad.maxTextureRanges;
        negative(bad, "RHI-ORIGINAL-RESOURCE-STATES-RANGE-CAPACITY");
        bad = budget;
        --bad.maxRetainedOwners;
        negative(bad, "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        bad = budget;
        --bad.maxResourceRegistrations;
        negative(bad, "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
        bad = budget;
        --bad.renderDescriptorUsage.maxTextureAccesses;
        negative(bad, "RHI-ORIGINAL-CONSUMPTION-TEXTURE-CAPACITY");
        bad = budget;
        bad.prototypeOptIn = false;
        negative(bad, "RHI-ORIGINAL-STATE-OPT-IN");
        CpuQuotaFactoryFixture probe(quota);
        const auto peak = probe.measureRetained(factory);
        probe.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && !factory());
            is(id, "RHI-ORIGINAL-RESOURCE-STATES-ALLOCATION");
        }
        assert(quota->liveBytes() == baseline);
        for (const auto version : {1u, 2u}) {
            const rhi::OriginalCommandStateSequenceBudget sequence{
                version, true, 8u, 0u, ranges, ranges + 3u, version == 2u ? 3u : 0u};
            is(rhi::common::OriginalCommandStateSequence::inspect(graph, sequence).diagnostic,
                "RHI-ORIGINAL-SEQUENCE-STATE-PROFILE");
        }
        const rhi::OriginalCommandDescriptorConsumptionContract::Budget bufferFootprint{
            1u, true, 1u, 1u, true, true, 1u, 3u};
        const rhi::OriginalCommandStateSequenceBudget oldSequence{
            2u, true, 8u, 0u, ranges, ranges + 3u, 3u};
        OriginalCommandTypedStreamFixture::verifyRejected(graph,
            OriginalCommandTypedStreamFixture::budget(
                graph, oldSequence, bufferFootprint, bindings),
            "RHI-ORIGINAL-STREAM-STATE-PROFILE");
        const rhi::OriginalTypedStateBoundUsageContract::Request oldUsage{
            1u, true, {1u, true, bufferFootprint, {}, 8u, 1u, ranges}};
        is(rhi::common::OriginalTypedUsageProjection::inspectStateBound({}, graph, oldUsage)
                .diagnostic,
            "RHI-ORIGINAL-STATE-BOUND-USAGE-STATE-VERSION");
        auto recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto transactionBudget = budget;
        ++transactionBudget.maxRetainedOwners;
        const bool occupied = sources->originalStateTransactionCount() != 0u;
        auto transaction = rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, transactionBudget, recordingQuota, id);
        assert(static_cast<bool>(transaction) != occupied);
        if (occupied)
            is(id, "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY");
        if (transaction) {
            assert(!id && transaction->journalEmpty());
            const rhi::OriginalCommandStateSequenceBudget undefinedSequence{
                3u, true, 8u, 0u, ranges, ranges + 3u, 3u * bindings + 3u};
            is(transaction->materializeOriginalPrototypePlan(undefinedSequence),
                "RHI-ORIGINAL-SEQUENCE-TEXTURE-ACCESS-STATE");
            assert(transaction->journalEmpty() && !transaction->hasOriginalPrototypePlan());
            transaction.reset();
        }
        auto oldTransaction = transactionBudget;
        oldTransaction.contractVersion = 2u;
        oldTransaction.descriptorConsumption = bufferFootprint;
        oldTransaction.renderDescriptorUsage = {};
        assert(!rhi::OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, oldTransaction, recordingQuota, id));
        is(id, "RHI-ORIGINAL-TRANSACTION-STATE-PROFILE");
        assert(!recordingQuota->liveBytes() && !graph->validate());
        std::printf(
            "ORIGINAL_TABLE_STATE_CAPTURE_OK Graphics state3 bindings%u ranges%u quota%llu exact-less-exhausted original-parent-view-generation revision undefined-not-initialized hot0 query0 undefined-sequence-and-execution-closed\n",
            bindings,
            ranges,
            static_cast<unsigned long long>(peak));
        return graph;
    }

    static StreamRetained verifySequence(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const std::shared_ptr<const rhi::OriginalCommandResourceStates>& untransitioned,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota)
    {
        using Graph = rhi::OriginalCommandResourceStates;
        using Sequence = rhi::common::OriginalCommandStateSequence;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using S = rhi::ResourceState;
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = usage.originalSources();
        const rhi::OriginalCommandStateSequenceBudget bare{3u, true, 7u, 0u, 10u, 10u, 9u};
        const auto undefined = Sequence::inspect(untransitioned, bare);
        is(undefined.diagnostic, "RHI-ORIGINAL-SEQUENCE-TEXTURE-ACCESS-STATE");
        assert(undefined.commandIndex == 0u);

        // These original barriers are CPU declarations only. They cannot stand
        // in for attachment clears, initialized depth, visibility or completion.
        enum class Probe {
            Valid,
            Missing,
            Late,
            WrongRange,
            WrongBefore,
            WrongState,
            MissingCoverage,
            UnusedCell
        };
        const auto assemble = [&](Probe probe) {
            core::pmr::Vector<rhi::CommandUsage> commands;
            core::pmr::Vector<rhi::OriginalCommandUsage::Reference> refs;
            core::pmr::Vector<rhi::CommandVariableArguments::Input> fields;
            core::pmr::Vector<rhi::OriginalCommandInputs::OriginalDescriptor> records;
            const auto appendBarrier = [&](std::uint32_t index) {
                const auto sourceIndex = index == 0u ? 6u : 2u * (index - 1u);
                const auto& parent =
                    std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                        sources->sources()[sourceIndex]);
                const auto element = index == 0u ? 0u : index - 1u;
                rhi::ResourceBarrierDesc barrier{parent->resource(),
                    {element,
                        1u,
                        element,
                        1u,
                        index == 0u ? rhi::TextureAspect::Color : rhi::TextureAspect::Depth},
                    S::Undefined,
                    index == 0u ? S::ColorAttachment : S::ShaderRead};
                barrier.sourceStages = rhi::PipelineStage::TopOfPipe;
                barrier.sourceAccess = rhi::ResourceAccess::None;
                barrier.destinationStages = index == 0u ? rhi::PipelineStage::ColorAttachmentOutput
                                                        : rhi::PipelineStage::FragmentShader;
                barrier.destinationAccess = index == 0u ? rhi::ResourceAccess::ColorAttachmentRead
                        | rhi::ResourceAccess::ColorAttachmentWrite
                                                        : rhi::ResourceAccess::ShaderRead;
                if (index == 0u && probe == Probe::MissingCoverage)
                    barrier.destinationAccess = rhi::ResourceAccess::ColorAttachmentWrite;
                if (index == 1u && probe == Probe::UnusedCell)
                    barrier.textureSubresources.arrayLayerCount = 2u;
                if (index == 3u) {
                    if (probe == Probe::WrongRange)
                        --barrier.textureSubresources.baseMipLevel;
                    if (probe == Probe::WrongBefore)
                        barrier.before = S::ShaderRead;
                    if (probe == Probe::WrongState) {
                        barrier.after = S::DepthAttachment;
                        barrier.destinationStages = rhi::PipelineStage::DepthStencil;
                        barrier.destinationAccess = rhi::ResourceAccess::DepthStencilWrite;
                    }
                }
                const auto ordinal = commands.size();
                commands.push_back(
                    {K::TextureBarrier, static_cast<std::uint32_t>(refs.size()), 1u});
                refs.push_back({sourceIndex});
                fields.push_back({*Scalar::capture(K::TextureBarrier, Scalar::None{}), {}});
                records.push_back({ordinal,
                    *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier)});
            };
            for (std::uint32_t i = 0u; i < 4u; ++i)
                if (i != 3u || (probe != Probe::Missing && probe != Probe::Late))
                    appendBarrier(i);
            const auto scopeBegin = commands.size();
            for (std::size_t i = 0u; i < usage.commands().size(); ++i) {
                const auto& original = usage.commands()[i];
                if (probe == Probe::Late && original.kind == K::Finish)
                    appendBarrier(3u);
                const auto ordinal = commands.size();
                commands.push_back({original.kind,
                    static_cast<std::uint32_t>(refs.size()),
                    original.referenceCount});
                for (std::size_t r = 0u; r < original.referenceCount; ++r)
                    refs.push_back(usage.references()[original.firstReference + r]);
                fields.push_back({original.kind == K::Draw
                        ? *Scalar::capture(original.kind, Scalar::Draw{3u, 1u, 0u, 0u})
                        : original.kind == K::BindRenderDescriptorTable
                        ? *Scalar::capture(original.kind, Scalar::DescriptorBinding{0u})
                        : *Scalar::capture(original.kind, Scalar::None{}),
                    {}});
                if (const auto* descriptor = inputs->descriptorAt(i)) {
                    auto value = *descriptor;
                    if (auto* pipeline = std::get_if<rhi::OriginalCommandPipelineBinding>(&value))
                        pipeline->scopeBeginCommandIndex = scopeBegin;
                    if (auto* table = std::get_if<rhi::OriginalCommandDescriptorBinding>(&value))
                        table->scopeBeginCommandIndex = scopeBegin;
                    records.push_back({ordinal, std::move(value)});
                }
            }
            const char* id = nullptr;
            auto result = OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                records.data(),
                records.size(),
                inputs->originalBudget(),
                inputs->originalMetadataQuota(),
                id);
            if (!result)
                std::fprintf(stderr, "depth table Sequence3 Inputs: %s\n", id ? id : "missing");
            assert(result && !id);
            return result;
        };
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 1u,
            UINT32_MAX,
            UINT32_MAX,
            owners,
            registrations));
        const rhi::OriginalCommandStateAdmissionBudget stateBudget{3u,
            true,
            1u,
            registrations,
            14u,
            14u,
            owners,
            bytes,
            {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u},
            {2u, true, 0u, 0u, false, false, 12u, 2u, 6u, 1u, true, 6u}};
        const auto prepare = [&](const auto& candidate) {
            const char* id = nullptr;
            auto graph = Graph::prepareOriginal(candidate, device, stateBudget, stateQuota, id);
            if (!graph)
                std::fprintf(stderr, "depth table Sequence3 State: %s\n", id ? id : "missing");
            assert(graph && !id);
            return graph;
        };
        const auto timeline = assemble(Probe::Valid);
        auto graph = prepare(timeline);
        const rhi::OriginalCommandStateSequenceBudget budget{3u, true, 11u, 0u, 14u, 14u, 9u};
        const auto verify = [&]() noexcept -> const char* {
            const auto result =
                rhi::common::OriginalCommandAccessValidation::inspect(graph, budget);
            if (result.diagnostic)
                return result.diagnostic;
            assert(result.commandIndex == 11u && result.stateCells == 14u && result.steps == 14u
                && !graph->nativeExecutionProven());
            for (std::size_t i = 0u; i < graph->textureEntries().size(); ++i) {
                const auto& entry = graph->textureEntries()[i];
                assert(entry.observation.state() == S::Undefined);
                if (entry.point.origin
                    == rhi::common::OriginalCommandTextureRanges::Point::Origin::ExplicitOperand)
                    continue;
                S state;
                assert(!Sequence::resolveOriginalTextureCell(
                           graph, i, 0u, entry.point.commandIndex, state)
                    && state == S::ShaderRead);
            }
            return nullptr;
        };
        if (const auto* id = verify())
            std::fprintf(stderr, "depth table Sequence3 Access: %s\n", id);
        assert(!verify());
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{
                    sources->originalMetadataQuota(), inputs->originalMetadataQuota(), stateQuota},
                verify));
        });
        auto bad = budget;
        --bad.maxCommands;
        is(Sequence::inspect(graph, bad).diagnostic, "RHI-ORIGINAL-SEQUENCE-COMMAND-CAPACITY");
        bad = budget;
        --bad.maxStateCells;
        is(Sequence::inspect(graph, bad).diagnostic, "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY");
        bad = budget;
        --bad.maxSteps;
        is(Sequence::inspect(graph, bad).diagnostic, "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
        bad = budget;
        --bad.maxImplicitAccesses;
        is(Sequence::inspect(graph, bad).diagnostic, "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY");
        for (const auto probe : {Probe::Missing,
                 Probe::Late,
                 Probe::WrongRange,
                 Probe::WrongBefore,
                 Probe::WrongState}) {
            const auto rejected = prepare(assemble(probe));
            const auto result = Sequence::inspect(rejected, budget);
            is(result.diagnostic,
                probe == Probe::WrongBefore ? "RHI-STATE-SEQUENCE-BEFORE-MISMATCH"
                                            : "RHI-ORIGINAL-SEQUENCE-SHADER-TEXTURE-STATE");
            // Failure occurs at the first bind, including the bind shadowed
            // before Draw, rather than accepting only the latest binding.
            assert(result.commandIndex
                == (probe == Probe::WrongBefore                           ? 3u
                        : probe == Probe::Missing || probe == Probe::Late ? 5u
                                                                          : 6u));
        }
        const char* id = nullptr;
        const auto factory = [&] {
            return Graph::prepareOriginal(timeline, device, stateBudget, stateQuota, id);
        };
        CpuQuotaFactoryFixture quotaProbe(stateQuota);
        const auto peak = quotaProbe.measureRetained(factory);
        quotaProbe.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(
                stateQuota, stateQuota->limitBytes() - stateQuota->liveBytes());
            assert(pressure.charged() && !factory());
            is(id, "RHI-ORIGINAL-RESOURCE-STATES-ALLOCATION");
        }
        assert(!graph->validate());
        std::printf(
            "ORIGINAL_TABLE_SEQUENCE_OK Graphics sequence3 commands11 cells14 steps14 implicit9 exact-less missing-late-range-before-state shadowed-bind hot0 query0 quota%llu declarations-not-initialization execution-closed\n",
            static_cast<unsigned long long>(peak));
        auto retained = OriginalCommandTypedStreamFixture::verifyTextures(
            graph, budget, stateBudget.descriptorConsumption, stateBudget.renderDescriptorUsage);
        verifyUsages(retained, device);
        const auto uncovered = prepare(assemble(Probe::MissingCoverage));
        auto coldBudget = retained.program->originalBudget();
        auto refused = OriginalCommandTypedStreamFixture::prepare(uncovered, coldBudget);
        refused.graph = uncovered;
        using Dependency = rhi::common::OriginalTypedDependencyProjection;
        std::size_t emitted = 0u;
        is(Dependency::visit(refused.program,
               uncovered,
               dependencyBudget(refused),
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE");
        assert(!emitted);
        const auto unusedGraph = prepare(assemble(Probe::UnusedCell));
        ++coldBudget.stateSequence.maxStateCells;
        ++coldBudget.stateSequence.maxSteps;
        auto unused = OriginalCommandTypedStreamFixture::prepare(unusedGraph, coldBudget);
        unused.graph = unusedGraph;
        is(Dependency::visit(unused.program,
               unusedGraph,
               dependencyBudget(unused),
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-RESOURCE-UNUSED");
        assert(!emitted);
        verifyTransaction(retained, device);
        return retained;
    }

    static void verifyTransaction(StreamRetained& retained, const rhi::GpuDevice& device)
    {
        using Root = rhi::OriginalCommandStateTransaction;
        using Graph = rhi::OriginalCommandResourceStates;
        using S = rhi::ResourceState;
        const auto inputs = retained.program->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const auto streamBudget = retained.program->originalBudget();
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!rhi::OriginalCommandResourceSources::registeredOwnerUnion(sources,
            inputs->retainedOwnerCount() + 2u,
            UINT32_MAX,
            UINT32_MAX,
            owners,
            registrations));
        rhi::OriginalCommandStateAdmissionBudget budget{3u,
            true,
            1u,
            registrations,
            14u,
            14u,
            owners,
            bytes,
            streamBudget.descriptorConsumption,
            streamBudget.renderDescriptorUsage};
        retained.recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        const auto make = [&] {
            return Root::prepareOriginalOwnership(
                retained.graph, device, budget, retained.recordingQuota, id);
        };
        const auto makePlan = [&] {
            auto root = make();
            if (root && (id = root->materializeOriginalPrototypePlan(streamBudget.stateSequence)))
                root.reset();
            return root;
        };
        auto root = make();
        if (!root)
            std::fprintf(stderr, "depth Transaction3 ownership: %s\n", id ? id : "missing");
        assert(root && !id && root->journalEmpty() && root->originalControlCount() == 4u
            && root->retainedOwnerCount() == owners && !root->nativeExecutionProven());
        is(root->beginOriginalPendingOwnership(), "RHI-ORIGINAL-TYPED-TRANSACTION-PLAN-REQUIRED");
        assert(!make());
        is(id, "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY");
        const auto baseline = retained.recordingQuota->liveBytes();
        root.reset();
        assert(!retained.recordingQuota->liveBytes());
        const auto rejects = [&](auto bad, const char* expected) {
            const auto saved = budget;
            budget = bad;
            assert(!make());
            is(id, expected);
            budget = saved;
            assert(!retained.recordingQuota->liveBytes()
                && !sources->originalStateTransactionCount() && !retained.graph->validate());
        };
        auto bad = budget;
        --bad.descriptorConsumption.maxTextureAccesses;
        rejects(bad, "RHI-ORIGINAL-TRANSACTION-FOOTPRINT-MISMATCH");
        bad = budget;
        --bad.renderDescriptorUsage.maxBindings;
        rejects(bad, "RHI-ORIGINAL-TRANSACTION-RENDER-FOOTPRINT-MISMATCH");
        bad = budget;
        --bad.maxStateObservations;
        rejects(bad, "RHI-ORIGINAL-TRANSACTION-OBSERVATION-CAPACITY");
        bad = budget;
        --bad.maxTextureRanges;
        rejects(bad, "RHI-ORIGINAL-TRANSACTION-RANGE-CAPACITY");
        bad = budget;
        --bad.maxResourceRegistrations;
        rejects(bad, "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
        bad = budget;
        --bad.maxRetainedOwners;
        rejects(bad, "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        bad = budget;
        bad.prototypeOptIn = false;
        rejects(bad, "RHI-ORIGINAL-STATE-OPT-IN");
        for (const auto& q : {sources->originalMetadataQuota(),
                 inputs->originalMetadataQuota(),
                 retained.graph->originalMetadataQuota()}) {
            assert(!Root::prepareOriginalOwnership(retained.graph, device, budget, q, id));
            is(id, "RHI-ORIGINAL-TRANSACTION-QUOTA-DOMAIN");
        }
        CpuQuotaFactoryFixture pressure(retained.recordingQuota);
        const auto peak = pressure.measure(makePlan);
        pressure.verify(peak, makePlan);
        {
            core::CpuMemoryQuotaLease full(retained.recordingQuota, bytes);
            assert(full.charged() && !makePlan());
            is(id, "RHI-ORIGINAL-TRANSACTION-ALLOCATION");
        }
        root = makePlan();
        if (!root)
            std::fprintf(stderr, "depth Transaction3 plan: %s\n", id ? id : "missing");
        assert(root && !id && root->plannedFinalCellCount() == 4u
            && root->plannedAccessStepCount() == 14u && root->plannedTransitionCount() == 4u
            && !root->plannedContentControlCount() && root->journalEmpty());
        assert(peak > baseline);
        assert(!rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginal(
            root, device, {1u, true, 1u, 1u, 1u, 1u, bytes}, id));
        is(id, "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-PROFILE");
        assert(!rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalTyped(
            root, retained.program, device, {2u, true, 1u, 1u, 1u, 1u, bytes, 1u, 1u}, id));
        is(id, "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-PROFILE");
        auto forged = std::shared_ptr<const Root>(retained.recordingQuota, root.get());
        is(Root::validateOriginal(forged), "RHI-ORIGINAL-TRANSACTION-OWNER");
        forged.reset();
        const auto verifyPending = [&]() noexcept -> const char* {
            if (const auto* diagnostic = root->applyOriginalPrototypePlan())
                return diagnostic;
            assert(root->status() == Root::Status::Pending && !root->journalEmpty()
                && retained.graph->validate() && retained.program->validate());
            if (const auto* diagnostic = root->validate())
                return diagnostic;
            is(root->commitOriginalPendingOwnership(),
                "RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-COMPLETION-REQUIRED");
            assert(root->status() == Root::Status::Pending && !root->journalEmpty());
            return root->abandonOriginalPendingOwnership();
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    retained.stateQuota,
                    retained.recordingQuota},
                verifyPending));
        });
        assert(root->status() == Root::Status::Abandoned && root->journalEmpty());
        root.reset();
        const auto rebuild = [&] {
            retained.program.reset();
            retained.graph.reset();
            auto stateBudget = budget;
            --stateBudget.maxRetainedOwners;
            retained.graph =
                Graph::prepareOriginal(inputs, device, stateBudget, retained.stateQuota, id);
            assert(retained.graph && !id);
            retained.program = rhi::OriginalCommandStreamProgram::prepareOriginalTyped(
                retained.graph, streamBudget, retained.quota, id);
            assert(retained.program && !id);
        };
        // Rollback restores values but intentionally advances revisions. Every
        // parent cell must remain Undefined, including cells outside each view.
        for (const auto index : {0u, 2u, 4u, 6u}) {
            const auto& parent = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                sources->sources()[index]);
            assert(parent->originalObject()->subresourceState(
                       rhi::defaultTextureSubresourceRange(parent->originalObject()->desc()))
                == S::Undefined);
        }
        rebuild();
        root = makePlan();
        assert(root);
        // Destructor rollback uses the same original journal, without a public
        // completion claim or a second set of state/content controls.
        assert(!root->applyOriginalPrototypePlan());
        root.reset();
        assert(retained.graph->validate() && retained.program->validate());
        rebuild();
        retained.transaction = makePlan();
        assert(retained.transaction && !id);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    retained.stateQuota,
                    retained.recordingQuota},
                [&]() noexcept -> const char* {
                    if (const auto* diagnostic = retained.transaction->validate())
                        return diagnostic;
                    is(retained.transaction->executionDiagnostic(rhi::Backend::WebGPU),
                        "RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-CLOSURE-UNAVAILABLE");
                    return nullptr;
                }));
        });
        std::printf(
            "ORIGINAL_TABLE_TRANSACTION_OK Graphics transaction3 controls4 finals4 steps14 transitions4 exact-less-exhausted quota%llu same-journal explicit-and-destructor-rollback no-content-or-native-completion hot0 query0\n",
            static_cast<unsigned long long>(peak));
    }

    static rhi::common::OriginalTypedUsageProjection::Budget usageBudget(
        const StreamRetained& retained)
    {
        const auto& stream = retained.program->originalBudget();
        if (stream.contractVersion == 4u)
            return {4u,
                true,
                stream.descriptorConsumption,
                stream.renderDescriptorUsage,
                31u,
                4u,
                18u,
                6u,
                11u,
                1u,
                1u,
                512u,
                stream.geometryUsage};
        return {2u,
            true,
            stream.descriptorConsumption,
            stream.renderDescriptorUsage,
            11u,
            1u,
            10u,
            9u,
            10u};
    }

    static rhi::OriginalTypedDependencyProjectionContract::Request dependencyBudget(
        const StreamRetained& retained)
    {
        if (retained.program->originalBudget().contractVersion == 4u)
            return {4u, true, {4u, true, usageBudget(retained)}, 11u, 41u, 738u, 153u, 21u, 9u};
        // Ten contributions: 45 pairs, 11 compatibility scans and two scans
        // for each of four authored cells. Parent geometry needs nine cells.
        return {2u, true, {2u, true, usageBudget(retained)}, 4u, 19u, 190u, 45u, 10u, 9u};
    }

    static void verifyUsages(const StreamRetained& retained, const rhi::GpuDevice& device)
    {
        using P = rhi::common::OriginalTypedUsageProjection;
        using D = rhi::common::OriginalTypedDependencyProjection;
        using Scope = rhi::semantics::TypedShaderUsageScopeContract;
        const auto& stream = retained.program;
        const auto& graph = retained.graph;
        const auto& inputs = stream->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const auto usage = usageBudget(retained);
        const P::StateBoundRequest joined{2u, true, usage};
        const auto dependencies = dependencyBudget(retained);
        P::Counts expected;
        expected.renderBindings = 2u;
        expected.shaderConsumptions = 1u;
        expected.renderBindingTextures = 6u;
        expected.shaderTextures = 3u;
        expected.attachments = 1u;
        expected.contributions = expected.textureCells = 10u;
        std::array<D::Record, 4u> records;
        const auto verify = [&]() noexcept -> const char* {
            std::size_t sampled = 0u, attachments = 0u;
            const auto result = P::visitStateBound(
                stream, graph, joined, [&](const P::Point& point) noexcept -> const char* {
                    if (const auto* texture = std::get_if<P::TexturePoint>(&point)) {
                        const auto element = sampled % 3u;
                        const auto& original = texture->original;
                        const auto& use = texture->use;
                        assert(original.commandIndex == 6u + sampled / 3u
                            && original.textureParentSourceIndex == 2u * element
                            && original.textureViewSourceIndex == 2u * element + 1u
                            && original.tableElementIndex == element
                            && original.descriptorSourceIndex == sources->sources().size() - 1u
                            && original.descriptorBindCommandIndex == (sampled < 3u ? 6u : 7u)
                            && original.scopeBeginCommandIndex == 4u
                            && original.pipelineBindCommandIndex == 5u);
                        const auto& parent =
                            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                                sources->sources()[original.textureParentSourceIndex]);
                        const auto& view =
                            std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                                sources->sources()[original.textureViewSourceIndex]);
                        const auto& table = std::get<
                            std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                            sources->sources()[original.descriptorSourceIndex]);
                        assert(original.registrationGeneration
                                == view->originalObject()
                                    ->originalResourceRegistration()
                                    .generation()
                            && original.textureParentRegistrationGeneration
                                == use.registrationGeneration
                            && use.registrationGeneration
                                == parent->originalObject()
                                    ->originalResourceRegistration()
                                    .generation()
                            && original.descriptorContentRevision == table->frozenContentRevision()
                            && use.resource == parent->resource()
                            && use.range == original.textureRange
                            && use.range.baseMipLevel == element
                            && use.range.baseArrayLayer == element && use.range.mipLevelCount == 1u
                            && use.range.arrayLayerCount == 1u
                            && use.range.aspect == rhi::TextureAspect::Depth
                            && use.scope.first == 4u && use.scope.last == 9u
                            && use.scope.kind == Scope::Kind::RenderPass
                            && use.contribution
                                == (sampled < 6u ? Scope::Contribution::RenderBinding
                                                 : Scope::Contribution::ShaderConsumption)
                            && use.access.state == rhi::ResourceState::ShaderRead
                            && use.access.stages == rhi::PipelineStage::FragmentShader
                            && use.access.access == rhi::ResourceAccess::ShaderRead);
                        ++sampled;
                    } else {
                        const auto& attachment = std::get<P::AttachmentPoint>(point);
                        assert(attachment.original.commandIndex == 4u
                            && attachment.original.sourceIndex == 7u
                            && attachment.registrationGeneration && attachment.scope.first == 4u
                            && attachment.scope.last == 9u);
                        ++attachments;
                    }
                    return nullptr;
                });
            if (result.diagnostic)
                return result.diagnostic;
            assert(result.counts.matches(expected) && sampled == 9u && attachments == 1u
                && !result.nativeExecutionProven() && !result.synchronizationProven());
            const auto direct = P::inspect(stream, device, usage);
            if (direct.diagnostic)
                return direct.diagnostic;
            assert(direct.counts.matches(expected));
            std::size_t emitted = 0u;
            const auto projected = D::visit(
                stream, graph, dependencies, [&](const D::Record& record) noexcept -> const char* {
                    const auto element = emitted ? emitted - 1u : 0u;
                    assert(record.commandIndex == emitted && record.texture
                        && record.projectionVersion == 2u
                        && record.sourceIndex == (emitted ? 2u * element : 6u)
                        && record.before == rhi::ResourceState::Undefined
                        && record.after
                            == (emitted ? rhi::ResourceState::ShaderRead
                                        : rhi::ResourceState::ColorAttachment)
                        && record.range.baseMipLevel == element
                        && record.range.baseArrayLayer == element
                        && record.range.mipLevelCount == 1u && record.range.arrayLayerCount == 1u
                        && !record.preceding && record.following
                        && record.clause == D::Clause::InitialBookkeeping
                        && record.following->scope.first == 4u && record.following->scope.last == 9u
                        && record.following->contributions == (emitted ? 3u : 1u));
                    records[emitted++] = record;
                    return nullptr;
                });
            if (projected.diagnostic)
                return projected.diagnostic;
            assert(emitted == 4u && projected.counts.dependencies == 4u
                && projected.counts.initial == 4u && !projected.counts.between
                && !projected.counts.final && projected.counts.projectionPasses == 19u
                && projected.counts.contributionVisits == 190u && projected.counts.pairChecks == 45u
                && projected.counts.endpointContributions == 10u
                && projected.counts.textureCells == 4u && !projected.nativeExecutionProven()
                && !projected.synchronizationProven());
            D::Counts frozen;
            for (const auto& record : records)
                if (const auto* id =
                        D::validateFrozenBinding(stream, graph, dependencies, record, frozen))
                    return id;
            return nullptr;
        };
        if (const auto* id = verify())
            std::fprintf(stderr, "depth Usage2/Dependency2: %s\n", id);
        assert(!verify());
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    graph->originalMetadataQuota(),
                    retained.quota},
                verify));
        });
        const auto rejectUsage = [&](const auto& bad, const char* diagnostic) {
            std::size_t emitted = 0u;
            is(P::visitStateBound(stream,
                   graph,
                   bad,
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   })
                    .diagnostic,
                diagnostic);
            assert(!emitted);
        };
        auto bad = joined;
        --bad.projection.maxCommands;
        rejectUsage(bad, "RHI-TYPED-USAGE-SCOPE-COMMAND-CAPACITY");
        bad = joined;
        --bad.projection.maxContributions;
        rejectUsage(bad, "RHI-ORIGINAL-USAGE-PROJECTION-CONTRIBUTION-CAPACITY");
        bad = joined;
        --bad.projection.maxAttachmentReferences;
        rejectUsage(bad, "RHI-ORIGINAL-USAGE-PROJECTION-ATTACHMENT-CAPACITY");
        bad = joined;
        --bad.projection.maxTextureAccesses;
        rejectUsage(bad, "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-ACCESS-CAPACITY");
        bad = joined;
        --bad.projection.maxTextureCells;
        rejectUsage(bad, "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-CELL-CAPACITY");
        bad = joined;
        bad.prototypeOptIn = false;
        rejectUsage(bad, "RHI-ORIGINAL-STATE-BOUND-USAGE-OPT-IN");
        bad = joined;
        --bad.projection.shaderConsumption.maxConsumptions;
        rejectUsage(bad, "RHI-ORIGINAL-CONSUMPTION-BUDGET");
        bad = joined;
        --bad.projection.shaderConsumption.maxTextureAccesses;
        rejectUsage(bad, "RHI-ORIGINAL-USAGE-PROJECTION-FOOTPRINT-MISMATCH");
        bad = joined;
        --bad.projection.renderBindings.maxCommands;
        rejectUsage(bad, "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-FOOTPRINT-MISMATCH");
        const auto rejectDependency = [&](const auto& request, const char* diagnostic) {
            std::size_t emitted = 0u;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            is(D::visit(stream,
                   graph,
                   request,
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   })
                    .diagnostic,
                diagnostic);
            assert(!emitted && allocations == core::defaultCpuAllocator().stats().allocationCount);
        };
        auto less = dependencies;
        --less.maxDependencies;
        rejectDependency(less, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-DEPENDENCY-CAPACITY");
        less = dependencies;
        --less.maxProjectionPasses;
        rejectDependency(less, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PASS-CAPACITY");
        less = dependencies;
        --less.maxContributionVisits;
        rejectDependency(less, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VISIT-CAPACITY");
        less = dependencies;
        --less.maxPairChecks;
        rejectDependency(less, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PAIR-CAPACITY");
        less = dependencies;
        --less.maxEndpointContributions;
        rejectDependency(less, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-CAPACITY");
        less = dependencies;
        --less.maxTextureCells;
        rejectDependency(less, "RHI-ORIGINAL-TEXTURE-RANGE-CELL-CAPACITY");
        less = dependencies;
        less.prototypeOptIn = false;
        rejectDependency(less, "RHI-ORIGINAL-DEPENDENCY-PROJECTION-OPT-IN");
        auto altered = records[3u];
        --altered.range.baseMipLevel;
        D::Counts ignored;
        assert(D::validateFrozenBinding(stream, graph, dependencies, altered, ignored));
        const P::StateBoundRequest old{1u,
            true,
            {1u,
                true,
                {1u, true, 1u, 1u, true, true, 1u, 3u},
                {1u, true, 1u, 1u, true, true, 11u, 2u, 6u},
                11u,
                1u,
                10u}};
        rejectUsage(old, "RHI-ORIGINAL-STATE-BOUND-USAGE-STATE-VERSION");
        auto oldDependency = dependencies;
        oldDependency.contractVersion = 1u;
        oldDependency.usages = old;
        rejectDependency(oldDependency, "RHI-ORIGINAL-STATE-BOUND-USAGE-STATE-VERSION");
        rhi::OriginalTypedDependencyProjectionContract::FreezeRequest freeze{
            1u, true, dependencies, 64u, bytes};
        is(rhi::OriginalTypedDependencyProjectionContract::validateFreezeIntent(freeze),
            "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        std::size_t forgedOutput = 0u;
        const auto forged =
            std::shared_ptr<const rhi::OriginalCommandResourceStates>(retained.quota, graph.get());
        is(D::visit(stream,
               forged,
               dependencies,
               [&](const auto&) noexcept -> const char* {
                   ++forgedOutput;
                   return nullptr;
               })
                .diagnostic,
            "RHI-ORIGINAL-RESOURCE-STATES-OWNER");
        assert(!forgedOutput);
        std::puts(
            "ORIGINAL_TABLE_USAGE_DEPENDENCY_OK Graphics usage2 dependency2 sampled9 attachment1 cells10 dependencies4 pairs45 visits190 exact-partial-cells readonly-overlap shadowed-bind no-partial-output hot0 query0 native-closed");
    }

    static StreamRetained verifyRenderBindings(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device)
    {
        using C = rhi::common::OriginalCommandDescriptorConsumption;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const auto pipelineIndex = static_cast<std::uint32_t>(sources->sources().size() - 2u);
        const auto tableIndex = pipelineIndex + 1u;
        const std::array<rhi::CommandUsage, 7u> commands{{{K::BeginRenderPass, 0u, 1u},
            {K::BindGraphicsPipeline, 1u, 1u},
            {K::BindRenderDescriptorTable, 2u, 1u},
            {K::BindRenderDescriptorTable, 3u, 1u},
            {K::Draw, 4u, 0u},
            {K::EndRenderPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        const std::array<rhi::OriginalCommandUsage::Reference, 4u> refs{
            {{7u}, {pipelineIndex}, {tableIndex}, {tableIndex}}};
        const rhi::CommandVariableArguments::Input empty{
            *Scalar::capture(K::Finish, Scalar::None{}), {}};
        std::array<rhi::CommandVariableArguments::Input, 7u> fields{
            {empty, empty, empty, empty, empty, empty, empty}};
        for (std::size_t i = 0u; i < fields.size(); ++i) {
            const auto kind = commands[i].kind;
            fields[i].scalar = kind == K::Draw
                ? *Scalar::capture(kind, Scalar::Draw{3u, 1u, 0u, 0u})
                : kind == K::BindRenderDescriptorTable
                ? *Scalar::capture(kind, Scalar::DescriptorBinding{0u})
                : *Scalar::capture(kind, Scalar::None{});
        }
        const std::array<rhi::OriginalCommandInputs::OriginalDescriptor, 4u> records{
            {{0u, *inputs->descriptorAt(0u)},
                {1u, *inputs->descriptorAt(1u)},
                {2u, *inputs->descriptorAt(2u)},
                {3u, *inputs->descriptorAt(2u)}}};
        auto inputBudget = inputs->originalBudget();
        inputBudget.maxTableBindings = 2u;
        const char* id = nullptr;
        auto shadowed = OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            fields.data(),
            records.data(),
            records.size(),
            inputBudget,
            inputs->originalMetadataQuota(),
            id);
        if (!shadowed)
            std::fprintf(stderr, "depth render binding Inputs: %s\n", id ? id : "missing");
        assert(shadowed && !id);
        const C::RenderBindingBudget budget{
            2u, true, 0u, 0u, false, false, 7u, 2u, 6u, 1u, true, 6u};
        const auto verify = [&]() noexcept -> const char* {
            std::size_t emitted = 0u;
            const auto result = C::visitRenderBindings(
                shadowed, device, budget, [&](const auto& point) noexcept -> const char* {
                    const auto bindingCommand = 2u + emitted / 3u;
                    const auto element = emitted % 3u;
                    assert(point.kind == C::Access::Kind::TextureView
                        && point.bufferSourceIndex == SIZE_MAX
                        && point.commandIndex == bindingCommand
                        && point.descriptorBindCommandIndex == bindingCommand
                        && point.pipelineBindCommandIndex == 1u
                        && point.scopeBeginCommandIndex == 0u);
                    assert(point.pipelineSourceIndex == pipelineIndex
                        && point.descriptorSourceIndex == tableIndex
                        && point.textureParentSourceIndex == 2u * element
                        && point.textureViewSourceIndex == 2u * element + 1u
                        && point.tableElementIndex == element && point.set == 0u
                        && point.binding == 0u);
                    assert(point.textureRange.baseMipLevel == element
                        && point.textureRange.baseArrayLayer == element
                        && point.textureRange.mipLevelCount == 1u
                        && point.textureRange.arrayLayerCount == 1u
                        && point.textureRange.aspect == rhi::TextureAspect::Depth
                        && point.stages == rhi::PipelineStage::FragmentShader
                        && point.access == rhi::ResourceAccess::ShaderRead);
                    ++emitted;
                    return nullptr;
                });
            assert(!result.diagnostic && result.bindings == 2u && result.logicalAccesses == 6u
                && result.textureAccesses == 6u && emitted == 6u
                && !result.nativeExecutionProven());
            const auto active = C::inspect(
                shadowed, device, {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u});
            assert(!active.diagnostic && active.consumptions == 1u && active.logicalAccesses == 3u
                && active.textureAccesses == 3u);
            return nullptr;
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(), inputs->originalMetadataQuota()},
                verify));
        });
        const auto reject = [&](auto bad, const char* expected) {
            std::size_t emitted = 0u;
            const auto result = C::visitRenderBindings(
                shadowed, device, bad, [&](const auto&) noexcept -> const char* {
                    ++emitted;
                    return nullptr;
                });
            is(result.diagnostic, expected);
            assert(!emitted && !result.logicalAccesses && !result.textureAccesses);
        };
        auto bad = budget;
        --bad.maxBindings;
        reject(bad, "RHI-ORIGINAL-RENDER-USAGE-BINDING-CAPACITY");
        bad = budget;
        --bad.maxCommands;
        reject(bad, "RHI-ORIGINAL-RENDER-USAGE-COMMAND-CAPACITY");
        bad = budget;
        --bad.maxTextureAccesses;
        reject(bad, "RHI-ORIGINAL-CONSUMPTION-TEXTURE-CAPACITY");
        bad = budget;
        --bad.maxLogicalAccesses;
        bad.maxTextureAccesses = bad.maxLogicalAccesses;
        reject(bad, "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY");
        const auto old =
            C::inspectRenderBindings(inputs, device, {1u, true, 1u, 1u, true, true, 8u, 1u, 3u});
        is(old.diagnostic, "RHI-ORIGINAL-RENDER-USAGE-INPUTS-VERSION");
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto states = verifyStates(shadowed, device, stateQuota, 2u);
        auto retained = verifySequence(shadowed, states, device, stateQuota);
        states.reset();
        assert(stateQuota->liveBytes());
        std::puts(
            "ORIGINAL_TABLE_RENDER_BINDINGS_OK Graphics render2 shadowed-bind-retained bindings2 accesses6 draw-accesses3 independent-ceilings no-partial-output hot0 query0 state-stream-and-execution-closed");
        return retained;
    }

    template <class Pipeline>
    static void verifyConsumption(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device,
        StreamRetained& retained)
    {
        using C = rhi::common::OriginalCommandDescriptorConsumption;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        C::Budget budget{2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u};
        std::size_t emitted = 0u;
        const auto verify = [&]() noexcept -> const char* {
            emitted = 0u;
            const auto result =
                C::visit(inputs, device, budget, [&](const auto& point) noexcept -> const char* {
                    assert(point.kind == C::Access::Kind::TextureView
                        && point.bufferSourceIndex == SIZE_MAX);
                    assert(point.commandIndex == 3u && point.scopeBeginCommandIndex == 0u
                        && point.pipelineBindCommandIndex == 1u
                        && point.descriptorBindCommandIndex == 2u);
                    assert(point.pipelineSourceIndex == sources->sources().size() - 2u
                        && point.descriptorSourceIndex == sources->sources().size() - 1u);
                    const auto& table =
                        std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                            sources->sources()[point.descriptorSourceIndex]);
                    assert(point.descriptorContentRevision == table->frozenContentRevision());
                    assert(point.textureViewSourceIndex == 2u * emitted + 1u
                        && point.textureParentSourceIndex == 2u * emitted
                        && point.tableElementIndex == emitted);
                    const auto& view =
                        std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                            sources->sources()[point.textureViewSourceIndex]);
                    assert(sources->originalLeaseIndex(view) == point.textureViewSourceIndex
                        && sources->originalLeaseIndex(view->originalParentLease())
                            == point.textureParentSourceIndex);
                    const auto& range = point.textureRange;
                    assert(range.baseMipLevel == emitted && range.mipLevelCount == 1u
                        && range.baseArrayLayer == emitted && range.arrayLayerCount == 1u
                        && range.aspect == rhi::TextureAspect::Depth);
                    assert(point.registrationGeneration
                            == view->originalObject()->originalResourceRegistration().generation()
                        && point.textureParentRegistrationGeneration
                            == view->originalParentLease()
                                ->originalObject()
                                ->originalResourceRegistration()
                                .generation());
                    assert(point.set == 0u && point.binding == 0u && !point.offsetBytes
                        && !point.sizeBytes && point.state == rhi::ResourceState::ShaderRead
                        && point.access == rhi::ResourceAccess::ShaderRead
                        && point.stages == rhi::PipelineStage::FragmentShader
                        && !point.nativeExecutionProven());
                    ++emitted;
                    return nullptr;
                });
            if constexpr (compute) {
                is(result.diagnostic, "RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING");
                assert(!emitted && !result.textureAccesses);
            } else {
                if (result.diagnostic)
                    std::fprintf(stderr, "depth consumption: %s\n", result.diagnostic);
                assert(!result.diagnostic && result.consumptions == 1u
                    && result.logicalAccesses == 3u && result.textureAccesses == 3u
                    && emitted == 3u);
            }
            return nullptr;
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(), inputs->originalMetadataQuota()},
                verify));
        });
        if constexpr (!compute) {
            retained = verifyRenderBindings(inputs, device);
            const auto reject = [&](const auto& b, const char* expected) {
                emitted = 0u;
                is(C::visit(inputs,
                       device,
                       b,
                       [&](const auto&) noexcept -> const char* {
                           ++emitted;
                           return nullptr;
                       })
                        .diagnostic,
                    expected);
                assert(!emitted);
            };
            auto bad = budget;
            --bad.maxTextureAccesses;
            reject(bad, "RHI-ORIGINAL-CONSUMPTION-TEXTURE-CAPACITY");
            bad = budget;
            --bad.maxLogicalAccesses;
            bad.maxTextureAccesses = bad.maxLogicalAccesses;
            reject(bad, "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY");
            bad = budget;
            bad.maxConsumptions = 0u;
            reject(bad, "RHI-ORIGINAL-CONSUMPTION-BUDGET");
            // A successful earlier draw cannot hide a missing table after a
            // pipeline rebind. Complete preflight must emit no earlier access.
            const auto pipelineIndex = static_cast<std::uint32_t>(sources->sources().size() - 2u);
            const auto tableIndex = pipelineIndex + 1u;
            const std::array<rhi::CommandUsage, 8u> commands{{{K::BeginRenderPass, 0u, 1u},
                {K::BindGraphicsPipeline, 1u, 1u},
                {K::BindRenderDescriptorTable, 2u, 1u},
                {K::Draw, 3u, 0u},
                {K::BindGraphicsPipeline, 3u, 1u},
                {K::Draw, 4u, 0u},
                {K::EndRenderPass, 4u, 0u},
                {K::Finish, 4u, 0u}}};
            const std::array<rhi::OriginalCommandUsage::Reference, 4u> refs{
                {{7u}, {pipelineIndex}, {tableIndex}, {pipelineIndex}}};
            const rhi::CommandVariableArguments::Input empty{
                *Scalar::capture(K::Finish, Scalar::None{}), {}};
            std::array<rhi::CommandVariableArguments::Input, 8u> fields{
                {empty, empty, empty, empty, empty, empty, empty, empty}};
            for (std::size_t i = 0u; i < fields.size(); ++i) {
                const auto kind = commands[i].kind;
                fields[i].scalar = kind == K::Draw
                    ? *Scalar::capture(kind, Scalar::Draw{3u, 1u, 0u, 0u})
                    : kind == K::BindRenderDescriptorTable
                    ? *Scalar::capture(kind, Scalar::DescriptorBinding{0u})
                    : *Scalar::capture(kind, Scalar::None{});
            }
            auto pipelineBinding =
                std::get<rhi::OriginalCommandPipelineBinding>(*inputs->descriptorAt(1u));
            const std::array<rhi::OriginalCommandInputs::OriginalDescriptor, 4u> records{
                {{0u, *inputs->descriptorAt(0u)},
                    {1u, pipelineBinding},
                    {2u, *inputs->descriptorAt(2u)},
                    {4u, pipelineBinding}}};
            auto inputBudget = inputs->originalBudget();
            inputBudget.maxPipelineBindings = 2u;
            const char* id = nullptr;
            auto rebound = OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                records.data(),
                records.size(),
                inputBudget,
                inputs->originalMetadataQuota(),
                id);
            if (!rebound)
                std::fprintf(stderr, "depth rebind Inputs: %s\n", id ? id : "missing");
            assert(rebound && !id);
            auto twoDraws = budget;
            twoDraws.maxConsumptions = 2u;
            twoDraws.maxLogicalAccesses = twoDraws.maxTextureAccesses = 6u;
            emitted = 0u;
            is(C::visit(rebound,
                   device,
                   twoDraws,
                   [&](const auto&) noexcept -> const char* {
                       ++emitted;
                       return nullptr;
                   })
                    .diagnostic,
                "RHI-ORIGINAL-CONSUMPTION-TABLE-MISSING");
            assert(!emitted);
        }
        std::printf(
            "ORIGINAL_TABLE_CONSUMPTION_OK %s consumption2 full-view-range exact-parent-view-generation all-elements bounded hot0 query0 preflight-no-partial-output compute-output-required state-and-execution-closed\n",
            compute ? "Compute" : "Graphics");
    }

    template <class Pipeline>
    static std::shared_ptr<const rhi::OriginalCommandInputs> verifyPositional(
        const std::shared_ptr<rhi::OriginalCommandResourceSources>& sources,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<Pipeline>& equivalentPipeline,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota,
        StreamRetained& retained)
    {
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Inputs = rhi::OriginalCommandInputs;
        using Binding = rhi::OriginalCommandDescriptorBinding;
        constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;
        constexpr auto begin = compute ? K::BeginComputePass : K::BeginRenderPass;
        constexpr auto bindPipeline = compute ? K::BindComputePipeline : K::BindGraphicsPipeline;
        constexpr auto bindTable =
            compute ? K::BindComputeDescriptorTable : K::BindRenderDescriptorTable;
        constexpr auto consume = compute ? K::Dispatch : K::Draw;
        constexpr auto end = compute ? K::EndComputePass : K::EndRenderPass;
        const auto pipelineIndex = sources->sources().size() - 2u;
        const auto tableIndex = pipelineIndex + 1u;
        const auto& source =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorTableSource>>(
                sources->sources()[tableIndex]);
        assert(equivalentPipeline && equivalentPipeline.get() != pipeline.get());
        assert(!source->originalObject()->originalDepthTablePipelineDiagnostic(*pipeline));
        is(source->originalObject()->originalDepthTablePipelineDiagnostic(*equivalentPipeline),
            "RHI-ORIGINAL-TABLE-BINDING-PIPELINE");

        const std::array<rhi::CommandUsage, 6u> commands{{{begin, 0u, compute ? 0u : 1u},
            {bindPipeline, compute ? 0u : 1u, 1u},
            {bindTable, compute ? 1u : 2u, 1u},
            {consume, compute ? 2u : 3u, 0u},
            {end, compute ? 2u : 3u, 0u},
            {K::Finish, compute ? 2u : 3u, 0u}}};
        std::array<rhi::OriginalCommandUsage::Reference, compute ? 2u : 3u> refs;
        if constexpr (!compute)
            refs[0u] = {7u};
        refs[compute ? 0u : 1u] = {static_cast<std::uint32_t>(pipelineIndex)};
        refs[compute ? 1u : 2u] = {static_cast<std::uint32_t>(tableIndex)};
        const char* id = nullptr;
        auto usage = rhi::OriginalCommandUsage::prepareOriginal(
            sources, commands.data(), commands.size(), refs.data(), refs.size(), graphQuota, id);
        if (!usage)
            std::fprintf(stderr, "depth-table Usage: %s\n", id ? id : "missing");
        assert(usage && !id);
        std::array<rhi::CommandVariableArguments::Input, 6u> fields{
            {{*Scalar::capture(begin, Scalar::None{}), {}},
                {*Scalar::capture(bindPipeline, Scalar::None{}), {}},
                {*Scalar::capture(bindTable, Scalar::DescriptorBinding{0u}), {}},
                {compute ? *Scalar::capture(consume, Scalar::Dispatch{1u, 1u, 1u})
                         : *Scalar::capture(consume, Scalar::Draw{3u, 1u, 0u, 0u}),
                    {}},
                {*Scalar::capture(end, Scalar::None{}), {}},
                {*Scalar::capture(K::Finish, Scalar::None{}), {}}}};
        const auto makeScalars = [&] {
            auto store = rhi::CommandVariableArguments::prepareOriginal(
                fields.data(), fields.size(), {1u, true, 6u, 0u, 0u, 0u, bytes}, graphQuota, id);
            assert(store && !id);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, store, graphQuota, id);
            assert(scalars && !id);
            return scalars;
        };
        auto scalars = makeScalars();
        const std::size_t recordCount = compute ? 2u : 3u;
        std::array<Inputs::OriginalDescriptor, 3u> records{
            {{SIZE_MAX, Binding{}}, {SIZE_MAX, Binding{}}, {SIZE_MAX, Binding{}}}};
        if constexpr (!compute) {
            const auto& target =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    sources->sources()[7u]);
            rhi::RenderPassDesc pass;
            rhi::RenderPassColorAttachmentDesc color;
            color.target = target->resource();
            color.format = target->originalViewLease()->layout().format;
            color.loadOp = rhi::LoadOp::Clear;
            color.storeOp = rhi::StoreOp::Store;
            pass.colorAttachments.push_back(color);
            auto frozen = rhi::RenderPassArguments::prepareOriginal(
                pass, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, graphQuota, id);
            assert(frozen && !id);
            records[0u] = {0u, frozen};
        }
        records[compute ? 0u : 1u] = {1u,
            rhi::OriginalCommandPipelineBinding{bindPipeline,
                pipelineIndex,
                0u,
                pipeline->originalResourceRegistration().generation()}};
        const Binding binding{bindTable,
            tableIndex,
            pipelineIndex,
            0u,
            source->originalObject()->originalResourceRegistration().generation(),
            source->frozenContentRevision()};
        const auto recordIndex = compute ? 1u : 2u;
        records[recordIndex] = {2u, binding};
        rhi::OriginalCommandInputsBudget budget{4u, true, 1u, 0u, 1u, 0u};
        const auto factory = [&] {
            return Inputs::prepareOriginal(
                scalars, records.data(), recordCount, budget, graphQuota, id);
        };
        auto inputs = factory();
        if (!inputs)
            std::fprintf(stderr, "depth-table Inputs4: %s\n", id ? id : "missing");
        assert(inputs && !id && !inputs->validate());
        const auto baseline = graphQuota->liveBytes();
        const auto reject = [&](const char* expected) {
            assert(!factory());
            is(id, expected);
            assert(graphQuota->liveBytes() == baseline);
        };
        const auto negative = [&](auto mutate, const char* expected) {
            mutate(std::get<Binding>(records[recordIndex].arguments));
            reject(expected);
            records[recordIndex] = {2u, binding};
        };
        negative(
            [](auto& b) {
                ++b.registrationGeneration;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-GENERATION");
        negative(
            [](auto& b) {
                ++b.contentRevision;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-REVISION");
        negative(
            [](auto& b) {
                b.pipelineSourceIndex = 0u;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-PIPELINE");
        negative(
            [](auto& b) {
                b.scopeBeginCommandIndex = 1u;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SCOPE");
        negative(
            [](auto& b) {
                b.sourceIndex = 0u;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE");
        negative(
            [](auto& b) {
                b.commandKind =
                    compute ? K::BindRenderDescriptorTable : K::BindComputeDescriptorTable;
            },
            "RHI-ORIGINAL-INPUTS-KIND");
        budget.prototypeOptIn = false;
        reject("RHI-ORIGINAL-TYPED-INPUT-OPT-IN");
        budget.prototypeOptIn = true;
        budget.contractVersion = 3u;
        reject(rhi::semantics::OriginalCommandDescriptorBindingLowering::tableProfileDiagnostic());
        budget.contractVersion = 2u;
        budget.maxTableBindings = 0u;
        reject("RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING");
        budget = {4u, true, 1u, 0u, 1u, 0u};
        auto excessive = budget;
        excessive.maxTableBindings = sources->originalBudget().maxInputDescriptors + 1u;
        assert(!Inputs::prepareOriginal(
            scalars, records.data(), recordCount, excessive, graphQuota, id));
        is(id, "RHI-ORIGINAL-TYPED-INPUT-GRAPH-CAPACITY");
        // Wrong logical slot is copied into its own original scalar root.
        fields[2u].scalar = *Scalar::capture(bindTable, Scalar::DescriptorBinding{1u});
        auto wrongScalars = makeScalars();
        assert(!Inputs::prepareOriginal(
            wrongScalars, records.data(), recordCount, budget, graphQuota, id));
        is(id, "RHI-ORIGINAL-TYPED-INPUT-TABLE-LAYOUT");
        wrongScalars.reset();
        fields[2u].scalar = *Scalar::capture(bindTable, Scalar::DescriptorBinding{0u});

        CpuQuotaFactoryFixture pressure(graphQuota);
        const auto peak = pressure.measureSingleUse([] {}, factory);
        pressure.verifySingleUse(peak, [] {}, factory);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{graphQuota, sources->originalMetadataQuota()}, [&]() noexcept {
                    return inputs->validate();
                }));
        });
        assert(!inputs->nativeExecutionProven() && inputs->executionDiagnostic(device.backend()));
        const auto consumption = rhi::common::OriginalCommandDescriptorConsumption::inspect(
            inputs, device, {1u, true, 1u, 1u, true, true, 1u, 3u});
        is(consumption.diagnostic, "RHI-ORIGINAL-CONSUMPTION-INPUTS-VERSION");
        verifyConsumption<Pipeline>(inputs, device, retained);
        std::printf(
            "ORIGINAL_TABLE_POSITIONAL_OK %s inputs4 same-original-pipeline equal-artifact-peer-refused scope-slot-generation-revision quota%llu exact-less-exhausted hot0 query0 consumption-recording-completion-closed\n",
            compute ? "Compute" : "Graphics",
            static_cast<unsigned long long>(peak));
        return inputs;
    }
#endif
public:
    struct Publication {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 12u> quotas;
        std::shared_ptr<rhi::OriginalCommandResourceSources> sources;
        std::shared_ptr<const rhi::OriginalCommandInputs> inputs;
        std::shared_ptr<const rhi::OriginalCommandResourceStates> states;
        StreamRetained typedStream;
        OriginalDepthTableContentPlanFixture::NativeCustody nativeCustody;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 17u> geometryQuotas;
    };

    static void verifyUsageUnavailable(const StreamRetained& retained, const rhi::GpuDevice& device)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!retained.program)
            return;
        const auto* expected = retained.program->validate();
        assert(expected);
        if (retained.transaction) {
            // Submitted physical loss invalidates the journal independently of
            // the still-retained Stream's native registration diagnostic.
            const auto* rootExpected = retained.transaction->status()
                    == rhi::OriginalCommandStateTransaction::Status::Invalidated
                ? "RHI-ORIGINAL-TRANSACTION-INVALIDATED"
                : expected;
            is(retained.transaction->validate(), rootExpected);
        }
        using P = rhi::common::OriginalTypedUsageProjection;
        using D = rhi::common::OriginalTypedDependencyProjection;
        std::size_t emitted = 0u;
        const auto usage = usageBudget(retained);
        is(P::visit(retained.program,
               device,
               usage,
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            expected);
        is(P::visitStateBound(retained.program,
               retained.graph,
               {usage.contractVersion, true, usage},
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            expected);
        is(D::visit(retained.program,
               retained.graph,
               dependencyBudget(retained),
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            expected);
        assert(!emitted);
#endif
    }

    static void verifyConsumptionUnavailable(
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device) noexcept
    {
        using C = rhi::common::OriginalCommandDescriptorConsumption;
        const auto* expected = inputs->validate();
        assert(expected);
        std::size_t emitted = 0u;
        rhi::OriginalCommandDescriptorConsumptionContract::Budget consumption{
            2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u};
        rhi::OriginalRenderDescriptorUsageBudget render{
            2u, true, 0u, 0u, false, false, 8u, 1u, 3u, 1u, true, 3u};
        if (inputs->originalBudget().contractVersion == 5u) {
            consumption.contractVersion = render.contractVersion = 3u;
            consumption.indexedDrawConsumptionVersion = 1u;
            consumption.indexedDrawConsumptionOptIn = true;
            consumption.vertexStreamConsumptionVersion = 1u;
            consumption.vertexStreamConsumptionOptIn = true;
            consumption.maxVertexStreams = 2u;
            render.maxCommands = 31u;
        }
        is(C::visit(inputs,
               device,
               consumption,
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            expected);
        assert(!emitted);
        is(C::visitRenderBindings(inputs,
               device,
               render,
               [&](const auto&) noexcept -> const char* {
                   ++emitted;
                   return nullptr;
               })
                .diagnostic,
            expected);
        assert(!emitted);
    }

    static void verifyContract()
    {
        using Positional = rhi::OriginalCommandPipelineBindingContract;
        rhi::OriginalCommandInputsBudget inputs{4u, true, 1u, 0u, 1u, 0u};
        assert(!Positional::validateBudget(inputs));
        auto invalidInputs = inputs;
        invalidInputs.prototypeOptIn = false;
        is(Positional::validateBudget(invalidInputs), "RHI-ORIGINAL-TYPED-INPUT-OPT-IN");
        invalidInputs = inputs;
        invalidInputs.maxTableBindings = 0u;
        is(Positional::validateBudget(invalidInputs), "RHI-ORIGINAL-TYPED-INPUT-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            using Lowering = rhi::semantics::OriginalCommandDescriptorBindingLowering;
            const auto absent = Lowering::depthTableLowering(backend);
            assert(!absent.positionalAvailable && !absent.executionAvailable);
            const auto selected = Lowering::depthTableLowering(backend, true);
            assert(selected.positionalAvailable == (backend == rhi::Backend::WebGPU)
                && !selected.executionAvailable
                && selected.validatorOnly == (backend == rhi::Backend::Null));
            assert(!Lowering::tableLowering(backend).positionalAvailable);
        }
        Request request{1u,
            true,
            Contract::retainedOwnerSlots,
            Contract::callbackCount,
            1u,
            {bytes, bytes, bytes}};
        assert(!Contract::validate(request));
        auto bad = request;
        bad.contractVersion = 0u;
        is(Contract::validate(bad), "RHI-ORIGINAL-TABLE-PUBLICATION-VERSION");
        bad = request;
        bad.prototypeOptIn = false;
        is(Contract::validate(bad), "RHI-ORIGINAL-TABLE-PUBLICATION-OPT-IN");
        for (const auto mutation : {0u, 1u, 2u}) {
            bad = request;
            if (mutation == 0u)
                --bad.maxRetainedOwnerSlots;
            if (mutation == 1u)
                --bad.maxCallbacks;
            if (mutation == 2u)
                bad.maxRetainedAllocationBytes = 0u;
            is(Contract::validate(bad), "RHI-ORIGINAL-TABLE-PUBLICATION-BUDGET");
        }
        for (std::size_t i = 0u; i < request.maxCpuBytes.size(); ++i) {
            bad = request;
            bad.maxCpuBytes[i] = 0u;
            is(Contract::validate(bad), "RHI-ORIGINAL-TABLE-PUBLICATION-CPU-BUDGET");
        }
        const Contract::Facts facts{true, true, true, true, true};
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision = Contract::decide(backend, facts);
            assert(!decision.executionAvailable);
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            assert(decision.preparationAvailable == (backend == rhi::Backend::WebGPU));
        }
        for (const auto field : {&Contract::Facts::device,
                 &Contract::Facts::group,
                 &Contract::Facts::layoutQuery,
                 &Contract::Facts::pushScope,
                 &Contract::Facts::popScope}) {
            auto missing = facts;
            missing.*field = false;
            assert(!Contract::decide(rhi::Backend::WebGPU, missing).preparationAvailable);
        }
        auto device = rhi::createNullDevice();
        rhi::OriginalCommandStateAdmissionBudget state{3u,
            true,
            1u,
            1u,
            7u,
            7u,
            64u,
            bytes,
            {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u},
            {2u, true, 0u, 0u, false, false, 8u, 1u, 3u, 1u, true, 3u}};
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* stateId = nullptr;
        assert(!rhi::OriginalCommandResourceStates::prepareOriginal(
            {}, *device, state, stateQuota, stateId));
        is(stateId, "RHI-ORIGINAL-RENDER-USAGE-NULL-VALIDATOR-ONLY");
        assert(!stateQuota->liveBytes());
        rhi::DescriptorArenaDesc ad;
        ad.maxSets = 1u;
        ad.maxDescriptors = 3u;
        auto arena = core::shareUnique(device->createDescriptorArena(ad));
        assert(arena);
        rhi::DescriptorTableDesc td;
        td.label = "Inputs";
        td.capacity = 3u;
        td.stages = rhi::ShaderStage::Compute;
        td.depthTexture = true;
        td.textureViewDimensionConstrained = true;
        auto table = arena->allocateDescriptorTable(td);
        assert(table);
        const auto revision = table->contentRevision();
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        is(table->beginOriginalDepthTablePublication(
               *device, arena, std::shared_ptr<rhi::ComputePipeline>{}, nullptr, 0u, request, {}),
            "RHI-ORIGINAL-TABLE-PUBLICATION-UNAVAILABLE");
        is(table->finishOriginalDepthTablePublication(),
            "RHI-ORIGINAL-TABLE-PUBLICATION-UNPREPARED");
        assert(table->contentRevision() == revision && !table->stats().writtenDescriptors
            && allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "ORIGINAL_TABLE_PUBLICATION_CONTRACT_OK explicit-request bounded-owners-memory-callbacks four-backends null-no-publication-or-execution");
    }

    template <class Pipeline>
    static Publication verifyNative(rhi::GpuDevice& device,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<rhi::DescriptorArena>& arena,
        core::UniquePtr<rhi::DescriptorTable>& candidate,
        const std::shared_ptr<Pipeline>& equivalentPipeline,
        bool keepCurrent = false,
        bool contentCells = false,
        bool reserveExecution = false,
        bool retainedReadback = false,
        bool geometry = false)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* id = nullptr;
        assert(candidate);
        auto& table = *candidate;
        if constexpr (std::is_same_v<Pipeline, rhi::RenderPipeline>)
            if (reserveExecution)
                TextureReadbackRetainedSourceFixture::verify(device);
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        auto contentQuota = contentCells ? core::makeShared<core::CpuMemoryQuota>(bytes) : nullptr;
        std::array<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>, 3u> attachments;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u> q;
        for (auto& domain : q)
            domain = core::makeShared<core::CpuMemoryQuota>(bytes);
        std::array<rhi::OriginalDepthTablePublicationWrite, 3u> writes;
        std::uint64_t retainedBytes = 0u;
        const rhi::OriginalCommandResourceLeaseBudget leaseBudget{
            1u, true, rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount, bytes};
        for (std::uint32_t i = 0u; i < writes.size(); ++i) {
            rhi::TextureDesc td;
            td.label = "Original Table depth parent";
            td.format = rhi::TextureFormat::D32Float;
            td.extent = {8u, 8u, 1u};
            td.mipLevelCount = 3u;
            td.arrayLayerCount = 3u;
            td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderAttachment;
            core::UniquePtr<rhi::GpuTexture> parent;
            core::UniquePtr<rhi::GpuTextureView> view;
            {
                core::CpuMemoryQuotaScope scope(sourceQuota);
                parent = contentCells
                    ? device.createOriginalContentTexture(
                          td, {2u, true, 1u, 1u, 1008u, 16u, bytes, 9u}, contentQuota, id)
                    : device.createTexture(td);
                if (!parent)
                    std::fprintf(stderr,
                        "depth-table parent registration: %.*s\n",
                        int(device.lastOriginalResourceRegistrationDiagnostic().size()),
                        device.lastOriginalResourceRegistrationDiagnostic().data());
                assert(parent);
                rhi::TextureViewDesc vd;
                vd.label = "Original Table depth view";
                vd.format = td.format;
                vd.usage = rhi::TextureUsage::Sampled;
                vd.aspect = rhi::TextureAspect::Depth;
                vd.baseMipLevel = i;
                vd.baseArrayLayer = i;
                view = device.createTextureView(*parent, vd);
                assert(view);
            }
            assert(!device.validateOriginalResourceRegistration(*parent)
                && !device.validateOriginalResourceRegistration(*view));
            retainedBytes += parent->allocation().estimatedSizeBytes;
            const auto parentLease = rhi::OriginalCommandTextureLease::prepareOriginal(
                parent, leaseBudget, sourceQuota, id);
            assert(parentLease && !id && !parent);
            const auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                view, leaseBudget, sourceQuota, id);
            assert(viewLease && !id && !view);
            writes[i].view = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                viewLease, parentLease, leaseBudget, sourceQuota, id);
            assert(writes[i].view && !id);
            writes[i].write = {i,
                rhi::DescriptorType::SampledTexture,
                writes[i].view->resource(),
                rhi::DescriptorResourceKind::TextureView};
            if (contentCells) {
                rhi::TextureViewDesc vd;
                vd.format = td.format;
                vd.usage = rhi::TextureUsage::RenderAttachment;
                vd.aspect = rhi::TextureAspect::Depth;
                vd.baseMipLevel = i;
                vd.baseArrayLayer = i;
                core::UniquePtr<rhi::GpuTextureView> attachment;
                {
                    core::CpuMemoryQuotaScope scope(sourceQuota);
                    attachment = device.createTextureView(*parentLease->originalObject(), vd);
                }
                assert(attachment && !device.validateOriginalResourceRegistration(*attachment));
                auto lease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                    attachment, leaseBudget, sourceQuota, id);
                attachments[i] = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                    lease, parentLease, leaseBudget, sourceQuota, id);
                assert(attachments[i] && !id);
            }
        }
        Request request{1u,
            true,
            Contract::retainedOwnerSlots,
            Contract::callbackCount,
            retainedBytes,
            {bytes, bytes, bytes}};
        const auto revision = table.contentRevision();
        const auto begin = [&](const auto& w, const auto& r, const auto& domains) {
            return table.beginOriginalDepthTablePublication(
                device, arena, pipeline, w.data(), w.size(), r, domains);
        };
        const auto clean = [&] {
            assert(table.contentRevision() == revision && !table.stats().writtenDescriptors);
            for (const auto& quota : q)
                assert(!quota->liveBytes());
        };
        auto bad = request;
        bad.prototypeOptIn = false;
        is(begin(writes, bad, q), "RHI-ORIGINAL-TABLE-PUBLICATION-OPT-IN");
        clean();
        auto changed = writes;
        changed[1].write.resource = writes[0].write.resource;
        is(begin(changed, request, q), "RHI-ORIGINAL-TABLE-PUBLICATION-VIEW-IDENTITY");
        clean();
        bad = request;
        --bad.maxRetainedAllocationBytes;
        is(begin(writes, bad, q), "RHI-ORIGINAL-TABLE-PUBLICATION-ALLOCATION-BUDGET");
        clean();
        auto alias = q;
        alias[1] = q[0];
        is(begin(writes, request, alias), "RHI-ORIGINAL-TABLE-PUBLICATION-QUOTA-DOMAIN");
        clean();
        for (const auto& quota : q) {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            is(begin(writes, request, q), "RHI-ORIGINAL-TABLE-PUBLICATION-ALLOCATION");
        }
        clean();
        // Each independent domain must admit its exact measured minimum and
        // refuse one byte less, preserving the original candidate and revision.
        std::array<std::uint64_t, 3u> requiredCpuBytes;
        for (std::size_t i = 0u; i < q.size(); ++i) {
            CpuQuotaFactoryFixture pressure(q[i]);
            const auto probe = [&] {
                return CandidateProbe(table, begin(writes, request, q));
            };
            requiredCpuBytes[i] = pressure.measureSingleUse([] {}, probe);
            pressure.verifySingleUse(requiredCpuBytes[i], [] {}, probe);
            clean();
        }
        std::printf(
            "ORIGINAL_TABLE_PUBLICATION_CPU_BUDGET_OK %s metadata%llu native%llu callback%llu exact-less-exhausted cancelled-real-scopes old-revision\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "Compute" : "Graphics",
            static_cast<unsigned long long>(requiredCpuBytes[0]),
            static_cast<unsigned long long>(requiredCpuBytes[1]),
            static_cast<unsigned long long>(requiredCpuBytes[2]));
        assert(!begin(writes, request, q));
        assert(table.contentRevision() == revision && !table.stats().writtenDescriptors);
        assert(table.finishOriginalDepthTablePublication());
        is(table.write(writes[0].write).diagnostics.c_str(),
            "RHI-ORIGINAL-TABLE-PUBLICATION-PENDING");
        assert(WebGpuAsyncWaitFixture::until([&] {
            return table.originalDepthTablePublicationScopesDrained();
        }));
        const auto* finishId = CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
            return table.finishOriginalDepthTablePublication();
        });
        if (finishId)
            std::printf("TABLE_PUBLICATION_FINISH_DIAGNOSTIC %s\n", finishId);
        assert(!finishId);
        assert(table.contentRevision() == revision + 1u && table.stats().writtenDescriptors == 3u
            && table.hasOriginalDepthTableContent()
            && !table.originalDepthTableContentDiagnostic());
        std::array<std::optional<rhi::DescriptorTableWriteObservation>, 3u> receipts;
        for (std::uint32_t i = 0u; i < receipts.size(); ++i) {
            auto receipt = table.observeSuccessfulDescriptorWrite(i, id);
            assert(receipt && !id);
            receipts[i].emplace(*receipt);
            assert(receipts[i] && !id && !receipts[i]->diagnostic()
                && receipts[i]->revision() == revision + 1u);
        }
        is(table.write(writes[0].write).diagnostics.c_str(),
            "RHI-ORIGINAL-TABLE-PUBLICATION-ORDINARY-CLOSED");
        auto native = rhi_webgpu::webGpuNativePayloadFor(table);
        assert(
            native && native->group && native->layout && native->containsOriginalDepthTableWrite());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
            const auto saved = native->bindings[0].write.resource;
            native->bindings[0].write.resource = {};
            assert(table.originalDepthTableContentDiagnostic());
            native->bindings[0].write.resource = saved;
            const auto savedLimit = q[0]->limitBytes();
            q[0]->setLimitBytes(savedLimit - 1u);
            assert(table.originalDepthTableContentDiagnostic());
            q[0]->setLimitBytes(savedLimit);
            return table.originalDepthTableContentDiagnostic();
        }));
        // A replacement retains old storage; cancellation preserves its revision
        // and valid receipts, while no pending candidate can issue new receipts.
        assert(!begin(writes, request, q));
        assert(!table.observeSuccessfulDescriptorWrite(0u, id) && id);
        assert(receipts[0]->diagnostic());
        assert(table.cancelOriginalDepthTablePublication());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return table.originalDepthTablePublicationScopesDrained();
        }));
        assert(!table.cancelOriginalDepthTablePublication());
        assert(!receipts[0]->diagnostic() && table.contentRevision() == revision + 1u);
        // The selected group joins the existing canonical inventory and Source2;
        // parent leases must be present explicitly as well as retained by views.
        using Sources = rhi::OriginalCommandResourceSources;
        using TableSource = rhi::OriginalCommandDescriptorTableSource;
        using PipelineSource = rhi::OriginalCommandPipelineSource<Pipeline>;
        constexpr bool compute = std::is_same_v<Pipeline, rhi::ComputePipeline>;
        assert(!contentCells || !compute);
        std::array<Sources::Source, compute ? 6u : 16u> prefix;
        const std::size_t prefixCount = compute ? 6u
            : geometry                          ? 16u
            : retainedReadback                  ? 13u
            : contentCells                      ? 11u
                                                : 8u;
        const rhi::OriginalRetainedReadbackCorrespondence correspondence = retainedReadback
            ? rhi::OriginalRetainedReadbackCorrespondence{1u, true, 1u, 7u}
            : rhi::OriginalRetainedReadbackCorrespondence{};
        std::array<TableSource::Element, 3u> elements;
        for (std::size_t i = 0u; i < writes.size(); ++i) {
            prefix[2u * i] = writes[i].view->originalParentLease();
            prefix[2u * i + 1u] = writes[i].view;
            elements[i] = {writes[i].write,
                2u * i + 1u,
                writes[i].view->originalObject()->originalResourceRegistration().generation()};
        }
        if constexpr (!compute) {
            // A genuine authored attachment is required even for positional
            // Graphics Inputs. It is separate from the sampled-only depth views.
            rhi::TextureDesc td;
            td.label = "Original Table positional target";
            td.format = rhi::TextureFormat::Rgba16Float;
            td.extent = {retainedReadback ? 4u : 2u, 2u, 1u};
            td.usage = rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::CopySrc;
            core::UniquePtr<rhi::GpuTexture> parent;
            core::UniquePtr<rhi::GpuTextureView> view;
            {
                core::CpuMemoryQuotaScope scope(sourceQuota);
                parent = contentCells
                    ? device.createOriginalContentTexture(td,
                          {2u, true, 1u, 1u, retainedReadback ? 64u : 32u, 16u, bytes, 1u},
                          contentQuota,
                          id)
                    : device.createTexture(td);
                assert(parent);
                rhi::TextureViewDesc vd;
                vd.label = "Original Table positional attachment";
                vd.format = td.format;
                vd.usage = rhi::TextureUsage::RenderAttachment;
                view = device.createTextureView(*parent, vd);
                assert(view);
            }
            const auto parentLease = rhi::OriginalCommandTextureLease::prepareOriginal(
                parent, leaseBudget, sourceQuota, id);
            assert(parentLease && !id);
            const auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
                view, leaseBudget, sourceQuota, id);
            assert(viewLease && !id);
            const auto target = rhi::OriginalCommandTextureViewSource::prepareOriginal(
                viewLease, parentLease, leaseBudget, sourceQuota, id);
            assert(target && !id);
            prefix[6u] = parentLease;
            prefix[7u] = target;
            if (contentCells)
                for (std::size_t i = 0u; i < attachments.size(); ++i)
                    prefix[8u + i] = attachments[i];
            if (retainedReadback) {
                assert(contentCells && !reserveExecution);
                rhi::BufferDesc bd;
                bd.sizeBytes = 512u;
                bd.usage = rhi::ResourceUsage::CopyDst;
                bd.memory = rhi::MemoryUsage::GpuToCpu;
                bd.initialState = rhi::ResourceState::CopyDst;
                core::UniquePtr<rhi::GpuBuffer> object;
                {
                    core::CpuMemoryQuotaScope scope(sourceQuota);
                    object = device.createBuffer(bd);
                }
                auto destination = rhi::OriginalCommandBufferLease::prepareOriginal(
                    object, leaseBudget, sourceQuota, id);
                assert(destination && !id);
                prefix[11u] = destination;
                rhi::ReadbackTextureDesc read;
                read.sourceResource = parentLease->resource();
                read.destinationReadbackBuffer = destination->resource();
                read.extent = td.extent;
                read.bytesPerRow = 256u;
                read.rowsPerImage = 2u;
                read.sizeBytes = 512u;
                read.sourceSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
                read.label = OriginalDepthTableContentPlanFixture::readbackLabel;
                auto input = rhi::TextureReadbackInputSnapshot::prepareOriginal(
                    read, {1u, true, 1u, 16u, bytes, bytes}, graphQuota, graphQuota, id);
                assert(input && !id);
                auto layout = rhi::TextureReadbackLayout::prepareOriginal(
                    input, td, {1u, true, 1u, 512u, bytes}, graphQuota, id);
                assert(layout && !id);
                auto request = TextureReadbackSourceLeaseFixture::request(layout, graphQuota);
                auto lb = TextureReadbackSourceLeaseFixture::budget(request);
                lb.contractVersion = 2u;
                lb.maxRetainedOwners = rhi::TextureReadbackSourceLease::retainedOriginalOwnerCount;
                auto lease = rhi::TextureReadbackSourceLease::prepareRetainedOriginal(
                    parentLease, request, lb, sourceQuota, id);
                assert(lease && !id);
                rhi::OriginalCommandReadbackSourceBudget rb{
                    2u, true, 8u, 1u, 16u, 512u, bytes, bytes};
                auto source = rhi::OriginalCommandReadbackTextureSource::prepareOriginal(
                    lease, rb, sourceQuota, graphQuota, id);
                assert(source && !id);
                rb.maxRetainedOwners =
                    rhi::OriginalCommandTextureReadbackSource::completeRetainedOwnerCount
                    + rhi::TextureReadbackSourceLease::retainedOriginalOwnerCount
                    - rhi::TextureReadbackSourceLease::retainedOwnerCount;
                auto closure = rhi::OriginalCommandTextureReadbackSource::prepareOriginal(
                    source, destination, rb, sourceQuota, id);
                assert(closure && !id);
                prefix[12u] = closure;
            }
        }
        OriginalVertexGeometryExecutionFixture::Prepared geometryPrepared;
        if (geometry) {
            assert(!compute && contentCells && retainedReadback && keepCurrent);
            const auto geometryResources = core::makeShared<core::CpuMemoryQuota>(bytes);
            geometryPrepared =
                OriginalVertexGeometryExecutionFixture::prepareInitializedStreams(device,
                    geometryResources,
                    OriginalVertexGeometryExecutionFixture::IndexPurpose::GeometryOnly,
                    sourceQuota);
            geometryPrepared.quotas[14u] = geometryResources;
            for (std::size_t i = 0u; i < 3u; ++i)
                prefix[13u + i] = geometryPrepared.initialized[i]->originalLease();
        }
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = compute ? 8u : 12u;
        admission.maxReferences = compute ? 2u : 8u;
        admission.maxInputDescriptors = compute ? 2u : 8u;
        admission.maxPassColorAttachments = compute ? 0u : 1u;
        admission.maxPassAttachmentReferences = compute ? 0u : 1u;
        admission.maxStateObservations = compute ? 0u : 14u;
        admission.maxStateTransactions = compute ? 0u : 1u;
        admission.maxTextureRanges = compute ? 0u : 14u;
        admission.maxGraphOwners = 1u + Contract::retainedOwnerSlots;
        if (contentCells) {
            admission.maxCommands = 21u;
            admission.maxReferences = admission.maxInputDescriptors = 13u;
            admission.maxPassAttachmentReferences = 4u;
            admission.maxStateObservations = admission.maxTextureRanges = 17u;
            admission.maxGraphOwners +=
                3u * rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount + 4u;
            // Author cold command/custody ceilings before Source admission:13 source
            // payloads, each with its payload owner and up to3 scoped quotas;
            // closure, loss subscription, stream and frozen dependency owners.
            admission.maxNativePayloadObjects = 15u; // resources13 + encoder1 + command buffer1
            admission.maxTypedNativeCommandCalls = 15u;
            admission.maxPendingSubmissions = 1u;
            admission.maxCallbackTickets = admission.maxCallbackRegistrations = 8u;
            admission.maxGraphOwners += 13u * (1u + Contract::quotaCount) + 4u;
            admission.maxGraphOwners +=
                rhi_webgpu::WebGpuOriginalTypedColdAdmission::additionalOwners
                + rhi::OriginalTypedNativeColdCommandContract::additionalOwners;
            if (reserveExecution)
                admission.maxGraphOwners +=
                    rhi::OriginalTypedCallbackCustodyContract::additionalOwners({2u,
                        true,
                        rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                        1u,
                        8u,
                        1u,
                        1u,
                        bytes,
                        bytes})
                    + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
            admission.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)] = bytes;
            admission.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = bytes;
        }
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Source)] = bytes;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Graph)] = bytes;
        admission.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)] = compute ? 0u : bytes;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] =
            compute ? 3u : 4u;
        admission
            .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] =
            compute        ? 3u
            : contentCells ? 7u
                           : 4u;
        admission.maxSourceOwners[static_cast<std::size_t>(
            rhi::CommandReferenceDomain::DescriptorTable)] = 1u;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            1u;
        if (retainedReadback) {
            admission.maxCommands = 25u;
            admission.maxReferences = 16u;
            admission.maxInputDescriptors = 15u;
            admission.maxStateObservations = 20u;
            admission.maxTextureRanges = 19u;
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                1u;
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Readback)] =
                1u;
            admission.maxGraphOwners += rhi::OriginalCommandBufferLease::retainedOwnerCount
                + rhi::OriginalCommandTextureReadbackSource::retainedOwnerCount
                + correspondence.maxNestedOwners + 1u;
            admission.maxMipMappings = 1u;
            // Inputs retain the authored scalar label and the readback input label.
            admission.maxLabelBytes = 2u * OriginalDepthTableContentPlanFixture::readbackLabelBytes;
            admission.maxUnpublishedResultBytes = admission.maxNativeStagingBytes = 512u;
            admission.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Result)] = bytes;
            ++admission.maxNativePayloadObjects;
            // This authored mixed program has one immutable mip copy in
            // addition to its15 shader/attachment native calls. Admission must
            // reject the previous15-call ceiling; no runtime quota is enlarged.
            admission.maxTypedNativeCommandCalls = 16u;
            if (OriginalDepthTableContentPlanFixture::mixedResultsRequested) {
                // One unique result adds map1 + scope3 before Source creation.
                admission.maxCallbackTickets = admission.maxCallbackRegistrations = 12u;
                admission.maxGraphOwners +=
                    rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 1u
                    + 4u; // Independently owned map/scope slots, authored before Source admission.
            }
            if (OriginalDepthTableContentPlanFixture::mixedIssuersRequested)
                admission.maxGraphOwners +=
                    rhi::OriginalTypedCallbackCustodyContract::additionalOwners({3u,
                        true,
                        rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                        1u,
                        8u,
                        1u,
                        1u,
                        bytes,
                        bytes})
                    + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
        }
        if (geometry) {
            admission.maxCommands = 31u;
            admission.maxReferences = 22u;
            admission.maxInputDescriptors = 21u;
            admission.maxStateObservations = 23u;
            admission.maxTextureRanges = 19u;
            // Closure retains17 resource/layout/pipeline payloads. Cold5 must
            // additionally reserve the real encoder and command buffer.
            admission.maxNativePayloadObjects = 19u;
            admission.maxTypedNativeCommandCalls = 19u;
            admission
                .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
                4u;
            admission.maxGraphOwners += 3u
                * (rhi_webgpu::WebGpuCanonicalBufferUploadSource::initializedIndexOwnerBound
                    + rhi::OriginalResourceRegistrationContract::perResourceOwnerCount + 1u);
        }
        auto sources = Sources::prepareColdOriginalTyped(
            prefix.data(), prefixCount, admission, {2u, true, 2u}, sourceQuota, id, correspondence);
        if (!sources)
            std::fprintf(stderr, "table publication Sources: %s\n", id ? id : "missing");
        assert(sources && !id);
        auto pipelineSource = PipelineSource::prepareOriginal(pipeline,
            {1u,
                true,
                PipelineSource::kind,
                pipeline->originalResourceRegistration().generation(),
                bytes,
                PipelineSource::exclusiveOwnerCount},
            sourceQuota,
            device,
            id);
        if (!pipelineSource)
            std::printf("TABLE_PUBLICATION_PIPELINE_SOURCE_DIAGNOSTIC %s\n", id ? id : "missing");
        assert(pipelineSource && !id);
        // Expansion inputs own their complete program, with no ordinary-module
        // child owners. The same identity must survive CPU owner projection.
        assert(core::SharedOwnerProjection::sameOwner(
            pipelineSource->cpuOwner(6u), [&]() -> std::shared_ptr<const void> {
                if constexpr (compute)
                    return pipeline->originalComputeCreation()->originalInput()->tableExpansion();
                else
                    return pipeline->originalGraphicsCreation()->originalInput()->tableExpansion();
            }()));
        if constexpr (!compute)
            assert(!pipelineSource->cpuOwner(8u) && !pipelineSource->cpuOwner(9u));
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{sourceQuota}, [&]() noexcept {
                return Sources::appendOriginalTyped(
                    sources, Sources::Source{pipelineSource}, device);
            }));
        const rhi::OriginalCommandDescriptorSourceBudget sourceBudget{
            2u, true, 3u, 3u, TableSource::canonicalExclusiveRetainedOwnerCount, bytes};
        const auto freeze = [&](const auto& input, const auto& budget) {
            return TableSource::prepareOriginal(candidate,
                arena,
                sources,
                input.data(),
                input.size(),
                budget,
                sourceQuota,
                device,
                id);
        };
        auto invalid = elements;
        ++invalid[1].registrationGeneration;
        assert(!freeze(invalid, sourceBudget) && candidate && id);
        auto tooSmall = sourceBudget;
        --tooSmall.maxLogicalElements;
        assert(!freeze(elements, tooSmall) && candidate && id);
        {
            core::CpuMemoryQuotaLease pressure(
                sourceQuota, sourceQuota->limitBytes() - sourceQuota->liveBytes());
            assert(pressure.charged() && !freeze(elements, sourceBudget) && candidate);
            is(id, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-ALLOCATION");
        }
        const auto originalCharge = core::inspectCpuAllocationContaining(candidate.get());
        auto source = freeze(elements, sourceBudget);
        if (!source)
            std::printf("TABLE_PUBLICATION_SOURCE_DIAGNOSTIC %s\n", id ? id : "missing");
        assert(source && !id && !candidate && source->originalObject() == &table);
        const auto adoptedCharge = core::inspectCpuAllocationContaining(source->originalObject());
        assert(originalCharge.found
            && adoptedCharge.allocationIdentity == originalCharge.allocationIdentity
            && adoptedCharge.quota == originalCharge.quota);
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{sourceQuota}, [&]() noexcept {
                return Sources::appendOriginalTyped(sources, Sources::Source{source}, device);
            }));
        assert(sources->sources().size() == prefixCount + 2u);
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{sourceQuota}, [&]() noexcept {
                return Sources::validateRegisteredLifetime(sources, device);
            }));
        std::uint32_t owners = 0u;
        const auto* ownerDiagnostic =
            CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{sourceQuota}, [&]() noexcept {
                return sources->cpuOwnerCount(2u, admission.maxGraphOwners, owners);
            });
        if (ownerDiagnostic) {
            std::fprintf(stderr,
                "TABLE_PUBLICATION_OWNER_DIAGNOSTIC %s ceiling%u owners%u\n",
                ownerDiagnostic,
                admission.maxGraphOwners,
                owners);
            for (std::size_t slot = 0u; slot < PipelineSource::cpuOwnerSlots; ++slot) {
                const std::shared_ptr<const void> owner = slot == 0u
                    ? std::shared_ptr<const void>(pipelineSource)
                    : pipelineSource->cpuOwner(slot);
                std::fprintf(stderr,
                    "TABLE_PUBLICATION_OWNER_SLOT %llu pointer%p refs%ld\n",
                    static_cast<unsigned long long>(slot),
                    owner.get(),
                    static_cast<long>(owner.use_count()));
            }
        }
        assert(!ownerDiagnostic);
        assert(owners > 2u);
        std::uint32_t refusedOwners = 0u;
        is(sources->cpuOwnerCount(2u, owners - 1u, refusedOwners),
            "RHI-ORIGINAL-USAGE-OWNER-CAPACITY");
        std::printf(
            "ORIGINAL_TABLE_PUBLICATION_CANONICAL_OWNERS_OK %s owners%u same-accepted-pipeline original-group source2 exact-less hot0\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "Compute" : "Graphics",
            owners);
        assert(source->frozenContentRevision() == revision + 1u);
        for (const auto& element : source->frozenElements())
            assert(!element.observation.diagnostic()
                && element.observation.revision() == revision + 1u);
        StreamRetained typedStream;
        std::shared_ptr<const rhi::OriginalCommandInputs> inputs;
        OriginalDepthTableContentPlanFixture::NativeCustody nativeCustody;
        if (contentCells) {
            typedStream = OriginalDepthTableContentPlanFixture::verify(sources,
                device,
                graphQuota,
                retainedReadback,
                OriginalDepthTableContentPlanFixture::mixedCompletionRequested ? &nativeCustody
                                                                               : nullptr,
                geometry ? &geometryPrepared.initialized : nullptr);
            inputs = typedStream.graph->originalInputs();
        } else
            inputs = verifyPositional(
                sources, pipeline, equivalentPipeline, device, graphQuota, typedStream);
        if (contentCells && !retainedReadback)
            nativeCustody = OriginalDepthTableContentPlanFixture::verifyCustody(
                typedStream, device, reserveExecution);
        std::shared_ptr<core::CpuMemoryQuota> stateQuota;
        std::shared_ptr<const rhi::OriginalCommandResourceStates> states;
        if constexpr (!compute) {
            if (contentCells)
                states = typedStream.graph;
            else {
                stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
                states = verifyStates(inputs, device, stateQuota, 1u);
            }
        }
        // The canonical source must see the same pending-content and native
        // identity gates as receipts, and recover only after genuine scope drain.
        assert(!begin(writes, request, q));
        assert(Sources::validateRegisteredLifetime(sources, device));
        assert(inputs->validate());
        OriginalDepthTableContentPlanFixture::verifyUnavailable(nativeCustody, inputs->validate());
        verifyConsumptionUnavailable(inputs, device);
        if (states)
            is(states->validate(), inputs->validate());
        if (typedStream.program)
            is(typedStream.program->validate(), inputs->validate());
        verifyUsageUnavailable(typedStream, device);
        is(table.cancelOriginalDepthTablePublication(),
            "RHI-ORIGINAL-TABLE-PUBLICATION-CALLBACK-DRAIN");
        assert(WebGpuAsyncWaitFixture::until([&] {
            return table.originalDepthTablePublicationScopesDrained();
        }));
        assert(!table.cancelOriginalDepthTablePublication());
        assert(!Sources::validateRegisteredLifetime(sources, device));
        assert(!inputs->validate());
        if (states)
            assert(!states->validate());
        if (typedStream.program)
            assert(!typedStream.program->validate());
        if (nativeCustody.closure)
            assert(!nativeCustody.closure->validate() && !nativeCustody.dependencies->validate());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{sourceQuota}, [&]() noexcept -> const char* {
                const auto saved = native->bindings[1].write.resource;
                native->bindings[1].write.resource = {};
                assert(Sources::validateRegisteredLifetime(sources, device));
                assert(inputs->validate());
                OriginalDepthTableContentPlanFixture::verifyUnavailable(
                    nativeCustody, inputs->validate());
                verifyConsumptionUnavailable(inputs, device);
                if (states)
                    is(states->validate(), inputs->validate());
                if (typedStream.program)
                    is(typedStream.program->validate(), inputs->validate());
                verifyUsageUnavailable(typedStream, device);
                native->bindings[1].write.resource = saved;
                if (states)
                    assert(!states->validate());
                if (typedStream.program)
                    assert(!typedStream.program->validate());
                return inputs->validate();
            }));
        // Genuine rejected GPU bind-group creation must preserve the previous
        // group/content and retain uncertain candidate storage through scopes.
        EM_ASM({
            // clang-format off
                Module.tablePublicationFailure = true;
            // clang-format on
        });
        assert(!begin(writes, request, q));
        EM_ASM({
            // clang-format off
                Module.tablePublicationFailure = false;
            // clang-format on
        });
        assert(table.contentRevision() == revision + 1u && table.stats().writtenDescriptors == 3u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return table.originalDepthTablePublicationScopesDrained();
        }));
        is(table.finishOriginalDepthTablePublication(),
            "RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-SCOPE");
        assert(!table.cancelOriginalDepthTablePublication());
        assert(!receipts[0]->diagnostic() && table.contentRevision() == revision + 1u);
        // Caller-owned inputs can disappear; the committed candidate retains
        // the exact original views, parents and resource allocation ownership.
        changed = {};
        writes = {};
        prefix = {};
        attachments = {};
        source.reset();
        pipelineSource.reset();
        assert(!table.originalDepthTableContentDiagnostic());
        assert(!Sources::validateRegisteredLifetime(sources, device));
        if (nativeCustody.closure)
            assert(!nativeCustody.closure->validate() && !nativeCustody.dependencies->validate());
        if (!keepCurrent) {
            arena->reset();
            assert(table.originalDepthTableContentDiagnostic() && receipts[0]->diagnostic());
            assert(Sources::validateRegisteredLifetime(sources, device));
            assert(inputs->validate());
            OriginalDepthTableContentPlanFixture::verifyUnavailable(
                nativeCustody, inputs->validate());
            verifyConsumptionUnavailable(inputs, device);
            if (states)
                is(states->validate(), inputs->validate());
            if (typedStream.program)
                is(typedStream.program->validate(), inputs->validate());
            verifyUsageUnavailable(typedStream, device);
        }
        std::printf(
            "ORIGINAL_TABLE_PUBLICATION_SOURCE2_OK %s adopted-original canonical-inventory parent-view-leases same-revision quota-refusal hot0 caller-drop arena-reset no-command-authority\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "Compute" : "Graphics");
        std::printf(
            "ORIGINAL_TABLE_PUBLICATION_NATIVE_OK %s ordered3 atomic-revision scalar-group real-scopes same-revision-receipts caller-drop quota-exhaustion cancel mutation reset hot0 commands-and-public-execution-closed\n",
            std::is_same_v<Pipeline, rhi::ComputePipeline> ? "Compute" : "Graphics");
        return {{q[0],
                    q[1],
                    q[2],
                    sourceQuota,
                    graphQuota,
                    stateQuota,
                    typedStream.stateQuota,
                    typedStream.quota,
                    typedStream.recordingQuota,
                    contentQuota,
                    nativeCustody.submissionQuota,
                    nativeCustody.callbackQuota},
            std::move(sources),
            std::move(inputs),
            std::move(states),
            std::move(typedStream),
            std::move(nativeCustody),
            std::move(geometryPrepared.quotas)};
#else
        return {};
#endif
    }
};
} // namespace arp::test
