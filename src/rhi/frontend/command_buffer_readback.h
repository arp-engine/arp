#pragma once
#include "rhi/frontend/bindless_snapshot_scope.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/frontend/command_stream_program.h"

namespace arp::rhi {
struct OriginalCommandBufferReadbackBindingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRetainedOwners = 0u;
    std::uint64_t maxOperationVisits = 0u, maxCpuMetadataBytes = 0u;
};

// Binds one real buffer request/result to the already validated original
// RootState /4 copy operation. This retains that same graph/program through
// future map custody, never creates another graph or a publication issuer.
class OriginalCommandBufferReadbackBinding final : public core::QuotaBoundSharedObjectIdentity {
    friend class OriginalBindlessRootReadbackSequence;
    friend class OriginalBindlessRootFaultCustody;
    friend class OriginalCommandBufferReadbackResultContract;

    class ConstructionKey {
        friend class OriginalCommandBufferReadbackBinding;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Graph = OriginalCommandResourceStates;
    using Program = OriginalCommandStreamProgram;
    using Request = BufferReadbackRequestDeclaration;
    using Result = OriginalCommandBufferReadbackResultSnapshot;

public:
    // Exact immediate owners; nested graph/program ownership remains the
    // existing canonical closure and must be projected once by native admission.
    static constexpr std::uint32_t retainedOwnerCount = 5u;

    OriginalCommandBufferReadbackBinding(ConstructionKey,
        std::shared_ptr<const Graph> graph,
        std::shared_ptr<const Program> program,
        std::shared_ptr<const Request> request,
        std::shared_ptr<const Result> result,
        std::size_t command,
        std::size_t root,
        OriginalCommandBufferReadbackBindingBudget budget)
        : graph_(std::move(graph)), program_(std::move(program)), request_(std::move(request)),
          result_(std::move(result)), command_(command), root_(root), budget_(budget)
    {}

    static constexpr const char* validateBudget(
        const OriginalCommandBufferReadbackBindingBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u && budget.contractVersion != 2u
            && budget.contractVersion != 3u && budget.contractVersion != 4u
            && budget.contractVersion != 5u && budget.contractVersion != 6u)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-OPT-IN";
        if (budget.maxRetainedOwners < retainedOwnerCount)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-OWNERS";
        return budget.maxOperationVisits && budget.maxCpuMetadataBytes
            ? nullptr
            : "RHI-ORIGINAL-BUFFER-READBACK-BINDING-BUDGET";
    }

    // /6 observes declared resource results from direct Root5/Inputs7 graphics.
    // /5 stays Root6/Inputs8; neither profile can borrow the other's authority.
    static constexpr std::uint32_t rootStateVersion(std::uint32_t version) noexcept
    {
        return version == 4u || version == 5u ? 6u
            : version == 3u || version == 6u  ? 5u
            : version == 1u || version == 2u  ? 4u
                                              : 0u;
    }

    static constexpr const char* validateProfile(std::uint32_t version,
        std::uint32_t roots,
        std::uint32_t state,
        std::uint32_t inputs,
        std::uint32_t geometryDraw,
        bool geometryOptIn) noexcept
    {
        const auto expected = rootStateVersion(version);
        if (!expected || roots != expected
            || (version >= 3u
                && (state != 1u || inputs != (expected == 6u ? 8u : 7u) || geometryDraw != 1u
                    || !geometryOptIn)))
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-PROFILE";
        return nullptr;
    }

    static std::shared_ptr<const OriginalCommandBufferReadbackBinding> prepareOriginal(
        std::shared_ptr<const Graph> graph,
        std::shared_ptr<const Program> program,
        std::shared_ptr<const Request> request,
        std::shared_ptr<const Result> result,
        std::size_t command,
        std::size_t root,
        const GpuDevice& device,
        const OriginalCommandBufferReadbackBindingBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(budget);
        if (diagnostic)
            return {};
        if ((diagnostic = semantics::OriginalBufferReadbackBindingLowering::lowering(
                 device.backend(), budget.contractVersion)
                    .diagnostic))
            return {};
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-ORIGINAL-BUFFER-READBACK-BINDING-QUOTA";
            return {};
        }
        if ((diagnostic =
                    validateOwners(graph, program, request, result, command, root, budget, quota)))
            return {};
        const auto& sources =
            graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        if ((diagnostic =
                    OriginalCommandResourceSources::validateRegisteredLifetime(sources, device)))
            return {};
        if (device.overlapsOriginalDeviceCpuOwner(quota)) {
            diagnostic = "RHI-ORIGINAL-BUFFER-READBACK-BINDING-QUOTA-DOMAIN";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            diagnostic = nullptr;
            return shareOriginal(
                core::makeUnique<OriginalCommandBufferReadbackBinding>(ConstructionKey{},
                    std::move(graph),
                    std::move(program),
                    std::move(request),
                    std::move(result),
                    command,
                    root,
                    budget));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-BUFFER-READBACK-BINDING-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandBufferReadbackBinding>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-OWNER";
        return self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = validateBudget(budget_))
            return id;
        if (!originalMetadataQuota()
            || originalMetadataQuota()->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-QUOTA";
        return validateOwners(
            graph_, program_, request_, result_, command_, root_, budget_, originalMetadataQuota());
    }

    static BindlessFaultSubmissionLease acquireOriginalFaultCustody(
        const std::shared_ptr<const OriginalCommandBufferReadbackBinding>& self,
        core::pmr::String& diagnostic)
    {
        if (const auto* id = validateOriginal(self)) {
            diagnostic = id;
            return {};
        }
        if (!self->supportsOriginalPendingCustody()) {
            diagnostic = "RHI-ORIGINAL-BUFFER-READBACK-PENDING-VERSION";
            return {};
        }
        if (!self->isOriginalSnapshotFaultReadback()) {
            diagnostic = "RHI-ORIGINAL-BUFFER-READBACK-FAULT-BINDING-REQUIRED";
            return {};
        }
        return BindlessFaultSubmissionLease::acquireOriginalJournal(
            self->originalSnapshot(), diagnostic);
    }

    // CPU-only inspection of existing Pending cells, not a substitute for a
    // backend-owned recording seal or authentic native completion evidence.
    static const char* validateOriginalPending(
        const std::shared_ptr<const OriginalCommandBufferReadbackBinding>& self,
        const std::shared_ptr<const OriginalCommandStateTransaction>& transaction,
        const BindlessFaultSubmissionLease& fault) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-OWNER";
        if (!self->supportsOriginalPendingCustody())
            return "RHI-ORIGINAL-BUFFER-READBACK-PENDING-VERSION";
        if (!transaction || !transaction->hasQuotaBoundSharedOwner(transaction))
            return "RHI-ORIGINAL-TRANSACTION-OWNER";
        if (const auto* id = transaction->validate())
            return id;
        const auto* planned = transaction->originalPlannedSequenceBudget();
        if (transaction->status() != OriginalCommandStateTransaction::Status::Pending || !planned)
            return "RHI-ORIGINAL-BUFFER-READBACK-PENDING-STATUS";
        if (!core::SharedOwnerProjection::sameOwner(transaction->originalGraph(), self->graph_))
            return "RHI-ORIGINAL-BUFFER-READBACK-PENDING-GRAPH";
        if (!self->matchesOriginalSequence(*planned))
            return "RHI-ORIGINAL-BUFFER-READBACK-PENDING-SEQUENCE";
        if (!fault.originalJournalOwned()
            || !core::SharedOwnerProjection::sameOwner(
                fault.originalDomain(), self->originalSnapshot()))
            return "RHI-ORIGINAL-BUFFER-READBACK-PENDING-FAULT-CUSTODY";
        if (!self->originalMetadataQuota()
            || self->originalMetadataQuota()->limitBytes() != self->budget_.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-QUOTA";
        return validateOwners(self->graph_,
            self->program_,
            self->request_,
            self->result_,
            self->command_,
            self->root_,
            self->budget_,
            self->originalMetadataQuota(),
            true);
    }

    const auto& originalGraph() const noexcept { return graph_; }

    const auto& originalProgram() const noexcept { return program_; }

    const auto& originalRequest() const noexcept { return request_; }

    const auto& originalResult() const noexcept { return result_; }

    std::size_t commandIndex() const noexcept { return command_; }

    std::size_t rootSourceIndex() const noexcept { return root_; }

    // Both leases are projections of the original copy operands. No second
    // resource owner or readback allocation is manufactured by this binding.
    const std::shared_ptr<const OriginalCommandBufferLease>& originalCopySource() const noexcept
    {
        return originalCopyOperand(0u);
    }

    const std::shared_ptr<const OriginalCommandBufferLease>& originalReadbackSource() const noexcept
    {
        return originalCopyOperand(1u);
    }

    bool isOriginalSnapshotFaultReadback() const noexcept
    {
        const auto& snapshot = originalSnapshot();
        return core::SharedOwnerProjection::sameOwner(originalCopySource(),
                   snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch))
            && core::SharedOwnerProjection::sameOwner(originalReadbackSource(),
                snapshot->originalBufferSource(
                    BindlessSnapshot::OriginalBufferRole::FaultReadback));
    }

    bool supportsOriginalPendingCustody() const noexcept
    {
        return budget_.contractVersion >= 2u && budget_.contractVersion <= 6u;
    }

    bool sharesOriginalFaultDomain(const OriginalCommandBufferReadbackBinding& other) const noexcept
    {
        return core::SharedOwnerProjection::sameOwner(originalSnapshot(), other.originalSnapshot())
            && core::SharedOwnerProjection::sameOwner(
                originalRootSource()->authorization(), other.originalRootSource()->authorization());
    }

    bool matchesOriginalSequence(const OriginalCommandStateSequenceBudget& actual) const noexcept
    {
        const auto fields = [](const auto& b) noexcept {
            return std::tie(b.contractVersion,
                b.prototypeOptIn,
                b.maxCommands,
                b.maxDebugDepth,
                b.maxStateCells,
                b.maxSteps,
                b.maxImplicitAccesses,
                b.bindlessRootStateVersion,
                b.bindlessRootStateOptIn);
        };
        const auto& expected = program_->originalBudget().stateSequence;
        return expected.bindlessRootStateVersion == rootStateVersion(budget_.contractVersion)
            && fields(actual) == fields(expected);
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::shared_ptr<const OriginalCommandBindlessRootSource>&
    originalRootSource() const noexcept
    {
        return std::get<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
            graph_->originalInputs()
                ->originalScalars()
                ->originalUsage()
                ->originalSources()
                ->sources()[root_]);
    }

    const std::shared_ptr<const OriginalCommandBufferLease>& originalCopyOperand(
        std::size_t operand) const noexcept
    {
        const auto& usage = *graph_->originalInputs()->originalScalars()->originalUsage();
        const auto& copy = usage.commands()[command_];
        return std::get<std::shared_ptr<const OriginalCommandBufferLease>>(usage.originalSources()
                ->sources()[usage.references()[copy.firstReference + operand].sourceIndex]);
    }

    const std::shared_ptr<const BindlessSnapshot>& originalSnapshot() const noexcept
    {
        return originalRootSource()->originalSnapshot();
    }

    static const char* validateOwners(const std::shared_ptr<const Graph>& graph,
        const std::shared_ptr<const Program>& program,
        const std::shared_ptr<const Request>& request,
        const std::shared_ptr<const Result>& result,
        std::size_t command,
        std::size_t rootIndex,
        const OriginalCommandBufferReadbackBindingBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool pending = false) noexcept
    {
        if (const auto* id = validateBudget(budget))
            return id;
        if (!pending) {
            if (const auto* id = Graph::validateOriginal(graph))
                return id;
            if (const auto* id = Program::validateOriginal(program))
                return id;
            if (const auto* id = program->compareOriginalStates(graph))
                return id;
        }
        if (const auto* id = validateProfile(budget.contractVersion,
                program->originalBudget().bindlessRoots.contractVersion,
                graph->stateContractVersion(),
                graph->originalInputs()->originalBudget().contractVersion,
                program->originalBudget().geometryUsage.drawConsumptionContractVersion,
                program->originalBudget().geometryUsage.drawConsumptionOptIn))
            return id;
        if (const auto* id = Request::validateOriginal(request))
            return id;
        if (const auto* id = Result::validateOriginalCold(result, request))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(quota, request->originalMetadataQuota()))
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-QUOTA-DOMAIN";
        const auto& inputs = graph->originalInputs();
        const auto& usage = *inputs->originalScalars()->originalUsage();
        const auto& inventory = usage.originalSources()->sources();
        if (command >= program->operations().size()
            || program->operations()[command].kind != CommandUsageKind::BufferCopy)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-COMMAND";
        if (rootIndex >= inventory.size())
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-ROOT";
        const auto* root = std::get_if<std::shared_ptr<const OriginalCommandBindlessRootSource>>(
            &inventory[rootIndex]);
        if (!root)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-ROOT";
        const auto& snapshot = (*root)->originalSnapshot();
        if (request->identity().resourceGeneration != snapshot->generation())
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-GENERATION";
        const auto& copy = usage.commands()[command];
        if (copy.referenceCount != 2u)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-COPY";
        const auto sourceIndex = usage.references()[copy.firstReference].sourceIndex;
        const auto destinationIndex = usage.references()[copy.firstReference + 1u].sourceIndex;
        const auto* source =
            std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&inventory[sourceIndex]);
        const auto* destination = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
            &inventory[destinationIndex]);
        const auto& latch =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
        const auto& faultReadback =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
        if (!source || !destination)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-COPY";
        const bool fault = core::SharedOwnerProjection::sameOwner(*source, latch)
            && core::SharedOwnerProjection::sameOwner(*destination, faultReadback);
        if (!fault && budget.contractVersion != 5u && budget.contractVersion != 6u)
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-COPY";
        if (budget.contractVersion == 6u
            && !std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                (*root)->originalPrepared()))
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-DIRECT-GRAPHICS-ROOT";
        if (!fault && budget.contractVersion == 6u
            && program->originalBudget().bindlessRoots.directResultCopies.empty())
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-DIRECT-COPY-INTENT";
        const auto& readback = *destination;
        if (const auto* id = request->matchesOriginalSource(readback))
            return id;
        const auto* descriptor = inputs->descriptorAt(command);
        const auto* arguments =
            descriptor ? std::get_if<TransferCopyArguments>(descriptor) : nullptr;
        BufferCopyDesc fields;
        if (!arguments || !arguments->write(fields)
            || fields.destinationOffsetBytes != request->offsetBytes()
            || fields.sizeBytes != request->byteCapacity())
            return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-RANGE";
        std::uint64_t visits = 0u;
        if (!fault) {
            // /5 (Root6) and /6 (direct Root5) join resource results to the
            // same snapshot journal. Only an explicitly
            // declared storage-buffer access range may be observed.
            bool declared = false;
            for (const auto& element : (*root)->elements()) {
                if (visits == budget.maxOperationVisits)
                    return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-VISIT-CAPACITY";
                ++visits;
                const auto& access = element.declaration;
                if (access.sourceIndex != sourceIndex
                    || (access.write.type != DescriptorType::StorageBuffer
                        && access.write.type != DescriptorType::ReadOnlyStorageBuffer))
                    continue;
                if (core::containsUnsignedRange(access.bufferAccessOffset,
                        access.bufferAccessBytes,
                        fields.sourceOffsetBytes,
                        fields.sizeBytes))
                    declared = true;
            }
            if (!declared)
                return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-UNDECLARED-SOURCE-RANGE";
        }
        // One request cannot ambiguously observe two writes to its readback
        // child. Scan only immutable operations under an independent ceiling.
        for (const auto& operation : program->operations()) {
            if (visits == budget.maxOperationVisits)
                return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-VISIT-CAPACITY";
            ++visits;
            if (operation.kind != CommandUsageKind::BufferCopy || operation.commandIndex == command)
                continue;
            const auto index = usage.references()[operation.firstReference + 1u].sourceIndex;
            if (OriginalCommandResourceSources::resource(inventory[index]) == readback->resource())
                return "RHI-ORIGINAL-BUFFER-READBACK-BINDING-DUPLICATE-WRITE";
        }
        return nullptr;
    }

    const std::shared_ptr<const Graph> graph_;
    const std::shared_ptr<const Program> program_;
    const std::shared_ptr<const Request> request_;
    const std::shared_ptr<const Result> result_;
    const std::size_t command_, root_;
    const OriginalCommandBufferReadbackBindingBudget budget_;
};

// Derive the existing Result/Map lifetime counts from authenticated bindings,
// not from caller-supplied texture requests or a second resource inventory.
class OriginalCommandBufferReadbackResultContract final {
public:
    using Binding = OriginalCommandBufferReadbackBinding;
    using Common = OriginalCommandReadbackResultContract;

    static Common::Inspection inspect(const std::shared_ptr<const Binding>* bindings,
        std::size_t count,
        std::uint64_t maxVisits,
        const Common::Budget& budget,
        const OriginalBindlessSnapshotScopeContract::Intent& snapshotScope = {}) noexcept
    {
        Common::Inspection inspected;
        const auto reject = [&](const char* id) noexcept {
            inspected.diagnostic = id;
            inspected.required = {};
            return inspected;
        };
        if (const auto* id = Common::validateIntent(budget))
            return reject(id);
        if (!bindings || !count || count > budget.maxReadbackCommands
            || count > budget.maxNativeCopyCommands || count > budget.maxResultDeclarations)
            return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-COUNT");
        Common::Requirements pending;
        const auto& first = bindings[0];
        if (const auto* id = Binding::validateOriginal(first))
            return reject(id);
        const auto& program = first->originalProgram();
        if (const auto* id = OriginalBindlessSnapshotScopeContract::validateIntent(snapshotScope))
            return reject(id);
        if (!snapshotScope.empty() && program->originalBudget().bindlessRoots.contractVersion != 6u)
            return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-SNAPSHOT-SCOPE-PROFILE");
        const auto& resultQuota = first->originalResult()->originalMetadataQuota();
        if (resultQuota->limitBytes() != budget.maxCpuResultBytes)
            return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-QUOTA");
        std::uint64_t visits = 0u, copyCount = 0u;
        bool exhausted = false;
        const auto visit = [&]() noexcept {
            if (visits == maxVisits) {
                exhausted = true;
                return false;
            }
            ++visits;
            return true;
        };
        for (const auto& operation : program->operations()) {
            if (!visit())
                return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-VISIT-CAPACITY");
            if (operation.kind == CommandUsageKind::BufferCopy)
                ++copyCount;
        }
        if (copyCount != count)
            return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-COPY-COVERAGE");
        for (std::size_t i = 0u; i < count; ++i) {
            if (!visit())
                return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-VISIT-CAPACITY");
            const auto& binding = bindings[i];
            if (const auto* id = Binding::validateOriginal(binding))
                return reject(id);
            if (!core::SharedOwnerProjection::sameOwner(program, binding->originalProgram())
                || !core::SharedOwnerProjection::sameOwner(
                    first->originalGraph(), binding->originalGraph())
                || (snapshotScope.empty() && !first->sharesOriginalFaultDomain(*binding)))
                return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-GRAPH");
            if (!core::SharedOwnerProjection::sameOwner(
                    resultQuota, binding->originalResult()->originalMetadataQuota()))
                return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-QUOTA");
            const auto bytes = binding->originalRequest()->byteCapacity();
            if (bytes > budget.maxDeclaredReadbackBytes - pending.declaredReadbackBytes
                || bytes > budget.maxUniqueResultBytes - pending.uniqueResultBytes)
                return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-BYTE-CAPACITY");
            pending.declaredReadbackBytes += bytes;
            pending.uniqueResultBytes += bytes;
        }
        if (!snapshotScope.empty()) {
            const auto scoped = OriginalBindlessSnapshotScopeContract::inspect(
                count,
                [&](std::size_t i) noexcept {
                    return bindings[i]->originalRootSource().get();
                },
                snapshotScope);
            if (scoped.diagnostic)
                return reject(scoped.diagnostic);
        }
        // The shared projection performs original owner/ID alias validation.
        // Each lookup charges the same explicit visit ceiling before access.
        for (bool requests : {false, true}) {
            const auto unique = core::SharedIdentityProjection::inspect(count,
                count,
                count,
                [&](std::size_t i) noexcept -> core::SharedIdentityProjection::Record {
                    if (!visit())
                        return {};
                    const auto& binding = bindings[i];
                    if (requests) {
                        const auto& request = binding->originalRequest();
                        return {request->identity().requestId, 1u, request, request.get()};
                    }
                    return {binding->commandIndex() + 1u, 1u, binding, binding.get()};
                });
            if (exhausted)
                return reject("RHI-ORIGINAL-BUFFER-READBACK-RESULT-VISIT-CAPACITY");
            if (unique.status != core::SharedIdentityProjection::Status::Complete
                || unique.identities != count)
                return reject(requests ? "RHI-ORIGINAL-BUFFER-READBACK-RESULT-REQUEST-IDENTITY"
                                       : "RHI-ORIGINAL-BUFFER-READBACK-RESULT-COPY-IDENTITY");
        }
        pending.readbackCommands = pending.nativeCopyCommands = pending.resultDeclarations = count;
        pending.cpuResultBytes = resultQuota->limitBytes();
        if (const auto* id = Common::validateRequirements(pending, budget))
            return reject(id);
        inspected.required = pending;
        return inspected;
    }
};
} // namespace arp::rhi
