#pragma once
#include "core/encoding/unsigned_element_span.h"
#include "rhi/frontend/visibility/gpu_visibility_consumer_alias.h"
#include "rhi/frontend/visibility/gpu_visibility_root_producer.h"

namespace arp::rhi::visibility {
// Push16 belongs to each physical indirect record. Shader consumers use the
// produced Mapping16 and the original cascade slice, never a CPU instance base.
struct GpuVisibilityDrawSelection {
    std::uint32_t cascade = 0u, group = 0u, reserved0 = 0u, reserved1 = 0u;
};

static_assert(std::is_standard_layout_v<GpuVisibilityDrawSelection>
    && sizeof(GpuVisibilityDrawSelection) == 16u
    && offsetof(GpuVisibilityDrawSelection, group) == 4u
    && offsetof(GpuVisibilityDrawSelection, reserved0) == 8u
    && offsetof(GpuVisibilityDrawSelection, reserved1) == 12u);

class GpuVisibilityRootConsumerContract final {
public:
    using Producer = GpuVisibilityRootProducerContract;
    using Root = Producer::Root;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxCommandVisits = 0u, maxDeclarationVisits = 0u;
    };

    struct Report {
        const char* diagnostic = nullptr;
        std::uint64_t draws = 0u, bindings = 0u, commandVisits = 0u, declarationVisits = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "PCG-GPU-VISIBILITY-CONSUMER-VERSION";
        if (!b.optIn)
            return "PCG-GPU-VISIBILITY-CONSUMER-OPT-IN";
        return b.maxCommandVisits && b.maxDeclarationVisits ? nullptr
                                                            : "PCG-GPU-VISIBILITY-CONSUMER-BUDGET";
    }

    // Value inspection only. The original stream authenticates these exact
    // bytes before the closed native command is allowed to retain them.
    static const char* inspectSelection(const void* bytes,
        std::size_t size,
        const GpuVisibilityLayoutContract::Input& layout,
        std::uint64_t argumentOffset,
        std::uint32_t records,
        GpuVisibilityDrawSelection& output) noexcept
    {
        output = {};
        if (!bytes || size != sizeof(GpuVisibilityDrawSelection) || records != 1u
            || !layout.groupCapacity || !layout.groupCount
            || layout.groupCount > layout.groupCapacity
            || (layout.cascades != 1u && layout.cascades != 3u))
            return "PCG-GPU-VISIBILITY-CONSUMER-SELECTION";
        const core::UnsignedElementSpan words(bytes,
            size,
            core::UnsignedElementSpan::Width::Uint32,
            core::UnsignedElementSpan::ByteOrder::LittleEndian);
        GpuVisibilityDrawSelection value;
        if (!words.read(0u, value.cascade) || !words.read(1u, value.group)
            || !words.read(2u, value.reserved0) || !words.read(3u, value.reserved1)
            || value.reserved0 || value.reserved1 || value.cascade >= layout.cascades
            || value.group >= layout.groupCount)
            return "PCG-GPU-VISIBILITY-CONSUMER-SELECTION";
        const auto slot = std::uint64_t(value.cascade) * layout.groupCapacity + value.group;
        if (argumentOffset != slot * sizeof(rhi::DrawIndexedIndirectCommand))
            return "PCG-GPU-VISIBILITY-CONSUMER-RECORD";
        output = value;
        return nullptr;
    }

    static Report inspect(const Producer::Roots& producers,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& stream,
        const GpuVisibilityLayoutContract::Input& layout,
        const GpuVisibilityLayoutContract::Budget& layoutBudget,
        std::uint32_t queriedWorkgroupLimit,
        const Producer::Budget& producerBudget,
        const Budget& budget) noexcept
    {
        using namespace rhi;
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (const auto* id = validateBudget(budget))
            return reject(id);
        const auto producer = Producer::inspect(producers,
            inventory,
            graph,
            stream,
            layout,
            layoutBudget,
            queriedWorkgroupLimit,
            producerBudget);
        if (producer.diagnostic)
            return reject(producer.diagnostic);
        if (producers[0u]->authorization()->package().gpuVisibilityConsumerContractVersion() != 2u)
            return reject("PCG-GPU-VISIBILITY-CONSUMER-ALGORITHM");
        const auto& usage = *stream->originalInputs()->originalScalars()->originalUsage();
        const auto& arguments = *stream->originalInputs()->originalScalars()->originalArguments();
        Report result;
        const Root* selected = nullptr;
        const void* selection = nullptr;
        std::size_t selectionBytes = 0u;
        bool render = false;
        for (const auto& op : stream->operations()) {
            if (result.commandVisits == budget.maxCommandVisits)
                return reject("PCG-GPU-VISIBILITY-CONSUMER-COMMAND-CAPACITY");
            ++result.commandVisits;
            using K = CommandUsageKind;
            if (op.kind == K::BeginRenderPass || op.kind == K::EndRenderPass) {
                render = op.kind == K::BeginRenderPass;
                selected = nullptr;
                selection = nullptr;
            } else if (op.kind == K::BindPreparedGraphicsBinding) {
                selected = nullptr;
                selection = nullptr;
                if (!render || op.referenceCount != 1u
                    || op.firstReference >= usage.references().size())
                    return reject("PCG-GPU-VISIBILITY-CONSUMER-BINDING");
                const auto source = usage.references()[op.firstReference].sourceIndex;
                if (source >= inventory->sources().size())
                    return reject("PCG-GPU-VISIBILITY-CONSUMER-BINDING");
                const auto* root =
                    std::get_if<std::shared_ptr<const Root>>(&inventory->sources()[source]);
                if (!root)
                    return reject("PCG-GPU-VISIBILITY-CONSUMER-BINDING");
                if (const auto* id = inspectRoot(*root,
                        producers[0u],
                        producer,
                        *inventory,
                        budget,
                        result.declarationVisits))
                    return reject(id);
                selected = root->get();
                ++result.bindings;
            } else if (op.kind == K::PushConstants) {
                const auto* push =
                    arguments.records()[op.commandIndex].scalar.get<CommandScalarArguments::Push>();
                if (!render || !selected || !push || push->stages != ShaderStage::Vertex
                    || push->offset || push->size != sizeof(GpuVisibilityDrawSelection))
                    return reject("PCG-GPU-VISIBILITY-CONSUMER-PUSH");
                selection = arguments.bytes(op.commandIndex);
                selectionBytes = arguments.records()[op.commandIndex].byteCount;
            } else if (op.kind == K::DrawIndexedIndirect) {
                const auto* draw = arguments.records()[op.commandIndex]
                                       .scalar.get<CommandScalarArguments::DrawIndirect>();
                if (!render || !selected || !draw || !selection)
                    return reject("PCG-GPU-VISIBILITY-CONSUMER-DRAW");
                GpuVisibilityDrawSelection decoded;
                if (const auto* id = inspectSelection(
                        selection, selectionBytes, layout, draw->offset, draw->count, decoded))
                    return reject(id);
                // One full block per record. Rebinding or a prior draw never
                // supplies an implicit selector for a subsequent record.
                selection = nullptr;
                ++result.draws;
            }
        }
        return result.draws == producer.indirectRecords && result.bindings
            ? result
            : reject("PCG-GPU-VISIBILITY-CONSUMER-COVERAGE");
    }

private:
    static const char* inspectRoot(const std::shared_ptr<const Root>& root,
        const std::shared_ptr<const Root>& producer,
        const Producer::Report& sources,
        const rhi::OriginalCommandResourceSources& inventory,
        const Budget& budget,
        std::uint64_t& visits) noexcept
    {
        using namespace rhi;
        if (const auto* id = Root::validateOriginal(root))
            return id;
        if (!root->matchesCanonicalSources(inventory)
            || !core::SharedOwnerProjection::sameOwner(
                root->authorization(), producer->authorization())
            || !core::SharedOwnerProjection::sameOwner(
                root->originalSnapshot(), producer->originalSnapshot()))
            return "PCG-GPU-VISIBILITY-CONSUMER-SNAPSHOT";
        const auto* binding = std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            &root->originalPrepared());
        if (!binding || !*binding)
            return "PCG-GPU-VISIBILITY-CONSUMER-PIPELINE";
        const auto& pipeline = *(*binding)->owners()->desc().pipeline;
        const auto& package = root->authorization()->package();
        const char* id = nullptr;
        ShaderStage stage;
        if (!selectBindlessGraphicsEntryAccess(package, pipeline, "visibilityVertexMain", stage, id)
            || stage != ShaderStage::Vertex)
            return id ? id : "PCG-GPU-VISIBILITY-CONSUMER-VERTEX";
        if (!selectBindlessGraphicsEntryAccess(
                package, pipeline, "visibilityFragmentMain", stage, id)
            || stage != ShaderStage::Fragment)
            return id ? id : "PCG-GPU-VISIBILITY-CONSUMER-FRAGMENT";
        constexpr std::array<std::size_t, 4u> roles{0u, 4u, 6u, 8u};
        std::array<bool, 4u> found{};
        for (const auto& element : root->elements()) {
            if (visits == budget.maxDeclarationVisits)
                return "PCG-GPU-VISIBILITY-CONSUMER-DECLARATION-CAPACITY";
            ++visits;
            for (std::size_t i = 0u; i < roles.size(); ++i) {
                const auto& table = package.artifact().tables[roles[i]];
                const auto& declaration = element.declaration;
                if (declaration.write.binding != table.firstBinding)
                    continue;
                if (found[i] || declaration.sourceIndex != sources.sources[roles[i]])
                    return "PCG-GPU-VISIBILITY-CONSUMER-RESOURCE";
                // Producer::inspect authenticated the full descriptor ranges.
                // The same selected Source and snapshot retain those exact
                // physical bindings; no alternate slice/view is accepted.
                const auto& expected = producer->elements();
                const OriginalCommandDescriptorSetElement* original = nullptr;
                for (const auto& candidate : expected) {
                    if (visits == budget.maxDeclarationVisits)
                        return "PCG-GPU-VISIBILITY-CONSUMER-DECLARATION-CAPACITY";
                    ++visits;
                    if (candidate.declaration.write.binding == table.firstBinding)
                        original = &candidate.declaration;
                }
                if (!original || declaration.dynamicOffset != original->dynamicOffset
                    || declaration.bufferAccessOffset != original->bufferAccessOffset
                    || declaration.bufferAccessBytes != original->bufferAccessBytes)
                    return "PCG-GPU-VISIBILITY-CONSUMER-RANGE";
                found[i] = true;
            }
        }
        for (const auto value : found)
            if (!value)
                return "PCG-GPU-VISIBILITY-CONSUMER-RESOURCE";
        // Consumer/2 reads exact original views. The same producer output is
        // writable only in Compute; its Vertex view must be read-only.
        using Aliases = GpuVisibilityConsumerAliasContract;
        if (root->ordinaryDomains().size() != 1u || root->ordinaryDomains()[0u].set != Aliases::set)
            return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-DOMAIN";
        const auto& aliases = root->ordinaryDomains()[0u].source->frozenElements();
        if (aliases.size() != Aliases::roles.size())
            return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-COVERAGE";
        std::array<bool, 4u> matched{};
        for (const auto& element : aliases) {
            if (visits == budget.maxDeclarationVisits)
                return "PCG-GPU-VISIBILITY-CONSUMER-DECLARATION-CAPACITY";
            ++visits;
            const auto& declaration = element.declaration;
            const auto ordinal = declaration.write.binding;
            if (ordinal >= matched.size() || matched[ordinal] || declaration.write.arrayElement
                || declaration.dynamicOffset)
                return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-COVERAGE";
            matched[ordinal] = true;
            const auto role = Aliases::roles[ordinal];
            const auto source = sources.sources[role];
            ShaderBufferMemoryRequirement memory;
            BindlessResourceAccess access;
            std::size_t actualSource = SIZE_MAX;
            if (const auto* failure = Root::originalOrdinaryBufferRequirement(root,
                    "visibilityVertexMain",
                    Aliases::set,
                    ordinal,
                    memory,
                    access,
                    actualSource,
                    2u))
                return failure;
            const auto* buffer = source < inventory.sources().size()
                ? std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                      &inventory.sources()[source])
                : nullptr;
            if (!buffer || actualSource != declaration.sourceIndex
                || declaration.bufferAccessOffset != access.bufferOffset
                || declaration.bufferAccessBytes != access.bufferSize
                || declaration.write.resourceState != ResourceState::ShaderRead
                || declaration.stages != PipelineStage::VertexShader)
                return "PCG-GPU-VISIBILITY-CONSUMER-ALIAS-RANGE";
            if (const auto* failure = Aliases::inspect(ordinal,
                    actualSource,
                    source,
                    declaration.write.type,
                    access.stages,
                    access.access,
                    access.bufferOffset,
                    access.bufferSize,
                    (*buffer)->layout().sizeBytes,
                    memory,
                    package.artifact().execution == BindlessExecution::Emulated))
                return failure;
        }
        return nullptr;
    }
};
} // namespace arp::rhi::visibility
