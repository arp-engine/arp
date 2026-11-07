#pragma once

#include "core/container/array_view.h"
#include "core/hash/sha256.h"

#include <array>
#include <cstring>

namespace arp::rhi_metal {
// Native archive metadata is an accelerator identity, never shader authority.
class MetalPipelineArchiveIdentity final {
public:
    using Device = std::array<std::uint64_t, 4>; // registry ID and queried OS version
    static constexpr std::size_t identityBytes = 8 + sizeof(Device);
    static constexpr std::size_t envelopeBytes = identityBytes + core::Sha256::digestByteCount;
    static constexpr std::size_t maximumFileBytes = 64u * 1024u * 1024u;

    static void seal(std::uint8_t* bytes, std::size_t size, const Device& device)
    {
        if (size <= envelopeBytes || size > maximumFileBytes)
            return;
        std::memcpy(bytes, "ARPMTA01", 8);
        std::memcpy(bytes + 8, device.data(), sizeof(device));
        const auto hash = core::Sha256::digest(
            {reinterpret_cast<const char*>(bytes + envelopeBytes), size - envelopeBytes});
        std::memcpy(bytes + identityBytes, hash.data(), hash.size());
    }

    static bool matches(core::ArrayView<const std::uint8_t> bytes, const Device& device)
    {
        if (bytes.size() <= envelopeBytes || bytes.size() > maximumFileBytes
            || std::memcmp(bytes.data(), "ARPMTA01", 8)
            || std::memcmp(bytes.data() + 8, device.data(), sizeof(device)))
            return false;
        const auto hash =
            core::Sha256::digest({reinterpret_cast<const char*>(bytes.data() + envelopeBytes),
                bytes.size() - envelopeBytes});
        return !std::memcmp(bytes.data() + identityBytes, hash.data(), hash.size());
    }
};
} // namespace arp::rhi_metal
