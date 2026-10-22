#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace arp::core {

struct ImageScalarStatistics {
    double minValue = 0.0, maxValue = 0.0, meanValue = 0.0, centerValue = 0.0;
    std::uint32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
    std::uint64_t sampleHash = 0;
    std::uint32_t finiteCount = 0, sampleCount = 0;
    bool valid = false;
};

// A bounded color/scalar image report. Only [0, channelCount) is meaningful.
// Channel order and decoding belong to the image-format consumer.
struct ImageChannelStatistics {
    std::array<ImageScalarStatistics, 4> channels{};
    std::uint32_t channelCount = 0;
    bool valid = false;
};

// Allocation-free row-major analysis. decodePixel supplies the domain's
// scalar interpretation. Padding is excluded from both hash and statistics.
// Keep the established ARP readback hash seed for baseline compatibility.
template <class DecodePixel>
ImageScalarStatistics analyzeStridedImage(const std::uint8_t* data,
    std::size_t dataSize,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t rowPitch,
    std::uint32_t pixelBytes,
    DecodePixel decodePixel)
{
    ImageScalarStatistics result;
    const auto count = std::uint64_t(width) * height;
    const auto rowBytes = std::uint64_t(width) * pixelBytes;
    if (!data || !width || !height || !pixelBytes || count > UINT32_MAX || rowBytes > rowPitch
        || rowBytes > dataSize || std::uint64_t(height - 1u) > (dataSize - rowBytes) / rowPitch) {
        return result;
    }
    result.sampleCount = static_cast<std::uint32_t>(count);
    result.sampleHash = 1469598103934665603ull;
    double sum = 0.0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto* pixel = data + std::size_t(y) * rowPitch + std::size_t(x) * pixelBytes;
            for (std::uint32_t byte = 0; byte < pixelBytes; ++byte) {
                result.sampleHash ^= pixel[byte];
                result.sampleHash *= 1099511628211ull;
            }
            const double value = decodePixel(pixel);
            if (std::isfinite(value)) {
                if (!result.finiteCount || value < result.minValue) {
                    result.minValue = value;
                    result.minX = x;
                    result.minY = y;
                }
                if (!result.finiteCount || value > result.maxValue) {
                    result.maxValue = value;
                    result.maxX = x;
                    result.maxY = y;
                }
                ++result.finiteCount;
                sum += value;
            }
            if (x == width / 2u && y == height / 2u)
                result.centerValue = value;
        }
    }
    result.meanValue = result.finiteCount ? sum / result.finiteCount : 0.0;
    result.valid = result.finiteCount == result.sampleCount;
    return result;
}

template <class DecodeChannel>
ImageChannelStatistics analyzeStridedImageChannels(const std::uint8_t* data,
    std::size_t dataSize,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t rowPitch,
    std::uint32_t pixelBytes,
    std::uint32_t channelCount,
    DecodeChannel decodeChannel)
{
    ImageChannelStatistics result;
    if (!channelCount || channelCount > result.channels.size())
        return result;
    result.channelCount = channelCount;
    result.valid = true;
    for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
        result.channels[channel] = analyzeStridedImage(
            data, dataSize, width, height, rowPitch, pixelBytes, [&](const std::uint8_t* pixel) {
                return decodeChannel(pixel, channel);
            });
        result.valid = result.valid && result.channels[channel].valid;
    }
    return result;
}

} // namespace arp::core
