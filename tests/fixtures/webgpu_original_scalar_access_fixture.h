#pragma once
#include "rhi/backends/webgpu/webgpu_original_scalar_access_validation.h"
#include "webgpu_original_command_inputs_fixture.h"
#include "webgpu_original_compute_batch_fixture.h"
#include "webgpu_original_graphics_batch_fixture.h"
#include "webgpu_original_graphics_stream_fixture.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalScalarAccessFixture final {
    using Groups = rhi_webgpu::WebGpuOrdinaryPhysicalGroups;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;
    using Validation = rhi_webgpu::WebGpuOriginalScalarAccessValidation;

public:
    static void verifyContract()
    {
        WebGpuOriginalComputeBatchFixture::verifyContract();
        WebGpuOriginalGraphicsStreamFixture::verifyContract();
        WebGpuOriginalGraphicsBatchFixture::verifyContract();
        WebGpuOriginalTransferStateValidationFixture::verifyContract();
        Groups::OriginalBufferBinding output;
        output.sizeBytes = UINT64_MAX;
        assert(std::string_view(Groups::originalBufferBinding({}, 0u, 0u, output))
            == "RHI-WEBGPU-ORIGINAL-BUFFER-GROUP-OWNER");
        assert(output.sizeBytes == UINT64_MAX && !output.resource);
        assert(std::string_view(Validation::compute({}, {}, 0u))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-STATE-OWNER");
        assert(std::string_view(Validation::graphics({}, {}, 0u))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-STATE-OWNER");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verifyPhysical(const std::shared_ptr<const Groups>& groups, const char* name)
    {
        Groups::OriginalBufferBinding output;
        output.sizeBytes = UINT64_MAX;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto forged = std::shared_ptr<const Groups>(groups->pipeline(), groups.get());
        assert(std::string_view(Groups::originalBufferBinding(forged, 0u, 0u, output))
            == "RHI-WEBGPU-ORIGINAL-BUFFER-GROUP-OWNER");
        assert(output.sizeBytes == UINT64_MAX);
        assert(std::string_view(Groups::originalBufferBinding(groups, UINT32_MAX, 0u, output))
            == "RHI-WEBGPU-ORIGINAL-BUFFER-SLOT");
        assert(std::string_view(Groups::originalBufferBinding(groups, 0u, UINT32_MAX, output))
            == "RHI-WEBGPU-ORIGINAL-BUFFER-SLOT");
        for (const auto& fact : groups->pipeline()->contract().facts()) {
            assert(!Groups::originalBufferBinding(
                groups, fact.mapping.webGpuGroup, fact.mapping.webGpuBinding, output));
            assert(output.resource && output.mapping == fact.mapping
                && output.type == fact.physicalType);
            assert(output.sizeBytes == output.resource->backing().bufferDesc.sizeBytes
                && !output.offsetBytes);
            auto native = output.resource->backing().buffer;
            const auto originalSize = native->sizeBytes;
            ++native->sizeBytes;
            assert(Groups::originalBufferBinding(
                groups, fact.mapping.webGpuGroup, fact.mapping.webGpuBinding, output));
            assert(output.sizeBytes == originalSize);
            --native->sizeBytes;
            assert(!Groups::originalBufferBinding(
                groups, fact.mapping.webGpuGroup, fact.mapping.webGpuBinding, output));
        }
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_BUFFER_BINDING_OK %s original mapping owner range mutation allocation0\n",
            name);
    }

    static void verifyCompute(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryScalarComputeRange>& original)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto stateQuota = core::makeShared<core::CpuMemoryQuota>(16384u);
        auto groups = original->originalGroups();
        auto pipeline = groups->pipeline();
        assert(groups->observedSets().size() == 1u);
        const auto& set = groups->observedSets()[0];
        const std::array<WebGpuCommandReference, 2> refs{
            {{CommandReferenceDomain::Pipeline, WebGpuCommandReferenceSource::Pipeline},
                {CommandReferenceDomain::Descriptor,
                    WebGpuCommandReferenceSource::PhysicalGroups}}};
        WebGpuGenericCommandProjectionBudget graphBudget{1u,
            true,
            0u,
            0u,
            1u,
            1u,
            2u,
            1u,
            {},
            quota->limitBytes(),
            groups->retainedAllocationBytes()};
        for (const auto& ref : refs)
            ++graphBudget.maxDomainReferences[static_cast<std::size_t>(ref.domain)];
        WebGpuGenericCommandProjection::Sources sources;
        sources.pipelines = &pipeline;
        sources.pipelineCount = 1u;
        sources.groups = &groups;
        sources.groupCount = 1u;
        const char* diagnostic = nullptr;
        auto graph =
            WebGpuGenericCommandProjection::shareOriginal(WebGpuGenericCommandProjection::prepare(
                std::move(sources), refs.data(), refs.size(), graphBudget, quota, diagnostic));
        assert(graph);
        const std::array<CommandUsage, 6> commands{{{K::BeginComputePass, 0u, 0u},
            {K::BindComputePipeline, 0u, 1u},
            {K::BindComputeDescriptor, 1u, 1u},
            {K::Dispatch, 2u, 0u},
            {K::EndComputePass, 2u, 0u},
            {K::Finish, 2u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 6u, 2u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(usage);
        const auto& offsets = set.observation->offsets();
        char label[] = "original compute frozen label \xE4\xB8\xAD";
        std::array<Store::Input, 6> raw{{{*A::capture(K::BeginComputePass, A::None{}),
                                             std::string_view(label, sizeof(label) - 1u)},
            {*A::capture(K::BindComputePipeline, A::None{})},
            {*A::capture(K::BindComputeDescriptor, A::DescriptorBinding{set.logicalSet}),
                {},
                offsets.data(),
                static_cast<std::uint32_t>(offsets.size())},
            {*A::capture(K::Dispatch, A::Dispatch{1u, 1u, 1u})},
            {*A::capture(K::EndComputePass, A::None{})},
            {*A::capture(K::Finish, A::None{})}}};
        const auto inputs = [&](const std::shared_ptr<const WebGpuGenericCommandUsage>&
                                    selectedUsage) {
            auto arguments = Store::prepareOriginal(raw.data(),
                raw.size(),
                {1u,
                    true,
                    6u,
                    static_cast<std::uint32_t>(offsets.size()),
                    sizeof(label) - 1u,
                    0u,
                    quota->limitBytes()},
                quota,
                diagnostic);
            assert(arguments);
            auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(selectedUsage,
                arguments,
                {1u, true, 6u, 0u, quota->limitBytes()},
                quota,
                diagnostic);
            assert(scalars);
            return WebGpuOriginalCommandInputs::prepareOriginal(
                scalars, {}, nullptr, 0u, {1u, true, 0u, quota->limitBytes()}, quota, diagnostic);
        };
        auto plan = inputs(usage);
        assert(plan);
        auto range = original;
        verifyComputeNegativeInputs(graph,
            usage,
            range,
            raw,
            static_cast<std::uint32_t>(offsets.size()),
            sizeof(label) - 1u,
            quota);
        verifyState(plan, stateQuota, range, 3u, "compute", quota);
        label[0] = 'X';
        raw[3].scalar = *A::capture(K::Dispatch, A::Dispatch{2u, 1u, 1u});
        WebGpuOriginalComputeBatchFixture::verify(plan, range);
        raw[3].scalar = *A::capture(K::Dispatch, A::Dispatch{2u, 1u, 1u});
        auto invalid = inputs(usage);
        auto states = States::prepareOriginal(
            invalid, {1u, true, 1u, 1u, stateQuota->limitBytes()}, stateQuota, diagnostic);
        assert(states);
        assert(std::string_view(Validation::compute(states, range, 3u))
            == "SHADER-COMPUTE-INDIRECT-PRODUCER-DISPATCH");
        states.reset();
        invalid.reset();
        verifyComputeClone(plan, range, raw, graphBudget, quota, stateQuota);
    }

    static void verifyGraphics(
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange>& range)
    {
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        verifyState(WebGpuOriginalCommandInputsFixture::originalGraphicsInputs(),
            quota,
            range,
            5u,
            "graphics");
        WebGpuOriginalGraphicsStreamFixture::verify(
            WebGpuOriginalCommandInputsFixture::originalGraphicsInputs(), range);
        WebGpuOriginalGraphicsBatchFixture::verify(
            WebGpuOriginalCommandInputsFixture::originalGraphicsInputs(), range);
    }

    static void verifyActualDeviceLoss()
    {
        using namespace rhi_webgpu;
        WebGpuOriginalGraphicsBatchFixture::verifyActualDeviceLoss();
        WebGpuOriginalGraphicsStreamFixture::verifyActualDeviceLoss();
        assert(lossCount_ == 2u && webGpuNativeDeviceLost());
        for (auto& loss : loss_) {
            assert(loss.states
                && std::string_view(loss.states->validate())
                    == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
            const auto* id = std::visit(
                [&](const auto& range) {
                    return selected(loss.states, range, loss.index);
                },
                loss.range);
            assert(id && std::string_view(id) == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
            loss.states.reset();
            loss.range = std::shared_ptr<const WebGpuOrdinaryScalarComputeRange>{};
            assert(!loss.quota->liveBytes());
            if (loss.inputQuota)
                assert(!loss.inputQuota->liveBytes());
            loss = {};
        }
        lossCount_ = 0u;
        std::puts(
            "BINDLESS_RHI_ORIGINAL_SCALAR_ACCESS_LOSS_OK compute graphics source-reject retire");
    }

private:
    static void verifyComputeClone(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs>& plan,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryScalarComputeRange>& range,
        std::array<rhi::CommandVariableArguments::Input, 6>& raw,
        const rhi_webgpu::WebGpuGenericCommandProjectionBudget& graphBudget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        const char* diagnostic = nullptr;
        auto groups = range->originalGroups();
        auto pipeline = groups->pipeline();
        const auto& set = groups->observedSets()[0];
        const auto& offsets = set.observation->offsets();
        const auto& usage = *plan->scalars()->usage();
        const auto& graph = *usage.projection();
        const auto& commands = usage.commands();
        // Actual second cold group from the same original pipeline/descriptor,
        // not an aliasing shared_ptr or mutable metadata-issued native owner.
        auto cloneQuota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const auto& issued = pipeline->contract();
        WebGpuOrdinaryPhysicalGroupBudget cloneBudget{1u,
            true,
            1u,
            static_cast<std::uint32_t>(issued.bindings().size()),
            1u,
            groups->groupCount(),
            groups->groupCount(),
            groups->groupCount(),
            groups->bufferBindingBytes(),
            groups->retainedAllocationBytes(),
            cloneQuota->limitBytes()};
        std::shared_ptr<const Groups> clone = Groups::shareOriginal(
            Groups::prepare(pipeline, &set, 1u, cloneBudget, cloneQuota, diagnostic));
        assert(clone && !clone->validate());
        WebGpuGenericCommandProjection::Sources cloneSources;
        cloneSources.pipelines = &pipeline;
        cloneSources.pipelineCount = 1u;
        cloneSources.groups = &clone;
        cloneSources.groupCount = 1u;
        auto cloneGraph = WebGpuGenericCommandProjection::shareOriginal(
            WebGpuGenericCommandProjection::prepare(std::move(cloneSources),
                graph.references().data(),
                graph.references().size(),
                graphBudget,
                quota,
                diagnostic));
        assert(cloneGraph);
        auto cloneUsage = WebGpuGenericCommandUsage::prepareOriginal(cloneGraph,
            commands.data(),
            commands.size(),
            {1u, true, 6u, 2u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(cloneUsage);
        raw[3].scalar = *A::capture(K::Dispatch, A::Dispatch{1u, 1u, 1u});
        auto cloneArguments = Store::prepareOriginal(raw.data(),
            raw.size(),
            {1u,
                true,
                6u,
                static_cast<std::uint32_t>(offsets.size()),
                raw[0].text.size(),
                0u,
                quota->limitBytes()},
            quota,
            diagnostic);
        auto cloneScalars = WebGpuFrozenCommandScalars::prepareOriginal(
            cloneUsage, cloneArguments, {1u, true, 6u, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(cloneScalars);
        auto cloneInputs = WebGpuOriginalCommandInputs::prepareOriginal(
            cloneScalars, {}, nullptr, 0u, {1u, true, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(cloneInputs);
        auto states = States::prepareOriginal(
            cloneInputs, {1u, true, 1u, 1u, stateQuota->limitBytes()}, stateQuota, diagnostic);
        assert(states);
        assert(std::string_view(Validation::compute(states, range, 3u))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-DESCRIPTOR");
        states.reset();
        cloneInputs.reset();
        cloneScalars.reset();
        cloneArguments.reset();
        cloneUsage.reset();
        cloneGraph.reset();
        clone.reset();
        assert(!cloneQuota->liveBytes());
    }

    static void verifyComputeNegativeInputs(
        const std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandProjection>& graph,
        const std::shared_ptr<const rhi_webgpu::WebGpuGenericCommandUsage>& usage,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryScalarComputeRange>& range,
        std::array<rhi::CommandVariableArguments::Input, 6>& raw,
        std::uint32_t offsetCount,
        std::uint64_t maxText,
        const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        const char* diagnostic = nullptr;
        const auto inputs = [&] {
            auto arguments = Store::prepareOriginal(raw.data(),
                raw.size(),
                {1u, true, 6u, offsetCount, maxText, 0u, quota->limitBytes()},
                quota,
                diagnostic);
            assert(arguments);
            auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
                usage, arguments, {1u, true, 6u, 0u, quota->limitBytes()}, quota, diagnostic);
            assert(scalars);
            return WebGpuOriginalCommandInputs::prepareOriginal(
                scalars, {}, nullptr, 0u, {1u, true, 0u, quota->limitBytes()}, quota, diagnostic);
        };
        const auto originalLabel = raw[0].text;
        for (const auto badLabel : std::array<std::string_view, 3>{
                 std::string_view("a\0b", 3u), "\xED\xA0\x80", "\xE4\xB8"}) {
            raw[0].text = badLabel;
            WebGpuOriginalComputeBatchFixture::verifyRejected(
                inputs(), range, "RHI-WEBGPU-ORIGINAL-COMPUTE-LABEL-ENCODING");
        }
        raw[0].text = originalLabel;
        std::array<CommandUsage, 7> extraCommands{};
        std::copy_n(usage->commands().begin(), 5u, extraCommands.begin());
        extraCommands[5] = {K::InsertDebugMarker, 2u, 0u};
        extraCommands[6] = usage->commands()[5];
        auto extraUsage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            extraCommands.data(),
            extraCommands.size(),
            {1u, true, 7u, 2u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(extraUsage);
        std::array<Store::Input, 7> extraRaw{{raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            {*A::capture(K::InsertDebugMarker, A::None{}), "extra"},
            raw[5]}};
        auto extraArguments = Store::prepareOriginal(extraRaw.data(),
            extraRaw.size(),
            {1u, true, 7u, offsetCount, maxText + 5u, 0u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(extraArguments);
        auto extraScalars = WebGpuFrozenCommandScalars::prepareOriginal(
            extraUsage, extraArguments, {1u, true, 7u, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(extraScalars);
        auto extraInputs = WebGpuOriginalCommandInputs::prepareOriginal(
            extraScalars, {}, nullptr, 0u, {1u, true, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(extraInputs);
        WebGpuOriginalComputeBatchFixture::verifyRejected(
            extraInputs, range, "RHI-WEBGPU-ORIGINAL-COMPUTE-LANGUAGE");
        extraInputs.reset();
        extraScalars.reset();
        extraArguments.reset();
        extraUsage.reset();
    }

    template <class Range>
    static const char* selected(const std::shared_ptr<const States>& states,
        const std::shared_ptr<const Range>& range,
        std::size_t index)
    {
        if constexpr (std::is_same_v<Range, rhi_webgpu::WebGpuOrdinaryScalarComputeRange>)
            return Validation::compute(states, range, index);
        else
            return Validation::graphics(states, range, index);
    }

    template <class Range>
    static void verifyState(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs>& inputs,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const std::shared_ptr<const Range>& range,
        std::size_t index,
        const char* name,
        std::shared_ptr<core::CpuMemoryQuota> inputQuota = {})
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return States::prepareOriginal(
                inputs, {1u, true, 2u, 2u, quota->limitBytes()}, quota, diagnostic);
        };
        Groups::OriginalBufferBinding binding;
        const auto& fact = range->originalGroups()->pipeline()->contract().facts()[0];
        assert(!Groups::originalBufferBinding(range->originalGroups(),
            fact.mapping.webGpuGroup,
            fact.mapping.webGpuBinding,
            binding));
        auto native = binding.resource->backing().buffer;
        const auto initial = native->recordedState().state();
        assert(!native->recordState(ResourceState::CopyDst));
        auto states = factory();
        assert(states);
        assert(std::string_view(selected(states, range, index)) == "RHI-ACCESS-STATE-MISMATCH");
        states.reset();
        const auto required = std::is_same_v<Range, WebGpuOrdinaryScalarComputeRange>
            ? ResourceState::ShaderWrite
            : ResourceState::ShaderRead;
        assert(!native->recordState(required));
        states = factory();
        assert(states);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!selected(states, range, index));
        assert(selected(states, range, UINT32_MAX) && selected(states, range, 0u));
        auto forgedRange = std::shared_ptr<const Range>(quota, range.get());
        assert(std::string_view(selected(states, forgedRange, index))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-RANGE-OWNER");
        auto forgedState = std::shared_ptr<const States>(quota, states.get());
        assert(std::string_view(selected(forgedState, range, index))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-STATE-OWNER");
        core::MutationJournal journal;
        assert(!native->recordState(required, &journal));
        assert(std::string_view(selected(states, range, index))
            == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        journal.rollback();
        assert(
            std::string_view(selected(states, range, index)) == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        forgedState.reset();
        states.reset();
        assert(!native->recordState(initial));
        states = factory();
        assert(states);
        assert(lossCount_ < loss_.size());
        loss_[lossCount_++] = {states, range, quota, index, std::move(inputQuota)};
        std::printf(
            "BINDLESS_RHI_ORIGINAL_SCALAR_ACCESS_OK %s exact resource state scalar pending rollback owner allocation0\n",
            name);
    }

    struct LossProbe {
        std::shared_ptr<const States> states;
        std::variant<std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryScalarComputeRange>,
            std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange>>
            range;
        std::shared_ptr<core::CpuMemoryQuota> quota;
        std::size_t index;
        std::shared_ptr<core::CpuMemoryQuota> inputQuota;
    };

    inline static std::array<LossProbe, 2> loss_{};
    inline static std::size_t lossCount_ = 0u;
#endif
};
} // namespace arp::test
