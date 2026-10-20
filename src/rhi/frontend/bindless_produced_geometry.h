#pragma once
#include "rhi/frontend/bindless_produced_program.h"
#include "rhi/frontend/bindless_root_native_sequence.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/visibility/gpu_visibility_root_geometry.h"

namespace arp::rhi {
// Borrowed cold scene correspondence. The native factory authenticates the
// actual initialized actors and consumes this view before claiming the seal.
// No caller-authored proof, native handle, second graph or completion issuer.
struct OriginalBindlessProducedGeometryInputs {
    visibility::GpuVisibilityLayoutContract::Input layout;
    const visibility::Instance64* instances = nullptr;
    const visibility::GpuVisibilityParameters* parameters = nullptr;
    const visibility::GpuVisibilityRootGeometryContract::Mesh* meshes = nullptr;
    std::size_t meshCount = 0u;
    const std::size_t* consumerRoots = nullptr;
    std::size_t consumerCount = 0u;
    visibility::GpuVisibilityRootGeometryContract::Budget budget;
    std::uint64_t maxContentVisits = 0u;
    OriginalBindlessProducedProgramContract::Budget commands;

    bool empty() const noexcept
    {
        return !layout.groups && !layout.instanceToGroup && !layout.templateIndexCounts
            && !layout.groupCount && !layout.instanceCount && !layout.templateCount
            && !layout.materialCapacity && !layout.instanceCapacity && !layout.groupCapacity
            && !layout.cascades && !instances && !parameters && !meshes && !meshCount
            && !consumerRoots && !consumerCount && !budget.contractVersion && !budget.optIn
            && !budget.inputs.layout.contractVersion && !budget.inputs.layout.optIn
            && !budget.inputs.layout.maxGroupVisits && !budget.inputs.layout.maxInstanceVisits
            && !budget.inputs.layout.maxPairVisits && !budget.inputs.producer.contractVersion
            && !budget.inputs.producer.optIn && !budget.inputs.producer.maxDeclarationVisits
            && !budget.inputs.producer.maxCommandVisits && !budget.inputs.frozen.contractVersion
            && !budget.inputs.frozen.optIn && !budget.inputs.frozen.maxWordVisits
            && !budget.fetch.contractVersion && !budget.fetch.prototypeOptIn
            && !budget.fetch.maxCommands && !budget.fetch.maxDraws && !budget.fetch.maxStreams
            && !budget.fetch.maxBindingScans && !budget.fetch.maxAnalyzedIndices
            && !budget.maxVertexBindingsPerTemplate && !budget.consumer.contractVersion
            && !budget.consumer.optIn && !budget.consumer.maxCommandVisits
            && !budget.consumer.maxDeclarationVisits && !budget.readbacks.contractVersion
            && !budget.readbacks.optIn && !budget.readbacks.maxCommandVisits
            && !budget.readbacks.maxResultVisits && !budget.readbacks.results.contractVersion
            && !budget.readbacks.results.prototypeOptIn
            && !budget.readbacks.results.maxReadbackCommands
            && !budget.readbacks.results.maxNativeCopyCommands
            && !budget.readbacks.results.maxResultDeclarations
            && !budget.readbacks.results.maxDeclaredReadbackBytes
            && !budget.readbacks.results.maxUniqueResultBytes
            && !budget.readbacks.results.maxCpuResultBytes && !maxContentVisits && commands.empty();
    }

    const char* validatePublic(const OriginalBindlessRootSequenceContract::Input& roots,
        const std::shared_ptr<const OriginalCommandStateTransaction>& transaction,
        bool producedProfile) const noexcept
    {
        if (!producedProfile)
            return empty() ? nullptr : "RHI-BINDLESS-PRODUCED-EXECUTION-INPUT-SCHEMA";
        if (empty() || !transaction || !transaction->hasOriginalBindlessProducedGeometryProfile()
            || !roots.roots || roots.count < 5u || !roots.closurePlans || !roots.closurePlanCount
            || !parameters || !instances || !meshes || !meshCount || !consumerRoots
            || !consumerCount || !maxContentVisits)
            return "RHI-BINDLESS-PRODUCED-EXECUTION-INPUTS";
        if (const auto* id = OriginalBindlessProducedProgramContract::validateBudget(commands))
            return id;
        if (const auto* id = OriginalBindlessRootSequenceContract::inspectClosurePlans(roots))
            return id;
        for (std::size_t i = 0u; i < roots.closurePlanCount; ++i) {
            const auto& plan = roots.closurePlans[i];
            if (plan.budget.contractVersion
                != OriginalBindlessRootNativeClosureContract::producedGeometryVersion)
                return "RHI-BINDLESS-PRODUCED-EXECUTION-CLOSURE-PROFILE";
            if (const auto* id = OriginalBindlessRootNativeClosureContract::producedRootDiagnostic(
                    *plan.root, plan.budget.contractVersion))
                return id;
            const bool graphics =
                std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                    plan.root->originalPrepared());
            if (graphics
                != OriginalBindlessRootNativeClosureContract::hasInitializedGeometry(plan.budget))
                return "RHI-BINDLESS-PRODUCED-EXECUTION-GEOMETRY-CLOSURE";
        }
        return nullptr;
    }

    // Closed backend Content types are inferred from the real native closure.
    // Every lookup scans the same bounded closure set and rejects inconsistent
    // initialized ownership for a shared canonical source.
    template <class Closure>
    const char* inspectNative(const std::shared_ptr<const Closure>* closures,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandResourceStates>& graph,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const std::shared_ptr<const OriginalCommandBufferReadbackBinding>& fault,
        const OriginalBindlessRootReadbackSequence::Input& readbacks,
        std::uint32_t queriedWorkgroupLimit,
        bool queriedNativeBase) const noexcept
    {
        if (!stream)
            return "RHI-BINDLESS-PRODUCED-GEOMETRY-STREAM";
        if (empty())
            return stream->originalBudget().bindlessRoots.contractVersion == 6u
                ? "RHI-BINDLESS-PRODUCED-GEOMETRY-INPUTS-REQUIRED"
                : nullptr;
        if (!closures || count < 5u || !parameters || !instances || !maxContentVisits
            || stream->originalBudget().bindlessRoots.contractVersion != 6u)
            return "RHI-BINDLESS-PRODUCED-GEOMETRY-INPUTS";
        if (const auto* id = OriginalBindlessRootReadbackSequence::inspectInput(fault, readbacks))
            return id;
        for (std::size_t i = 0u; i < count; ++i) {
            if (const auto* id = Closure::validateOriginal(closures[i]))
                return id;
            if (const auto* id = OriginalBindlessRootNativeClosureContract::producedRootDiagnostic(
                    *closures[i]->originalRoot(),
                    OriginalBindlessRootNativeClosureContract::producedGeometryVersion))
                return id;
            if (!core::SharedOwnerProjection::sameOwner(
                    closures[i]->originalInventory(), closures[0u]->originalInventory()))
                return "RHI-BINDLESS-PRODUCED-GEOMETRY-INVENTORY";
        }
        using Owner = std::decay_t<decltype(closures[0u]->initializedGeometry(std::size_t{}))>;
        std::uint64_t visits = 0u;
        const char* lookupDiagnostic = nullptr;
        const auto lookup = [&](std::size_t source) noexcept -> Owner {
            Owner result;
            if (lookupDiagnostic)
                return result;
            for (std::size_t i = 0u; i < count; ++i) {
                const auto cost = closures[i]->resourceEntryCount();
                if (cost > maxContentVisits - visits) {
                    lookupDiagnostic = "RHI-BINDLESS-PRODUCED-GEOMETRY-CONTENT-CAPACITY";
                    return {};
                }
                visits += cost;
                const auto& actor = closures[i]->initializedGeometry(source);
                if (!actor)
                    continue;
                if (result
                    && (result.get() != actor.get()
                        || !core::SharedOwnerProjection::sameOwner(result, actor))) {
                    lookupDiagnostic = "RHI-BINDLESS-PRODUCED-GEOMETRY-CONTENT-AMBIGUOUS";
                    return {};
                }
                result = actor;
            }
            return result;
        };
        const auto report = visibility::GpuVisibilityRootGeometryContract::inspect(
            std::array{closures[0u], closures[1u], closures[2u], closures[3u]},
            graph,
            stream,
            layout,
            instances,
            *parameters,
            meshes,
            meshCount,
            consumerRoots,
            consumerCount,
            lookup,
            queriedWorkgroupLimit,
            queriedNativeBase,
            budget,
            readbacks.empty() ? &fault : readbacks.bindings,
            OriginalBindlessRootReadbackSequence::resultCount(readbacks));
        return lookupDiagnostic ? lookupDiagnostic : report.diagnostic;
    }
};
} // namespace arp::rhi
