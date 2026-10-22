#pragma once
#include <cstdint>
#include <limits>

namespace arp::core {
// Reusable dense rectangular indexing; capacity is declared by its consumer.
// Validation precedes multiplication, including the full exclusive size.
class RowMajorIndexGrid final {
public:
    RowMajorIndexGrid(std::uint64_t rows,
        std::uint64_t columns,
        std::uint64_t maxElements = std::numeric_limits<std::uint64_t>::max()) noexcept
        : rows_(rows), columns_(columns), valid_(rows && columns && rows <= maxElements / columns)
    {}

    bool valid() const noexcept { return valid_; }

    std::uint64_t size() const noexcept { return valid_ ? rows_ * columns_ : 0u; }

    bool index(std::uint64_t row, std::uint64_t column, std::uint64_t& output) const noexcept
    {
        output = 0u;
        if (!valid_ || row >= rows_ || column >= columns_)
            return false;
        output = row * columns_ + column;
        return true;
    }

    bool coordinates(std::uint64_t index, std::uint64_t& row, std::uint64_t& column) const noexcept
    {
        row = column = 0u;
        if (!valid_ || index >= size())
            return false;
        row = index / columns_;
        column = index % columns_;
        return true;
    }

private:
    const std::uint64_t rows_, columns_;
    const bool valid_;
};
} // namespace arp::core
