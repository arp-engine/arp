#pragma once

#include "core/ibl_bake.h"
#include "core/strided_image_statistics.h"
#include "rhi/gpu_common.h"

#include <cstring>

namespace arp::rhi {

// Canonical L2 numerical value. Publication/provenance is supplied separately.
struct TextureReadbackViewAnalysis {
    core::ImageScalarStatistics scalar;
    core::ImageChannelStatistics channels;
};

// TextureReadback scalar interpretation shared by all native backends.
// Float vectors report channel zero; byte colors report canonical luminance.
inline core::ImageScalarStatistics analyzeTextureReadback(TextureFormat format,
    const std::uint8_t* bytes,
    std::size_t size,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t pitch)
{
    switch (format) {
    case TextureFormat::R8Unorm:
    case TextureFormat::R32Float:
    case TextureFormat::D32Float:
    case TextureFormat::Rg16Float:
    case TextureFormat::Rgba16Float:
    case TextureFormat::Rgba8Unorm:
    case TextureFormat::Rgba8UnormSrgb:
    case TextureFormat::Bgra8Unorm:
        break;
    default:
        return {};
    }
    return core::analyzeStridedImage(bytes,
        size,
        width,
        height,
        pitch,
        static_cast<std::uint32_t>(bytesPerPixel(format)),
        [format](const std::uint8_t* pixel) -> double {
            if (format == TextureFormat::R8Unorm)
                return pixel[0] / 255.0;
            if (format == TextureFormat::R32Float || format == TextureFormat::D32Float) {
                float value;
                std::memcpy(&value, pixel, sizeof(value));
                return value;
            }
            if (format == TextureFormat::Rg16Float || format == TextureFormat::Rgba16Float) {
                std::uint16_t bits;
                std::memcpy(&bits, pixel, sizeof(bits));
                return core::ibl::halfToFloat(bits);
            }
            return canonicalColorLuminance(format, pixel);
        });
}

template <class Sample>
void applyTextureReadbackStatistics(Sample& sample, const core::ImageScalarStatistics& values)
{
    sample.minValue = values.minValue;
    sample.maxValue = values.maxValue;
    sample.meanValue = values.meanValue;
    sample.centerValue = values.centerValue;
    sample.minX = values.minX;
    sample.minY = values.minY;
    sample.maxX = values.maxX;
    sample.maxY = values.maxY;
    sample.sampleHash = values.sampleHash;
    sample.finiteCount = values.finiteCount;
    sample.sampleCount = values.sampleCount;
    sample.valid = values.valid;
}

inline std::uint32_t textureReadbackChannelCount(TextureFormat format)
{
    switch (format) {
    case TextureFormat::R8Unorm:
    case TextureFormat::R32Float:
    case TextureFormat::D32Float:
        return 1;
    case TextureFormat::Rg16Float:
        return 2;
    case TextureFormat::Rgba16Float:
    case TextureFormat::Rgba8Unorm:
    case TextureFormat::Rgba8UnormSrgb:
    case TextureFormat::Bgra8Unorm:
        return 4;
    default:
        return 0;
    }
}

inline core::ImageChannelStatistics analyzeTextureReadbackChannels(TextureFormat format,
    const std::uint8_t* bytes,
    std::size_t size,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t pitch)
{
    const auto channels = textureReadbackChannelCount(format);
    if (!channels)
        return {};
    return core::analyzeStridedImageChannels(bytes,
        size,
        width,
        height,
        pitch,
        static_cast<std::uint32_t>(bytesPerPixel(format)),
        channels,
        [format](const std::uint8_t* pixel, std::uint32_t channel) -> double {
            if (format == TextureFormat::R32Float || format == TextureFormat::D32Float) {
                float value;
                std::memcpy(&value, pixel, sizeof(value));
                return value;
            }
            if (format == TextureFormat::Rg16Float || format == TextureFormat::Rgba16Float) {
                std::uint16_t bits;
                std::memcpy(&bits, pixel + channel * sizeof(bits), sizeof(bits));
                return core::ibl::halfToFloat(bits);
            }
            const auto index =
                format == TextureFormat::Bgra8Unorm && channel < 3u ? 2u - channel : channel;
            return pixel[index] / 255.0;
        });
}

template <class Sample>
void applyTextureReadbackStatistics(Sample& sample,
    TextureFormat format,
    const std::uint8_t* bytes,
    std::size_t size,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t pitch)
{
    applyTextureReadbackStatistics(
        sample, analyzeTextureReadback(format, bytes, size, width, height, pitch));
    sample.channelStatisticsVersion = 1;
    sample.channelStatistics =
        analyzeTextureReadbackChannels(format, bytes, size, width, height, pitch);
}

} // namespace arp::rhi
