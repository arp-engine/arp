#pragma once

#include <limits>
#include <type_traits>

namespace arp::core {

// Number of significant binary digits; zero has width zero. Also the number
// of levels in a complete mip chain for an unsigned maximum texture extent.
template <class UInt> constexpr unsigned unsignedBitWidth(UInt value) noexcept
{
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);
    unsigned width = 0u;
    for (; value; value >>= 1u)
        ++width;
    return width;
}

template <class UInt> constexpr bool isPowerOfTwoUnsigned(UInt value) noexcept
{
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);
    return value != UInt{0} && (value & (value - UInt{1})) == UInt{0};
}

// Least multiple of a nonzero alignment at or above value. No overflowing
// intermediate sum; callers need not restrict alignment to powers of two.
template <class UInt>
constexpr bool roundUpUnsigned(UInt value, UInt alignment, UInt& output) noexcept
{
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);
    output = 0;
    if (!alignment)
        return false;
    const auto remainder = value % alignment;
    const auto padding = remainder ? alignment - remainder : UInt{0};
    if (padding > std::numeric_limits<UInt>::max() - value)
        return false;
    output = value + padding;
    return true;
}

// Multiplication guard shared by finite byte-layout and capacity contracts.
template <class UInt> constexpr bool canMultiplyUnsigned(UInt lhs, UInt rhs) noexcept
{
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);
    return !rhs || lhs <= std::numeric_limits<UInt>::max() / rhs;
}

// Number of unordered distinct pairs without an overflowing product before
// division. Shared by independently bounded alias/usage traversals.
template <class UInt> constexpr bool countUnorderedPairsUnsigned(UInt count, UInt& output) noexcept
{
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);
    output = 0;
    if (count < UInt{2})
        return true;
    UInt lhs = count, rhs = count - UInt{1};
    if (lhs % UInt{2} == 0)
        lhs /= UInt{2};
    else
        rhs /= UInt{2};
    if (!canMultiplyUnsigned(lhs, rhs))
        return false;
    output = lhs * rhs;
    return true;
}

// Finite nonempty intervals, including a final element at UInt's maximum.
// Difference comparison never forms an overflowing exclusive end address.
template <class UInt>
constexpr bool overlapsUnsignedRanges(
    UInt lhsStart, UInt lhsCount, UInt rhsStart, UInt rhsCount) noexcept
{
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);
    constexpr auto maximum = std::numeric_limits<UInt>::max();
    if (!lhsCount || !rhsCount || lhsCount - 1u > maximum - lhsStart
        || rhsCount - 1u > maximum - rhsStart)
        return false;
    return lhsStart <= rhsStart ? rhsStart - lhsStart < lhsCount : lhsStart - rhsStart < rhsCount;
}

// Finite, nonempty half-open ranges. Never forms an overflowing end address.
template <class UInt>
constexpr bool containsUnsignedRange(
    UInt outerStart, UInt outerCount, UInt innerStart, UInt innerCount) noexcept
{
    static_assert(std::is_integral_v<UInt> && std::is_unsigned_v<UInt>);
    constexpr auto maximum = std::numeric_limits<UInt>::max();
    if (!outerCount || !innerCount || outerCount - 1 > maximum - outerStart
        || innerCount - 1 > maximum - innerStart || innerStart < outerStart)
        return false;
    const auto offset = innerStart - outerStart;
    return offset < outerCount && innerCount <= outerCount - offset;
}

} // namespace arp::core
