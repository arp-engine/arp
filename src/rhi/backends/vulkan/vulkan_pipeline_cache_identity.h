#pragma once

#include "core/container/array_view.h"
#include "core/hash/sha256.h"

#include <array>
#include <cstring>
#include <vulkan/vulkan.h>

namespace arp::rhi_vulkan {
// Persisted accelerator / 2. An outer version/driver envelope, integrity
// digest and the native Vulkan header all match before bytes are admitted.
class VulkanPipelineCacheIdentity final {
public:
    static constexpr std::size_t identityBytes = 16;
    static constexpr std::size_t envelopeBytes = identityBytes + core::Sha256::digestByteCount;
    static constexpr std::size_t maximumFileBytes = 64u * 1024u * 1024u;

    static void envelope(std::uint8_t* out, const VkPhysicalDeviceProperties& properties)
    {
        std::memcpy(out, "ARPPSO02", 8);
        std::memcpy(out + 8, &properties.driverVersion, 4);
        std::memcpy(out + 12, &properties.apiVersion, 4);
    }

    static void seal(std::uint8_t* bytes, std::size_t size)
    {
        if (size < envelopeBytes)
            return;
        const auto digest = core::Sha256::digest(
            {reinterpret_cast<const char*>(bytes + envelopeBytes), size - envelopeBytes});
        std::memcpy(bytes + identityBytes, digest.data(), digest.size());
    }

    static bool matches(
        core::ArrayView<const std::uint8_t> bytes, const VkPhysicalDeviceProperties& properties)
    {
        if (bytes.size() < envelopeBytes + 32 || bytes.size() > maximumFileBytes)
            return false;
        std::array<std::uint8_t, identityBytes> expected{};
        envelope(expected.data(), properties);
        if (std::memcmp(bytes.data(), expected.data(), expected.size()))
            return false;
        const auto digest =
            core::Sha256::digest({reinterpret_cast<const char*>(bytes.data() + envelopeBytes),
                bytes.size() - envelopeBytes});
        if (std::memcmp(bytes.data() + identityBytes, digest.data(), digest.size()))
            return false;
        std::array<std::uint32_t, 4> header{};
        std::memcpy(header.data(), bytes.data() + envelopeBytes, sizeof(header));
        return header[0] >= 32 && header[0] <= bytes.size() - envelopeBytes
            && header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && header[2] == properties.vendorID
            && header[3] == properties.deviceID
            && std::memcmp(
                   bytes.data() + envelopeBytes + 16, properties.pipelineCacheUUID, VK_UUID_SIZE)
            == 0;
    }
};
} // namespace arp::rhi_vulkan
