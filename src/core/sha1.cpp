#include "core/sha1.h"

namespace arp::core {

namespace {

std::uint32_t rotateLeft(std::uint32_t value, std::uint32_t bits) noexcept
{
    return (value << bits) | (value >> (32U - bits));
}

void appendUint64(ByteBuffer& bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(
            static_cast<std::uint8_t>((value >> static_cast<std::uint32_t>(shift)) & 0xffU));
    }
}

void appendUint32(ByteBuffer& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

} // namespace

ByteBuffer Sha1::digest(StringView text)
{
    ByteBuffer message{&defaultCpuMemoryResource()};
    message.reserve(text.size() + 72);
    for (char c : text) {
        message.push_back(static_cast<std::uint8_t>(c));
    }

    const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U) {
        message.push_back(0U);
    }
    appendUint64(message, bitLength);

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xefcdab89U;
    std::uint32_t h2 = 0x98badcfeU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xc3d2e1f0U;

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64U) {
        std::uint32_t words[80]{};
        for (std::size_t i = 0; i < 16U; ++i) {
            const std::size_t offset = chunk + (i * 4U);
            words[i] = (static_cast<std::uint32_t>(message[offset]) << 24U)
                | (static_cast<std::uint32_t>(message[offset + 1U]) << 16U)
                | (static_cast<std::uint32_t>(message[offset + 2U]) << 8U)
                | static_cast<std::uint32_t>(message[offset + 3U]);
        }
        for (std::size_t i = 16U; i < 80U; ++i) {
            words[i] =
                rotateLeft(words[i - 3U] ^ words[i - 8U] ^ words[i - 14U] ^ words[i - 16U], 1U);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (std::size_t i = 0; i < 80U; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20U) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999U;
            } else if (i < 40U) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1U;
            } else if (i < 60U) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcU;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6U;
            }
            const std::uint32_t temp = rotateLeft(a, 5U) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotateLeft(b, 30U);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    ByteBuffer out{&defaultCpuMemoryResource()};
    out.reserve(20);
    appendUint32(out, h0);
    appendUint32(out, h1);
    appendUint32(out, h2);
    appendUint32(out, h3);
    appendUint32(out, h4);
    return out;
}

} // namespace arp::core
