#pragma once
#include "core/container/array_view.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace arp::core::encoding {
class BinaryWriter final {
public:
    explicit BinaryWriter(std::size_t limit) : limit_(limit) {}

    void integer(std::uint64_t value, unsigned bytes = 8)
    {
        if (bytes > 8)
            throw std::runtime_error("BINARY-WIDTH");
        reserve(bytes);
        for (unsigned i = 0; i < bytes; ++i)
            bytes_.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }

    void raw(ArrayView<const std::uint8_t> value)
    {
        reserve(value.size());
        if (!value.empty())
            bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void text(std::string_view value)
    {
        integer(value.size());
        raw({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
    }

    pmr::Vector<std::uint8_t> take() { return std::move(bytes_); }

    const auto& bytes() const { return bytes_; }

private:
    void reserve(std::size_t count)
    {
        if (count > limit_ - bytes_.size())
            throw std::runtime_error("BINARY-BYTE-BUDGET");
    }

    std::size_t limit_;
    pmr::Vector<std::uint8_t> bytes_{&defaultCpuMemoryResource()};
};

class BinaryReader final {
public:
    explicit BinaryReader(ArrayView<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint64_t integer(unsigned bytes = 8)
    {
        if (bytes > 8)
            throw std::runtime_error("BINARY-WIDTH");
        auto v = raw(bytes);
        std::uint64_t result = 0;
        for (unsigned i = 0; i < bytes; ++i)
            result |= std::uint64_t(v[i]) << (8 * i);
        return result;
    }

    ArrayView<const std::uint8_t> raw(std::uint64_t count)
    {
        if (count > remaining())
            throw std::runtime_error("BINARY-TRUNCATED");
        auto result = ArrayView<const std::uint8_t>(
            bytes_.data() + position_, static_cast<std::size_t>(count));
        position_ += static_cast<std::size_t>(count);
        return result;
    }

    pmr::String text(std::size_t limit)
    {
        const auto size = integer();
        if (size > limit)
            throw std::runtime_error("BINARY-STRING-BUDGET");
        auto v = raw(size);
        return pmr::String(
            reinterpret_cast<const char*>(v.data()), v.size(), &defaultCpuMemoryResource());
    }

    std::size_t remaining() const { return bytes_.size() - position_; }

private:
    ArrayView<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
};
} // namespace arp::core::encoding
