#pragma once
#include "rhi/frontend/bindless_root_initialized_shader.h"
#include "rhi/frontend/bindless_snapshot_scope.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/semantics/bindless_root_sequence_lowering.h"

namespace arp::rhi {
// Borrowed cold correspondence over the authoritative original stream. Entries
// describe binding occurrences, so rebinding the same authenticated Root is
// legal. This creates no resource identity, state owner, journal or receipt.
class OriginalBindlessRootSequenceContract final {
public:
    using Root = OriginalCommandBindlessRootSource;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t generation = 0u, maxBindings = 0u, maxResourceVisits = 0u;
        // /1 retains its single snapshot rule. /2 anchors generation to the
        // first Root while preserving every other snapshot's issued identity.
        OriginalBindlessSnapshotScopeContract::Intent snapshotScope;
    };

    struct Input {
        struct ClosurePlan {
            std::shared_ptr<const Root> root;
            OriginalBindlessRootNativeClosureBudget budget;
            OriginalBindlessInitializedSources initialized;
        };

        const std::shared_ptr<const Root>* roots = nullptr;
        std::size_t count = 0u;
        Budget budget;
        // One explicit cold plan per distinct Root. Producer and consumer
        // scopes need not share a native closure profile or resource ceiling.
        // The resulting authentic closure copies its own budget; Pending never
        // borrows this table. A plan grants no execution permission.
        const ClosurePlan* closurePlans = nullptr;
        std::size_t closurePlanCount = 0u;
        std::uint64_t maxClosurePlanVisits = 0u;

        bool empty() const noexcept
        {
            return !roots && !count && !budget.contractVersion && !budget.prototypeOptIn
                && !budget.generation && !budget.maxBindings && !budget.maxResourceVisits
                && budget.snapshotScope.empty() && !closurePlans && !closurePlanCount
                && !maxClosurePlanVisits;
        }

        const OriginalBindlessRootNativeClosureBudget& closureBudget(
            const std::shared_ptr<const Root>& selected,
            const OriginalBindlessRootNativeClosureBudget& fallback) const noexcept
        {
            if (!closurePlans && !closurePlanCount && !maxClosurePlanVisits)
                return fallback;
            for (std::size_t i = 0u; closurePlans && i < closurePlanCount; ++i)
                if (closurePlans[i].root.get() == selected.get()
                    && core::SharedOwnerProjection::sameOwner(closurePlans[i].root, selected))
                    return closurePlans[i].budget;
            static const OriginalBindlessRootNativeClosureBudget missing;
            return missing;
        }

        OriginalBindlessInitializedSources initializedSources(
            const std::shared_ptr<const Root>& selected,
            const OriginalBindlessInitializedSources& fallback) const noexcept
        {
            if (!closurePlans && !closurePlanCount && !maxClosurePlanVisits)
                return fallback;
            for (std::size_t i = 0u; closurePlans && i < closurePlanCount; ++i)
                if (closurePlans[i].root.get() == selected.get()
                    && core::SharedOwnerProjection::sameOwner(closurePlans[i].root, selected))
                    return closurePlans[i].initialized;
            return {};
        }

        const char* validateInitializedSources(const std::shared_ptr<const Root>& first,
            const OriginalBindlessRootNativeClosureBudget& fallbackBudget,
            const OriginalBindlessInitializedSources& fallback) const noexcept
        {
            if (const auto* id = inspectClosurePlans(*this))
                return id;
            if ((closurePlans || closurePlanCount || maxClosurePlanVisits) && !fallback.empty())
                return "RHI-BINDLESS-ROOT-SEQUENCE-INITIALIZED-INPUT-SCHEMA";
            return OriginalBindlessInitializedSourcesContract::validatePublicInputs(
                closureBudget(first, fallbackBudget), initializedSources(first, fallback));
        }
    };

    static const char* inspectClosurePlans(const Input& input) noexcept
    {
        if (!input.closurePlans && !input.closurePlanCount && !input.maxClosurePlanVisits)
            return nullptr;
        if (!input.roots || !input.count || input.count > input.budget.maxBindings
            || !input.closurePlans || !input.closurePlanCount
            || input.closurePlanCount > input.count || !input.maxClosurePlanVisits)
            return "RHI-BINDLESS-ROOT-SEQUENCE-CLOSURE-PLAN-INPUTS";
        std::uint64_t visits = 0u;
        const auto visit = [&]() noexcept {
            if (visits == input.maxClosurePlanVisits)
                return false;
            ++visits;
            return true;
        };
        for (std::size_t i = 0u; i < input.closurePlanCount; ++i) {
            const auto& plan = input.closurePlans[i];
            if (const auto* id = Root::validateOriginal(plan.root))
                return id;
            if (const auto* id = OriginalBindlessInitializedSourcesContract::validatePublicInputs(
                    plan.budget, plan.initialized))
                return id;
            for (std::size_t prior = 0u; prior < i; ++prior) {
                if (!visit())
                    return "RHI-BINDLESS-ROOT-SEQUENCE-CLOSURE-PLAN-VISIT-CAPACITY";
                if (core::SharedOwnerProjection::sameOwner(
                        plan.root, input.closurePlans[prior].root)
                    || plan.root.get() == input.closurePlans[prior].root.get())
                    return "RHI-BINDLESS-ROOT-SEQUENCE-CLOSURE-PLAN-DUPLICATE";
            }
            bool consumed = false;
            for (std::size_t binding = 0u; binding < input.count; ++binding) {
                if (!visit())
                    return "RHI-BINDLESS-ROOT-SEQUENCE-CLOSURE-PLAN-VISIT-CAPACITY";
                consumed = consumed
                    || (plan.root.get() == input.roots[binding].get()
                        && core::SharedOwnerProjection::sameOwner(plan.root, input.roots[binding]));
            }
            if (!consumed)
                return "RHI-BINDLESS-ROOT-SEQUENCE-CLOSURE-PLAN-UNCONSUMED";
        }
        for (std::size_t binding = 0u; binding < input.count; ++binding) {
            bool covered = false;
            for (std::size_t i = 0u; i < input.closurePlanCount; ++i) {
                if (!visit())
                    return "RHI-BINDLESS-ROOT-SEQUENCE-CLOSURE-PLAN-VISIT-CAPACITY";
                const auto& plan = input.closurePlans[i];
                covered = covered
                    || (plan.root.get() == input.roots[binding].get()
                        && core::SharedOwnerProjection::sameOwner(plan.root, input.roots[binding]));
            }
            if (!covered)
                return "RHI-BINDLESS-ROOT-SEQUENCE-CLOSURE-PLAN-COVERAGE";
        }
        return nullptr;
    }

    // Historical inputs remain an exact prefix, never an alternative owner
    // list. A nonempty sequence is inspected in full before native allocation.
    static const char* inspectPublicInput(const Input& input,
        const std::array<std::shared_ptr<const Root>, 3u>& historical,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream) noexcept
    {
        if (input.empty())
            return inspectOriginal(historical.data(),
                1u + bool(historical[1u]) + bool(historical[2u]),
                inventory,
                stream);
        if (const auto* id =
                inspect(input.roots, input.count, inventory, stream, input.budget).diagnostic)
            return id;
        if (const auto* id = inspectClosurePlans(input))
            return id;
        for (std::size_t i = 0u; i < historical.size(); ++i) {
            const bool present = i < input.count;
            if (present != bool(historical[i])
                || (present
                    && (input.roots[i].get() != historical[i].get()
                        || !core::SharedOwnerProjection::sameOwner(input.roots[i], historical[i]))))
                return "RHI-BINDLESS-ROOT-SEQUENCE-HISTORICAL-CORRESPONDENCE";
        }
        return nullptr;
    }

    struct Inspection {
        const char* diagnostic = nullptr;
        std::uint64_t bindings = 0u, computeBindings = 0u, graphicsBindings = 0u;
        std::uint64_t resourceVisits = 0u;

        constexpr bool nativeExecutionProven() const noexcept { return false; }
    };

    static constexpr const char* validateBudget(const Budget& b) noexcept
    {
        if (b.contractVersion != 1u && b.contractVersion != 2u)
            return "RHI-BINDLESS-ROOT-SEQUENCE-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-BINDLESS-ROOT-SEQUENCE-OPT-IN";
        if (b.contractVersion == 1u ? !b.snapshotScope.empty() : b.snapshotScope.empty())
            return "RHI-BINDLESS-ROOT-SEQUENCE-SNAPSHOT-SCOPE";
        if (const auto* id = OriginalBindlessSnapshotScopeContract::validateIntent(b.snapshotScope))
            return id;
        return b.generation && b.maxBindings && b.maxResourceVisits
            ? nullptr
            : "RHI-BINDLESS-ROOT-SEQUENCE-BUDGET";
    }

    static Inspection inspect(const std::shared_ptr<const Root>* roots,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const Budget& budget) noexcept
    {
        if (!roots)
            return {"RHI-BINDLESS-ROOT-SEQUENCE-BINDING-CAPACITY"};
        return inspectProjected(
            count,
            [&](std::size_t i) noexcept -> const auto& {
                return roots[i];
            },
            inventory,
            stream,
            budget);
    }

    // Native closure storage projects its authentic Roots directly. Reuse the
    // authoritative stream inspection without allocating a second owner list.
    template <class RootAt>
    static Inspection inspectProjected(std::size_t count,
        RootAt&& rootAt,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const Budget& budget) noexcept
    {
        const auto reject = [](const char* id) noexcept {
            Inspection result;
            result.diagnostic = id;
            return result;
        };
        if (const auto* id = validateBudget(budget))
            return reject(id);
        if (!count || count > budget.maxBindings)
            return reject("RHI-BINDLESS-ROOT-SEQUENCE-BINDING-CAPACITY");
        if (const auto* id = OriginalCommandResourceSources::validateOriginal(inventory))
            return reject(id);
        if (const auto* id = OriginalCommandStreamProgram::validateOriginal(stream))
            return reject(id);
        const auto& sources =
            stream->originalInputs()->originalScalars()->originalUsage()->originalSources();
        if (!core::SharedOwnerProjection::sameOwner(sources, inventory))
            return reject("RHI-BINDLESS-ROOT-SEQUENCE-INVENTORY");
        if (budget.contractVersion == 2u
            && stream->originalBudget().bindlessRoots.contractVersion != 6u)
            return reject("RHI-BINDLESS-ROOT-SEQUENCE-SNAPSHOT-SCOPE-PROFILE");
        Inspection result;
        const auto visit = [&]() noexcept {
            if (result.resourceVisits == budget.maxResourceVisits)
                return false;
            ++result.resourceVisits;
            return true;
        };
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& root = rootAt(i);
            if (const auto* id = Root::validateOriginal(root))
                return reject(id);
            if (!root->matchesCanonicalSources(*inventory)
                || ((budget.contractVersion == 1u || !i)
                    && root->originalSnapshot()->generation() != budget.generation))
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-GENERATION-OR-SOURCES");
            if (budget.contractVersion == 1u
                && !core::SharedOwnerProjection::sameOwner(
                    root->originalSnapshot(), rootAt(0u)->originalSnapshot()))
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-SNAPSHOT");
        }
        if (budget.contractVersion == 2u) {
            const auto scope = OriginalBindlessSnapshotScopeContract::inspect(
                count,
                [&](std::size_t i) noexcept {
                    return rootAt(i).get();
                },
                budget.snapshotScope);
            if (scope.diagnostic)
                return reject(scope.diagnostic);
        }
        const auto& references =
            stream->originalInputs()->originalScalars()->originalUsage()->references();
        for (const auto& op : stream->operations()) {
            if (!visit())
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-VISIT-CAPACITY");
            const bool compute = op.kind == CommandUsageKind::BindPreparedComputeBinding;
            if (!compute && op.kind != CommandUsageKind::BindPreparedGraphicsBinding)
                continue;
            if (result.bindings == count || op.referenceCount != 1u
                || op.firstReference >= references.size())
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-ORDER");
            const auto index = references[op.firstReference].sourceIndex;
            if (index >= inventory->sources().size())
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-INVENTORY");
            const auto* root =
                std::get_if<std::shared_ptr<const Root>>(&inventory->sources()[index]);
            if (!visit())
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-VISIT-CAPACITY");
            const auto& expected = rootAt(result.bindings);
            if (!root || !core::SharedOwnerProjection::sameOwner(*root, expected)
                || root->get() != expected.get())
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-ORDER");
            if (compute
                != std::holds_alternative<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                    expected->originalPrepared()))
                return reject("RHI-BINDLESS-ROOT-SEQUENCE-STAGE");
            ++result.bindings;
            result.computeBindings += compute;
            result.graphicsBindings += !compute;
        }
        return result.bindings == count ? result : reject("RHI-BINDLESS-ROOT-SEQUENCE-UNCONSUMED");
    }

    // Selection is by canonical Source identity, never a default-first Root.
    // This works with any bounded native storage; a missing/ambiguous mapping
    // fails closed. The caller still authenticates each native closure and seal.
    template <class ClosureAt>
    static auto selectClosure(std::size_t count,
        ClosureAt&& closureAt,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        std::size_t sourceIndex) noexcept -> decltype(closureAt(std::size_t{}).get())
    {
        decltype(closureAt(std::size_t{}).get()) found = nullptr;
        if (!inventory || sourceIndex >= inventory->sources().size())
            return nullptr;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& closure = closureAt(i);
            if (!closure
                || !core::SharedOwnerProjection::sameOwner(closure->originalInventory(), inventory))
                continue;
            if (inventory->originalLeaseIndex(closure->originalRoot()) != sourceIndex)
                continue;
            if (found && found != closure.get())
                return nullptr;
            found = closure.get();
        }
        return found;
    }

    // Existing execution profiles already carry command ceilings in their
    // original stream budget. Reuse those bounds; no backend-selected capacity.
    static const char* inspectOriginal(const std::shared_ptr<const Root>* roots,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream) noexcept
    {
        if (!roots || !count || !stream)
            return "RHI-BINDLESS-ROOT-SEQUENCE-INPUTS";
        if (const auto* id = Root::validateOriginal(roots[0]))
            return id;
        const auto commands = stream->originalBudget().maxCommands;
        if (count > UINT64_MAX - commands)
            return "RHI-BINDLESS-ROOT-SEQUENCE-VISIT-OVERFLOW";
        return inspect(roots,
            count,
            inventory,
            stream,
            {1u, true, roots[0]->originalSnapshot()->generation(), count, commands + count})
            .diagnostic;
    }
};
} // namespace arp::rhi
