#pragma once

#include <cassert>
#include <cstdint>
#include <limits>

namespace arp::core {

// Exact signed truncating division for a fixed 32-bit divisor. Preparation is
// outside the hot path. The quotient is wide so INT_MIN / -1 remains defined.
class Int32Divisor final {
public:
    constexpr Int32Divisor() = default;

    explicit constexpr Int32Divisor(std::int32_t divisor) : divisor_(divisor)
    {
        magnitude_ = magnitude(divisor);
        if (magnitude_ != 0)
            reciprocal_ = (scale + magnitude_ - 1u) / magnitude_;
    }

    constexpr bool valid() const { return magnitude_ != 0; }

    constexpr std::int32_t divisor() const { return divisor_; }

    std::int64_t quotient(std::int32_t dividend) const
    {
        assert(valid());
        const auto result = static_cast<std::int64_t>(unsignedQuotient(magnitude(dividend)));
        return (dividend < 0) != (divisor_ < 0) ? -result : result;
    }

    std::int32_t remainder(std::int32_t dividend) const
    {
        assert(valid());
        const auto numerator = magnitude(dividend);
        const auto result =
            static_cast<std::int64_t>(numerator - unsignedQuotient(numerator) * magnitude_);
        return static_cast<std::int32_t>(dividend < 0 ? -result : result);
    }

private:
    static constexpr int bits = std::numeric_limits<std::uint32_t>::digits;
    static constexpr std::uint64_t scale = std::uint64_t(1) << bits;

    static constexpr std::uint32_t magnitude(std::int32_t value)
    {
        const auto bits = static_cast<std::uint32_t>(value);
        return value < 0 ? std::uint32_t(0) - bits : bits;
    }

    std::uint64_t unsignedQuotient(std::uint32_t numerator) const
    {
        // M=ceil(2^32/d): n*M/2^32 >= n/d and excess < n/2^32 <= 1/2.
        // Its integer part is q or q+1. One exact product comparison corrects
        // the possible overestimate; n*M <= 2^63 for signed-32 magnitudes.
        auto estimate = (static_cast<std::uint64_t>(numerator) * reciprocal_) >> bits;
        estimate -= estimate * magnitude_ > numerator;
        return estimate;
    }

    std::int32_t divisor_ = 0;
    std::uint32_t magnitude_ = 0;
    std::uint64_t reciprocal_ = 0;
};

} // namespace arp::core
