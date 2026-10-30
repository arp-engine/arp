#pragma once

#include "core/hash/sha256.h"

#include <array>
#include <cstdint>

namespace arp::core::io {

enum class FileVersionKind : std::uint8_t {
    None,
    ContentSha256,
    MetadataObservation,
    DirectorySnapshotSha256,
};

struct FileVersionToken {
    FileVersionKind kind = FileVersionKind::None;
    std::array<std::uint8_t, 32> digest{};

    bool valid() const noexcept
    {
        if (kind == FileVersionKind::None)
            return digest == std::array<std::uint8_t, 32>{};
        return kind == FileVersionKind::ContentSha256
            || kind == FileVersionKind::MetadataObservation
            || kind == FileVersionKind::DirectorySnapshotSha256;
    }

    bool operator==(const FileVersionToken& other) const noexcept
    {
        return kind == other.kind && digest == other.digest;
    }

    bool operator!=(const FileVersionToken& other) const noexcept { return !(*this == other); }
};

// Canonical IO version preimages use fixed little-endian integer fields.
class FileVersionEncoding final {
public:
    static void appendNumber(core::Sha256& hash, std::uint64_t value) noexcept
    {
        std::array<std::uint8_t, sizeof(value)> bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
        hash.update({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    }

    static FileVersionToken finish(core::Sha256& hash, FileVersionKind kind)
    {
        FileVersionToken token;
        token.kind = kind;
        const auto value = hash.finalize();
        for (std::size_t i = 0; i < token.digest.size(); ++i)
            token.digest[i] = value[i];
        return token;
    }
};

} // namespace arp::core::io
