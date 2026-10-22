#pragma once
#include <cstdint>
#include <utility>

namespace arp::core {
// Inclusive unsigned domain; predicate must be false below one threshold and
// true at/above it. Predicate side effects belong to the caller. No storage,
// allocation or unchecked endpoint addition; at most 65 predicate evaluations
// for the complete uint64 domain (one upper check plus 64 bisections).
class MonotoneThresholdSearch final {
public:
    enum class Status { Found, NoSatisfyingValue, InvalidRange };

    struct Result {
        Status status = Status::InvalidRange;
        std::uint64_t value = 0u;
        std::uint32_t evaluations = 0u;
    };

    template <class Predicate>
    static Result findMinimum(std::uint64_t first, std::uint64_t last, Predicate&& satisfies)
    {
        Result result;
        if (first > last)
            return result;
        ++result.evaluations;
        if (!satisfies(last)) {
            result.status = Status::NoSatisfyingValue;
            return result;
        }
        while (first < last) {
            const auto middle = first + (last - first) / 2u;
            ++result.evaluations;
            if (satisfies(middle))
                last = middle;
            else
                first = middle + 1u; // middle < last, so this cannot overflow
        }
        result.status = Status::Found;
        result.value = first;
        return result;
    }
};
} // namespace arp::core
