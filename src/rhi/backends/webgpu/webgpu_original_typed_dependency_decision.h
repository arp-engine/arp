#pragma once
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/frontend/command_usage_projection.h"

namespace arp::rhi_webgpu {
// Backend-private frozen original positional clauses, rooted in the genuine
// loss-bound Closure2/3/4 (Freeze1/2/3), geometry Closure6 (Freeze4),
// exact composed geometry/readback Closure7 (Freeze5), or GeneralWrite1's
// canonical scalar resources in Closure8 (Freeze6), or finite GeneralWrite2
// resources and initialized backing in Closure9 (Freeze7), or finite Graphics
// initialized content/attachment custody in Closure10 (Freeze8).
// The current strategy is a CPU candidate for a separately
// admitted executor payload; this object cannot record, submit or prove sync.
class WebGpuOriginalTypedDependencyDecision final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuNativeCommandPreparation;
    using C = rhi::OriginalTypedDependencyProjectionContract;
    using P = rhi::common::OriginalTypedDependencyProjection;
    using Closure = WebGpuCanonicalNativeClosure;

    class ConstructionKey {
        friend class WebGpuOriginalTypedDependencyDecision;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Request = C::FreezeRequest;
    enum class CandidateOperation {
        InitialStateLedger,
        InterScopeCommandListBoundary,
        FinalStateLedger
    };

    struct Record {
        P::Record original;
        CandidateOperation candidate = CandidateOperation::InitialStateLedger;

        bool matches(const Record& other) const noexcept
        {
            return candidate == other.candidate && original.matches(other.original);
        }
    };

    WebGpuOriginalTypedDependencyDecision(
        ConstructionKey, std::shared_ptr<const Closure> closure, Request request, P::Counts counts)
        : closure_(std::move(closure)), request_(request), counts_(counts),
          records_(static_cast<std::size_t>(counts.dependencies))
    {}

    static std::shared_ptr<const WebGpuOriginalTypedDependencyDecision> prepareOriginal(
        std::shared_ptr<const Closure> closure, const Request& request, const char*& diagnostic)
    {
        if ((diagnostic = preflight(closure, request)))
            return {};
        const auto& graph = closure->originalRoot()->originalGraph();
        const auto& stream = closure->originalStream();
        const auto inspected = P::inspect(stream, graph, request.projection);
        if ((diagnostic = inspected.diagnostic))
            return {};
        if (inspected.counts.dependencies > SIZE_MAX) {
            diagnostic = "RHI-WEBGPU-TYPED-DEPENDENCY-SIZE-CAPACITY";
            return {};
        }
        try {
            // Recording quota is already retained/accounted by the original
            // closure; the new request independently admits this metadata cost.
            core::CpuMemoryQuotaScope scope(closure->originalMetadataQuota());
            auto result = core::makeUnique<WebGpuOriginalTypedDependencyDecision>(
                ConstructionKey{}, closure, request, inspected.counts);
            const auto replay = P::visit(stream,
                graph,
                request.projection,
                [&](const P::Record& record) noexcept -> const char* {
                    return result->records_.append({record, candidate(record.clause)})
                        ? nullptr
                        : "RHI-WEBGPU-TYPED-DEPENDENCY-RECORD-CAPACITY";
                });
            if ((diagnostic = replay.diagnostic))
                return {};
            if (!inspected.counts.matches(replay.counts)) {
                diagnostic = "RHI-WEBGPU-TYPED-DEPENDENCY-REPLAY";
                return {};
            }
            auto shared = shareOriginal(std::move(result));
            if ((diagnostic = shared->validate()))
                return {};
            return shared;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-WEBGPU-TYPED-DEPENDENCY-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-WEBGPU-TYPED-DEPENDENCY-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalTypedDependencyDecision>& self) noexcept
    {
        return !self || !self->hasQuotaBoundSharedOwner(self) ? "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER"
                                                              : self->validate();
    }

    const char* validate() const noexcept
    {
        if (const auto* id = preflight(closure_, request_))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                originalMetadataQuota(), closure_->originalMetadataQuota()))
            return "RHI-WEBGPU-TYPED-DEPENDENCY-QUOTA-DOMAIN";
        if (records_.values().size() != counts_.dependencies)
            return "RHI-WEBGPU-TYPED-DEPENDENCY-COVERAGE";
        std::size_t index = 0u;
        const auto replay = P::visit(closure_->originalStream(),
            closure_->originalRoot()->originalGraph(),
            request_.projection,
            [&](const P::Record& record) noexcept -> const char* {
                const Record current{record, candidate(record.clause)};
                if (index >= records_.values().size()
                    || !current.matches(records_.values()[index++]))
                    return "RHI-WEBGPU-TYPED-DEPENDENCY-BINDING";
                return nullptr;
            });
        if (replay.diagnostic)
            return replay.diagnostic;
        return index == records_.values().size() && counts_.matches(replay.counts)
            ? nullptr
            : "RHI-WEBGPU-TYPED-DEPENDENCY-COUNTS";
    }

    const char* compareOriginalClosure(const std::shared_ptr<const Closure>& closure) const noexcept
    {
        if (const auto* id = validate())
            return id;
        if (const auto* id = Closure::validateOriginal(closure))
            return id;
        return core::SharedOwnerProjection::sameOwner(closure_, closure)
            ? nullptr
            : "RHI-WEBGPU-TYPED-DEPENDENCY-CLOSURE-IDENTITY";
    }

    const auto& records() const noexcept { return records_.values(); }

    const auto& counts() const noexcept { return counts_; }

    const Request& originalRequest() const noexcept { return request_; }

    const auto& originalClosure() const noexcept { return closure_; }

    std::uint32_t retainedOwnerCount() const noexcept
    {
        return closure_->retainedOwnerCount() + 1u;
    }

    bool synchronizationProven() const noexcept { return false; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        return "RHI-WEBGPU-TYPED-DEPENDENCY-EXECUTOR-PAYLOAD-MISSING";
    }

private:
    const char* validateImmutableBindings() const noexcept
    {
        if (const auto* id = C::validateFreezeIntent(request_))
            return id;
        if (!core::SharedOwnerProjection::sameOwner(
                originalMetadataQuota(), closure_->originalMetadataQuota()))
            return "RHI-WEBGPU-TYPED-DEPENDENCY-QUOTA-DOMAIN";
        P::Counts checked;
        std::size_t index = 0u;
        for (const auto& operation : closure_->originalStream()->operations())
            if (P::isDependency(operation.kind)) {
                if (index >= records_.values().size())
                    return "RHI-WEBGPU-TYPED-DEPENDENCY-COVERAGE";
                const auto& record = records_.values()[index++];
                if (record.original.commandIndex != operation.commandIndex
                    || record.candidate != candidate(record.original.clause))
                    return "RHI-WEBGPU-TYPED-DEPENDENCY-BINDING";
                if (const auto* id = P::validateFrozenBinding(closure_->originalStream(),
                        closure_->originalRoot()->originalGraph(),
                        request_.projection,
                        record.original,
                        checked))
                    return id;
                ++checked.dependencies;
                if (record.original.clause == P::Clause::InitialBookkeeping)
                    ++checked.initial;
                else if (record.original.clause == P::Clause::BetweenScopes)
                    ++checked.between;
                else
                    ++checked.final;
            }
        return index == records_.values().size() && checked.dependencies == counts_.dependencies
                && checked.initial == counts_.initial && checked.between == counts_.between
                && checked.final == counts_.final && checked.textureCells == counts_.textureCells
                && checked.endpointContributions == counts_.endpointContributions
            ? nullptr
            : "RHI-WEBGPU-TYPED-DEPENDENCY-COUNTS";
    }

    static CandidateOperation candidate(P::Clause clause) noexcept
    {
        return clause == P::Clause::InitialBookkeeping ? CandidateOperation::InitialStateLedger
            : clause == P::Clause::BetweenScopes ? CandidateOperation::InterScopeCommandListBoundary
                                                 : CandidateOperation::FinalStateLedger;
    }

    static const char* preflight(
        const std::shared_ptr<const Closure>& closure, const Request& request) noexcept
    {
        if (const auto* id = C::validateFreezeIntent(request))
            return id;
        const auto lowering =
            rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                rhi::Backend::WebGPU, request.contractVersion);
        if (!lowering.projectionAvailable)
            return lowering.diagnostic;
        if (const auto* id = Closure::validateOriginal(closure))
            return id;
        if (!C::closureProfileMatches(request.contractVersion, closure->closureContractVersion()))
            return "RHI-WEBGPU-TYPED-DEPENDENCY-CLOSURE-VERSION";
        if (const auto* id = closure->nativeLossDiagnostic())
            return id;
        const auto& quota = closure->originalMetadataQuota();
        const auto& root = closure->originalRoot();
        const auto& graph = root->originalGraph();
        const auto& sources =
            graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        if (request.maxCpuMetadataBytes != quota->limitBytes()
            || request.maxCpuMetadataBytes
                != sources->originalBudget().maxCpuBytes[static_cast<std::size_t>(
                    rhi::OriginalCommandAdmissionBudget::CpuDomain::Recording)])
            return "RHI-WEBGPU-TYPED-DEPENDENCY-QUOTA";
        // Closure8/9/10 have independent native custody ceilings. Sources' CPU
        // graph bound remains authoritative for the root and every old profile.
        const auto ownerCeiling = (request.contractVersion == 6u || request.contractVersion == 7u
                                      || request.contractVersion == 8u)
            ? static_cast<std::uint64_t>(closure->budget_.maxRetainedOwners) + 1u
            : sources->originalBudget().maxGraphOwners;
        if (closure->retainedOwnerCount() == UINT32_MAX
            || closure->retainedOwnerCount() + 1u > request.maxRetainedOwners
            || request.maxRetainedOwners > ownerCeiling)
            return "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER-CAPACITY";
        return nullptr;
    }

    const std::shared_ptr<const Closure> closure_;
    const Request request_;
    const P::Counts counts_;
    core::BoundedSequence<Record> records_;
};
} // namespace arp::rhi_webgpu
