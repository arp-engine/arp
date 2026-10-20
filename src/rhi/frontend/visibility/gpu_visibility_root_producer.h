#pragma once
#include "rhi/frontend/bindless_root_sequence.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/visibility/gpu_visibility_consumer_alias.h"
#include "rhi/frontend/visibility/gpu_visibility_layout.h"
#include "rhi/semantics/bindless_entry_access.h"

namespace arp::rhi::visibility {
// Cold correspondence over the original Root/Inputs8/State6/stream. This
// authenticates algorithm selection, physical bindings and dispatch order. The
// native closure must additionally retain and authenticate the immutable input
// payloads and geometry fetch proof through the original completion seal.
class GpuVisibilityRootProducerContract final {
public:
    using Root = rhi::OriginalCommandBindlessRootSource;
    using Roots = std::array<std::shared_ptr<const Root>, 4u>;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxDeclarationVisits = 0u, maxCommandVisits = 0u;
    };

    struct Report {
        const char* diagnostic = nullptr;
        std::array<std::size_t, 4u> roots{SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
        std::array<std::size_t, 4u> dispatches{SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
        // Closed shader roles /1: Groups, Instances, Owners, Counts, Visible,
        // Draws, Bases, Control, Parameters, Depth. These are canonical indices,
        // not another resource inventory or native ownership graph.
        std::array<std::size_t, 10u> sources{SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX,
            SIZE_MAX};
        std::uint64_t declarationVisits = 0u, commandVisits = 0u;
        std::uint64_t indirectCalls = 0u, indirectRecords = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "PCG-GPU-VISIBILITY-ROOT-VERSION";
        if (!b.optIn)
            return "PCG-GPU-VISIBILITY-ROOT-OPT-IN";
        return b.maxDeclarationVisits && b.maxCommandVisits ? nullptr
                                                            : "PCG-GPU-VISIBILITY-ROOT-BUDGET";
    }

    static Report inspect(const Roots& roots,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const rhi::OriginalCommandResourceStates>& graph,
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& stream,
        const GpuVisibilityLayoutContract::Input& input,
        const GpuVisibilityLayoutContract::Budget& layoutBudget,
        std::uint32_t queriedWorkgroupLimit,
        const Budget& budget) noexcept
    {
        using namespace rhi;
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (const auto* id = validateBudget(budget))
            return reject(id);
        GpuVisibilityLayoutContract::Report sizes;
        if (const auto* id = GpuVisibilityLayoutContract::inspect(input, layoutBudget, sizes))
            return reject(id);
        if (const auto* id = OriginalCommandResourceSources::validateOriginal(inventory))
            return reject(id);
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(stream))
            return reject(id);
        if (const auto* id = stream->compareOriginalStates(graph))
            return reject(id);
        if (stream->originalBudget().bindlessRoots.contractVersion != 6u
            || stream->originalInputs()->originalBudget().contractVersion != 8u)
            return reject("PCG-GPU-VISIBILITY-ROOT-STREAM-PROFILE");
        const auto& originalInventory =
            stream->originalInputs()->originalScalars()->originalUsage()->originalSources();
        if (!core::SharedOwnerProjection::sameOwner(originalInventory, inventory))
            return reject("PCG-GPU-VISIBILITY-ROOT-INVENTORY");
        Report report;
        constexpr std::array<core::StringView, 4u> entries{"visibilityResetComputeMain",
            "visibilityCullComputeMain",
            "visibilityEmitComputeMain",
            "visibilityFinalizeComputeMain"};
        const std::array<std::uint64_t, 9u> requiredBytes{sizes.drawGroupBytes,
            sizes.inputInstanceBytes,
            sizes.ownerMapBytes,
            sizes.countBytes,
            sizes.visibleInstanceBytes,
            sizes.indirectBytes,
            sizes.zeroBaseMappingBytes,
            sizes.controlBytes,
            sizes.parameterBytes};
        constexpr std::array<DescriptorType, 9u> types{DescriptorType::ReadOnlyStorageBuffer,
            DescriptorType::ReadOnlyStorageBuffer,
            DescriptorType::ReadOnlyStorageBuffer,
            DescriptorType::StorageBuffer,
            DescriptorType::StorageBuffer,
            DescriptorType::StorageBuffer,
            DescriptorType::StorageBuffer,
            DescriptorType::StorageBuffer,
            DescriptorType::UniformBuffer};
        for (std::size_t phase = 0u; phase < roots.size(); ++phase) {
            const auto& root = roots[phase];
            if (const auto* id = Root::validateOriginal(root))
                return reject(id);
            report.roots[phase] = inventory->originalLeaseIndex(root);
            if (report.roots[phase] == SIZE_MAX || !root->matchesCanonicalSources(*inventory))
                return reject("PCG-GPU-VISIBILITY-ROOT-INVENTORY");
            if (!core::SharedOwnerProjection::sameOwner(
                    root->authorization(), roots[0u]->authorization())
                || !core::SharedOwnerProjection::sameOwner(
                    root->originalSnapshot(), roots[0u]->originalSnapshot()))
                return reject("PCG-GPU-VISIBILITY-ROOT-SNAPSHOT");
            for (std::size_t prior = 0u; prior < phase; ++prior)
                if (report.roots[prior] == report.roots[phase])
                    return reject("PCG-GPU-VISIBILITY-ROOT-DUPLICATE");
            const auto* prepared =
                std::get_if<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                    &root->originalPrepared());
            const auto& package = root->authorization()->package();
            if (!prepared || !*prepared || package.gpuVisibilityKernelContractVersion() != 1u)
                return reject("PCG-GPU-VISIBILITY-ROOT-ALGORITHM");
            const char* id = nullptr;
            const auto* selected = selectBindlessComputeEntryAccess(
                package, (*prepared)->owners()->desc().pipeline->shaderReflection(), id);
            if (!selected)
                return reject(id);
            if (selected->entry != entries[phase]
                || selected->reflection.reflection.computeWorkgroupSizeX
                    != GpuVisibilityDispatchContract::localSizeX
                || selected->reflection.reflection.computeWorkgroupSizeY != 1u
                || selected->reflection.reflection.computeWorkgroupSizeZ != 1u)
                return reject("PCG-GPU-VISIBILITY-ROOT-ENTRY");
            // Table IDs are part of the closed compiler identity. Resolve their
            // real physical mappings, including scalar Emulated bindings.
            if (package.artifact().tables.size() != report.sources.size())
                return reject("PCG-GPU-VISIBILITY-ROOT-TABLES");
            std::array<bool, 10u> found{};
            for (const auto& element : root->elements()) {
                if (report.declarationVisits == budget.maxDeclarationVisits)
                    return reject("PCG-GPU-VISIBILITY-ROOT-DECLARATION-CAPACITY");
                ++report.declarationVisits;
                const auto& declaration = element.declaration;
                const BindlessTableMapping* role = nullptr;
                for (const auto& mapping : package.artifact().tables)
                    if (mapping.firstBinding == declaration.write.binding) {
                        role = &mapping;
                        break;
                    }
                if (!role)
                    continue; // Original metadata/fault bindings remain in the Root.
                if (!role->tableId || role->tableId > found.size() || role->capacity != 1u)
                    return reject("PCG-GPU-VISIBILITY-ROOT-TABLES");
                const auto index = role->tableId - 1u;
                if (found[index] || declaration.sourceIndex >= inventory->sources().size())
                    return reject("PCG-GPU-VISIBILITY-ROOT-DECLARATION");
                found[index] = true;
                if (phase == 0u)
                    report.sources[index] = declaration.sourceIndex;
                else if (report.sources[index] != declaration.sourceIndex)
                    return reject("PCG-GPU-VISIBILITY-ROOT-RESOURCE-CORRESPONDENCE");
                if (index == 9u) {
                    const auto* depth =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &inventory->sources()[declaration.sourceIndex]);
                    if (!depth || declaration.write.type != DescriptorType::SampledTexture
                        || (*depth)->originalObject()->desc().dimension
                            != TextureViewDimension::Texture2D)
                        return reject("PCG-GPU-VISIBILITY-ROOT-DEPTH");
                    continue;
                }
                const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                    &inventory->sources()[declaration.sourceIndex]);
                if (!lease || declaration.write.type != types[index])
                    return reject("PCG-GPU-VISIBILITY-ROOT-BUFFER");
                DescriptorBufferBindingRangeContract::Resolved range;
                if (const auto* failure = DescriptorBufferBindingRangeContract::resolve(
                        declaration.write.bufferBindingRange,
                        (*lease)->layout().sizeBytes,
                        declaration.dynamicOffset,
                        range))
                    return reject(failure);
                if (range.offsetBytes || range.sizeBytes != requiredBytes[index]
                    || declaration.bufferAccessOffset
                    || declaration.bufferAccessBytes != range.sizeBytes)
                    return reject("PCG-GPU-VISIBILITY-ROOT-RANGE");
                if (index == 5u
                    && (((*lease)->layout().usage & ResourceUsage::IndirectBuffer)
                        == ResourceUsage::None))
                    return reject("PCG-GPU-VISIBILITY-ROOT-INDIRECT-USAGE");
            }
            for (const auto present : found)
                if (!present)
                    return reject("PCG-GPU-VISIBILITY-ROOT-MISSING-RESOURCE");
            // Consumer/2 retains its read-only Vertex aliases in every Root.
            // They must name these exact producer resources and be explicitly
            // inactive in this Compute entry's verified target reflection.
            using Aliases = GpuVisibilityConsumerAliasContract;
            if (package.gpuVisibilityConsumerContractVersion() == Aliases::version) {
                if (root->originalShaderEntryAccessVersion() != 3u
                    || root->ordinaryDomains().size() != 1u
                    || root->ordinaryDomains()[0u].set != Aliases::set)
                    return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS-DOMAIN");
                const auto& aliases = root->ordinaryDomains()[0u].source->frozenElements();
                if (aliases.size() != Aliases::roles.size())
                    return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS-COVERAGE");
                std::array<bool, Aliases::roles.size()> matched{};
                for (const auto& element : aliases) {
                    if (report.declarationVisits == budget.maxDeclarationVisits)
                        return reject("PCG-GPU-VISIBILITY-ROOT-DECLARATION-CAPACITY");
                    ++report.declarationVisits;
                    const auto& declaration = element.declaration;
                    const auto ordinal = declaration.write.binding;
                    if (ordinal >= matched.size() || matched[ordinal]
                        || declaration.write.arrayElement || declaration.dynamicOffset)
                        return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS-COVERAGE");
                    matched[ordinal] = true;
                    const auto role = Aliases::roles[ordinal];
                    if (declaration.sourceIndex != report.sources[role]
                        || declaration.write.type
                            != (ordinal == 3u ? DescriptorType::UniformBuffer
                                              : DescriptorType::ReadOnlyStorageBuffer)
                        || declaration.write.resourceState != ResourceState::ShaderRead
                        || declaration.access != ResourceAccess::ShaderRead
                        || declaration.stages != PipelineStage::VertexShader
                        || declaration.bufferAccessOffset
                        || declaration.bufferAccessBytes != requiredBytes[role])
                        return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS-RESOURCE");
                    const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                        inventory->sources()[declaration.sourceIndex]);
                    DescriptorBufferBindingRangeContract::Resolved range;
                    if (const auto* failure = DescriptorBufferBindingRangeContract::resolve(
                            declaration.write.bufferBindingRange,
                            lease->layout().sizeBytes,
                            declaration.dynamicOffset,
                            range))
                        return reject(failure);
                    if (range.offsetBytes || range.sizeBytes != requiredBytes[role])
                        return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS-RANGE");
                    bool active = false;
                    if (const auto* failure =
                            semantics::OriginalBindlessEntryAccessContract::binding(
                                selected->reflection.reflection,
                                package.artifact().execution,
                                Aliases::set,
                                ordinal,
                                ShaderStage::Compute,
                                active))
                        return reject(failure);
                    if (active)
                        return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS-COMPUTE-ACTIVE");
                }
            } else if (!root->ordinaryDomains().empty())
                return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS-DOMAIN");
        }
        for (std::size_t i = 0u; i < report.sources.size(); ++i)
            for (std::size_t j = 0u; j < i; ++j)
                if (report.sources[i] == report.sources[j])
                    return reject("PCG-GPU-VISIBILITY-ROOT-ALIAS");

        const auto& usage = *stream->originalInputs()->originalScalars()->originalUsage();
        const auto& snapshot = roots[0u]->originalSnapshot();
        const auto& metadata =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::Metadata);
        const auto& fault =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
        if (!metadata || !fault)
            return reject("PCG-GPU-VISIBILITY-ROOT-INTERNAL-BUFFERS");
        return inspectCommandOrder(report,
            input,
            queriedWorkgroupLimit,
            budget,
            stream->operations(),
            usage.references(),
            stream->originalInputs()->originalScalars()->originalArguments()->records(),
            inventory->originalLeaseIndex(metadata),
            inventory->originalLeaseIndex(fault));
    }

    // Pure positional inspection used by native cold admission and the CPU
    // order tests. Copied source indices here are not resource-owner proofs;
    // inspect() above authenticates those owners and the original state graph.
    template <class Operations, class References, class Arguments>
    static Report inspectCommandOrder(Report report,
        const GpuVisibilityLayoutContract::Input& input,
        std::uint32_t queriedWorkgroupLimit,
        const Budget& budget,
        const Operations& operations,
        const References& references,
        const Arguments& arguments,
        std::size_t metadataSource,
        std::size_t faultSource) noexcept
    {
        using namespace rhi;
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (const auto* id = validateBudget(budget))
            return reject(id);
        if (!input.groupCount || input.groupCount > input.groupCapacity
            || (input.cascades != 1u && input.cascades != 3u) || metadataSource == SIZE_MAX
            || faultSource == SIZE_MAX)
            return reject("PCG-GPU-VISIBILITY-ROOT-ORDER-INPUTS");
        for (const auto source : report.roots)
            if (source == SIZE_MAX)
                return reject("PCG-GPU-VISIBILITY-ROOT-ORDER-INPUTS");
        for (const auto source : report.sources)
            if (source == SIZE_MAX)
                return reject("PCG-GPU-VISIBILITY-ROOT-ORDER-INPUTS");
        report.dispatches.fill(SIZE_MAX);
        report.commandVisits = report.indirectCalls = report.indirectRecords = 0u;
        std::size_t phase = 0u;
        std::uint32_t consumerCascade = 0u, consumerGroup = 0u;
        bool computeOpen = false, renderOpen = false, bound = false, dispatched = false;
        for (const auto& op : operations) {
            if (report.commandVisits == budget.maxCommandVisits)
                return reject("PCG-GPU-VISIBILITY-ROOT-COMMAND-CAPACITY");
            ++report.commandVisits;
            if (op.commandIndex != report.commandVisits - 1u || op.commandIndex >= arguments.size()
                || op.firstReference > references.size()
                || op.referenceCount > references.size() - op.firstReference)
                return reject("PCG-GPU-VISIBILITY-ROOT-COMMAND-REFERENCES");
            using K = CommandUsageKind;
            if (op.kind == K::BeginComputePass) {
                if (computeOpen || phase == report.roots.size())
                    return reject("PCG-GPU-VISIBILITY-ROOT-COMPUTE-SCOPE");
                computeOpen = true;
                bound = dispatched = false;
            } else if (op.kind == K::BindPreparedComputeBinding) {
                if (!computeOpen || bound || phase == report.roots.size() || op.referenceCount != 1u
                    || references[op.firstReference].sourceIndex != report.roots[phase])
                    return reject("PCG-GPU-VISIBILITY-ROOT-BIND-ORDER");
                bound = true;
            } else if (op.kind == K::Dispatch) {
                const auto* fields = arguments[op.commandIndex]
                                         .scalar.template get<CommandScalarArguments::Dispatch>();
                if (!computeOpen || !bound || dispatched || phase == report.roots.size() || !fields)
                    return reject("PCG-GPU-VISIBILITY-ROOT-DISPATCH-ORDER");
                if (const auto* id = GpuVisibilityDispatchContract::validate(input,
                        static_cast<GpuVisibilityDispatchContract::Phase>(phase),
                        queriedWorkgroupLimit,
                        fields->x,
                        fields->y,
                        fields->z))
                    return reject(id);
                report.dispatches[phase] = op.commandIndex;
                dispatched = true;
            } else if (op.kind == K::EndComputePass) {
                if (!computeOpen || !dispatched)
                    return reject("PCG-GPU-VISIBILITY-ROOT-COMPUTE-SCOPE");
                computeOpen = false;
                ++phase;
            } else if (op.kind == K::BeginRenderPass || op.kind == K::DrawIndexedIndirect) {
                if (computeOpen || phase != report.roots.size())
                    return reject("PCG-GPU-VISIBILITY-ROOT-CONSUMER-ORDER");
                if (op.kind == K::BeginRenderPass) {
                    if (renderOpen)
                        return reject("PCG-GPU-VISIBILITY-ROOT-RENDER-SCOPE");
                    renderOpen = true;
                }
                if (op.kind == K::DrawIndexedIndirect) {
                    const auto* draw =
                        arguments[op.commandIndex]
                            .scalar.template get<CommandScalarArguments::DrawIndirect>();
                    const auto slot =
                        std::uint64_t(consumerCascade) * input.groupCapacity + consumerGroup;
                    if (!renderOpen || !draw || !draw->count || consumerCascade >= input.cascades
                        || draw->count > input.groupCount - consumerGroup
                        || draw->offset != slot * sizeof(DrawIndexedIndirectCommand)
                        || draw->stride != sizeof(DrawIndexedIndirectCommand)
                        || op.referenceCount != 1u
                        || references[op.firstReference].sourceIndex != report.sources[5u])
                        return reject("PCG-GPU-VISIBILITY-ROOT-INDIRECT-CORRESPONDENCE");
                    ++report.indirectCalls;
                    report.indirectRecords += draw->count;
                    consumerGroup += draw->count;
                    if (consumerGroup == input.groupCount) {
                        consumerGroup = 0u;
                        ++consumerCascade;
                    }
                }
            } else if (op.kind == K::EndRenderPass) {
                if (!renderOpen)
                    return reject("PCG-GPU-VISIBILITY-ROOT-RENDER-SCOPE");
                renderOpen = false;
            } else if (op.kind == K::BufferCopy) {
                if (computeOpen || op.referenceCount != 2u)
                    return reject("PCG-GPU-VISIBILITY-ROOT-FOREIGN-WRITE");
                const auto destination = references[op.firstReference + 1u].sourceIndex;
                for (const auto resource : report.sources)
                    if (destination == resource)
                        return reject("PCG-GPU-VISIBILITY-ROOT-FOREIGN-WRITE");
                if (destination == metadataSource || destination == faultSource)
                    return reject("PCG-GPU-VISIBILITY-ROOT-FOREIGN-WRITE");
            } else if (op.kind == K::ClearFaultBuffer || op.kind == K::ClearSnapshotFault) {
                if (computeOpen || phase)
                    return reject("PCG-GPU-VISIBILITY-ROOT-FAULT-RESET-AFTER-PRODUCER");
            } else if (op.kind == K::DispatchIndirect || op.kind == K::DrawIndexedIndirectCount
                || op.kind == K::BindComputePipeline || op.kind == K::BindComputeDescriptor
                || op.kind == K::BindComputeDescriptorTable
                || op.kind == K::ExecuteSecondaryCommandBuffer || op.kind == K::BufferUpload
                || op.kind == K::LegacySizeOnlyBufferCopy || op.kind == K::AliasBarrier
                || op.kind == K::Draw || op.kind == K::DrawIndexed
                || op.kind == K::RecordIndirectProducer || op.kind == K::RecordIndirectProcessor
                || op.kind == K::IndirectProcessorDraw || op.kind == K::BufferToTextureCopy
                || op.kind == K::TextureToBufferCopy || op.kind == K::TextureToTextureCopy
                || op.kind == K::TextureUpload)
                return reject("PCG-GPU-VISIBILITY-ROOT-FOREIGN-PRODUCER");
            // Original graph validation remains the sole barrier/state fact
            // source. This correspondence grants neither a second journal nor
            // GPU-content validity to a CPU inspection of scene data.
        }
        return !computeOpen && !renderOpen && phase == report.roots.size()
                && consumerCascade == input.cascades && !consumerGroup
            ? report
            : reject("PCG-GPU-VISIBILITY-ROOT-INCOMPLETE");
    }
};
} // namespace arp::rhi::visibility
