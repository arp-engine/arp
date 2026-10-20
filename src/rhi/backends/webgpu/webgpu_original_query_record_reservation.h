#pragma once
#include "rhi/backends/webgpu/webgpu_original_query_slots.h"
#include "rhi/frontend/query_command_points.h"

namespace arp::rhi_webgpu {
struct WebGpuOriginalQueryRecordReservationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxCommandPoints = 0u;
    std::uint64_t maxReferencedQuerySlots = 0u, maxReferencedScopeIds = 0u,
                  maxReferencedLabelBytes = 0u, maxCpuMetadataBytes = 0u;
};

// A CPU recording declaration reservation, never a native recorded/submit proof.
// Original stable controls and native owners outlive the journal that points at them.
class WebGpuOriginalQueryRecordReservation final : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalQueryRecordReservation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    using Slots = WebGpuOriginalQuerySlots;
    using Points = rhi::QueryCommandPoints;
    using Control = rhi::common::QuerySlotDeclarationControl;

    struct Entry {
        Slots::Observation original;
        std::optional<Control::PendingObservation> pending;
    };

public:
    enum class Status { Prepared, Pending, Abandoned };

    struct Lowering {
        bool validatorOnly = false, reservationImplemented = false;
        const char* diagnostic = "RHI-QUERY-RECORD-RESERVATION-BACKEND";
    };

    static constexpr Lowering lowering(rhi::Backend backend) noexcept
    {
        switch (backend) {
        case rhi::Backend::Null:
            return {true, false, "RHI-QUERY-RECORD-RESERVATION-VALIDATOR-ONLY"};
        case rhi::Backend::Vulkan:
        case rhi::Backend::Metal:
            return {false, false, "RHI-QUERY-RECORD-RESERVATION-NATIVE-UNIMPLEMENTED"};
        case rhi::Backend::WebGPU:
            return {false, true, "RHI-QUERY-RECORD-RESERVATION-NATIVE-UNPROVEN"};
        case rhi::Backend::WebGL:
            break;
        }
        return {};
    }

    WebGpuOriginalQueryRecordReservation(
        ConstructionKey, std::shared_ptr<const Points> points, std::size_t count)
        : points_(std::move(points))
    {
        entries_.reserve(count);
    }

    ~WebGpuOriginalQueryRecordReservation() { abandon(); }

    static const char* validateBudget(const WebGpuOriginalQueryRecordReservationBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-QUERY-RECORD-RESERVATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-QUERY-RECORD-RESERVATION-OPT-IN";
        if (!b.maxCommandPoints || !b.maxReferencedQuerySlots || !b.maxCpuMetadataBytes)
            return "RHI-QUERY-RECORD-RESERVATION-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOriginalQueryRecordReservation> prepareOriginal(
        std::shared_ptr<const Points> points,
        const Slots::Observation* observations,
        std::size_t count,
        const WebGpuOriginalQueryRecordReservationBudget& b,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        diagnostic = validateBudget(b);
        if (diagnostic)
            return {};
        if (!quota || quota->limitBytes() != b.maxCpuMetadataBytes) {
            diagnostic = "RHI-QUERY-RECORD-RESERVATION-QUOTA";
            return {};
        }
        if (!points || !points->hasQuotaBoundSharedOwner(points)) {
            diagnostic = "RHI-QUERY-RECORD-RESERVATION-POINTS-OWNER";
            return {};
        }
        if (!observations || !count || count != points->points().size()
            || count > b.maxCommandPoints) {
            diagnostic = "RHI-QUERY-RECORD-RESERVATION-COVERAGE";
            return {};
        }
        std::uint64_t slots = 0u, scopes = 0u, labels = 0u;
        for (std::size_t i = 0u; i < count; ++i) {
            const auto& original = observations[i];
            const auto& graph = original.originalGraph();
            if ((diagnostic = Slots::validateCurrentOriginal(graph, original)))
                return {};
            if (!original.originalBinding().originalCommand().matches(
                    points->points()[i].original)) {
                diagnostic = "RHI-QUERY-RECORD-RESERVATION-ORIGINAL-POINT";
                return {};
            }
            if (original.phase() != Control::Phase::NotRecorded) {
                diagnostic = "RHI-QUERY-RECORD-RESERVATION-PHASE";
                return {};
            }
            for (std::size_t j = 0u; j < i; ++j)
                if (original.index() == observations[j].index()
                    && core::SharedOwnerProjection::sameOwner(
                        graph, observations[j].originalGraph())) {
                    diagnostic = "RHI-QUERY-RECORD-RESERVATION-DUPLICATE-SLOT";
                    return {};
                }
            const auto& input = *graph->originalOwner()->originalInput();
            if (graph->slotCount() > b.maxReferencedQuerySlots - slots
                || input.scopeIds().size() > b.maxReferencedScopeIds - scopes
                || input.label().size() > b.maxReferencedLabelBytes - labels) {
                diagnostic = "RHI-QUERY-RECORD-RESERVATION-CAPACITY";
                return {};
            }
            slots += graph->slotCount();
            scopes += input.scopeIds().size();
            labels += input.label().size();
        }
        try {
            core::CpuMemoryQuotaScope metadata(quota);
            auto result = core::makeUnique<WebGpuOriginalQueryRecordReservation>(
                ConstructionKey{}, std::move(points), count);
            for (std::size_t i = 0u; i < count; ++i)
                result->entries_.push_back({observations[i], {}});
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-QUERY-RECORD-RESERVATION-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-QUERY-RECORD-RESERVATION-ALLOCATION";
            return {};
        }
    }

    static const char* reserveOriginal(
        const std::shared_ptr<WebGpuOriginalQueryRecordReservation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-QUERY-RECORD-RESERVATION-OWNER";
        if (self->status_ != Status::Prepared)
            return "RHI-QUERY-RECORD-RESERVATION-STATUS";
        // Check every original observation before touching any control.
        if (const auto* id = self->validate())
            return id;
        for (auto& entry : self->entries_) {
            auto& control = self->control(entry);
            if (const auto* id = control.declare(Control::Event::Record, &self->journal_)) {
                self->abandon();
                return id;
            }
            entry.pending.emplace(*control.observePending(self->journal_));
        }
        self->status_ = Status::Pending;
        return nullptr;
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalQueryRecordReservation>& self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-QUERY-RECORD-RESERVATION-OWNER";
        return self->validate();
    }

    Status status() const noexcept { return status_; }

    const auto& originalPoints() const noexcept { return points_; }

    bool journalEmpty() const noexcept { return journal_.empty(); }

    void abandon() noexcept
    {
        journal_.rollback();
        status_ = Status::Abandoned;
    }

    bool nativeExecutionProven() const noexcept { return false; }

    bool nativeAvailabilityProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        if (const auto* id = validate())
            return id;
        for (const auto& entry : entries_)
            if (const auto* id = entry.original.originalBinding().executionDiagnostic())
                return id;
        return "RHI-QUERY-RECORD-RESERVATION-NATIVE-PAYLOAD-UNPROVEN";
    }

private:
    static Control& control(const Entry& entry) noexcept
    {
        return *entry.original.originalGraph()->slots_.values()[entry.original.index()];
    }

    const char* validate() const noexcept
    {
        if (status_ == Status::Abandoned)
            return "RHI-QUERY-RECORD-RESERVATION-ABANDONED";
        for (const auto& entry : entries_) {
            if (const auto* id = Slots::validateGraph(entry.original.originalGraph()))
                return id;
            if (status_ == Status::Prepared) {
                if (const auto* id = Slots::validateCurrentOriginal(
                        entry.original.originalGraph(), entry.original))
                    return id;
                if (entry.original.phase() != Control::Phase::NotRecorded)
                    return "RHI-QUERY-RECORD-RESERVATION-PHASE";
            } else if (!entry.pending || entry.pending->value() != Control::Phase::Recorded
                || !control(entry).matchesPending(*entry.pending, journal_))
                return "RHI-QUERY-RECORD-RESERVATION-PENDING-MISMATCH";
        }
        return nullptr;
    }

    const std::shared_ptr<const Points> points_;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    core::MutationJournal journal_;
    Status status_ = Status::Prepared;
};
} // namespace arp::rhi_webgpu
