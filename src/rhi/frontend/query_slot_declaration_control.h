#pragma once
#include "core/ownership/versioned_mutation_value.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/query_lowering.h"

namespace arp::rhi::common {
// CPU lifecycle declarations only. Native completion/Available require separate
// original executor evidence; this control cannot provide or imitate it.
class QuerySlotDeclarationControl final {
public:
    enum class Phase {
        NotRecorded,
        Recorded,
        Submitted,
        Completed,
        SnapshotDeclared,
        Quarantined,
        FailedIdle
    };
    enum class Event { Record, Submit, Complete, DeclareSnapshot, Fail, RetireFailure, Reset };

private:
    using Value = core::VersionedMutationValue<Phase>;

public:
    using Observation = Value::Observation;
    using PendingObservation = Value::PendingObservation;

    explicit QuerySlotDeclarationControl(
        Phase initial = Phase::NotRecorded, std::uint64_t firstRevision = 1u) noexcept
        : value_(initial, firstRevision)
    {}

    struct Lowering {
        bool validatorOnly = false, declarationImplemented = false;
        const char* diagnostic = "RHI-QUERY-SLOT-BACKEND";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::QueryLowering::QuerySlotDeclarationControl_lowering<Lowering>(backend);
    }

    static constexpr bool known(Phase phase) noexcept
    {
        switch (phase) {
        case Phase::NotRecorded:
        case Phase::Recorded:
        case Phase::Submitted:
        case Phase::Completed:
        case Phase::SnapshotDeclared:
        case Phase::Quarantined:
        case Phase::FailedIdle:
            return true;
        }
        return false;
    }

    static constexpr bool resettable(Phase phase) noexcept
    {
        return phase == Phase::NotRecorded || phase == Phase::SnapshotDeclared
            || phase == Phase::FailedIdle;
    }

    static std::optional<Phase> after(Phase phase, Event event) noexcept
    {
        if (!known(phase))
            return {};
        switch (event) {
        case Event::Record:
            if (phase == Phase::NotRecorded)
                return Phase::Recorded;
            break;
        case Event::Submit:
            if (phase == Phase::Recorded)
                return Phase::Submitted;
            break;
        case Event::Complete:
            if (phase == Phase::Submitted)
                return Phase::Completed;
            break;
        case Event::DeclareSnapshot:
            if (phase == Phase::Completed)
                return Phase::SnapshotDeclared;
            break;
        case Event::Fail:
            if (phase == Phase::NotRecorded || phase == Phase::SnapshotDeclared)
                return Phase::FailedIdle;
            if (phase == Phase::Recorded || phase == Phase::Submitted || phase == Phase::Completed)
                return Phase::Quarantined;
            break;
        case Event::RetireFailure:
            if (phase == Phase::Quarantined)
                return Phase::FailedIdle;
            break;
        case Event::Reset:
            if (resettable(phase))
                return Phase::NotRecorded;
            break;
        }
        return {};
    }

    bool nativeExecutionProven() const noexcept { return false; }

    bool nativeAvailabilityProven() const noexcept { return false; }

    Phase phase() const noexcept { return value_.value(); }

    std::uint64_t revision() const noexcept { return value_.revision(); }

    bool active() const noexcept { return value_.active() && known(phase()); }

    bool pending() const noexcept { return value_.pending(); }

    auto observe() const noexcept
    {
        return known(phase()) ? value_.observe() : std::optional<Observation>{};
    }

    bool matches(const Observation& observation) const noexcept
    {
        return known(phase()) && value_.matches(observation);
    }

    auto observePending(const core::MutationJournal& journal) const noexcept
    {
        return known(phase()) ? value_.observePending(journal)
                              : std::optional<PendingObservation>{};
    }

    bool matchesPending(
        const PendingObservation& observation, const core::MutationJournal& journal) const noexcept
    {
        return known(phase()) && value_.matchesPending(observation, journal);
    }

    void invalidate() noexcept { value_.invalidate(); }

    const char* declare(Event event, core::MutationJournal* journal = nullptr) noexcept
    {
        if (!active())
            return "RHI-QUERY-SLOT-INACTIVE";
        if (event == Event::Reset && pending())
            return "RHI-QUERY-SLOT-RESET-PENDING";
        const auto next = after(phase(), event);
        if (!next)
            return "RHI-QUERY-SLOT-TRANSITION";
        switch (value_.tryWrite(*next, journal)) {
        case Value::WriteResult::Written:
            return nullptr;
        case Value::WriteResult::Conflict:
            return "RHI-QUERY-SLOT-JOURNAL-CONFLICT";
        case Value::WriteResult::Exhausted:
            return "RHI-QUERY-SLOT-REVISION-EXHAUSTED";
        case Value::WriteResult::EnlistmentExhausted:
            return "RHI-QUERY-SLOT-ENLISTMENT-EXHAUSTED";
        }
        return "RHI-QUERY-SLOT-REVISION-EXHAUSTED";
    }

private:
    Value value_;
};
} // namespace arp::rhi::common
