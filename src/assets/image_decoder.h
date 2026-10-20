#pragma once

#include "core/api.h"
#include "core/array_view.h"
#include "core/cpu_allocator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::assets {

struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    bool srgb = true;
    bool alpha = false;
    std::pmr::vector<uint8_t> rgba8;
};

struct ImageDecodeResult {
    bool success = false;
    std::pmr::string diagnostics;
    DecodedImage image;
};

ARP_API ImageDecodeResult decodeImageToRgba8(
    const std::pmr::vector<uint8_t>& encodedBytes, const std::pmr::string& mimeType, bool srgb);

ARP_API ImageDecodeResult decodeImageToRgba8(core::ArrayView<const uint8_t> encodedBytes,
    const std::pmr::string& mimeType,
    bool srgb,
    std::size_t maxDecodedBytes);

} // namespace arp::assets
