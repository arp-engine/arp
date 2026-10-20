#pragma once

#include "core/base64.h"
#include "core/unsigned_range.h"

#include <algorithm>
#include <array>
#include <limits>

namespace arp::core {

// Borrowed cold binary view. Explicit width/order; no alignment or host-endian
// assumptions. Algorithms can be reused outside RHI and allocate only on copy.
class UnsignedElementSpan final {
public:
    enum class Width { Uint16, Uint32 };
    enum class ByteOrder { LittleEndian, BigEndian, Unknown };

    struct Extrema {
        std::uint32_t minimum = 0, maximum = 0;
    };

    UnsignedElementSpan(const void* data, std::size_t bytes, Width width, ByteOrder order) noexcept
        : data_(static_cast<const std::uint8_t*>(data)), bytes_(bytes), width_(width), order_(order)
    {
        if (width != Width::Uint16 && width != Width::Uint32)
            diagnostic_ = "CORE-UNSIGNED-SPAN-WIDTH-UNKNOWN";
        else if (order != ByteOrder::LittleEndian && order != ByteOrder::BigEndian)
            diagnostic_ = "CORE-UNSIGNED-SPAN-BYTE-ORDER-UNKNOWN";
        else if (!data || !bytes || bytes % elementBytes())
            diagnostic_ = "CORE-UNSIGNED-SPAN-BYTES-INVALID";
    }

    const char* diagnostic() const noexcept { return diagnostic_; }

    std::size_t elementBytes() const noexcept
    {
        switch (width_) {
        case Width::Uint16:
            return sizeof(std::uint16_t);
        case Width::Uint32:
            return sizeof(std::uint32_t);
        }
        return 0u;
    }

    std::uint64_t size() const noexcept { return diagnostic_ ? 0u : bytes_ / elementBytes(); }

    bool read(std::uint64_t index, std::uint32_t& output) const noexcept
    {
        output = 0u;
        if (diagnostic_ || index >= size())
            return false;
        output = element(static_cast<std::size_t>(index));
        return true;
    }

    // Compare decoded values with an explicit native uint32 footprint. Neither
    // alignment nor the byte order of this borrowed binary view is assumed.
    bool matchesWords(const std::uint32_t* expected, std::size_t count) const noexcept
    {
        if (!expected || !count || diagnostic_ || size() != count)
            return false;
        for (std::size_t i = 0u; i < count; ++i) {
            std::uint32_t actual = 0u;
            if (!read(i, actual) || actual != expected[i])
                return false;
        }
        return true;
    }

    // Copy one decoded scalar into a caller-owned canonical footprint. Reading
    // first permits in-place element normalization; failure leaves output intact.
    bool copyElementLittleEndian(
        std::uint64_t index, void* destination, std::size_t capacity) const noexcept
    {
        std::uint32_t value = 0u;
        if (!destination || capacity < elementBytes() || !read(index, value))
            return false;
        return writeLittleEndian(value, width_, destination, capacity);
    }

    // Checked scalar authoring, independent of a borrowed source. Rejection is
    // atomic and never truncates a uint32 value into a uint16 footprint.
    static bool writeLittleEndian(
        std::uint32_t value, Width width, void* destination, std::size_t capacity) noexcept
    {
        const auto count = width == Width::Uint16 ? sizeof(std::uint16_t)
            : width == Width::Uint32              ? sizeof(std::uint32_t)
                                                  : 0u;
        if (!destination || !count || capacity < count
            || (width == Width::Uint16 && value > UINT16_MAX))
            return false;
        auto* bytes = static_cast<std::uint8_t*>(destination);
        for (std::size_t j = 0; j < count; ++j)
            bytes[j] = static_cast<std::uint8_t>(value >> (j * 8u));
        return true;
    }

    // Exact typed uint32 footprint: every scalar and destination extent is
    // representable by construction. Source and destination are distinct
    // typed arrays; no heap storage or host byte-order assumption is involved.
    template <std::size_t Count>
    static void encodeWordsLittleEndian(const std::array<std::uint32_t, Count>& words,
        std::array<std::uint8_t, Count * sizeof(std::uint32_t)>& bytes) noexcept
    {
        for (std::size_t i = 0u; i < Count; ++i)
            writeLittleEndian(words[i],
                Width::Uint32,
                bytes.data() + i * sizeof(std::uint32_t),
                sizeof(std::uint32_t));
    }

    const char* extrema(std::uint64_t first, std::uint64_t count, Extrema& output) const noexcept
    {
        output = {};
        if (diagnostic_)
            return diagnostic_;
        if (!containsUnsignedRange(std::uint64_t{0}, size(), first, count))
            return "CORE-UNSIGNED-SPAN-RANGE-INVALID";
        Extrema result{std::numeric_limits<std::uint32_t>::max(), 0u};
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto value = element(static_cast<std::size_t>(first + i));
            result.minimum = std::min(result.minimum, value);
            result.maximum = std::max(result.maximum, value);
        }
        output = result;
        return nullptr;
    }

    const char* copyLittleEndian(ByteBuffer& output) const
    {
        if (diagnostic_)
            return diagnostic_;
        ByteBuffer normalized{output.get_allocator().resource()};
        normalized.resize(bytes_);
        for (std::size_t i = 0; i < size(); ++i)
            copyElementLittleEndian(i, normalized.data() + i * elementBytes(), elementBytes());
        output = std::move(normalized);
        return nullptr;
    }

private:
    std::uint32_t element(std::size_t index) const noexcept
    {
        std::uint32_t value = 0;
        for (std::size_t j = 0; j < elementBytes(); ++j) {
            const auto shift =
                (order_ == ByteOrder::LittleEndian ? j : elementBytes() - 1u - j) * 8u;
            value |= std::uint32_t{data_[index * elementBytes() + j]} << shift;
        }
        return value;
    }

    const std::uint8_t* data_;
    std::size_t bytes_;
    Width width_;
    ByteOrder order_;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::core
