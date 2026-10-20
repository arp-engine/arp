#include "core/sha256.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace arp::core {
pmr::String Sha256::hexDigest(StringView text)
{
    const auto bytes = digest(text);
    static constexpr char digits[] = "0123456789abcdef";
    pmr::String result("sha256:", &defaultCpuMemoryResource());
    result.reserve(7 + bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

namespace {

constexpr std::uint32_t rotateRight(std::uint32_t value, std::uint32_t bits) noexcept
{
    return (value >> bits) | (value << (32U - bits));
}

void appendUint32(ByteBuffer& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

constexpr std::uint32_t kRoundConstants[64]{
    0x428a2f98U,
    0x71374491U,
    0xb5c0fbcfU,
    0xe9b5dba5U,
    0x3956c25bU,
    0x59f111f1U,
    0x923f82a4U,
    0xab1c5ed5U,
    0xd807aa98U,
    0x12835b01U,
    0x243185beU,
    0x550c7dc3U,
    0x72be5d74U,
    0x80deb1feU,
    0x9bdc06a7U,
    0xc19bf174U,
    0xe49b69c1U,
    0xefbe4786U,
    0x0fc19dc6U,
    0x240ca1ccU,
    0x2de92c6fU,
    0x4a7484aaU,
    0x5cb0a9dcU,
    0x76f988daU,
    0x983e5152U,
    0xa831c66dU,
    0xb00327c8U,
    0xbf597fc7U,
    0xc6e00bf3U,
    0xd5a79147U,
    0x06ca6351U,
    0x14292967U,
    0x27b70a85U,
    0x2e1b2138U,
    0x4d2c6dfcU,
    0x53380d13U,
    0x650a7354U,
    0x766a0abbU,
    0x81c2c92eU,
    0x92722c85U,
    0xa2bfe8a1U,
    0xa81a664bU,
    0xc24b8b70U,
    0xc76c51a3U,
    0xd192e819U,
    0xd6990624U,
    0xf40e3585U,
    0x106aa070U,
    0x19a4c116U,
    0x1e376c08U,
    0x2748774cU,
    0x34b0bcb5U,
    0x391c0cb3U,
    0x4ed8aa4aU,
    0x5b9cca4fU,
    0x682e6ff3U,
    0x748f82eeU,
    0x78a5636fU,
    0x84c87814U,
    0x8cc70208U,
    0x90befffaU,
    0xa4506cebU,
    0xbef9a3f7U,
    0xc67178f2U,
};

} // namespace

Sha256::Sha256() noexcept
    : state_{
          0x6a09e667U,
          0xbb67ae85U,
          0x3c6ef372U,
          0xa54ff53aU,
          0x510e527fU,
          0x9b05688cU,
          0x1f83d9abU,
          0x5be0cd19U,
      }
{}

void Sha256::transform(const std::uint8_t* block) noexcept
{
    std::uint32_t words[64]{};
    for (std::size_t index = 0; index < 16U; ++index) {
        const auto offset = index * 4U;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U)
            | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U)
            | (static_cast<std::uint32_t>(block[offset + 2U]) << 8U)
            | static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < 64U; ++index) {
        const auto s0 = rotateRight(words[index - 15U], 7U) ^ rotateRight(words[index - 15U], 18U)
            ^ (words[index - 15U] >> 3U);
        const auto s1 = rotateRight(words[index - 2U], 17U) ^ rotateRight(words[index - 2U], 19U)
            ^ (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t index = 0; index < 64U; ++index) {
        const auto s1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
        const auto ch = (e & f) ^ ((~e) & g);
        const auto temp1 = h + s1 + ch + kRoundConstants[index] + words[index];
        const auto s0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
        const auto maj = (a & b) ^ (a & c) ^ (b & c);
        const auto temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(StringView bytes) noexcept
{
    if (finalized_ || bytes.empty())
        return;
    byteCount_ += static_cast<std::uint64_t>(bytes.size());
    const auto* input = reinterpret_cast<const std::uint8_t*>(bytes.data());
    std::size_t remaining = bytes.size();
    if (bufferedBytes_ != 0u) {
        const auto count = std::min(remaining, buffer_.size() - bufferedBytes_);
        std::memcpy(buffer_.data() + bufferedBytes_, input, count);
        bufferedBytes_ += count;
        input += count;
        remaining -= count;
        if (bufferedBytes_ == buffer_.size()) {
            transform(buffer_.data());
            bufferedBytes_ = 0u;
        }
    }
    while (remaining >= buffer_.size()) {
        transform(input);
        input += buffer_.size();
        remaining -= buffer_.size();
    }
    if (remaining != 0u) {
        std::memcpy(buffer_.data(), input, remaining);
        bufferedBytes_ = remaining;
    }
}

ByteBuffer Sha256::finalize()
{
    if (!finalized_) {
        const auto bitLength = byteCount_ * 8u;
        buffer_[bufferedBytes_++] = 0x80u;
        if (bufferedBytes_ > 56u) {
            std::fill(buffer_.begin() + bufferedBytes_, buffer_.end(), 0u);
            transform(buffer_.data());
            bufferedBytes_ = 0u;
        }
        std::fill(buffer_.begin() + bufferedBytes_, buffer_.begin() + 56u, 0u);
        for (std::size_t index = 0; index < 8u; ++index) {
            buffer_[56u + index] =
                static_cast<std::uint8_t>((bitLength >> ((7u - index) * 8u)) & 0xffu);
        }
        transform(buffer_.data());
        bufferedBytes_ = 0u;
        finalized_ = true;
    }

    ByteBuffer result{&defaultCpuMemoryResource()};
    result.reserve(32U);
    for (const auto value : state_) {
        appendUint32(result, value);
    }
    return result;
}

ByteBuffer Sha256::digest(StringView text)
{
    Sha256 hash;
    hash.update(text);
    return hash.finalize();
}

bool Sha256::matchesHexDigest(StringView bytes, StringView expected)
{
    if (expected.size() != digestByteCount * 2u)
        return false;
    return matchesRawHexDigest(digest(bytes), expected);
}

bool Sha256::matchesRawHexDigest(const ByteBuffer& hash, StringView expected) noexcept
{
    return matchesRawHexDigest(hash.data(), hash.size(), expected);
}

bool Sha256::matchesRawHexDigest(
    const std::array<std::uint8_t, digestByteCount>& hash, StringView expected) noexcept
{
    return matchesRawHexDigest(hash.data(), hash.size(), expected);
}

bool Sha256::matchesRawHexDigest(
    const std::uint8_t* hash, std::size_t size, StringView expected) noexcept
{
    if (size != digestByteCount || expected.size() != digestByteCount * 2u)
        return false;
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < size; ++i)
        if (expected[i * 2u] != hex[hash[i] >> 4u] || expected[i * 2u + 1u] != hex[hash[i] & 15u])
            return false;
    return true;
}

bool Sha256::digestFile(const std::filesystem::path& path, ByteBuffer& digest)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.good())
        return false;
    Sha256 hash;
    std::array<char, 64u * 1024u> buffer{};
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0)
            hash.update(StringView(buffer.data(), static_cast<std::size_t>(count)));
    }
    if (!input.eof())
        return false;
    digest = hash.finalize();
    return true;
}

} // namespace arp::core
