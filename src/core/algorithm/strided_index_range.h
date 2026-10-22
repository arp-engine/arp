#pragma once
#include <cstdint>
#include <limits>

namespace arp::core {
// Exact unsigned index sequence. Validating the last index proves every
// intermediate multiply/add; the maximum is supplied by the consuming format.
class StridedIndexRange final {
public:
    StridedIndexRange(std::uint64_t first,
        std::uint64_t count,
        std::uint64_t stride,
        std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max()) noexcept
        : first_(first), count_(count), stride_(stride),
          valid_(count && stride && first <= maximum && count - 1u <= (maximum - first) / stride)
    {}

    bool valid() const noexcept { return valid_; }

    std::uint64_t size() const noexcept { return valid_ ? count_ : 0u; }

    bool index(std::uint64_t ordinal, std::uint64_t& result) const noexcept
    {
        result = 0u;
        if (!valid_ || ordinal >= count_)
            return false;
        result = first_ + ordinal * stride_;
        return true;
    }

private:
    std::uint64_t first_, count_, stride_;
    bool valid_;
};
} // namespace arp::core
