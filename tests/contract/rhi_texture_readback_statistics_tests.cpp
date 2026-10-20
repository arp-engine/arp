#include "rhi/frontend/transfer_readback_statistics.h"
#include "rhi/gpu_extensions.h"

#include <array>
#include <cassert>
#include <limits>

int main()
{
    using namespace arp::rhi;
    const std::array<std::uint8_t, 8> padded{0, 255, 99, 99, 128, 0, 88, 88};
    const std::array<std::uint8_t, 4> tight{0, 255, 128, 0};
    auto a = analyzeTextureReadback(TextureFormat::R8Unorm, padded.data(), padded.size(), 2, 2, 4);
    auto b = analyzeTextureReadback(TextureFormat::R8Unorm, tight.data(), tight.size(), 2, 2, 2);
    assert(a.valid && b.valid && a.sampleHash == b.sampleHash);
    assert(a.sampleCount == 4 && a.finiteCount == 4);
    assert(a.minValue == 0 && a.maxValue == 1 && a.centerValue == 0);
    assert(a.minX == 0 && a.minY == 0 && a.maxX == 1 && a.maxY == 0);
    assert(a.meanValue == (1.0 + 128.0 / 255.0) / 4.0);
    assert(!analyzeTextureReadback(TextureFormat::R8Unorm, padded.data(), 5, 2, 2, 4).valid);
    assert(!analyzeTextureReadback(TextureFormat::R8Unorm, padded.data(), 8, 2, 2, 1).valid);
    assert(!analyzeTextureReadback(TextureFormat::R8Unorm, nullptr, 8, 2, 2, 4).valid);
    assert(!analyzeTextureReadback(TextureFormat::Unknown, padded.data(), 8, 2, 2, 4).valid);
    assert(!analyzeTextureReadback(
        TextureFormat::R8Unorm, padded.data(), 8, UINT32_MAX, UINT32_MAX, UINT32_MAX)
            .valid);
    const std::array<std::uint8_t, 4> rgba{255, 0, 0, 255}, bgra{0, 0, 255, 255};
    a = analyzeTextureReadback(TextureFormat::Rgba8Unorm, rgba.data(), 4, 1, 1, 4);
    b = analyzeTextureReadback(TextureFormat::Bgra8Unorm, bgra.data(), 4, 1, 1, 4);
    assert(a.valid && b.valid && a.meanValue == b.meanValue && a.meanValue == 0.2126);
    const std::array<float, 4> scalar{-2.0f, 4.0f, std::numeric_limits<float>::infinity(), 1.0f};
    a = analyzeTextureReadback(TextureFormat::R32Float,
        reinterpret_cast<const std::uint8_t*>(scalar.data()),
        sizeof(scalar),
        2,
        2,
        8);
    assert(!a.valid && a.finiteCount == 3 && a.sampleCount == 4);
    assert(a.minValue == -2 && a.maxValue == 4 && a.meanValue == 1);
    const std::array<std::uint16_t, 4> half{0x3c00, 0x7c00, 0xbc00, 0};
    a = analyzeTextureReadback(TextureFormat::Rg16Float,
        reinterpret_cast<const std::uint8_t*>(half.data()),
        sizeof(half),
        2,
        1,
        8);
    assert(a.valid && a.minValue == -1 && a.maxValue == 1 && a.meanValue == 0);
    // The legacy R-only statistic intentionally remains valid; the new report
    // must catch the infinite G component independently.
    const auto rg = analyzeTextureReadbackChannels(TextureFormat::Rg16Float,
        reinterpret_cast<const std::uint8_t*>(half.data()),
        sizeof(half),
        2,
        1,
        8);
    assert(rg.channelCount == 2 && !rg.valid);
    assert(rg.channels[0].valid && !rg.channels[1].valid);
    assert(rg.channels[1].sampleCount == 2 && rg.channels[1].finiteCount == 1);
    for (std::uint32_t channel = 0; channel < 4; ++channel) {
        for (const std::uint16_t invalid : {0x7c00u, 0xfc00u, 0x7e00u}) {
            // Two rows with padding containing non-finite bit patterns. Only
            // one selected image channel is invalid; all others stay finite.
            std::array<std::uint16_t, 12> pixels{0x3c00,
                0x4000,
                0x4200,
                0x4400,
                0x7c00,
                0x7e00,
                0xbc00,
                0xc000,
                0xc200,
                0xc400,
                0x7c00,
                0x7e00};
            pixels[channel] = invalid;
            const auto report = analyzeTextureReadbackChannels(TextureFormat::Rgba16Float,
                reinterpret_cast<const std::uint8_t*>(pixels.data()),
                sizeof(pixels),
                1,
                2,
                12);
            assert(report.channelCount == 4 && !report.valid);
            for (std::uint32_t c = 0; c < 4; ++c) {
                assert(report.channels[c].sampleCount == 2);
                assert(report.channels[c].finiteCount == (c == channel ? 1u : 2u));
                assert(report.channels[c].valid == (c != channel));
            }
            pixels[channel] = static_cast<std::uint16_t>(0x3c00u + channel * 0x400u);
            assert(analyzeTextureReadbackChannels(TextureFormat::Rgba16Float,
                reinterpret_cast<const std::uint8_t*>(pixels.data()),
                sizeof(pixels),
                1,
                2,
                12)
                    .valid);
        }
    }
    const auto rgbaChannels = analyzeTextureReadbackChannels(
        TextureFormat::Rgba8Unorm, rgba.data(), rgba.size(), 1, 1, 4);
    const auto bgraChannels = analyzeTextureReadbackChannels(
        TextureFormat::Bgra8Unorm, bgra.data(), bgra.size(), 1, 1, 4);
    assert(rgbaChannels.valid && bgraChannels.valid);
    for (std::uint32_t c = 0; c < 4; ++c)
        assert(rgbaChannels.channels[c].meanValue == bgraChannels.channels[c].meanValue);
    assert(!analyzeTextureReadbackChannels(TextureFormat::Unknown, rgba.data(), 4, 1, 1, 4).valid);
    assert(
        !analyzeTextureReadbackChannels(TextureFormat::Rgba8Unorm, rgba.data(), 3, 1, 1, 4).valid);
    assert(
        !analyzeTextureReadbackChannels(TextureFormat::Rgba8Unorm, rgba.data(), 4, 1, 1, 3).valid);
    TextureReadbackSample sample;
    assert(sample.channelStatisticsVersion == 0);
    applyTextureReadbackStatistics(
        sample, TextureFormat::Rgba8UnormSrgb, rgba.data(), rgba.size(), 1, 1, 4);
    assert(sample.valid && sample.channelStatisticsVersion == 1);
    assert(sample.channelStatistics.valid && sample.channelStatistics.channelCount == 4);
    assert(sample.channelStatistics.channels[3].meanValue == 1.0);
}
