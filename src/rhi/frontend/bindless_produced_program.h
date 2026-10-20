#pragma once
#include "rhi/frontend/bindless_root_native_sequence.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/command_push_consumption.h"
#include "rhi/semantics/bindless_push_consumption_lowering.h"

namespace arp::rhi {
// Structural admission of the original produced stream. This value inspector
// owns no resource, journal, seal or completion authority. Geometry/content and
// native owners must still be authenticated by the cold factory.
class OriginalBindlessProducedProgramContract final {
public:
    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool optIn = false;
        std::uint64_t maxCommandVisits = 0u;
        std::uint64_t maxFaultVisits = 0u;

        bool empty() const noexcept
        {
            return !contractVersion && !optIn && !maxCommandVisits && !maxFaultVisits;
        }
    };

    struct Report {
        const char* diagnostic = nullptr;
        std::uint64_t commandVisits = 0u, computePasses = 0u, renderPasses = 0u;
        std::uint64_t bindings = 0u, dispatches = 0u, draws = 0u, copies = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateBudget(const Budget& budget) noexcept
    {
        if (budget.contractVersion != 1u && budget.contractVersion != 2u)
            return "RHI-BINDLESS-PRODUCED-PROGRAM-VERSION";
        if (!budget.optIn)
            return "RHI-BINDLESS-PRODUCED-PROGRAM-OPT-IN";
        if ((budget.contractVersion == 2u) != bool(budget.maxFaultVisits))
            return "RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-BUDGET";
        return budget.maxCommandVisits ? nullptr : "RHI-BINDLESS-PRODUCED-PROGRAM-BUDGET";
    }

    template <class Closure>
    static const char* inspectNativePushRecording(
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const typename OriginalBindlessRootNativeSequence<Closure>::Input& roots,
        bool recordingCapability,
        std::uint32_t nativeLimit) noexcept
    {
        if (!stream || !roots.closures || !roots.count)
            return "RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-INPUTS";
        const auto& budget = stream->originalBudget().bindlessRoots.pushConsumption;
        if (!recordingCapability || !budget.recordingOptIn || budget.recordingContractVersion != 1u)
            return "RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-RECORDING";
        if (const auto* id = budget.diagnostic())
            return id;
        for (std::size_t i = 0u; i < roots.count; ++i) {
            if (!roots.closures[i])
                return "RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-INPUTS";
            const auto& prepared = roots.closures[i]->originalRoot()->originalPrepared();
            const auto* graphics =
                std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(&prepared);
            if (!graphics)
                continue;
            if (!*graphics || !(*graphics)->owners() || !(*graphics)->owners()->desc().pipeline)
                return "RHI-BINDLESS-PRODUCED-PROGRAM-GRAPHICS-PIPELINE";
            if (const auto* id =
                    semantics::OriginalBindlessPushConsumptionLowering::validateNativeSize(
                        (*graphics)
                            ->owners()
                            ->desc()
                            .pipeline->shaderReflection()
                            .pushConstantByteCount,
                        kL2AtomicPushConstantLimitBytes,
                        nativeLimit))
                return id;
        }
        return nullptr;
    }

    // Consumes actual native closure identities, not a caller-authored report.
    // The sequence validator checks the full original inventory and generation;
    // the readback validator authenticates every exact copy and result owner.
    template <class Closure>
    static Report inspectNative(const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const typename OriginalBindlessRootNativeSequence<Closure>::Input& roots,
        const std::shared_ptr<const OriginalCommandBufferReadbackBinding>& fault,
        const OriginalBindlessRootReadbackSequence::Input& readbacks,
        const Budget& budget) noexcept
    {
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(stream))
            return reject(id);
        if (stream->originalBudget().bindlessRoots.contractVersion != 6u || !roots.closures
            || roots.count < 5u)
            return reject("RHI-BINDLESS-PRODUCED-PROGRAM-ROOT-PROFILE");
        const auto& usage = *stream->originalInputs()->originalScalars()->originalUsage();
        const auto& inventory = usage.originalSources();
        if (const auto* id = OriginalBindlessRootNativeSequence<Closure>::inspectInput(
                roots, inventory, stream, [](const auto& closure) noexcept {
                    return Closure::validateOriginal(closure);
                }))
            return reject(id);
        if (const auto* id = OriginalBindlessRootReadbackSequence::inspectInput(fault, readbacks))
            return reject(id);
        if (const auto* id = OriginalBindlessRootReadbackSequence::inspectRootCoverage(
                fault,
                readbacks,
                roots.count,
                [&](std::size_t i) noexcept -> const auto& {
                    return roots.closures[i]->originalRoot();
                },
                roots.budget.snapshotScope))
            return reject(id);
        if ((budget.contractVersion == 2u) != !roots.budget.snapshotScope.empty())
            return reject("RHI-BINDLESS-PRODUCED-PROGRAM-SNAPSHOT-SCOPE");
        auto report = inspectOperations(
            stream->operations(),
            usage.references(),
            roots.count,
            [&](std::size_t i) noexcept {
                return inventory->originalLeaseIndex(roots.closures[i]->originalRoot());
            },
            fault->rootSourceIndex(),
            OriginalBindlessRootReadbackSequence::resultCount(readbacks),
            budget,
            OriginalBindlessRootReadbackSequence::resultCount(readbacks),
            [&](std::size_t i) noexcept {
                const auto& result =
                    OriginalBindlessRootReadbackSequence::resultAt(fault, readbacks, i);
                return result->isOriginalSnapshotFaultReadback() ? result->rootSourceIndex()
                                                                 : SIZE_MAX;
            });
        if (report.diagnostic)
            return report;
        const auto& pushBudget = stream->originalBudget().bindlessRoots.pushConsumption;
        OriginalCommandPushConsumption pushConsumer;
        const RenderPipeline* pipeline = nullptr;
        std::size_t boundRoots = 0u;
        std::uint64_t visits = 0u;
        const auto visit = [&](std::uint64_t cost) noexcept {
            if (cost > roots.budget.maxResourceVisits - visits)
                return false;
            visits += cost;
            return true;
        };
        for (const auto& op : stream->operations()) {
            // inspectOperations has bounded this immutable stream already.
            // This second pass checks retained payloads and must not charge
            // the same command count again or reject an exact-size budget.
            using K = CommandUsageKind;
            if (op.kind == K::BindPreparedComputeBinding
                || op.kind == K::BindPreparedGraphicsBinding) {
                const auto& prepared =
                    roots.closures[boundRoots++]->originalRoot()->originalPrepared();
                pipeline = nullptr;
                pushConsumer.onBind();
                if (op.kind == K::BindPreparedGraphicsBinding) {
                    const auto* graphics =
                        std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                            &prepared);
                    if (!graphics || !*graphics || !(*graphics)->owners()
                        || !(*graphics)->owners()->desc().pipeline)
                        return reject("RHI-BINDLESS-PRODUCED-PROGRAM-GRAPHICS-PIPELINE");
                    pipeline = (*graphics)->owners()->desc().pipeline.get();
                }
            } else if (op.kind == K::PushConstants) {
                if (!pipeline || !pipeline->layout())
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-LAYOUT");
                PushConstantsDesc block;
                if (const auto* id = pushConsumer.update(
                        stream->originalInputs()->originalScalars()->originalArguments(),
                        op.commandIndex,
                        pipeline->layout()->desc(),
                        pipeline->shaderReflection(),
                        ShaderStage::Vertex | ShaderStage::Fragment,
                        pushBudget,
                        block))
                    return reject(id);
            } else if (op.kind == K::DrawIndexedIndirect) {
                if (!pipeline)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-LAYOUT");
                if (const auto* id = pushConsumer.consume(pipeline->shaderReflection(), pushBudget))
                    return reject(id);
            }
            if (op.kind == CommandUsageKind::BufferCopy
                && !OriginalBindlessRootReadbackSequence::containsCopy(
                    fault, readbacks, op.commandIndex))
                return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FOREIGN-COPY");
            for (std::size_t r = 0u; r < op.referenceCount; ++r) {
                const auto source = usage.references()[op.firstReference + r].sourceIndex;
                if (source >= inventory->sources().size())
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-RESOURCE-SOURCE");
                const auto* buffer = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                    &inventory->sources()[source]);
                if (!buffer)
                    continue;
                bool retained = false;
                for (std::size_t i = 0u; i < roots.count; ++i) {
                    if (!visit(roots.closures[i]->resourceEntryCount()))
                        return reject("RHI-BINDLESS-PRODUCED-PROGRAM-RESOURCE-CAPACITY");
                    retained = retained || roots.closures[i]->retainsOriginalBufferSource(source);
                }
                for (std::size_t i = 0u;
                    i < OriginalBindlessRootReadbackSequence::resultCount(readbacks);
                    ++i) {
                    if (!visit(1u))
                        return reject("RHI-BINDLESS-PRODUCED-PROGRAM-RESOURCE-CAPACITY");
                    const auto& binding =
                        OriginalBindlessRootReadbackSequence::resultAt(fault, readbacks, i);
                    retained = retained
                        || core::SharedOwnerProjection::sameOwner(
                            *buffer, binding->originalCopySource())
                        || core::SharedOwnerProjection::sameOwner(
                            *buffer, binding->originalReadbackSource());
                }
                if (!retained)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-UNRETAINED-BUFFER");
            }
        }
        return report;
    }

    // rootAt projects an already authenticated original sequence. This helper
    // is also used by CPU value tests; accepting values is never backend proof.
    template <class Operations, class References, class RootAt>
    static Report inspectOperations(const Operations& operations,
        const References& references,
        std::size_t rootCount,
        RootAt&& rootAt,
        std::size_t faultRoot,
        std::size_t resultCount,
        const Budget& budget) noexcept
    {
        return inspectOperations(operations,
            references,
            rootCount,
            std::forward<RootAt>(rootAt),
            faultRoot,
            resultCount,
            budget,
            1u,
            [&](std::size_t) noexcept {
                return faultRoot;
            });
    }

    template <class Operations, class References, class RootAt, class FaultAt>
    static Report inspectOperations(const Operations& operations,
        const References& references,
        std::size_t rootCount,
        RootAt&& rootAt,
        std::size_t faultRoot,
        std::size_t resultCount,
        const Budget& budget,
        std::size_t faultSlots,
        FaultAt&& faultAt) noexcept
    {
        const auto reject = [](const char* id) noexcept {
            return Report{id};
        };
        if (const auto* id = validateBudget(budget))
            return reject(id);
        if (rootCount < 5u || faultRoot == SIZE_MAX || resultCount < 2u)
            return reject("RHI-BINDLESS-PRODUCED-PROGRAM-INPUTS");
        enum class Scope { Outside, Compute, Render, Transfer, Finished };
        Scope scope = Scope::Outside;
        Report report;
        bool cleared = false, bound = false, dispatched = false;
        std::uint64_t faultVisits = 0u, expectedFaults = 0u, clears = 0u;
        if (budget.contractVersion == 2u) {
            if (!faultSlots || faultSlots > resultCount)
                return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-COVERAGE");
            for (std::size_t i = 0u; i < faultSlots; ++i) {
                if (faultVisits == budget.maxFaultVisits)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-VISIT-CAPACITY");
                ++faultVisits;
                expectedFaults += faultAt(i) != SIZE_MAX;
            }
            if (!expectedFaults)
                return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-COVERAGE");
        } else
            expectedFaults = 1u;
        bool viewport = false, scissor = false, vertex = false, index = false, push = false;
        std::size_t selected = SIZE_MAX;
        std::uint64_t passDraws = 0u;
        for (const auto& op : operations) {
            if (report.commandVisits == budget.maxCommandVisits)
                return reject("RHI-BINDLESS-PRODUCED-PROGRAM-COMMAND-CAPACITY");
            if (op.commandIndex != report.commandVisits || scope == Scope::Finished
                || op.firstReference > references.size()
                || op.referenceCount > references.size() - op.firstReference)
                return reject("RHI-BINDLESS-PRODUCED-PROGRAM-COMMAND-REFERENCES");
            ++report.commandVisits;
            const auto source = op.referenceCount == 1u
                ? std::size_t(references[op.firstReference].sourceIndex)
                : SIZE_MAX;
            using K = CommandUsageKind;
            switch (op.kind) {
            case K::BufferBarrier:
            case K::TextureBarrier:
                if (scope != Scope::Outside)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-BARRIER-SCOPE");
                break;
            case K::ClearSnapshotFault:
                if (scope != Scope::Outside || report.computePasses || source == SIZE_MAX)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-CLEAR");
                if (budget.contractVersion == 1u) {
                    if (cleared || source != faultRoot)
                        return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-CLEAR");
                } else {
                    std::size_t matches = 0u;
                    for (std::size_t i = 0u; i < faultSlots; ++i) {
                        if (faultVisits == budget.maxFaultVisits)
                            return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-VISIT-CAPACITY");
                        ++faultVisits;
                        matches += faultAt(i) == source;
                    }
                    if (matches != 1u)
                        return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-COVERAGE");
                    for (const auto& prior : operations) {
                        if (prior.commandIndex == op.commandIndex)
                            break;
                        if (faultVisits == budget.maxFaultVisits)
                            return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-VISIT-CAPACITY");
                        ++faultVisits;
                        if (prior.kind == K::ClearSnapshotFault
                            && references[prior.firstReference].sourceIndex == source)
                            return reject("RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-DUPLICATE");
                    }
                }
                ++clears;
                cleared = true;
                break;
            case K::BeginComputePass:
                if (scope != Scope::Outside || !cleared || clears != expectedFaults
                    || report.computePasses == 4u || report.renderPasses || report.copies)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-PRODUCER-ORDER");
                scope = Scope::Compute;
                bound = dispatched = false;
                ++report.computePasses;
                break;
            case K::BindPreparedComputeBinding:
                if (scope != Scope::Compute || bound || dispatched || report.bindings >= rootCount
                    || source != rootAt(report.bindings))
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-COMPUTE-ROOT");
                selected = source;
                bound = true;
                ++report.bindings;
                break;
            case K::Dispatch:
                if (scope != Scope::Compute || !bound || dispatched)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-DISPATCH");
                dispatched = true;
                ++report.dispatches;
                break;
            case K::EndComputePass:
                if (scope != Scope::Compute || !dispatched)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-COMPUTE-END");
                scope = Scope::Outside;
                selected = SIZE_MAX;
                break;
            case K::BeginRenderPass:
                if (scope != Scope::Outside || report.dispatches != 4u || report.copies)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-CONSUMER-ORDER");
                scope = Scope::Render;
                bound = viewport = scissor = vertex = index = push = false;
                selected = SIZE_MAX;
                passDraws = 0u;
                ++report.renderPasses;
                break;
            case K::BindPreparedGraphicsBinding:
                if (scope != Scope::Render || report.bindings >= rootCount
                    || source != rootAt(report.bindings))
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-GRAPHICS-ROOT");
                selected = source;
                bound = true;
                push = false;
                ++report.bindings;
                break;
            case K::SetViewport:
            case K::SetScissor:
                if (scope != Scope::Render)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-RASTER-SCOPE");
                if (op.kind == K::SetViewport)
                    viewport = true;
                else
                    scissor = true;
                break;
            case K::BindVertexBuffer:
            case K::BindIndexBuffer:
                if (scope != Scope::Render || !bound || source == SIZE_MAX)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-GEOMETRY-SCOPE");
                if (op.kind == K::BindVertexBuffer)
                    vertex = true;
                else
                    index = true;
                break;
            case K::PushConstants:
                if (scope != Scope::Render || !bound || source != selected || push)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-ROOT");
                push = true;
                break;
            case K::DrawIndexedIndirect:
                if (scope != Scope::Render || !bound || !viewport || !scissor || !vertex || !index
                    || !push || source == SIZE_MAX)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-INDIRECT-DRAW");
                push = false;
                ++passDraws;
                ++report.draws;
                break;
            case K::EndRenderPass:
                if (scope != Scope::Render || !passDraws || push)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-RENDER-END");
                selected = SIZE_MAX;
                scope = Scope::Outside;
                break;
            case K::BeginTransferPass:
                if (scope != Scope::Outside || !report.draws || report.bindings != rootCount)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-READBACK-ORDER");
                scope = Scope::Transfer;
                break;
            case K::BufferCopy:
                if (scope != Scope::Transfer || op.referenceCount != 2u
                    || report.copies == resultCount)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-READBACK");
                ++report.copies;
                break;
            case K::EndTransferPass:
                if (scope != Scope::Transfer)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-TRANSFER-END");
                scope = Scope::Outside;
                break;
            case K::Finish:
                if (scope != Scope::Outside || report.bindings != rootCount
                    || report.dispatches != 4u || !report.draws || report.copies != resultCount)
                    return reject("RHI-BINDLESS-PRODUCED-PROGRAM-COVERAGE");
                scope = Scope::Finished;
                break;
            default:
                return reject("RHI-BINDLESS-PRODUCED-PROGRAM-OPERATION");
            }
        }
        return scope == Scope::Finished ? report : reject("RHI-BINDLESS-PRODUCED-PROGRAM-FINISH");
    }
};
} // namespace arp::rhi
