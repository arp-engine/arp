#pragma once
#include "rhi/frontend/bindless_graphics_pass_preparation.h"
#include "rhi/frontend/bindless_root_native_sequence.h"

namespace arp::rhi {
// Cold native-pass budgets over the same produced stream. No second attachment
// inventory, state graph or execution/completion issuer is introduced.
struct OriginalBindlessProducedPassInputs {
    OriginalBindlessGraphicsPassBudget pass;
    std::uint64_t maxPasses = 0u, maxCommandVisits = 0u;
    std::uint64_t maxRetainedOwners = 0u;
    std::shared_ptr<core::AtomicCapacityBudget> capacity;
    std::shared_ptr<core::CpuMemoryQuota> quota;

    bool empty() const noexcept
    {
        return !pass.contractVersion && !pass.prototypeOptIn && !pass.maxAttachmentViews
            && !pass.maxNativeObjects && !pass.maxRetainedOwners && !pass.maxCpuMetadataBytes
            && !pass.attachmentClear.contractVersion && !pass.attachmentClear.prototypeOptIn
            && pass.commandIndex == SIZE_MAX && !pass.maxCommandVisits && !maxPasses
            && !maxCommandVisits && !maxRetainedOwners && !capacity && !quota;
    }
};

class OriginalBindlessGraphicsPassSequenceContract final {
public:
    template <class Pass> struct Input {
        const std::shared_ptr<const Pass>* passes = nullptr;
        std::size_t count = 0u;

        bool empty() const noexcept { return !passes && !count; }
    };

    struct Footprint {
        std::uint64_t owners = 0u, nativeObjects = 0u;
    };

    template <class Pass, class Transaction, class Closure>
    static const char* inspectPrepared(const Input<Pass>& input,
        const std::shared_ptr<const Transaction>& transaction,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const typename OriginalBindlessRootNativeSequence<Closure>::Input& roots,
        Footprint& output) noexcept
    {
        output = {};
        if (input.empty())
            return stream && stream->originalBudget().bindlessRoots.contractVersion == 6u
                ? "RHI-BINDLESS-GRAPHICS-SEQUENCE-INPUTS-REQUIRED"
                : nullptr;
        if (!input.passes || !input.count || !stream || !transaction
            || !transaction->hasOriginalBindlessProducedGeometryProfile()
            || stream->originalBudget().bindlessRoots.contractVersion != 6u || !roots.closures
            || !roots.count || input.count > stream->originalBudget().maxRenderPasses)
            return "RHI-BINDLESS-GRAPHICS-SEQUENCE-PREPARED-INPUTS";
        const auto& inventory =
            stream->originalInputs()->originalScalars()->originalUsage()->originalSources();
        Footprint result;
        std::size_t index = 0u;
        for (const auto& op : stream->operations()) {
            if (op.kind != CommandUsageKind::BeginRenderPass)
                continue;
            if (index == input.count)
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-PASS-COVERAGE";
            const auto& pass = input.passes[index++];
            if (const auto* id = Pass::validateOriginal(pass))
                return id;
            if (pass->commandIndex() != op.commandIndex
                || !core::SharedOwnerProjection::sameOwner(pass->originalStream(), stream)
                || !core::SharedOwnerProjection::sameOwner(
                    pass->originalTransaction(), transaction))
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-PREPARED-CORRESPONDENCE";
            const auto* closure = OriginalBindlessRootSequenceContract::selectClosure(
                roots.count,
                [&](std::size_t i) noexcept -> const auto& {
                    return roots.closures[i];
                },
                inventory,
                inventory->originalLeaseIndex(pass->originalClosure()->originalRoot()));
            if (closure != pass->originalClosure().get())
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-UNRETAINED-ROOT";
            const auto owners = std::uint64_t(pass->retainedOwnerCount()) + 1u;
            if (owners > UINT64_MAX - result.owners
                || pass->nativeObjectCount() > UINT64_MAX - result.nativeObjects)
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-FOOTPRINT-OVERFLOW";
            result.owners += owners;
            result.nativeObjects += pass->nativeObjectCount();
        }
        if (index != input.count)
            return "RHI-BINDLESS-GRAPHICS-SEQUENCE-PASS-COVERAGE";
        output = result;
        return nullptr;
    }

    // Prepare into a temporary vector; a failure destroys all already prepared
    // passes and returns every capacity charge before the seal can be claimed.
    template <class Closure, class Pass, class Prepare>
    static const char* prepare(const std::shared_ptr<const Closure>* closures,
        std::size_t count,
        const std::shared_ptr<const OriginalCommandStreamProgram>& stream,
        const OriginalBindlessProducedPassInputs& input,
        core::pmr::Vector<std::shared_ptr<const Pass>>& output,
        Prepare&& preparePass)
    {
        if (!output.empty())
            return "RHI-BINDLESS-GRAPHICS-SEQUENCE-DESTINATION-NOT-EMPTY";
        if (!stream)
            return "RHI-BINDLESS-GRAPHICS-SEQUENCE-STREAM";
        const bool produced = stream->originalBudget().bindlessRoots.contractVersion == 6u;
        if (input.empty())
            return produced ? "RHI-BINDLESS-GRAPHICS-SEQUENCE-INPUTS-REQUIRED" : nullptr;
        if (!produced || !closures || !count || !input.maxPasses || !input.maxCommandVisits
            || !input.maxRetainedOwners || !input.capacity || !input.quota
            || input.pass.contractVersion != 2u || input.pass.commandIndex != SIZE_MAX
            || input.pass.maxCommandVisits || input.pass.attachmentClear.contractVersion
            || input.pass.attachmentClear.prototypeOptIn
            || input.quota->limitBytes() != input.pass.maxCpuMetadataBytes
            || input.capacity->capacity() != input.pass.maxNativeObjects)
            return "RHI-BINDLESS-GRAPHICS-SEQUENCE-INPUTS";
        auto intent = input.pass;
        intent.commandIndex = 0u;
        intent.maxCommandVisits = input.maxCommandVisits;
        if (const auto* id = OriginalBindlessGraphicsPassContract::validateBudget(intent))
            return id;
        const auto& operations = stream->operations();
        const auto& usage = *stream->originalInputs()->originalScalars()->originalUsage();
        const auto& inventory = usage.originalSources();
        std::uint64_t visits = 0u, passes = 0u, attachments = 0u;
        const auto visit = [&](std::uint64_t cost) noexcept {
            if (cost > input.maxCommandVisits - visits)
                return false;
            visits += cost;
            return true;
        };
        // Count and inspect the entire batch before allocating the first pass.
        for (const auto& op : operations) {
            if (!visit(1u))
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-VISIT-CAPACITY";
            if (op.kind != CommandUsageKind::BeginRenderPass)
                continue;
            if (++passes > input.maxPasses || op.referenceCount > input.pass.maxAttachmentViews
                || !op.referenceCount || op.referenceCount > UINT64_MAX - attachments)
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-PASS-CAPACITY";
            attachments += op.referenceCount;
        }
        if (!passes || passes > SIZE_MAX
            || attachments > inventory->originalBudget().maxPassAttachmentReferences)
            return "RHI-BINDLESS-GRAPHICS-SEQUENCE-ATTACHMENT-CAPACITY";
        core::pmr::Vector<std::shared_ptr<const Pass>> prepared(output.get_allocator());
        prepared.reserve(static_cast<std::size_t>(passes));
        std::uint64_t owners = 0u, nativeObjects = 0u;
        for (const auto& op : operations) {
            if (!visit(1u))
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-VISIT-CAPACITY";
            if (op.kind != CommandUsageKind::BeginRenderPass)
                continue;
            OriginalBindlessGraphicsPassContract::RootSelection selection;
            if (const auto* id = OriginalBindlessGraphicsPassContract::selectRoot(operations,
                    usage.references(),
                    op.commandIndex,
                    input.maxCommandVisits - visits,
                    selection))
                return id;
            if (!visit(selection.visits))
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-VISIT-CAPACITY";
            if (!visit(count))
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-VISIT-CAPACITY";
            const auto* selected = OriginalBindlessRootSequenceContract::selectClosure(
                count,
                [&](std::size_t i) noexcept -> const auto& {
                    return closures[i];
                },
                inventory,
                selection.source);
            if (!selected)
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-UNRETAINED-ROOT";
            std::shared_ptr<const Closure> owner;
            for (std::size_t i = 0u; i < count; ++i) {
                if (!visit(1u))
                    return "RHI-BINDLESS-GRAPHICS-SEQUENCE-VISIT-CAPACITY";
                if (closures[i].get() == selected)
                    owner = closures[i];
            }
            auto budget = input.pass;
            budget.commandIndex = op.commandIndex;
            budget.maxCommandVisits = operations.size();
            if (!visit(budget.maxCommandVisits))
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-VISIT-CAPACITY";
            const char* diagnostic = nullptr;
            auto pass = preparePass(owner, budget, diagnostic);
            if (!pass || diagnostic)
                return diagnostic ? diagnostic : "RHI-BINDLESS-GRAPHICS-SEQUENCE-FACTORY";
            if ((diagnostic = Pass::validateOriginal(pass)))
                return diagnostic;
            if (pass->commandIndex() != op.commandIndex
                || pass->retainedOwnerCount() > input.maxRetainedOwners - owners
                || pass->nativeObjectCount() > input.capacity->capacity() - nativeObjects
                || nativeObjects + pass->nativeObjectCount()
                    >= inventory->originalBudget().maxNativePayloadObjects)
                return "RHI-BINDLESS-GRAPHICS-SEQUENCE-OWNER-CAPACITY";
            owners += pass->retainedOwnerCount();
            nativeObjects += pass->nativeObjectCount();
            prepared.push_back(std::move(pass));
        }
        output.swap(prepared);
        return nullptr;
    }
};
} // namespace arp::rhi
