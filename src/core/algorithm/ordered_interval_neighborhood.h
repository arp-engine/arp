#pragma once
#include <cstdint>
#include <optional>
#include <type_traits>

namespace arp::core {
// Select the nearest complete intervals on either side of a point. Input may
// be unordered and contain identical intervals (multiple contributions to one
// scope). Equal selected boundaries with different intervals are ambiguous.
// Inclusive ends avoid sentinel and end+1 overflow; failures clear both sides.
template <class UInt> class OrderedIntervalNeighborhood final {
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);

public:
    struct Interval {
        UInt first = 0u, last = 0u;

        bool matches(const Interval& other) const noexcept
        {
            return first == other.first && last == other.last;
        }
    };
    enum class Status {
        Complete,
        InvalidDomain,
        InputCapacity,
        InvalidInterval,
        ContainsPoint,
        AmbiguousBoundary
    };

    struct Result {
        Status status = Status::Complete;
        std::optional<Interval> before, after;
        std::uint64_t inputs = 0u;
    };

    OrderedIntervalNeighborhood(UInt point, UInt domainCount, std::uint64_t maxInputs) noexcept
        : point_(point), domainCount_(domainCount), maxInputs_(maxInputs)
    {
        if (!domainCount || point >= domainCount)
            fail(Status::InvalidDomain);
    }

    Status observe(Interval interval) noexcept
    {
        if (result_.status != Status::Complete)
            return result_.status;
        if (result_.inputs == maxInputs_)
            return fail(Status::InputCapacity);
        if (interval.first > interval.last || interval.last >= domainCount_)
            return fail(Status::InvalidInterval);
        if (interval.first <= point_ && point_ <= interval.last)
            return fail(Status::ContainsPoint);
        auto& selected = interval.last < point_ ? result_.before : result_.after;
        if (selected) {
            const auto boundary = interval.last < point_ ? interval.last : interval.first;
            const auto previous = interval.last < point_ ? selected->last : selected->first;
            if (boundary == previous && !interval.matches(*selected))
                return fail(Status::AmbiguousBoundary);
            if (interval.last < point_ ? boundary > previous : boundary < previous)
                selected = interval;
        } else
            selected = interval;
        ++result_.inputs;
        return result_.status;
    }

    const Result& result() const noexcept { return result_; }

private:
    Status fail(Status status) noexcept
    {
        result_ = {status, {}, {}, 0u};
        return status;
    }

    const UInt point_, domainCount_;
    const std::uint64_t maxInputs_;
    Result result_;
};
} // namespace arp::core
