#pragma once

#include "core/algorithm/preflight_replay.h"
#include "core/math/unsigned_range.h"
#include "rhi/frontend/command_geometry_fetch.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/transfer_copy.h"
#include "rhi/frontend/transfer_readback_layout_validation.h"
#include "rhi/semantics/command_lowering.h"

#include <array>
#include <tuple>

// region original_command_stream_program
namespace arp::rhi {
// Complete positional field program for the supported original Input profile.
// Reuses the same Inputs/Usage/Source closure; operations contain indices only.
// Input validation supplies order/provenance/descriptor binding. Resource
// access/state legality remains OriginalCommandAccessValidation's prerequisite.
// Owns the existing immutable input/state facade; creates no new state owner,
// journal, native object, staging or execution proof.
class OriginalCommandStreamProgram final : public core::QuotaBoundSharedObjectIdentity {
    using Graph = OriginalCommandResourceStates;
    using Root =
        std::variant<std::shared_ptr<const OriginalCommandInputs>, std::shared_ptr<const Graph>>;

    class ConstructionKey {
        friend class OriginalCommandStreamProgram;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Family = CommandInputContract::Family;
    static constexpr std::size_t noDescriptor = SIZE_MAX;

    struct Operation {
        std::size_t commandIndex = 0u, descriptorIndex = noDescriptor, firstReference = 0u,
                    referenceCount = 0u;
        CommandUsageKind kind = CommandUsageKind::Count;
        Family family = Family::Unsupported;
        // Separate ordinal spaces: Copy elements, Buffer uploads, Texture upload
        // mips, Texture readback mips, or render-pass attachment references.
        // /2 adds Pipeline/Descriptor bind and consumption ordinal spaces;
        // implicit accesses have their own space. /1 Scalar has no elements.
        std::uint64_t firstElement = 0u, elementCount = 0u, firstImplicitAccess = 0u,
                      implicitAccessCount = 0u;

        bool matches(const Operation& rhs) const noexcept
        {
            return std::tie(commandIndex,
                       descriptorIndex,
                       firstReference,
                       referenceCount,
                       kind,
                       family,
                       firstElement,
                       elementCount,
                       firstImplicitAccess,
                       implicitAccessCount)
                == std::tie(rhs.commandIndex,
                    rhs.descriptorIndex,
                    rhs.firstReference,
                    rhs.referenceCount,
                    rhs.kind,
                    rhs.family,
                    rhs.firstElement,
                    rhs.elementCount,
                    rhs.firstImplicitAccess,
                    rhs.implicitAccessCount);
        }
    };

    struct Counts {
        std::uint64_t commands = 0u, descriptors = 0u, references = 0u, copyElements = 0u,
                      bufferUploads = 0u;
        std::uint64_t textureUploadMips = 0u, textureReadbackMips = 0u, renderPasses = 0u,
                      attachmentReferences = 0u;
        std::uint64_t bufferUploadDeclaredBytes = 0u, textureUploadPixelBytes = 0u,
                      readbackDeclaredBytes = 0u;
        std::uint64_t pipelineBinds = 0u, descriptorBinds = 0u, consumptions = 0u,
                      implicitAccesses = 0u, geometryBinds = 0u, geometryDraws = 0u,
                      geometryDrawAccesses = 0u;

        bool matches(const Counts& rhs) const noexcept
        {
            return std::tie(commands,
                       descriptors,
                       references,
                       copyElements,
                       bufferUploads,
                       textureUploadMips,
                       textureReadbackMips,
                       renderPasses,
                       attachmentReferences,
                       bufferUploadDeclaredBytes,
                       textureUploadPixelBytes,
                       readbackDeclaredBytes,
                       pipelineBinds,
                       descriptorBinds,
                       consumptions,
                       implicitAccesses,
                       geometryBinds,
                       geometryDraws,
                       geometryDrawAccesses)
                == std::tie(rhs.commands,
                    rhs.descriptors,
                    rhs.references,
                    rhs.copyElements,
                    rhs.bufferUploads,
                    rhs.textureUploadMips,
                    rhs.textureReadbackMips,
                    rhs.renderPasses,
                    rhs.attachmentReferences,
                    rhs.bufferUploadDeclaredBytes,
                    rhs.textureUploadPixelBytes,
                    rhs.readbackDeclaredBytes,
                    rhs.pipelineBinds,
                    rhs.descriptorBinds,
                    rhs.consumptions,
                    rhs.implicitAccesses,
                    rhs.geometryBinds,
                    rhs.geometryDraws,
                    rhs.geometryDrawAccesses);
        }
    };

    struct Inspection {
        const char* diagnostic = nullptr;
        std::size_t commandIndex = 0u;
        Counts counts;
    };

    OriginalCommandStreamProgram(
        ConstructionKey, Root root, const OriginalCommandStreamBudget& budget, const Counts& counts)
        : root_(std::move(root)), budget_(budget), counts_(counts),
          operations_(static_cast<std::size_t>(counts.commands))
    {}

    static Inspection inspect(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& budget) noexcept
    {
        return inspectRoot(Root{inputs}, budget);
    }

    static Inspection inspectTyped(const std::shared_ptr<const Graph>& graph,
        const OriginalCommandStreamBudget& budget) noexcept
    {
        return inspectRoot(Root{graph}, budget);
    }

private:
    static Inspection inspectRoot(
        const Root& root, const OriginalCommandStreamBudget& budget) noexcept
    {
        Inspection result;
        if ((result.diagnostic = preflight(root, budget)))
            return result;
        const auto& inputs = inputsOf(root);
        auto mapper = Mapper(*inputs, budget, result, graphOf(root));
        Operation operation;
        for (std::size_t i = 0u; i < inputs->originalScalars()->originalUsage()->commands().size();
            ++i)
            if (!mapper(i, operation))
                return result;
        return result;
    }

public:
    // Allocation-free CPU traversal for cold backend projection. Every operation
    // and count validates before the first visitor call. The visitor cannot gain
    // native authority from this traversal; a rejection after consumption is
    // terminal for that projection. Inputs must remain immutable/serialized.
    template <class Visitor>
    static const char* visitOriginal(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const OriginalCommandStreamBudget& budget,
        Visitor&& visitor) noexcept
    {
        return visitRoot(Root{inputs}, budget, std::forward<Visitor>(visitor));
    }

    template <class Visitor>
    static const char* visitOriginalTyped(const std::shared_ptr<const Graph>& graph,
        const OriginalCommandStreamBudget& budget,
        Visitor&& visitor) noexcept
    {
        return visitRoot(Root{graph}, budget, std::forward<Visitor>(visitor));
    }

private:
    template <class Visitor>
    static const char* visitRoot(
        const Root& root, const OriginalCommandStreamBudget& budget, Visitor&& visitor) noexcept
    {
        static_assert(noexcept(visitor(std::declval<const Operation&>())),
            "Stream projection visitor must not throw");
        static_assert(
            std::is_same_v<decltype(visitor(std::declval<const Operation&>())), const char*>,
            "Stream projection visitor returns a diagnostic");
        if (const auto* id = preflight(root, budget))
            return id;
        const auto& inputs = inputsOf(root);
        Inspection result;
        const char* consumerDiagnostic = nullptr;
        const auto status = core::PreflightReplay<Operation>::run(
            inputs->originalScalars()->originalUsage()->commands().size(),
            [&] {
                return Mapper(*inputs, budget, result, graphOf(root));
            },
            [] {
                return true;
            },
            [&](const Operation& operation) {
                if (!consumerDiagnostic)
                    consumerDiagnostic = visitor(operation);
            });
        if (status != core::PreflightReplayStatus::Completed)
            return result.diagnostic ? result.diagnostic : "RHI-ORIGINAL-STREAM-REPLAY";
        return consumerDiagnostic;
    }

public:
    static std::shared_ptr<const OriginalCommandStreamProgram> prepareOriginal(
        std::shared_ptr<const OriginalCommandInputs> inputs,
        const OriginalCommandStreamBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareRoot(Root{std::move(inputs)}, budget, std::move(quota), diagnostic);
    }

    static std::shared_ptr<const OriginalCommandStreamProgram> prepareOriginalTyped(
        std::shared_ptr<const Graph> graph,
        const OriginalCommandStreamBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        return prepareRoot(Root{std::move(graph)}, budget, std::move(quota), diagnostic);
    }

private:
    static std::shared_ptr<const OriginalCommandStreamProgram> prepareRoot(Root root,
        const OriginalCommandStreamBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = preflight(root, budget);
        if (diagnostic)
            return {};
        const auto& inputs = inputsOf(root);
        if ((diagnostic = validateQuota(root, budget, quota)))
            return {};
        Inspection result;
        std::shared_ptr<OriginalCommandStreamProgram> program;
        try {
            core::CpuMemoryQuotaScope scope(quota);
            const auto status = core::PreflightReplay<Operation>::run(
                inputs->originalScalars()->originalUsage()->commands().size(),
                [&] {
                    return Mapper(*inputs, budget, result, graphOf(root));
                },
                [&] {
                    program = shareOriginal(core::makeUnique<OriginalCommandStreamProgram>(
                        ConstructionKey{}, root, budget, result.counts));
                    return bool(program);
                },
                [&](const Operation& operation) {
                    program->operations_.append(operation);
                });
            if (status != core::PreflightReplayStatus::Completed) {
                diagnostic = result.diagnostic ? result.diagnostic : "RHI-ORIGINAL-STREAM-REPLAY";
                return {};
            }
            return program;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-STREAM-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-STREAM-ALLOCATION";
            return {};
        }
    }

public:
    static const char* validateOriginal(
        const std::shared_ptr<const OriginalCommandStreamProgram>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-ORIGINAL-STREAM-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = preflight(root_, budget_))
            return id;
        if (const auto* id = validateQuota(root_, budget_, originalMetadataQuota()))
            return id;
        if (operations_.values().size() != counts_.commands)
            return "RHI-ORIGINAL-STREAM-COVERAGE";
        Inspection result;
        Mapper mapper(*originalInputs(), budget_, result, graphOf(root_));
        for (std::size_t i = 0u; i < operations_.values().size(); ++i) {
            Operation operation;
            if (!mapper(i, operation))
                return result.diagnostic;
            if (!operation.matches(operations_.values()[i]))
                return "RHI-ORIGINAL-STREAM-BINDING";
        }
        return counts_.matches(result.counts) ? nullptr : "RHI-ORIGINAL-STREAM-COUNTS";
    }

    const char* compareOriginalInputs(
        const std::shared_ptr<const OriginalCommandInputs>& inputs) const noexcept
    {
        if (const auto* id = validate())
            return id;
        return core::SharedOwnerProjection::sameOwner(originalInputs(), inputs)
            ? nullptr
            : "RHI-ORIGINAL-STREAM-INPUTS";
    }

    const std::shared_ptr<const OriginalCommandInputs>& originalInputs() const noexcept
    {
        return inputsOf(root_);
    }

    const char* compareOriginalStates(const std::shared_ptr<const Graph>& graph) const noexcept
    {
        if (const auto* id = validate())
            return id;
        const auto* owned = std::get_if<std::shared_ptr<const Graph>>(&root_);
        return owned && core::SharedOwnerProjection::sameOwner(*owned, graph)
            ? nullptr
            : "RHI-ORIGINAL-STREAM-STATE-IDENTITY";
    }

    std::uint32_t streamContractVersion() const noexcept { return budget_.contractVersion; }

    const OriginalCommandStreamBudget& originalBudget() const noexcept { return budget_; }

    const auto& operations() const noexcept { return operations_.values(); }

    const Counts& counts() const noexcept { return counts_; }

    // Join the existing State4/Stream4 and exact original Inputs5 to immutable
    // initialized-index analysis. This never records or authorizes GPU work.
    template <class Analyzer, class Visitor>
    OriginalIndexedDrawFetchContract::Result visitIndexedDrawFetch(
        const OriginalIndexedDrawFetchContract::Budget& budget,
        Analyzer&& analyzer,
        Visitor&& visitor) const noexcept
    {
        using Fetch = OriginalIndexedDrawFetchContract;
        if (const auto* id = validate())
            return {id};
        if (budget_.contractVersion != 4u
            || budget_.descriptorConsumption.indexedDrawConsumptionVersion != 1u
            || !budget_.descriptorConsumption.indexedDrawConsumptionOptIn)
            return {"RHI-ORIGINAL-STREAM-INDEXED-CONSUMPTION-REQUIRED"};
        const auto joined = [&](const Fetch::Point& point) noexcept -> const char* {
            if (point.commandIndex >= operations().size()
                || operations()[point.commandIndex].kind != CommandUsageKind::DrawIndexed
                || operations()[point.commandIndex].elementCount != 1u)
                return "RHI-ORIGINAL-STREAM-INDEXED-CORRESPONDENCE";
            return visitor(point);
        };
        return Fetch::visit(originalInputs(), budget, analyzer, joined);
    }

    // RootState5/Stream1 uses the same fetch walker with genuine initialized
    // index and vertex actors supplied by backend custody. This only checks
    // correspondence; it cannot grant native recording or completion authority.
    template <class Analyzer, class VertexAnalyzer, class Visitor>
    OriginalIndexedDrawFetchContract::Result visitInitializedIndexedDrawFetch(
        const OriginalIndexedDrawFetchContract::Budget& budget,
        Analyzer&& analyzer,
        VertexAnalyzer&& vertex,
        Visitor&& visitor) const noexcept
    {
        using Fetch = OriginalIndexedDrawFetchContract;
        if (const auto* id = validate())
            return {id};
        if (budget.contractVersion != 2u || budget_.contractVersion != 1u
            || budget_.bindlessRoots.contractVersion != 5u
            || budget_.geometryUsage.drawConsumptionContractVersion != 1u
            || !budget_.geometryUsage.drawConsumptionOptIn)
            return {"RHI-ORIGINAL-ROOT-STREAM-INDEXED-FETCH-PROFILE"};
        const auto correspondence = [&](const Fetch::Point& point) noexcept -> const char* {
            if (point.commandIndex >= operations().size()
                || operations()[point.commandIndex].kind != CommandUsageKind::DrawIndexed
                || operations()[point.commandIndex].elementCount != 1u)
                return "RHI-ORIGINAL-STREAM-INDEXED-CORRESPONDENCE";
            return nullptr;
        };
        const auto checked =
            Fetch::visitInitialized(originalInputs(), budget, analyzer, vertex, correspondence);
        if (checked.diagnostic)
            return checked;
        return Fetch::visitInitialized(originalInputs(), budget, analyzer, vertex, visitor);
    }

    // RootState6 retains the exact same Inputs8, original indirect operand and
    // geometry epoch. Bounds come from the closed producer's immutable inputs;
    // native execution still requires its content, hazard and completion join.
    template <class Bounds, class Analyzer, class VertexAnalyzer, class Visitor>
    OriginalIndexedDrawFetchContract::Result visitInitializedProducedDrawFetch(
        const OriginalIndexedDrawFetchContract::Budget& budget,
        Bounds&& bounds,
        Analyzer&& analyzer,
        VertexAnalyzer&& vertex,
        Visitor&& visitor) const noexcept
    {
        using Fetch = OriginalIndexedDrawFetchContract;
        if (const auto* id = validate())
            return {id};
        if (budget.contractVersion != 3u || budget_.contractVersion != 1u
            || budget_.bindlessRoots.contractVersion != 6u
            || budget_.geometryUsage.drawConsumptionContractVersion != 1u
            || !budget_.geometryUsage.drawConsumptionOptIn)
            return {"RHI-ORIGINAL-PRODUCED-STREAM-INDEXED-FETCH-PROFILE"};
        const auto joined = [&](const Fetch::Point& point) noexcept -> const char* {
            if (point.commandIndex >= operations().size()
                || operations()[point.commandIndex].kind != CommandUsageKind::DrawIndexedIndirect)
                return "RHI-ORIGINAL-PRODUCED-STREAM-INDEXED-CORRESPONDENCE";
            return visitor(point);
        };
        return Fetch::visitProducedInitialized(
            originalInputs(), budget, bounds, analyzer, vertex, joined);
    }

    // Shared actor correspondence for backend-owned frozen geometry. Lookup
    // returns the authentic actor for an original inventory source index.
    template <class Lookup>
    OriginalIndexedDrawFetchContract::Result inspectInitializedGeometry(
        const OriginalIndexedDrawFetchContract::Budget& budget, Lookup&& lookup) const noexcept
    {
        const auto unused = [](const auto&, auto&) noexcept -> const char* {
            return "RHI-ORIGINAL-PRODUCED-INDEXED-FETCH-BOUNDS-REQUIRED";
        };
        return inspectInitializedActors<false>(budget, unused, lookup);
    }

    template <class Bounds, class Lookup>
    OriginalIndexedDrawFetchContract::Result inspectInitializedProducedGeometry(
        const OriginalIndexedDrawFetchContract::Budget& budget,
        Bounds&& bounds,
        Lookup&& lookup) const noexcept
    {
        return inspectInitializedActors<true>(budget, bounds, lookup);
    }

private:
    template <bool Produced, class Bounds, class Lookup>
    OriginalIndexedDrawFetchContract::Result inspectInitializedActors(
        const OriginalIndexedDrawFetchContract::Budget& budget,
        Bounds& bounds,
        Lookup& lookup) const noexcept
    {
        using Actor = typename std::decay_t<decltype(lookup(std::size_t{}))>::element_type;
        const auto analyze = [&](const auto& point, const auto& lease, auto& output) noexcept {
            return Actor::analyzeInitializedIndices(lookup(point.indexSource),
                lease,
                point.index.format,
                point.index.offset,
                point.draw.firstIndex,
                point.draw.indices,
                point.draw.vertexOffset,
                output);
        };
        const auto vertex = [&](const auto& point,
                                const auto&,
                                const auto& layout,
                                const auto& fields,
                                const auto& lease) noexcept {
            const auto& inventory =
                originalInputs()->originalScalars()->originalUsage()->originalSources();
            return Actor::validateInitializedVertices(lookup(inventory->originalLeaseIndex(lease)),
                lease,
                layout,
                fields.offset,
                point.fetch.vertexCount,
                point.fetch.firstVertex,
                point.draw.instances,
                point.draw.firstInstance);
        };
        const auto discard = [](const auto&) noexcept -> const char* {
            return nullptr;
        };
        if constexpr (Produced)
            return visitInitializedProducedDrawFetch(budget, bounds, analyze, vertex, discard);
        else
            return visitInitializedIndexedDrawFetch(budget, analyze, vertex, discard);
    }

public:
    std::uint32_t retainedOwnerCount() const noexcept { return ownersOf(root_) + 1u; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic(Backend backend) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (!budget_.bindlessRoots.empty())
            return semantics::OriginalCommandStreamLowering::bindlessRootLowering(
                backend, budget_.bindlessRoots.contractVersion)
                .diagnostic;
        return (budget_.contractVersion >= 2u
                ? arp::rhi::semantics::OriginalCommandStreamLowering::typedLowering(
                      backend, budget_.contractVersion)
                : arp::rhi::semantics::OriginalCommandStreamLowering::lowering(backend))
            .diagnostic;
    }

private:
    static const Graph* graphOf(const Root& root) noexcept
    {
        const auto* graph = std::get_if<std::shared_ptr<const Graph>>(&root);
        return graph ? graph->get() : nullptr;
    }

    static const std::shared_ptr<const OriginalCommandInputs>& inputsOf(const Root& root) noexcept
    {
        if (const auto* inputs = std::get_if<std::shared_ptr<const OriginalCommandInputs>>(&root))
            return *inputs;
        return std::get<std::shared_ptr<const Graph>>(root)->originalInputs();
    }

    static std::uint32_t ownersOf(const Root& root) noexcept
    {
        return graphOf(root) ? graphOf(root)->retainedOwnerCount()
                             : inputsOf(root)->retainedOwnerCount();
    }

    static const char* preflight(
        const Root& root, const OriginalCommandStreamBudget& budget) noexcept
    {
        if (const auto* id = OriginalCommandStreamContract::validateIntent(budget))
            return id;
        if (budget.contractVersion >= 2u || !budget.bindlessRoots.empty()) {
            const auto* graph = std::get_if<std::shared_ptr<const Graph>>(&root);
            if (!graph)
                return "RHI-ORIGINAL-STREAM-TYPED-STATE-REQUIRED";
            if (const auto* id = Graph::validateOriginal(*graph))
                return id;
            if ((*graph)->stateContractVersion() != budget.contractVersion)
                return "RHI-ORIGINAL-STREAM-STATE-PROFILE";
            if (!budget.bindlessRoots.matches((*graph)->budget_.bindlessRoots))
                return "RHI-ORIGINAL-BINDLESS-STREAM-FOOTPRINT-MISMATCH";
            if (!OriginalCommandDescriptorConsumptionContract::matches(
                    budget.descriptorConsumption, (*graph)->budget_.descriptorConsumption))
                return "RHI-ORIGINAL-STREAM-FOOTPRINT-MISMATCH";
            if (budget.contractVersion >= 3u
                && !budget.renderDescriptorUsage.matches((*graph)->budget_.renderDescriptorUsage))
                return "RHI-ORIGINAL-STREAM-RENDER-FOOTPRINT-MISMATCH";
            if ((budget.contractVersion == 4u || budget.bindlessRoots.geometryJoined())
                && !budget.geometryUsage.matches((*graph)->budget_.geometryUsage))
                return "RHI-ORIGINAL-STREAM-GEOMETRY-FOOTPRINT-MISMATCH";
            if (const auto* id =
                    common::OriginalCommandAccessValidation::inspect(*graph, budget.stateSequence)
                        .diagnostic)
                return id;
        } else {
            if (std::holds_alternative<std::shared_ptr<const Graph>>(root))
                return "RHI-ORIGINAL-STREAM-STATE-PROFILE";
            if (const auto* id = OriginalCommandInputs::validateOriginal(inputsOf(root)))
                return id;
            if (inputsOf(root)->originalBudget().contractVersion >= 3u)
                return "RHI-ORIGINAL-STREAM-TYPED-PROFILE";
            for (const auto& command :
                inputsOf(root)->originalScalars()->originalUsage()->commands())
                if (command.kind == CommandUsageKind::BindPreparedComputeBinding
                    || command.kind == CommandUsageKind::BindPreparedGraphicsBinding)
                    return "RHI-ORIGINAL-BINDLESS-STREAM-STATE-REQUIRED";
        }
        if (ownersOf(root) >= budget.maxRetainedOwners)
            return "RHI-ORIGINAL-STREAM-OWNER-CAPACITY";
        return nullptr;
    }

    static const char* validateQuota(const Root& root,
        const OriginalCommandStreamBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        const auto& inputs = *inputsOf(root);
        const auto& sources = inputs.originalScalars()->originalUsage()->originalSources();
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-STREAM-QUOTA";
        const std::array<const std::shared_ptr<core::CpuMemoryQuota>*, 4> domains{&quota,
            &inputs.originalMetadataQuota(),
            &sources->originalMetadataQuota(),
            graphOf(root) ? &graphOf(root)->originalMetadataQuota() : &quota};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                graphOf(root) ? 4u : 3u, [&](std::size_t i) -> const auto& {
                    return *domains[i];
                }))
            return "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN";
        using D = OriginalCommandAdmissionBudget::CpuDomain;
        if (budget.maxCpuMetadataBytes
            > sources->originalBudget().maxCpuBytes[static_cast<std::size_t>(D::Recording)])
            return "RHI-ORIGINAL-STREAM-SOURCE-CPU-BUDGET";
        return nullptr;
    }

    class Mapper final {
    public:
        Mapper(const OriginalCommandInputs& inputs,
            const OriginalCommandStreamBudget& budget,
            Inspection& result,
            const Graph* graph = nullptr) noexcept
            : inputs_(inputs), budget_(budget), result_(result), graph_(graph)
        {
            result_ = {};
        }

        bool operator()(std::size_t index, Operation& out) noexcept
        {
            result_.commandIndex = index;
            const auto& usage = *inputs_.originalScalars()->originalUsage();
            const auto& command = usage.commands()[index];
            auto& c = result_.counts;
            if (!charge(c.commands, 1u, budget_.maxCommands, "RHI-ORIGINAL-STREAM-COMMAND-CAPACITY")
                || !charge(c.references,
                    command.referenceCount,
                    budget_.maxReferences,
                    "RHI-ORIGINAL-STREAM-REFERENCE-CAPACITY"))
                return false;
            out = {index,
                noDescriptor,
                command.firstReference,
                command.referenceCount,
                command.kind,
                CommandInputContract::family(command.kind),
                0u,
                0u};
            const auto* positional = inputs_.descriptorAt(index);
            if (!budget_.bindlessRoots.empty()) {
                using K = CommandUsageKind;
                if (command.kind == K::BindPreparedComputeBinding
                    || command.kind == K::BindPreparedGraphicsBinding) {
                    out.firstElement = c.pipelineBinds;
                    out.elementCount = 1u;
                    if (!charge(c.pipelineBinds,
                            1u,
                            budget_.maxPipelineBinds,
                            "RHI-ORIGINAL-STREAM-PIPELINE-BIND-CAPACITY"))
                        return false;
                }
                if (command.kind == K::Dispatch || command.kind == K::Draw
                    || command.kind == K::ClearSnapshotFault
                    || (budget_.bindlessRoots.geometryJoined() && command.kind == K::DrawIndexed)
                    || (budget_.bindlessRoots.contractVersion == 6u
                        && command.kind == K::DrawIndexedIndirect)) {
                    out.firstElement = c.consumptions;
                    out.elementCount = 1u;
                    out.firstImplicitAccess = c.implicitAccesses;
                    if (command.kind != K::ClearSnapshotFault
                        && !charge(c.consumptions,
                            1u,
                            budget_.maxConsumptions,
                            "RHI-ORIGINAL-STREAM-CONSUMPTION-CAPACITY"))
                        return false;
                    if (command.kind == K::ClearSnapshotFault)
                        out.firstElement = out.elementCount = 0u;
                    const auto accesses =
                        common::OriginalCommandBindlessRootAccesses::visitValidatedOriginal(
                            graph_->originalInputs(),
                            budget_.bindlessRoots,
                            [&](const auto& access) noexcept -> const char* {
                                return access.commandIndex == index ? chargeImplicit(out) : nullptr;
                            });
                    if (accesses.diagnostic)
                        return fail(accesses.diagnostic);
                }
            }
            if (out.family != Family::Scalar
                || ((budget_.contractVersion >= 2u || budget_.bindlessRoots.geometryJoined())
                    && positional)) {
                if (const auto* id = CommandInputContract::consumeDescriptor(index,
                        command.kind,
                        inputs_.descriptors().data(),
                        inputs_.descriptors().size(),
                        consumed_,
                        "RHI-ORIGINAL-STREAM-DESCRIPTOR-ORDER",
                        "RHI-ORIGINAL-STREAM-DESCRIPTOR-KIND"))
                    return fail(id);
                out.descriptorIndex = consumed_ - 1u;
                if (!charge(c.descriptors,
                        1u,
                        budget_.maxDescriptors,
                        "RHI-ORIGINAL-STREAM-DESCRIPTOR-CAPACITY"))
                    return false;
                const auto& value = inputs_.descriptors()[out.descriptorIndex].arguments;
                if (!positional || positional != &value)
                    return fail("RHI-ORIGINAL-STREAM-DESCRIPTOR-BINDING");
                switch (out.family) {
                case Family::Copy: {
                    out.firstElement = c.copyElements;
                    const auto* fields = std::get_if<TransferCopyArguments>(&value);
                    const TransferCopyInputSnapshot* full = nullptr;
                    if (const auto* owner =
                            std::get_if<std::shared_ptr<const TransferCopyInputSnapshot>>(&value)) {
                        full = owner->get();
                        fields = &full->scalarFields();
                    }
                    if (!fields)
                        return fail("RHI-ORIGINAL-STREAM-COPY");
                    const auto visitor = [&](const auto&) noexcept -> const char* {
                        if (!charge(c.copyElements,
                                1u,
                                budget_.maxCopyElements,
                                "RHI-ORIGINAL-STREAM-COPY-CAPACITY"))
                            return result_.diagnostic;
                        ++out.elementCount;
                        return nullptr;
                    };
                    const char* id = nullptr;
                    switch (command.kind) {
                    case CommandUsageKind::BufferCopy:
                        id = TransferCopyProjection::visit<BufferCopyDesc>(*fields, full, visitor);
                        break;
                    case CommandUsageKind::BufferToTextureCopy:
                        id = TransferCopyProjection::visit<BufferToTextureCopyDesc>(
                            *fields, full, visitor);
                        break;
                    case CommandUsageKind::TextureToBufferCopy:
                        id = TransferCopyProjection::visit<TextureToBufferCopyDesc>(
                            *fields, full, visitor);
                        break;
                    case CommandUsageKind::TextureToTextureCopy:
                        id = TransferCopyProjection::visit<TextureToTextureCopyDesc>(
                            *fields, full, visitor);
                        break;
                    default:
                        id = "RHI-ORIGINAL-STREAM-COPY";
                        break;
                    }
                    if (id)
                        return fail(id);
                    break;
                }
                case Family::BufferUpload: {
                    BufferCopyDesc declared;
                    if (!std::get<BufferUploadArguments>(value).copyArguments().write(declared))
                        return fail("RHI-ORIGINAL-STREAM-BUFFER-UPLOAD");
                    out.firstElement = c.bufferUploads;
                    out.elementCount = 1u;
                    if (!charge(c.bufferUploads,
                            1u,
                            budget_.maxBufferUploads,
                            "RHI-ORIGINAL-STREAM-BUFFER-UPLOAD-CAPACITY")
                        || !charge(c.bufferUploadDeclaredBytes,
                            declared.sizeBytes,
                            budget_.maxBufferUploadDeclaredBytes,
                            "RHI-ORIGINAL-STREAM-BUFFER-UPLOAD-BYTES"))
                        return false;
                    break;
                }
                case Family::TextureUpload: {
                    const auto& input =
                        *std::get<TextureUploadOriginalInputArguments>(value).originalInput();
                    out.firstElement = c.textureUploadMips;
                    out.elementCount = semantics::TextureUploadLayoutValidation::mipCount(input);
                    if (!charge(c.textureUploadMips,
                            out.elementCount,
                            budget_.maxTextureUploadMips,
                            "RHI-ORIGINAL-STREAM-UPLOAD-MIP-CAPACITY")
                        || !charge(c.textureUploadPixelBytes,
                            input.pixelBytes().size(),
                            budget_.maxTextureUploadPixelBytes,
                            "RHI-ORIGINAL-STREAM-UPLOAD-PIXEL-BYTES"))
                        return false;
                    break;
                }
                case Family::TextureReadback: {
                    const auto& input =
                        *std::get<TextureReadbackOriginalInputArguments>(value).originalInput();
                    out.firstElement = c.textureReadbackMips;
                    out.elementCount = semantics::TextureReadbackLayoutValidation::mipCount(input);
                    if (!charge(c.textureReadbackMips,
                            out.elementCount,
                            budget_.maxTextureReadbackMips,
                            "RHI-ORIGINAL-STREAM-READBACK-MIP-CAPACITY")
                        || !charge(c.readbackDeclaredBytes,
                            input.scalarFields().byteCapacity(),
                            budget_.maxReadbackDeclaredBytes,
                            "RHI-ORIGINAL-STREAM-READBACK-BYTES"))
                        return false;
                    break;
                }
                case Family::Pass:
                    out.firstElement = c.attachmentReferences;
                    out.elementCount = command.referenceCount;
                    if (!charge(c.renderPasses,
                            1u,
                            budget_.maxRenderPasses,
                            "RHI-ORIGINAL-STREAM-PASS-CAPACITY")
                        || !charge(c.attachmentReferences,
                            out.elementCount,
                            budget_.maxAttachmentReferences,
                            "RHI-ORIGINAL-STREAM-ATTACHMENT-CAPACITY"))
                        return false;
                    break;
                case Family::Scalar:
                    if (std::holds_alternative<OriginalCommandPipelineBinding>(value)) {
                        out.firstElement = c.pipelineBinds;
                        out.elementCount = 1u;
                        if (!charge(c.pipelineBinds,
                                1u,
                                budget_.maxPipelineBinds,
                                "RHI-ORIGINAL-STREAM-PIPELINE-BIND-CAPACITY"))
                            return false;
                    } else if (std::holds_alternative<OriginalCommandDescriptorBinding>(value)) {
                        out.firstElement = c.descriptorBinds;
                        out.elementCount = 1u;
                        if (!charge(c.descriptorBinds,
                                1u,
                                budget_.maxDescriptorBinds,
                                "RHI-ORIGINAL-STREAM-DESCRIPTOR-BIND-CAPACITY"))
                            return false;
                    } else if (std::holds_alternative<OriginalCommandGeometryBinding>(value)) {
                        if (budget_.contractVersion != 4u
                            && !budget_.bindlessRoots.geometryJoined())
                            return fail("RHI-ORIGINAL-STREAM-GEOMETRY-PROFILE");
                        out.firstElement = c.geometryBinds;
                        out.elementCount = 1u;
                        if (!charge(c.geometryBinds,
                                1u,
                                budget_.geometryUsage.maxBindings,
                                "RHI-ORIGINAL-STREAM-GEOMETRY-BIND-CAPACITY"))
                            return false;
                    } else
                        return fail("RHI-ORIGINAL-STREAM-TYPED-RECORD");
                    break;
                case Family::Dependency:
                    break;
                default:
                    return fail("RHI-ORIGINAL-STREAM-UNSUPPORTED");
                }
            }
            if (budget_.contractVersion >= 3u
                && (command.kind == CommandUsageKind::BindRenderDescriptor
                    || command.kind == CommandUsageKind::BindRenderDescriptorTable)) {
                out.firstImplicitAccess = c.implicitAccesses;
                const auto accesses = graph_->visitRenderDescriptorAccesses(
                    budget_.renderDescriptorUsage, [&](const auto& access) noexcept -> const char* {
                        return access.commandIndex == index ? chargeImplicit(out) : nullptr;
                    });
                if (accesses.diagnostic)
                    return fail(accesses.diagnostic);
            }
            if ((budget_.contractVersion == 4u || budget_.bindlessRoots.geometryJoined())
                && (OriginalCommandGeometryBindingContract::isBinding(command.kind)
                    || (!budget_.geometryUsage.drawConsumptionEmpty()
                        && (command.kind == CommandUsageKind::Draw
                            || command.kind == CommandUsageKind::DrawIndexed
                            || (budget_.bindlessRoots.contractVersion == 6u
                                && command.kind == CommandUsageKind::DrawIndexedIndirect))))) {
                if (!out.implicitAccessCount)
                    out.firstImplicitAccess = c.implicitAccesses;
                if (!OriginalCommandGeometryBindingContract::isBinding(command.kind)
                    && !charge(c.geometryDraws,
                        1u,
                        budget_.geometryUsage.maxDraws,
                        "RHI-ORIGINAL-STREAM-GEOMETRY-DRAW-CAPACITY"))
                    return false;
                const auto geometry = graph_->visitGeometryAccesses(
                    budget_.geometryUsage, [&](const auto& access) noexcept -> const char* {
                        if (access.commandIndex != index)
                            return nullptr;
                        if (access.drawConsumption()
                            && !charge(c.geometryDrawAccesses,
                                1u,
                                budget_.geometryUsage.maxDrawAccesses,
                                "RHI-ORIGINAL-STREAM-GEOMETRY-DRAW-ACCESS-CAPACITY"))
                            return result_.diagnostic;
                        return chargeImplicit(out);
                    });
                if (geometry.diagnostic)
                    return fail(geometry.diagnostic);
            }
            if (budget_.contractVersion >= 2u
                && OriginalCommandDescriptorConsumptionContract::isConsumption(
                    command.kind, budget_.descriptorConsumption)) {
                out.firstElement = c.consumptions;
                out.elementCount = 1u;
                if (!out.implicitAccessCount)
                    out.firstImplicitAccess = c.implicitAccesses;
                if (!charge(c.consumptions,
                        1u,
                        budget_.maxConsumptions,
                        "RHI-ORIGINAL-STREAM-CONSUMPTION-CAPACITY"))
                    return false;
                const auto accesses = graph_->visitDescriptorAccesses(
                    [&](const auto& access) noexcept -> const char* {
                        if (access.commandIndex != index)
                            return nullptr;
                        return chargeImplicit(out);
                    });
                if (accesses.diagnostic)
                    return fail(accesses.diagnostic);
            }
            if (index + 1u == usage.commands().size()) {
                if (consumed_ != inputs_.descriptors().size())
                    return fail("RHI-ORIGINAL-STREAM-DESCRIPTOR-ORDER");
                result_.commandIndex = index + 1u;
            }
            return true;
        }

    private:
        const char* chargeImplicit(Operation& operation) noexcept
        {
            if (!charge(result_.counts.implicitAccesses,
                    1u,
                    budget_.maxImplicitAccesses,
                    "RHI-ORIGINAL-STREAM-IMPLICIT-CAPACITY"))
                return result_.diagnostic;
            ++operation.implicitAccessCount;
            return nullptr;
        }

        bool fail(const char* id) noexcept
        {
            result_.diagnostic = id;
            return false;
        }

        bool charge(
            std::uint64_t& used, std::uint64_t count, std::uint64_t limit, const char* id) noexcept
        {
            if (!count)
                return true;
            if (!core::containsUnsignedRange<std::uint64_t>(0u, limit, used, count))
                return fail(id);
            used += count;
            return true;
        }

        const OriginalCommandInputs& inputs_;
        const OriginalCommandStreamBudget& budget_;
        Inspection& result_;
        const Graph* graph_;
        std::size_t consumed_ = 0u;
    };

    const Root root_;
    const OriginalCommandStreamBudget budget_;
    const Counts counts_;
    core::BoundedSequence<Operation> operations_;
};
} // namespace arp::rhi

// endregion original_command_stream_program
