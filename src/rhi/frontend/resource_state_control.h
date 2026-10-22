#pragma once
#include "core/ownership/versioned_mutation_value.h"
#include "rhi/frontend/resource_recorded_state_domain.h"
#include "rhi/frontend/resource_state_validation.h"
#include "rhi/gpu_common.h"

namespace arp::rhi::common {

// Stable original L2 scalar state; retaining its containing original resource
// is mandatory. This control grants no native barrier or command authority.
class RecordedResourceStateControl final {
    friend class rhi::GpuDevice;
    using Value = core::VersionedMutationValue<ResourceState>;

public:
    using Observation = Value::Observation;
    using PendingObservation = Value::PendingObservation;

    explicit RecordedResourceStateControl(ResourceState initial) noexcept : value_(initial) {}

    ResourceState state() const noexcept { return value_.value(); }

    std::uint64_t revision() const noexcept { return value_.revision(); }

    bool pending() const noexcept { return value_.pending(); }

    bool active() const noexcept { return value_.active() && !domain_.diagnostic(); }

    const char* originalDomainDiagnostic() const noexcept { return domain_.diagnostic(); }

    bool canPrepare(const core::MutationJournal& journal) const noexcept
    {
        return active() && semantics::isKnownResourceState(state()) && value_.canPrepare(journal);
    }

    bool canWriteBatch(const core::MutationJournal& journal, std::uint64_t writes) const noexcept
    {
        return active() && semantics::isKnownResourceState(state())
            && value_.canWriteBatch(journal, writes);
    }

    bool prepare(core::MutationJournal& journal) noexcept
    {
        return active() && semantics::isKnownResourceState(state()) && value_.prepare(journal);
    }

    auto observe() const noexcept
    {
        return active() && semantics::isKnownResourceState(state()) ? value_.observe()
                                                                    : std::optional<Observation>{};
    }

    bool matches(const Observation& input) const noexcept
    {
        return active() && semantics::isKnownResourceState(state()) && value_.matches(input);
    }

    auto observePending(const core::MutationJournal& journal) const noexcept
    {
        return active() && semantics::isKnownResourceState(state())
            ? value_.observePending(journal)
            : std::optional<PendingObservation>{};
    }

    bool matchesPending(
        const PendingObservation& input, const core::MutationJournal& journal) const noexcept
    {
        return active() && semantics::isKnownResourceState(state())
            && value_.matchesPending(input, journal);
    }

    void invalidate() noexcept { value_.invalidate(); }

    const char* writeBuffer(ResourceState after, core::MutationJournal* journal) noexcept
    {
        if (const auto* id = domain_.diagnostic())
            return id;
        if (!semantics::isBufferResourceState(state()) || !semantics::isBufferResourceState(after))
            return "RHI-BUFFER-RESOURCE-STATE-INVALID";
        return write(after, journal);
    }

    const char* write(ResourceState after, core::MutationJournal* journal) noexcept
    {
        if (const auto* id = domain_.diagnostic())
            return id;
        if (!semantics::isKnownResourceState(after))
            return "RHI-RESOURCE-STATE-INVALID";
        switch (value_.tryWrite(after, journal)) {
        case Value::WriteResult::Written:
            return nullptr;
        case Value::WriteResult::Conflict:
            return "RHI-RESOURCE-STATE-RECORDING-CONFLICT";
        case Value::WriteResult::Exhausted:
            return "RHI-RESOURCE-STATE-REVISION-EXHAUSTED";
        case Value::WriteResult::EnlistmentExhausted:
            return "RHI-RESOURCE-STATE-ENLISTMENT-EXHAUSTED";
        }
        return "RHI-RESOURCE-STATE-REVISION-EXHAUSTED";
    }

private:
    OriginalRecordedStateDomain domain_;
    Value value_;
};
} // namespace arp::rhi::common
