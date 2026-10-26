#pragma once
#include "core/container/bounded_sequence.h"
#include "core/ownership/quota_bound_shared_identity.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/frontend/command_arguments.h"
#include "rhi/semantics/query_lowering.h"

namespace arp::rhi {
// Exact CPU fields only; no original QueryPool/native ownership or result proof.
class QueryPoolArguments final {
public:
    static QueryPoolArguments captureScalarFields(const QueryPoolDesc& d) noexcept
    {
        return QueryPoolArguments(d);
    }

    bool matchesScalarFields(const QueryPoolDesc& d) const noexcept
    {
        return type_ == d.type && count_ == d.count && scopeCount_ == d.timingScopeIdCount
            && scopePointerPresent_ == (d.timingScopeIds != nullptr)
            && mask_ == d.pipelineStatisticsMask;
    }

    QueryPoolDescriptorValidation validateDescriptor() const noexcept
    {
        return QueryPoolDescriptorValidation::fromFields(
            type_, count_, scopePointerPresent_, scopeCount_, mask_);
    }

    QueryType type() const noexcept { return type_; }

    std::uint32_t count() const noexcept { return count_; }

    std::uint32_t scopeCount() const noexcept { return scopeCount_; }

    bool scopePointerPresent() const noexcept { return scopePointerPresent_; }

    PipelineStatisticsMask statisticMask() const noexcept { return mask_; }

private:
    explicit QueryPoolArguments(const QueryPoolDesc& d) noexcept
        : type_(d.type), count_(d.count), scopeCount_(d.timingScopeIdCount),
          scopePointerPresent_(d.timingScopeIds != nullptr), mask_(d.pipelineStatisticsMask)
    {}

    const QueryType type_;
    const std::uint32_t count_, scopeCount_;
    const bool scopePointerPresent_;
    const PipelineStatisticsMask mask_;
};

struct QueryPoolInputSnapshotBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxScopeIds = 0u;
    std::uint64_t maxLabelBytes = 0u, maxCpuMetadataBytes = 0u, maxCpuLabelBytes = 0u,
                  maxCpuScopeBytes = 0u;
};

class QueryPoolInputSnapshot final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class QueryPoolInputSnapshot;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Lowering {
        bool cpuMetadataImplemented = false, validatorOnly = false;
        const char* diagnostic = "RHI-QUERY-INPUT-BACKEND-UNKNOWN";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::QueryLowering::QueryPoolInputSnapshot_lowering<Lowering>(backend);
    }

    QueryPoolInputSnapshot(ConstructionKey,
        const QueryPoolDesc& d,
        core::pmr::String label,
        core::BoundedSequence<std::uint64_t> scopes)
        : fields_(QueryPoolArguments::captureScalarFields(d)), label_(std::move(label)),
          scopes_(std::move(scopes))
    {}

    static const char* validateBudget(const QueryPoolInputSnapshotBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-QUERY-INPUT-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-QUERY-INPUT-OPT-IN";
        if (!b.maxCpuMetadataBytes || !b.maxCpuLabelBytes || !b.maxCpuScopeBytes
            || b.maxLabelBytes > SIZE_MAX)
            return "RHI-QUERY-INPUT-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<const QueryPoolInputSnapshot> prepareOriginal(const QueryPoolDesc& d,
        const QueryPoolInputSnapshotBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> metadataQuota,
        std::shared_ptr<core::CpuMemoryQuota> labelQuota,
        std::shared_ptr<core::CpuMemoryQuota> scopeQuota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!metadataQuota || metadataQuota->limitBytes() != b.maxCpuMetadataBytes || !labelQuota
            || labelQuota->limitBytes() != b.maxCpuLabelBytes || !scopeQuota
            || scopeQuota->limitBytes() != b.maxCpuScopeBytes) {
            diagnostic = "RHI-QUERY-INPUT-QUOTA";
            return {};
        }
        if (d.timingScopeIdCount && !d.timingScopeIds) {
            diagnostic = "RHI-QUERY-INPUT-SCOPE-POINTER";
            return {};
        }
        if (d.timingScopeIdCount > b.maxScopeIds || d.label.size() > b.maxLabelBytes) {
            diagnostic = "RHI-QUERY-INPUT-CAPACITY";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(scopeQuota);
            core::BoundedSequence<std::uint64_t> scopes(b.maxScopeIds);
            for (std::uint32_t i = 0u; i < d.timingScopeIdCount; ++i)
                scopes.append(d.timingScopeIds[i]);
            core::CpuMemoryQuotaScope labelScope(labelQuota);
            core::pmr::String label(d.label, &core::defaultCpuMemoryResource());
            core::CpuMemoryQuotaScope metadata(metadataQuota);
            return shareOriginal(core::makeUnique<QueryPoolInputSnapshot>(
                ConstructionKey{}, d, std::move(label), std::move(scopes)));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-QUERY-INPUT-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-QUERY-INPUT-ALLOCATION";
            return {};
        }
    }

    const auto& scalarFields() const noexcept { return fields_; }

    std::string_view label() const noexcept { return label_; }

    const auto& scopeIds() const noexcept { return scopes_.values(); }

    bool matches(const QueryPoolDesc& d) const noexcept
    {
        if (!fields_.matchesScalarFields(d) || label() != d.label
            || (d.timingScopeIdCount && !d.timingScopeIds))
            return false;
        for (std::size_t i = 0u; i < scopes_.values().size(); ++i)
            if (scopes_.values()[i] != d.timingScopeIds[i])
                return false;
        return true;
    }

    QueryPoolDescriptorValidation validateDescriptor() const noexcept
    {
        return fields_.validateDescriptor();
    }

    const char* executionDiagnostic() const noexcept { return "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN"; }

private:
    const QueryPoolArguments fields_;
    const core::pmr::String label_;
    core::BoundedSequence<std::uint64_t> scopes_;
};

class QueryPoolOriginalInputArguments final {
public:
    static std::optional<QueryPoolOriginalInputArguments> captureOriginal(CommandUsageKind kind,
        std::uint32_t index,
        std::shared_ptr<const QueryPoolInputSnapshot> input) noexcept
    {
        if (!input || !input->hasQuotaBoundSharedOwner(input))
            return {};
        const auto scalar =
            CommandScalarArguments::capture(kind, CommandScalarArguments::Query{index});
        if (!scalar)
            return {};
        return QueryPoolOriginalInputArguments(*scalar, std::move(input));
    }

    CommandUsageKind kind() const noexcept { return scalar_.kind(); }

    const auto& originalInput() const noexcept { return input_; }

    const auto& scalar() const noexcept { return scalar_; }

    bool matches(const QueryPoolOriginalInputArguments& other) const noexcept
    {
        return input_->hasQuotaBoundSharedOwner(input_)
            && other.input_->hasQuotaBoundSharedOwner(other.input_)
            && core::SharedOwnerProjection::sameOwner(input_, other.input_)
            && scalar_.matches(other.scalar_);
    }

    const char* validateDeclaration() const noexcept
    {
        if (!input_->hasQuotaBoundSharedOwner(input_))
            return "RHI-QUERY-INPUT-OWNER";
        const auto validation = input_->validateDescriptor();
        if (!validation.success)
            return validation.diagnostics;
        if ((kind() == CommandUsageKind::Timestamp)
            != (input_->scalarFields().type() == QueryType::Timestamp))
            return "RHI-QUERY-COMMAND-TYPE";
        return scalar_.get<CommandScalarArguments::Query>()->index < input_->scalarFields().count()
            ? nullptr
            : "RHI-QUERY-COMMAND-INDEX";
    }

private:
    QueryPoolOriginalInputArguments(
        CommandScalarArguments scalar, std::shared_ptr<const QueryPoolInputSnapshot> input)
        : scalar_(scalar), input_(std::move(input))
    {}

    // Value declarations remain assignable like the other positional argument
    // types. The retained snapshot and all public accessors stay immutable.
    CommandScalarArguments scalar_;
    std::shared_ptr<const QueryPoolInputSnapshot> input_;
};
} // namespace arp::rhi
