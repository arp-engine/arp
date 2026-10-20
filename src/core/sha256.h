#pragma once

#include "core/api.h"
#include "core/base64.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace arp::core {

/// Returns the raw 32-byte SHA-256 digest for a UTF-8 byte string.
class ARP_API Sha256 {
public:
    static constexpr std::size_t digestByteCount = 32u;
    Sha256() noexcept;

    void update(StringView bytes) noexcept;
    ByteBuffer finalize();

    static ByteBuffer digest(StringView text);
    static pmr::String hexDigest(StringView text);

    static bool isHexDigest(StringView text) noexcept
    {
        if (text.size() != digestByteCount * 2u)
            return false;
        for (const auto c : text)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
        return true;
    }

    static bool matchesRawHexDigest(const ByteBuffer& digest, StringView lowercaseHex) noexcept;
    static bool matchesRawHexDigest(
        const std::array<std::uint8_t, digestByteCount>& digest, StringView lowercaseHex) noexcept;
    static bool matchesHexDigest(StringView bytes, StringView lowercaseHex);
    static bool digestFile(const std::filesystem::path& path, ByteBuffer& digest);

private:
    static bool matchesRawHexDigest(const std::uint8_t*, std::size_t, StringView) noexcept;
    void transform(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t byteCount_ = 0u;
    std::size_t bufferedBytes_ = 0u;
    bool finalized_ = false;
};

} // namespace arp::core
