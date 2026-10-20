#pragma once
#include "rhi/frontend/command_indirect_accesses.h"
#include "rhi/frontend/command_push_consumption.h"
#include "rhi/frontend/command_usage.h"

namespace arp::rhi {
class OriginalCommandResourceStates;
class OriginalCommandStreamProgram;
class OriginalCommandStateTransaction;
} // namespace arp::rhi

namespace arp::rhi::common {
class OriginalCommandStateSequence;

// Borrowed projection into the existing inventory. No retained access graph or
// independently manufactured state identity is introduced.
class OriginalCommandBindlessRootAccesses final {
public:
    struct Access {
        std::size_t commandIndex, rootSourceIndex, sourceIndex;
        BindlessResourceAccess declaration;
        std::size_t viewSourceIndex = SIZE_MAX;
    };

    struct Result {
        const char* diagnostic = nullptr;
        std::uint64_t roots = 0u, accesses = 0u, sourceVisits = 0u, textureAccesses = 0u,
                      faultClears = 0u, faultCopies = 0u, directResultCopies = 0u;
    };

    template <class Emit>
    static Result visit(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalBindlessRootStateBudget& budget,
        Emit&& emit) noexcept
    {
        return visitOriginal<false>(inputs, budget, std::forward<Emit>(emit));
    }

private:
    friend class OriginalCommandStateSequence;
    friend class arp::rhi::OriginalCommandResourceStates;
    friend class arp::rhi::OriginalCommandStreamProgram;
    friend class arp::rhi::OriginalCommandStateTransaction;

    // Only private, synchronous internal visitors may enter after complete
    // original Inputs/graph admission. No external callbacks or retained token.
    template <class Emit>
    static Result visitValidatedOriginal(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalBindlessRootStateBudget& budget,
        Emit&& emit) noexcept
    {
        return visitOriginal<true>(inputs, budget, std::forward<Emit>(emit));
    }

    template <bool Validated, class Emit>
    static Result visitOriginal(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalBindlessRootStateBudget& budget,
        Emit&& emit) noexcept
    {
        Result result;
        if ((result.diagnostic = budget.diagnostic()))
            return result;
        if constexpr (!Validated)
            if ((result.diagnostic = OriginalCommandInputs::validateOriginal(inputs)))
                return result;
        if (budget.contractVersion >= 2u)
            return visitPrepared<Validated>(inputs, budget, std::forward<Emit>(emit));
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = usage.originalSources()->sources();
        for (std::size_t commandIndex = 0u; commandIndex < usage.commands().size();
            ++commandIndex) {
            const auto& command = usage.commands()[commandIndex];
            for (std::size_t operand = 0u; operand < command.referenceCount; ++operand) {
                const auto index = usage.references()[command.firstReference + operand].sourceIndex;
                const auto* owner =
                    std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        &sources[index]);
                if (!owner)
                    continue;
                if (command.kind != CommandUsageKind::RetainIndexedSnapshot) {
                    result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-COMMAND-CLOSED";
                    return result;
                }
                if (result.roots == budget.maxRoots) {
                    result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-ROOT-CAPACITY";
                    return result;
                }
                ++result.roots;
                if ((result.diagnostic = project<Validated>(**owner,
                         index,
                         commandIndex,
                         ShaderStage::None,
                         sources,
                         budget,
                         result,
                         emit)))
                    return result;
            }
        }
        return result;
    }

private:
    using Sources = OriginalCommandResourceSources;

    // /4 reuses the existing explicit BufferCopy operands/accesses and ordinal.
    // This authenticates the snapshot association without projecting duplicate
    // implicit accesses or introducing another transfer graph/result object.
    static const char* validateFaultCopy(const OriginalCommandInputs& inputs,
        std::size_t commandIndex,
        const OriginalBindlessRootStateBudget& budget,
        Result& result) noexcept
    {
        if (result.faultCopies == budget.maxFaultCopies)
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-CAPACITY";
        const auto& usage = *inputs.originalScalars()->originalUsage();
        const auto& sources = usage.originalSources()->sources();
        const auto& command = usage.commands()[commandIndex];
        const auto* descriptor = inputs.descriptorAt(commandIndex);
        const auto* copy = descriptor ? std::get_if<TransferCopyArguments>(descriptor) : nullptr;
        BufferCopyDesc fields;
        if (!copy || !copy->write(fields))
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-DESCRIPTOR";
        if (fields.sourceOffsetBytes || fields.destinationOffsetBytes
            || fields.sizeBytes != sizeof(std::uint32_t))
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-RANGE";
        const auto& source = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            sources[usage.references()[command.firstReference].sourceIndex]);
        const auto& destination = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            sources[usage.references()[command.firstReference + 1u].sourceIndex]);
        const OriginalCommandBindlessRootSource* found = nullptr;
        for (const auto& candidate : sources) {
            if (result.sourceVisits == budget.maxSourceVisits)
                return "RHI-ORIGINAL-BINDLESS-STATE-VISIT-CAPACITY";
            ++result.sourceVisits;
            const auto* root =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(&candidate);
            if (!root)
                continue;
            const auto& snapshot = (*root)->originalSnapshot();
            const auto& latch =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
            const auto& readback =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
            if (!latch || !readback || fields.sourceBuffer != latch->resource()
                || fields.destinationBuffer != readback->resource())
                continue;
            if (!core::SharedOwnerProjection::sameOwner(latch, source)
                || !core::SharedOwnerProjection::sameOwner(readback, destination))
                return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-CANONICAL-OWNER";
            if (latch->layout().sizeBytes != sizeof(std::uint32_t)
                || readback->layout().sizeBytes != sizeof(std::uint32_t)
                || readback->originalObject()->desc().memory != MemoryUsage::GpuToCpu)
                return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-BUFFERS";
            // Several authenticated entries may share the exact snapshot and
            // its one fault submission lease. Different snapshot issuers may
            // never claim the same copy, even if buffer IDs happen to agree.
            if (found
                && (!core::SharedOwnerProjection::sameOwner(found->originalSnapshot(), snapshot)
                    || !core::SharedOwnerProjection::sameOwner(
                        found->authorization(), (*root)->authorization())))
                return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-AMBIGUOUS-ROOT";
            found = root->get();
        }
        if (!found)
            return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-SNAPSHOT";
        if (result.roots == budget.maxRoots)
            return "RHI-ORIGINAL-BINDLESS-STATE-ROOT-CAPACITY";
        ++result.roots;
        ++result.faultCopies;
        return nullptr;
    }

    // Root6 and explicit DirectResultCopy/1 carry declared result buffers. Copies use
    // the original transfer operands and state journal. Keep snapshot-internal
    // copies in the exact fault profile; a general copy cannot disguise one.
    static const char* classifySnapshotCopy(const OriginalCommandInputs& inputs,
        std::size_t commandIndex,
        const OriginalBindlessRootStateBudget& budget,
        Result& result,
        bool& snapshotCopy) noexcept
    {
        snapshotCopy = false;
        const auto& usage = *inputs.originalScalars()->originalUsage();
        const auto& command = usage.commands()[commandIndex];
        const auto& sources = usage.originalSources()->sources();
        if (command.referenceCount != 2u)
            return "RHI-ORIGINAL-BINDLESS-STATE-PRODUCED-COPY-OPERANDS";
        const auto from =
            Sources::resource(sources[usage.references()[command.firstReference].sourceIndex]);
        const auto to =
            Sources::resource(sources[usage.references()[command.firstReference + 1u].sourceIndex]);
        for (const auto& source : sources) {
            if (result.sourceVisits == budget.maxSourceVisits)
                return "RHI-ORIGINAL-BINDLESS-STATE-VISIT-CAPACITY";
            ++result.sourceVisits;
            const auto* root =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(&source);
            if (!root)
                continue;
            const auto& snapshot = (*root)->originalSnapshot();
            for (const auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                     BindlessSnapshot::OriginalBufferRole::FaultLatch,
                     BindlessSnapshot::OriginalBufferRole::FaultReadback}) {
                const auto& buffer = snapshot->originalBufferSource(role);
                if (buffer && (buffer->resource() == from || buffer->resource() == to))
                    snapshotCopy = true;
            }
        }
        return nullptr;
    }

    static const char* validateDirectResultCopy(const OriginalCommandInputs& inputs,
        std::size_t commandIndex,
        const OriginalBindlessRootStateBudget& budget,
        Result& result) noexcept
    {
        if (result.directResultCopies == budget.directResultCopies.maxCopies)
            return "RHI-BINDLESS-DIRECT-RESULT-COPY-CAPACITY";
        const auto& usage = *inputs.originalScalars()->originalUsage();
        const auto& sources = usage.originalSources()->sources();
        const auto& command = usage.commands()[commandIndex];
        const auto sourceIndex = usage.references()[command.firstReference].sourceIndex;
        const auto destinationIndex = usage.references()[command.firstReference + 1u].sourceIndex;
        const auto* source =
            std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&sources[sourceIndex]);
        const auto* destination = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
            &sources[destinationIndex]);
        const auto* descriptor = inputs.descriptorAt(commandIndex);
        const auto* copy = descriptor ? std::get_if<TransferCopyArguments>(descriptor) : nullptr;
        BufferCopyDesc fields;
        if (!source || !destination || !copy || !copy->write(fields)
            || (*destination)->originalObject()->desc().memory != MemoryUsage::GpuToCpu
            || !fields.sizeBytes || fields.sourceBuffer != (*source)->resource()
            || fields.destinationBuffer != (*destination)->resource()
            || !core::containsUnsignedRange(std::uint64_t{0u},
                (*destination)->layout().sizeBytes,
                fields.destinationOffsetBytes,
                fields.sizeBytes))
            return "RHI-BINDLESS-DIRECT-RESULT-COPY-BUFFERS";
        bool declared = false;
        for (const auto& candidate : sources) {
            if (result.sourceVisits == budget.maxSourceVisits)
                return "RHI-ORIGINAL-BINDLESS-STATE-VISIT-CAPACITY";
            ++result.sourceVisits;
            const auto* root =
                std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(&candidate);
            if (!root
                || !std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                    (*root)->originalPrepared()))
                continue;
            for (const auto& element : (*root)->elements()) {
                if (result.sourceVisits == budget.maxSourceVisits)
                    return "RHI-ORIGINAL-BINDLESS-STATE-VISIT-CAPACITY";
                ++result.sourceVisits;
                const auto& access = element.declaration;
                if (access.sourceIndex == sourceIndex
                    && (access.write.type == DescriptorType::StorageBuffer
                        || access.write.type == DescriptorType::ReadOnlyStorageBuffer)
                    && core::containsUnsignedRange(access.bufferAccessOffset,
                        access.bufferAccessBytes,
                        fields.sourceOffsetBytes,
                        fields.sizeBytes))
                    declared = true;
            }
        }
        if (!declared)
            return "RHI-BINDLESS-DIRECT-RESULT-COPY-UNDECLARED-RANGE";
        ++result.directResultCopies;
        return nullptr;
    }

    template <class Emit>
    static const char* projectAccess(const BindlessResourceAccess& access,
        std::size_t rootIndex,
        std::size_t commandIndex,
        const core::pmr::Vector<Sources::Source>& sources,
        const OriginalBindlessRootStateBudget& budget,
        Result& result,
        Emit&& emit,
        std::size_t declarationSourceIndex = SIZE_MAX) noexcept
    {
        if (result.accesses == budget.maxAccesses)
            return "RHI-ORIGINAL-BINDLESS-STATE-ACCESS-CAPACITY";
        std::size_t canonical = SIZE_MAX;
        for (std::size_t sourceIndex = 0u; sourceIndex < sources.size(); ++sourceIndex) {
            if (result.sourceVisits == budget.maxSourceVisits)
                return "RHI-ORIGINAL-BINDLESS-STATE-VISIT-CAPACITY";
            ++result.sourceVisits;
            const bool typed = access.kind == BindlessAccessKind::Buffer
                ? std::holds_alternative<std::shared_ptr<const OriginalCommandBufferLease>>(
                      sources[sourceIndex])
                : std::holds_alternative<std::shared_ptr<const OriginalCommandTextureLease>>(
                      sources[sourceIndex]);
            if (typed
                && OriginalCommandResourceSources::resource(sources[sourceIndex])
                    == access.resource) {
                canonical = sourceIndex;
                break;
            }
        }
        if (canonical == SIZE_MAX)
            return "RHI-ORIGINAL-BINDLESS-STATE-CANONICAL-RESOURCE";
        ++result.accesses;
        if (access.kind == BindlessAccessKind::Texture)
            ++result.textureAccesses;
        return emit(Access{commandIndex,
            rootIndex,
            canonical,
            access,
            access.kind == BindlessAccessKind::Texture ? declarationSourceIndex : SIZE_MAX});
    }

    template <bool Validated, class Emit>
    static const char* project(const OriginalCommandBindlessRootSource& root,
        std::size_t rootIndex,
        std::size_t commandIndex,
        ShaderStage stages,
        const core::pmr::Vector<Sources::Source>& sources,
        const OriginalBindlessRootStateBudget& budget,
        Result& result,
        Emit& emit) noexcept
    {
        for (std::size_t accessIndex = 0u; accessIndex < root.size(); ++accessIndex) {
            BindlessResourceAccess access;
            std::size_t declaration = SIZE_MAX;
            if (stages != ShaderStage::None && root.originalShaderEntryAccessVersion()) {
                bool active = false;
                const auto* id = Validated ? root.readValidatedShaderDeclaration(accessIndex,
                                                 stages,
                                                 access,
                                                 declaration,
                                                 active,
                                                 root.originalShaderEntryAccessVersion())
                                           : root.readOriginalShaderDeclaration(accessIndex,
                                                 stages,
                                                 access,
                                                 declaration,
                                                 active,
                                                 root.originalShaderEntryAccessVersion());
                if (id)
                    return id;
                if (!active)
                    continue;
            } else if (!(Validated
                               ? root.readValidatedDeclaration(accessIndex, access, declaration)
                               : root.readOriginalDeclaration(accessIndex, access, declaration))) {
                return "RHI-ORIGINAL-BINDLESS-STATE-DECLARATION";
            }
            if (stages != ShaderStage::None) {
                // A readback destination belongs to copy commands, never
                // to the shader invocation which writes the fault latch.
                access.stages = access.stages & stages;
                if (access.stages == ShaderStage::None)
                    continue;
                access.pipelineStages = bindlessPipelineStagesForShader(access.stages);
            }
            if (const auto* id = projectAccess(
                    access, rootIndex, commandIndex, sources, budget, result, emit, declaration))
                return id;
        }
        return nullptr;
    }

    template <bool Validated, class Emit>
    static Result visitPrepared(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalBindlessRootStateBudget& budget,
        Emit&& emit) noexcept
    {
        Result result;
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = usage.originalSources()->sources();
        if (budget.geometryJoined()
            && inputs->originalBudget().contractVersion != (budget.contractVersion == 6u ? 8u : 7u))
            return {"RHI-ORIGINAL-BINDLESS-GEOMETRY-INPUT-PROFILE"};
        if (!budget.geometryJoined() && inputs->originalBudget().contractVersion >= 7u)
            return {"RHI-ORIGINAL-BINDLESS-GEOMETRY-STATE-PROFILE"};
        if (budget.contractVersion == 6u) {
            const auto indirect = OriginalCommandIndirectAccesses::visit(
                inputs, budget, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            if (indirect.diagnostic)
                return {indirect.diagnostic};
            if (!indirect.calls)
                return {"RHI-ORIGINAL-BINDLESS-INDIRECT-CONSUMPTION-REQUIRED"};
        }
        std::size_t active = SIZE_MAX;
        OriginalCommandPushConsumption push;
        semantics::CommandRecordingOrder order(
            usage.originalSources()->originalBudget().maxDebugDepth);
        using K = CommandUsageKind;
        for (std::size_t commandIndex = 0u; commandIndex < usage.commands().size();
            ++commandIndex) {
            const auto& command = usage.commands()[commandIndex];
            if ((result.diagnostic = order.append(command.kind)))
                return result;
            switch (command.kind) {
            case K::BeginComputePass:
            case K::BeginRenderPass:
            case K::BeginTransferPass:
            case K::EndComputePass:
            case K::EndRenderPass:
            case K::EndTransferPass:
            case K::BindComputePipeline:
            case K::BindGraphicsPipeline:
                active = SIZE_MAX;
                push.onBind();
                break;
            default:
                break;
            }
            for (std::size_t operand = 0u; operand < command.referenceCount; ++operand) {
                const auto index = usage.references()[command.firstReference + operand].sourceIndex;
                const auto* root =
                    std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        &sources[index]);
                if (!root)
                    continue;
                if (command.kind == K::RetainIndexedSnapshot)
                    continue;
                if (command.kind == K::ClearSnapshotFault && budget.contractVersion >= 3u) {
                    if (result.faultClears == budget.maxFaultClears) {
                        result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-FAULT-CAPACITY";
                        return result;
                    }
                    if (result.roots == budget.maxRoots) {
                        result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-ROOT-CAPACITY";
                        return result;
                    }
                    const auto& snapshot = (*root)->originalSnapshot();
                    auto access = snapshot->faultLatchAccess();
                    const auto& latch = snapshot->originalBufferSource(
                        BindlessSnapshot::OriginalBufferRole::FaultLatch);
                    if (!latch || !access.resource.isValid()
                        || access.kind != BindlessAccessKind::Buffer
                        || access.resource != latch->resource() || access.bufferOffset
                        || access.bufferSize != sizeof(std::uint32_t)) {
                        result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-FAULT-LATCH";
                        return result;
                    }
                    access.stages = ShaderStage::None;
                    access.pipelineStages = PipelineStage::Transfer;
                    access.access = ResourceAccess::TransferWrite;
                    access.state = ResourceState::CopyDst;
                    ++result.roots;
                    ++result.faultClears;
                    result.diagnostic = projectAccess(access,
                        index,
                        commandIndex,
                        sources,
                        budget,
                        result,
                        [&](const auto& point) noexcept -> const char* {
                            const auto& canonical =
                                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                                    sources[point.sourceIndex]);
                            if (!core::SharedOwnerProjection::sameOwner(canonical, latch))
                                return "RHI-ORIGINAL-BINDLESS-STATE-FAULT-CANONICAL-OWNER";
                            return emit(point);
                        });
                    if (result.diagnostic)
                        return result;
                    continue;
                }
                if (command.kind == K::PushConstants) {
                    if (budget.pushConsumption.empty() || active != index)
                        return {"RHI-ORIGINAL-PUSH-CONSUMPTION-ACTIVE-ROOT"};
                    continue;
                }
                const bool compute = command.kind == K::BindPreparedComputeBinding;
                const bool graphics = command.kind == K::BindPreparedGraphicsBinding;
                if (!compute && !graphics) {
                    result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-COMMAND-CLOSED";
                    return result;
                }
                if (std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                        (*root)->originalPrepared())
                    != compute) {
                    result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-PREPARED-KIND";
                    return result;
                }
                if (graphics) {
                    if constexpr (Validated)
                        result.diagnostic =
                            OriginalCommandBindlessRootSource::validateScopedGraphicsBufferRanges(
                                *root);
                    else
                        result.diagnostic =
                            OriginalCommandBindlessRootSource::validateOriginalGraphicsBufferRanges(
                                *root);
                    if (result.diagnostic)
                        return result;
                }
                if (graphics && budget.contractVersion < 5u) {
                    const auto& pipeline =
                        std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                            (*root)->originalPrepared())
                            ->owners()
                            ->desc()
                            .pipeline;
                    const auto* vertices = pipeline->bindlessVertexBuffers(1u);
                    if (vertices && !vertices->empty()) {
                        result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-GEOMETRY-JOIN-MISSING";
                        return result;
                    }
                }
                active = index;
                push.onBind();
            }
            if (command.kind == K::PushConstants) {
                if (command.referenceCount != 1u
                    || usage.references()[command.firstReference].sourceIndex != active)
                    return {"RHI-ORIGINAL-PUSH-CONSUMPTION-ACTIVE-ROOT"};
                if (active == SIZE_MAX)
                    return {"RHI-ORIGINAL-PUSH-CONSUMPTION-ACTIVE-ROOT"};
                const auto& root =
                    *std::get<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                        sources[active]);
                result.diagnostic = std::visit(
                    [&](const auto& prepared) noexcept -> const char* {
                        const auto& pipeline = *prepared->owners()->desc().pipeline;
                        if (!pipeline.layout())
                            return "RHI-ORIGINAL-PUSH-CONSUMPTION-LAYOUT";
                        PushConstantsDesc block;
                        return push.update(inputs->originalScalars()->originalArguments(),
                            commandIndex,
                            pipeline.layout()->desc(),
                            pipeline.shaderReflection(),
                            order.scope() == semantics::CommandRecordingOrder::Scope::Compute
                                ? ShaderStage::Compute
                                : ShaderStage::Vertex | ShaderStage::Fragment,
                            budget.pushConsumption,
                            block);
                    },
                    root.originalPrepared());
                if (result.diagnostic)
                    return result;
            }
            if (command.kind == K::BufferCopy && budget.contractVersion >= 4u) {
                bool snapshotCopy = true;
                if (budget.contractVersion == 6u || !budget.directResultCopies.empty())
                    result.diagnostic =
                        classifySnapshotCopy(*inputs, commandIndex, budget, result, snapshotCopy);
                if (!result.diagnostic && snapshotCopy)
                    result.diagnostic = validateFaultCopy(*inputs, commandIndex, budget, result);
                if (!result.diagnostic && !snapshotCopy && !budget.directResultCopies.empty())
                    result.diagnostic =
                        validateDirectResultCopy(*inputs, commandIndex, budget, result);
                if (result.diagnostic)
                    return result;
            }
            const bool compute = command.kind == K::Dispatch;
            const bool graphics = command.kind == K::Draw
                || (budget.geometryJoined() && command.kind == K::DrawIndexed)
                || (budget.contractVersion == 6u && command.kind == K::DrawIndexedIndirect);
            if (budget.contractVersion < 5u
                && (command.kind == K::BindVertexBuffer || command.kind == K::BindIndexBuffer
                    || command.kind == K::DrawIndexed)) {
                result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-GEOMETRY-JOIN-MISSING";
                return result;
            }
            if (command.kind == K::DispatchIndirect
                || (command.kind == K::DrawIndexedIndirect && budget.contractVersion != 6u)
                || command.kind == K::DrawIndexedIndirectCount
                || command.kind == K::IndirectProcessorDraw) {
                result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-INDIRECT-JOIN-MISSING";
                return result;
            }
            if (!compute && !graphics)
                continue;
            if (active == SIZE_MAX) {
                result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-ACTIVE-ROOT";
                return result;
            }
            const auto& root = *std::get<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
                sources[active]);
            if (std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                    root.originalPrepared())
                != compute) {
                result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-PREPARED-KIND";
                return result;
            }
            result.diagnostic = std::visit(
                [&](const auto& prepared) noexcept {
                    return push.consume(prepared->owners()->desc().pipeline->shaderReflection(),
                        budget.pushConsumption);
                },
                root.originalPrepared());
            if (result.diagnostic)
                return result;
            if (result.roots == budget.maxRoots) {
                result.diagnostic = "RHI-ORIGINAL-BINDLESS-STATE-ROOT-CAPACITY";
                return result;
            }
            ++result.roots;
            const auto stages = compute
                ? ShaderStage::Compute
                : std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                      root.originalPrepared())
                      ->owners()
                      ->stages();
            if ((result.diagnostic = project<Validated>(
                     root, active, commandIndex, stages, sources, budget, result, emit)))
                return result;
        }
        if (!order.complete())
            result.diagnostic = "RHI-COMMAND-ORDER-INCOMPLETE";
        return result;
    }
};

// Ordered declared-range projection only. Original Inputs validation owns all
// positional fields and leases; geometry/state/native execution remain separate.
class OriginalCommandTextureRanges final {
    friend class OriginalTypedUsageProjection;

public:
    struct Point {
        std::size_t commandIndex, referenceIndex, sourceIndex;
        TextureSubresourceRange range;
        enum class Origin { ExplicitOperand, ShaderConsumption, RenderBinding, BindlessRoot };
        Origin origin = Origin::ExplicitOperand;
        std::size_t viewSourceIndex = SIZE_MAX, descriptorSourceIndex = SIZE_MAX;
        std::uint32_t tableElementIndex = 0u;
        ResourceState state = ResourceState::Undefined;
        PipelineStage stages = PipelineStage::None;
        ResourceAccess access = ResourceAccess::None;

        bool matches(const Point& other) const noexcept
        {
            return commandIndex == other.commandIndex && referenceIndex == other.referenceIndex
                && sourceIndex == other.sourceIndex && range == other.range
                && origin == other.origin && viewSourceIndex == other.viewSourceIndex
                && descriptorSourceIndex == other.descriptorSourceIndex
                && tableElementIndex == other.tableElementIndex && state == other.state
                && stages == other.stages && access == other.access;
        }
    };

    static const GpuTexture* originalTexture(
        const OriginalCommandResourceSources::Source& source) noexcept
    {
        return std::visit(
            [](const auto& value) -> const GpuTexture* {
                using T = std::remove_const_t<typename std::decay_t<decltype(value)>::element_type>;
                if constexpr (std::is_same_v<T, OriginalCommandTextureLease>
                    || std::is_same_v<T, OriginalCommandReadbackTextureSource>)
                    return value->originalObject();
                else if constexpr (std::is_same_v<T, OriginalCommandTextureViewSource>)
                    return value->originalParentLease()->originalObject();
                else
                    return nullptr;
            },
            source);
    }

    template <class Emit>
    static const char* visit(
        const std::shared_ptr<const OriginalCommandInputs>& inputs, Emit&& emit) noexcept
    {
        if (const auto* id = OriginalCommandInputs::validateOriginal(inputs))
            return id;
        return visitValidatedOriginal(inputs, std::forward<Emit>(emit));
    }

private:
    template <class Emit>
    static const char* visitValidatedOriginal(
        const std::shared_ptr<const OriginalCommandInputs>& inputs, Emit&& emit) noexcept
    {
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& sources = usage.originalSources()->sources();
        for (std::size_t commandIndex = 0u; commandIndex < usage.commands().size();
            ++commandIndex) {
            const auto& command = usage.commands()[commandIndex];
            const auto* descriptor = inputs->descriptorAt(commandIndex);
            for (std::size_t operand = 0u; operand < command.referenceCount; ++operand) {
                const auto reference = command.firstReference + operand;
                const auto sourceIndex = usage.references()[reference].sourceIndex;
                const auto& source = sources[sourceIndex];
                const auto* texture = originalTexture(source);
                if (!texture)
                    continue;
                TextureSubresourceRange range;
                bool whole = false;
                if (const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &source)) {
                    if (command.kind != CommandUsageKind::BeginRenderPass)
                        return "RHI-ORIGINAL-TEXTURE-RANGES-OPERAND-UNSUPPORTED";
                    const auto& layout = (*view)->originalViewLease()->layout();
                    range = {layout.baseMipLevel,
                        layout.mipLevelCount,
                        layout.baseArrayLayer,
                        layout.arrayLayerCount,
                        layout.aspect};
                } else {
                    using K = CommandUsageKind;
                    switch (command.kind) {
                    case K::BufferToTextureCopy:
                    case K::TextureToBufferCopy:
                    case K::TextureToTextureCopy: {
                        const TransferCopyArguments* copy =
                            std::get_if<TransferCopyArguments>(descriptor);
                        if (!copy)
                            if (const auto* snapshot =
                                    std::get_if<std::shared_ptr<const TransferCopyInputSnapshot>>(
                                        descriptor))
                                copy = &(*snapshot)->scalarFields();
                        if (!copy)
                            return "RHI-ORIGINAL-TEXTURE-RANGES-DESCRIPTOR";
                        range = operand ? copy->destinationRange() : copy->sourceRange();
                        break;
                    }
                    case K::TextureBarrier: {
                        const auto* dependency =
                            std::get_if<ResourceDependencyArguments>(descriptor);
                        ResourceBarrierDesc fields;
                        if (!dependency || !dependency->write(command.kind, fields))
                            return "RHI-ORIGINAL-TEXTURE-RANGES-DESCRIPTOR";
                        range = fields.textureSubresources;
                        break;
                    }
                    case K::TextureUpload: {
                        const auto* upload =
                            std::get_if<TextureUploadOriginalInputArguments>(descriptor);
                        if (!upload)
                            return "RHI-ORIGINAL-TEXTURE-RANGES-DESCRIPTOR";
                        range = upload->originalInput()->scalarFields().range();
                        break;
                    }
                    case K::TextureReadback: {
                        const auto* readback =
                            std::get_if<TextureReadbackOriginalInputArguments>(descriptor);
                        if (!readback)
                            return "RHI-ORIGINAL-TEXTURE-RANGES-DESCRIPTOR";
                        range = readback->originalInput()->scalarFields().range();
                        break;
                    }
                    case K::TextureMemoryBarrier:
                    case K::AliasBarrier:
                    case K::RetainTexture:
                        range = defaultTextureSubresourceRange(texture->desc());
                        whole = true;
                        break;
                    default:
                        return "RHI-ORIGINAL-TEXTURE-RANGES-OPERAND-UNSUPPORTED";
                    }
                }
                if (const auto* id =
                        emit(Point{commandIndex, reference, sourceIndex, range}, *texture))
                    return id;
                if (whole && texture->desc().format == TextureFormat::Depth24Stencil8) {
                    range.aspect = TextureAspect::Stencil;
                    if (const auto* id =
                            emit(Point{commandIndex, reference, sourceIndex, range}, *texture))
                        return id;
                }
            }
        }
        return nullptr;
    }
};
} // namespace arp::rhi::common
