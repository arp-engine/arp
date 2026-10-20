#pragma once
#include "core/versioned_mutation_array.h"
#include "core/versioned_mutation_value.h"
#include "rhi/frontend/resource_loss_validation.h"
#include "rhi/frontend/resource_texture_validation.h"

namespace arp::rhi {
class GpuDevice;
class GpuTexture;
class OriginalCommandStateTransaction;
} // namespace arp::rhi

namespace arp::rhi::common {
// One exclusive domain: version1 whole upload, version2 fixed original cells,
// or version3 native Undefined whole creation paired only with ContentPlan /7.
// No CPU commit or ordinary upload can complete an original content generation.
// Generation IDs are control-issued and
// never recycle, including after root rollback. No standalone journal exists.
class OriginalTextureContentControl final {
    friend class rhi::GpuDevice;
    friend class rhi::GpuTexture;
    friend class rhi::OriginalCommandStateTransaction;

    class ConstructionKey {
        friend class rhi::GpuDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    enum class State { Uninitialized, Proposed, Completed };

    struct Value {
        State state = State::Uninitialized;
        std::uint64_t generation = 0u;

        bool operator==(const Value& v) const noexcept
        {
            return state == v.state && generation == v.generation;
        }
    };

    using Control = core::VersionedMutationValue<Value>;
    using Observation = Control::Observation;
    using PendingObservation = Control::PendingObservation;
    using Cells = core::VersionedMutationArray<Value>;

    struct RangeObservation {
        TextureSubresourceRange range;
        Value value;
        Cells::Observation stamp;
    };

    OriginalTextureContentControl(ConstructionKey,
        const OriginalTextureContentCreationBudget& budget,
        OriginalDeviceNativeLossObservation loss,
        std::shared_ptr<core::CpuMemoryQuota> quota) noexcept
        : budget_(budget), loss_(std::move(loss)), quota_(std::move(quota)), value_(Value{})
    {}

    OriginalTextureContentControl(ConstructionKey,
        const OriginalTextureContentCreationBudget& budget,
        OriginalDeviceNativeLossObservation loss,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        semantics::OriginalTextureStateRange geometry)
        : budget_(budget), loss_(std::move(loss)), quota_(std::move(quota)), value_(Value{}),
          geometry_(geometry),
          cells_(std::in_place, static_cast<std::size_t>(geometry.cellCount()), Value{})
    {}

    std::uint32_t contractVersion() const noexcept { return budget_.contractVersion; }

    std::optional<RangeObservation> observeRange(
        const TextureSubresourceRange& range) const noexcept
    {
        if (diagnostic() || !cells_ || !geometry_ || geometry_->validate(range))
            return {};
        const auto stamp = cells_->observe();
        const auto value = uniformRange(range);
        return stamp && value ? std::optional<RangeObservation>{{range, *value, *stamp}}
                              : std::nullopt;
    }

    bool matchesRange(const RangeObservation& observation) const noexcept
    {
        const auto current = observeRange(observation.range);
        return current && cells_->matches(observation.stamp) && current->value == observation.value;
    }

    // Identity remains authentic while the original journal has a pending
    // mutation. This is never a proof of initialized or completed contents.
    const char* identityDiagnostic() const noexcept
    {
        if (const auto* id = loss_.diagnostic())
            return id;
        if (!quota_ || quota_->limitBytes() != budget_.maxCpuContentBytes)
            return "RHI-CANONICAL-CONTENT-CONTROL-QUOTA";
        if (OriginalTextureContentCreationContract::isCellCreation(budget_.contractVersion)) {
            if (!cells_ || !geometry_ || !cells_->active())
                return "RHI-CANONICAL-CONTENT-CONTROL-INVALIDATED";
            return nullptr;
        }
        if (!value_.active())
            return "RHI-CANONICAL-CONTENT-CONTROL-INVALIDATED";
        return nullptr;
    }

    const char* diagnostic() const noexcept
    {
        if (const auto* id = identityDiagnostic())
            return id;
        const bool pending =
            OriginalTextureContentCreationContract::isCellCreation(budget_.contractVersion)
            ? cells_->pending()
            : value_.pending();
        return pending ? "RHI-CANONICAL-CONTENT-CONTROL-PENDING" : nullptr;
    }

private:
    const char* canProposeCells(core::MutationJournal& journal,
        std::uint64_t count,
        std::uint32_t planVersion) const noexcept
    {
        // Content9/Geometry2 may retain a completed HDR chain through the
        // independent ProducedImageRead intent. The transaction authenticates
        // every cell before appending it; this branch can never propose one.
        if (planVersion == 9u && (budget_.contractVersion == 6u || budget_.contractVersion == 7u)) {
            if (count || !cells_)
                return "RHI-BINDLESS-PRODUCED-IMAGE-MUTATION";
            return diagnostic();
        }
        // Canonical Graphics depth is read-only. Its independent Creation5
        // attachment is the only writable cell domain of Content19.
        if (planVersion == 19u && budget_.contractVersion == 2u) {
            if (count || !cells_)
                return "RHI-BINDLESS-GRAPHICS-DEPTH-CONTENT-READONLY";
            return diagnostic();
        }
        const auto depthVersion =
            OriginalStorageTextureContentPlanContract::isMixedVersion(planVersion)
                && budget_.contractVersion == (planVersion == 15u ? 4u : 2u)
            ? OriginalStorageTextureContentPlanContract::depthPlanVersion(planVersion)
            : planVersion;
        if (OriginalCompletedDepthRootReadPlanContract::isVersion(depthVersion)) {
            if (const auto* id =
                    OriginalCompletedDepthRootReadPlanContract::validateReadOnlyControl(
                        depthVersion, budget_.contractVersion, bool(cells_), count))
                return id;
            return diagnostic();
        }
        if (!OriginalTextureContentCreationContract::cellControlMatchesPlan(
                budget_.contractVersion, planVersion)
            || !cells_)
            return "RHI-ORIGINAL-CONTENT-PLAN-CELL-CONTROL";
        if (const auto* id = diagnostic())
            return id;
        // Read-only content remains committed. The original resource-state
        // journal serializes conflicting writers; no array claim is needed.
        if (!count)
            return nullptr;
        if (count > budget_.maxUploadGenerations - issued_ || !generations_.hasRemaining(count))
            return "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY";
        // Reserve one proposal and one later completion write per changed cell,
        // as well as the core's rollback revision.
        if (count > UINT64_MAX / 2u || !cells_->canWriteBatch(journal, count * 2u))
            return "RHI-CANONICAL-CONTENT-JOURNAL-CAPACITY";
        return nullptr;
    }

    const char* proposeCell(core::MutationJournal& journal,
        std::uint64_t index,
        bool stored,
        Value& proposed,
        std::uint32_t planVersion) noexcept
    {
        if (!OriginalTextureContentCreationContract::cellControlMatchesPlan(
                budget_.contractVersion, planVersion))
            return "RHI-ORIGINAL-CONTENT-PLAN-CELL-CONTROL";
        if (!cells_ || index >= cells_->size() || issued_ >= budget_.maxUploadGenerations)
            return "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY";
        const auto next = generations_.tryAcquire();
        if (!next)
            return "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY";
        ++issued_;
        proposed = {stored ? State::Proposed : State::Uninitialized, next};
        return cells_->writeIndices(
                   1u,
                   [&](std::size_t) noexcept {
                       return static_cast<std::size_t>(index);
                   },
                   proposed,
                   &journal)
                == Cells::WriteResult::Written
            ? nullptr
            : "RHI-CANONICAL-CONTENT-PROPOSAL-FAILED";
    }

    std::optional<Value> uniformRange(const TextureSubresourceRange& range) const noexcept
    {
        if (!geometry_ || !cells_)
            return {};
        const auto count = geometry_->rangeCellCount(range);
        if (!count)
            return {};
        std::uint64_t index = 0u;
        geometry_->cellIndex(range, 0u, index);
        const auto value = *cells_->valueAt(static_cast<std::size_t>(index));
        for (std::uint64_t i = 1u; i < count; ++i) {
            geometry_->cellIndex(range, i, index);
            if (!(*cells_->valueAt(static_cast<std::size_t>(index)) == value))
                return {};
        }
        return value;
    }

    const char* canUpload(core::MutationJournal& journal,
        std::uint64_t count,
        std::uint32_t planVersion) const noexcept
    {
        if (const auto* id = OriginalUndefinedTextureUploadPlanContract::validateControl(
                budget_.contractVersion, planVersion))
            return id;
        if (const auto* id = diagnostic())
            return id;
        if (!count || count > budget_.maxUploadGenerations - issued_
            || !generations_.hasRemaining(count))
            return "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY";
        // One additional completion write; core preflight also reserves rollback.
        if (count == UINT64_MAX || !value_.canWriteBatch(journal, count + 1u))
            return "RHI-CANONICAL-CONTENT-JOURNAL-CAPACITY";
        return nullptr;
    }

    const char* propose(core::MutationJournal& journal,
        std::uint64_t& generation,
        std::uint32_t planVersion) noexcept
    {
        generation = 0u;
        if (const auto* id = OriginalUndefinedTextureUploadPlanContract::validateControl(
                budget_.contractVersion, planVersion))
            return id;
        if (issued_ >= budget_.maxUploadGenerations)
            return "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY";
        const auto next = generations_.tryAcquire();
        if (!next)
            return "RHI-CANONICAL-CONTENT-GENERATION-CAPACITY";
        ++issued_;
        if (value_.tryWrite({State::Proposed, next}, &journal) != Control::WriteResult::Written)
            return "RHI-CANONICAL-CONTENT-PROPOSAL-FAILED";
        generation = next;
        return nullptr;
    }

    const char* complete(core::MutationJournal& journal, std::uint32_t planVersion) noexcept
    {
        if (const auto* id = OriginalUndefinedTextureUploadPlanContract::validateControl(
                budget_.contractVersion, planVersion))
            return id;
        const auto pending = value_.observePending(journal);
        if (!pending || pending->value().state != State::Proposed || !pending->value().generation)
            return "RHI-CANONICAL-CONTENT-COMPLETE-PENDING";
        return value_.tryWrite({State::Completed, pending->value().generation}, &journal)
                == Control::WriteResult::Written
            ? nullptr
            : "RHI-CANONICAL-CONTENT-COMPLETE-FAILED";
    }

    // Only the original transaction/seal completion path may call this. A
    // native receipt never selects a new cell or substitutes its generation.
    const char* completeCell(core::MutationJournal& journal,
        std::uint64_t index,
        const Value& expected,
        Value& completed,
        std::uint32_t planVersion) noexcept
    {
        if (!OriginalTextureContentCreationContract::cellControlMatchesPlan(
                budget_.contractVersion, planVersion)
            || !cells_ || index >= cells_->size())
            return "RHI-ORIGINAL-CONTENT-COMPLETE-CELL-CONTROL";
        if (const auto* id = loss_.diagnostic())
            return id;
        const auto stamp = cells_->observePending(journal);
        const auto* current = cells_->valueAt(static_cast<std::size_t>(index));
        if (!stamp || !current || !(*current == expected) || !expected.generation
            || (expected.state != State::Proposed && expected.state != State::Uninitialized))
            return "RHI-ORIGINAL-CONTENT-COMPLETE-CELL-PENDING";
        completed = expected;
        if (expected.state == State::Uninitialized)
            return nullptr;
        completed.state = State::Completed;
        return cells_->writeIndices(
                   1u,
                   [&](std::size_t) noexcept {
                       return static_cast<std::size_t>(index);
                   },
                   completed,
                   &journal)
                == Cells::WriteResult::Written
            ? nullptr
            : "RHI-ORIGINAL-CONTENT-COMPLETE-CELL-FAILED";
    }

    const OriginalTextureContentCreationBudget budget_;
    const OriginalDeviceNativeLossObservation loss_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::MonotonicIdSequence generations_;
    std::uint64_t issued_ = 0u;
    Control value_;
    const std::optional<semantics::OriginalTextureStateRange> geometry_;
    std::optional<Cells> cells_;
};
} // namespace arp::rhi::common
