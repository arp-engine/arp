#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace arp::core {

class StableHash64 {
public:
    static constexpr std::uint64_t kOffset = 1469598103934665603ull;
    static constexpr std::uint64_t kPrime = 1099511628211ull;

    constexpr StableHash64() noexcept = default;

    explicit constexpr StableHash64(std::uint64_t seed) noexcept : value_(seed) {}

    void appendByte(std::uint8_t value) noexcept
    {
        value_ ^= value;
        value_ *= kPrime;
    }

    void appendBytes(const void* data, std::size_t size) noexcept
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            appendByte(bytes[index]);
        }
    }

    void appendString(std::string_view value) noexcept { appendBytes(value.data(), value.size()); }

    void appendU64(std::uint64_t value) noexcept
    {
        for (std::uint32_t byte = 0; byte < 8u; ++byte) {
            appendByte(static_cast<std::uint8_t>((value >> (byte * 8u)) & 0xffu));
        }
    }

    void appendFloat(float value) noexcept
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t), "StableHash64 expects 32-bit float");
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        appendU64(bits);
    }

    constexpr std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_ = kOffset;
};

} // namespace arp::core
