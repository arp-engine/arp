#pragma once
#include "core/diagnostics/diagnostic_error.h"
#include "core/ownership/versioned_mutation_array.h"
#include "rhi/frontend/resource_recorded_state_domain.h"
#include "rhi/frontend/resource_state_validation.h"
#include "rhi/frontend/resource_texture_state_range.h"

namespace arp::rhi::common {
// Creation-bound normalized original control. Any write invalidates every
// frozen range revision; no sparse insertion or per-cell revision is inferred.
class RecordedTextureStateControl final {
    friend class rhi::GpuDevice;
    using Values = core::VersionedMutationArray<ResourceState>;

public:
    struct Observation {
        TextureSubresourceRange range;
        ResourceState state;
        std::uint64_t revision;
    };

    class PendingObservation final {
        friend class RecordedTextureStateControl;

        PendingObservation(TextureSubresourceRange range,
            ResourceState state,
            Values::PendingObservation stamp) noexcept
            : range_(range), state_(state), stamp_(stamp)
        {}

    public:
        PendingObservation(const PendingObservation&) = default;

        const auto& range() const noexcept { return range_; }

        ResourceState state() const noexcept { return state_; }

        std::uint64_t revision() const noexcept { return stamp_.revision(); }

    private:
        TextureSubresourceRange range_;
        ResourceState state_;
        Values::PendingObservation stamp_;
    };

    RecordedTextureStateControl(const TextureDesc& desc, ResourceState initial)
        : geometry_(requireGeometry(desc)), format_(desc.format),
          values_(
              static_cast<std::size_t>(geometry_.cellCount()), requireState(desc.format, initial))
    {}

    const auto& geometry() const noexcept { return geometry_; }

    std::uint64_t revision() const noexcept { return values_.revision(); }

    bool active() const noexcept { return values_.active() && !domain_.diagnostic(); }

    const char* originalDomainDiagnostic() const noexcept { return domain_.diagnostic(); }

    bool pending() const noexcept { return values_.pending(); }

    void invalidate() noexcept { values_.invalidate(); }

    bool canPrepare(const core::MutationJournal& journal) const noexcept
    {
        return active() && values_.canPrepare(journal);
    }

    bool canWriteBatch(const core::MutationJournal& journal, std::uint64_t writes) const noexcept
    {
        return active() && values_.canWriteBatch(journal, writes);
    }

    bool prepare(core::MutationJournal& journal) noexcept
    {
        return active() && values_.prepare(journal);
    }

    std::optional<ResourceState> uniformState(const TextureSubresourceRange& range) const noexcept
    {
        const auto count = geometry_.rangeCellCount(range);
        if (!count)
            return {};
        std::uint64_t index = 0u;
        geometry_.cellIndex(range, 0u, index);
        const auto first = *values_.valueAt(static_cast<std::size_t>(index));
        if (!semantics::isTextureResourceState(format_, first))
            return {};
        for (std::uint64_t i = 1u; i < count; ++i) {
            geometry_.cellIndex(range, i, index);
            if (*values_.valueAt(static_cast<std::size_t>(index)) != first)
                return {};
        }
        return first;
    }

    // Whole-resource consumers include every aspect plane. A nonuniform result
    // is absent, never an invented Undefined state.
    std::optional<ResourceState> uniformWholeState() const noexcept
    {
        const auto first = *values_.valueAt(0u);
        if (!semantics::isTextureResourceState(format_, first))
            return {};
        for (std::size_t i = 1u; i < values_.size(); ++i)
            if (*values_.valueAt(i) != first)
                return {};
        return first;
    }

    TextureSubresourceRange wholePrimaryAspectRange() const noexcept
    {
        TextureSubresourceRange first, last;
        geometry_.cellRange(0u, first);
        const auto primaryCells = format_ == TextureFormat::Depth24Stencil8
            ? geometry_.cellCount() / 2u
            : geometry_.cellCount();
        geometry_.cellRange(primaryCells - 1u, last);
        first.mipLevelCount = last.baseMipLevel + 1u;
        first.arrayLayerCount = last.baseArrayLayer + 1u;
        return first;
    }

    const char* writeWhole(ResourceState after, core::MutationJournal* journal) noexcept
    {
        if (const auto* id = domain_.diagnostic())
            return id;
        if (!semantics::isTextureResourceState(format_, after))
            return "RHI-RESOURCE-STATE-INVALID";
        if (!active())
            return "RHI-RESOURCE-STATE-REVISION-EXHAUSTED";
        if (journal && values_.enlistmentExhausted())
            return "RHI-RESOURCE-STATE-ENLISTMENT-EXHAUSTED";
        if (journal ? !canPrepare(*journal) : pending())
            return "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        if (!uniformWholeState())
            return "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM";
        return writeResult(values_.writeIndices(
            values_.size(),
            [](std::size_t i) noexcept {
                return i;
            },
            after,
            journal));
    }

    const char* committedDiagnostic(const TextureSubresourceRange& range) const noexcept
    {
        if (const auto* id = domain_.diagnostic())
            return id;
        if (const auto* id = geometry_.validate(range))
            return id;
        if (!active())
            return "RHI-ORIGINAL-TEXTURE-STATE-INACTIVE";
        if (pending())
            return "RHI-ORIGINAL-TEXTURE-STATE-PENDING";
        return uniformState(range) ? nullptr : "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM";
    }

    std::optional<Observation> observe(const TextureSubresourceRange& range) const noexcept
    {
        return committedDiagnostic(range) ? std::optional<Observation>{}
                                          : Observation{range, *uniformState(range), revision()};
    }

    const char* validate(const Observation& observation) const noexcept
    {
        if (const auto* id = committedDiagnostic(observation.range))
            return id;
        return revision() == observation.revision
                && *uniformState(observation.range) == observation.state
            ? nullptr
            : "RHI-ORIGINAL-TEXTURE-STATE-STALE";
    }

    std::optional<PendingObservation> observePending(
        const TextureSubresourceRange& range, const core::MutationJournal& journal) const noexcept
    {
        if (!active())
            return {};
        const auto stamp = values_.observePending(journal);
        const auto state = uniformState(range);
        return stamp && state
            ? std::optional<PendingObservation>{PendingObservation{range, *state, *stamp}}
            : std::nullopt;
    }

    bool matchesPending(
        const PendingObservation& observation, const core::MutationJournal& journal) const noexcept
    {
        return active() && values_.matchesPending(observation.stamp_, journal)
            && uniformState(observation.range_) == observation.state_;
    }

    const char* write(const TextureSubresourceRange& range,
        ResourceState before,
        ResourceState after,
        core::MutationJournal* journal) noexcept
    {
        if (const auto* id = domain_.diagnostic())
            return id;
        if (const auto* id = geometry_.validate(range))
            return id;
        if (!semantics::isTextureResourceState(format_, before)
            || !semantics::isTextureResourceState(format_, after))
            return "RHI-RESOURCE-STATE-INVALID";
        if (!active())
            return "RHI-RESOURCE-STATE-REVISION-EXHAUSTED";
        if (journal && values_.enlistmentExhausted())
            return "RHI-RESOURCE-STATE-ENLISTMENT-EXHAUSTED";
        if (journal ? !canPrepare(*journal) : pending())
            return "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        const auto state = uniformState(range);
        if (!state)
            return "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM";
        if (*state != before)
            return "RHI-ORIGINAL-TEXTURE-STATE-BEFORE-MISMATCH";
        const auto count = static_cast<std::size_t>(geometry_.rangeCellCount(range));
        const auto result = values_.writeIndices(
            count,
            [&](std::size_t i) noexcept {
                std::uint64_t index = 0u;
                geometry_.cellIndex(range, i, index);
                return static_cast<std::size_t>(index);
            },
            after,
            journal);
        return writeResult(result);
    }

private:
    static const char* writeResult(Values::WriteResult result) noexcept
    {
        switch (result) {
        case Values::WriteResult::Written:
            return nullptr;
        case Values::WriteResult::Conflict:
            return "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        case Values::WriteResult::InvalidRange:
            return "RHI-ORIGINAL-TEXTURE-RANGE-BOUNDS";
        case Values::WriteResult::Exhausted:
            return "RHI-RESOURCE-STATE-REVISION-EXHAUSTED";
        case Values::WriteResult::EnlistmentExhausted:
            return "RHI-RESOURCE-STATE-ENLISTMENT-EXHAUSTED";
        }
        return "RHI-RESOURCE-STATE-REVISION-EXHAUSTED";
    }

    static semantics::OriginalTextureStateRange requireGeometry(const TextureDesc& desc)
    {
        const char* diagnostic = nullptr;
        const auto geometry = semantics::OriginalTextureStateRange::capture(desc,
            core::pmr::Vector<ResourceState>{&core::defaultCpuMemoryResource()}.max_size(),
            diagnostic);
        if (!geometry)
            throw core::DiagnosticError(diagnostic);
        return *geometry;
    }

    static ResourceState requireState(TextureFormat format, ResourceState state)
    {
        if (!semantics::isTextureResourceState(format, state))
            throw core::DiagnosticError("RHI-RESOURCE-STATE-INVALID");
        return state;
    }

    const semantics::OriginalTextureStateRange geometry_;
    const TextureFormat format_;
    OriginalRecordedStateDomain domain_;
    Values values_;
};
} // namespace arp::rhi::common
