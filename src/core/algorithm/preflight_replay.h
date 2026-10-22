#pragma once

#include <cstddef>

namespace arp::core {

enum class PreflightReplayStatus {
    Completed,
    ValidatedOnly,
    PreflightRejected,
    AdmissionRejected,
    ReplayRejected
};

// Replay deterministic, immutable input without retaining a materialized plan
// array. The factory supplies a fresh mapper (including fresh cursor state)
// for each pass. Validate every item before admission or observable consumption.
// Callback construction/mapping/consumption costs remain caller-owned. A replay
// rejection may follow consumption; callers must fail terminally, never pretend
// that irreversible output can be rolled back. No concurrent input mutation.
template <class Value> class PreflightReplay final {
public:
    template <class MapperFactory, class Admission, class Consumer>
    static PreflightReplayStatus run(std::size_t count,
        MapperFactory&& makeMapper,
        Admission&& admit,
        Consumer&& consume,
        bool validateOnly = false)
    {
        if (!count)
            return validateOnly ? PreflightReplayStatus::ValidatedOnly
                                : PreflightReplayStatus::Completed;
        auto preflight = makeMapper();
        for (std::size_t index = 0u; index < count; ++index) {
            Value value{};
            if (!preflight(index, value))
                return PreflightReplayStatus::PreflightRejected;
        }
        if (validateOnly)
            return PreflightReplayStatus::ValidatedOnly;
        if (!admit())
            return PreflightReplayStatus::AdmissionRejected;
        auto replay = makeMapper();
        for (std::size_t index = 0u; index < count; ++index) {
            Value value{};
            if (!replay(index, value))
                return PreflightReplayStatus::ReplayRejected;
            consume(value);
        }
        return PreflightReplayStatus::Completed;
    }
};

} // namespace arp::core
