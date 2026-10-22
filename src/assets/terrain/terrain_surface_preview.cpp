#include "assets/terrain/terrain_surface_preview.h"

#include <algorithm>
#include <cmath>

namespace arp::assets {
namespace {
bool validImage(const DecodedImage& image, bool srgb)
{
    return image.width && image.height && image.srgb == srgb
        && std::uint64_t(image.width) * image.height <= SIZE_MAX / 4
        && image.rgba8.size() == std::uint64_t(image.width) * image.height * 4;
}

float linear(float value)
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

const std::array<float, 256> linearByte = [] {
    std::array<float, 256> values{};
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = linear(float(i) / 255.0f);
    return values;
}();

std::uint8_t encode(float value, bool srgb = false)
{
    value = std::clamp(value, 0.0f, 1.0f);
    if (srgb)
        value =
            value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(value * 255));
}

// Source texel centers, repeat addressing. sRGB conversion precedes filtering.
std::array<float, 4> sample(const DecodedImage& image, float u, float v)
{
    const float x = (u - std::floor(u)) * image.width - 0.5f;
    const float y = (v - std::floor(v)) * image.height - 0.5f;
    const auto ix = static_cast<std::int64_t>(std::floor(x));
    const auto iy = static_cast<std::int64_t>(std::floor(y));
    const float fx = x - std::floor(x), fy = y - std::floor(y);
    std::array<float, 4> result{};
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const auto sx = (ix + dx + image.width) % image.width;
            const auto sy = (iy + dy + image.height) % image.height;
            const auto offset = (sy * image.width + sx) * 4;
            const float weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            for (std::size_t c = 0; c < 4; ++c) {
                float value = image.rgba8[offset + c] / 255.0f;
                if (image.srgb && c < 3)
                    value = linearByte[image.rgba8[offset + c]];
                result[c] += value * weight;
            }
        }
    return result;
}
} // namespace

TerrainSurfacePreviewResult TerrainSurfacePreviewBaker::bake(
    const TerrainSurfacePreviewInput& input, const TerrainSurfacePreviewTile& tile) const
{
    TerrainSurfacePreviewResult out;
    out.diagnostic = "EDITOR-TERRAIN-PREVIEW-INPUT";
    const std::uint64_t count = std::uint64_t(tile.samplesPerEdge) * tile.samplesPerEdge;
    const std::uint64_t pixels = std::uint64_t(tile.outputResolution) * tile.outputResolution;
    if (tile.samplesPerEdge < 2 || tile.outputResolution < 2 || pixels > SIZE_MAX / 20
        || pixels * 20 > tile.maxOutputBytes || tile.heights.size() != count
        || tile.pageA.size() != count * 4 || tile.pageB.size() != count * 4 || !tile.heights.data()
        || !tile.pageA.data() || !tile.pageB.data() || !std::isfinite(tile.worldSize)
        || tile.worldSize <= 0 || !std::isfinite(tile.originX) || !std::isfinite(tile.originZ)
        || !std::isfinite(input.snowLine) || !std::isfinite(input.snowFeather)
        || input.snowFeather <= 0)
        return out;
    for (const auto& layer : input.layers)
        if (!std::isfinite(layer.physicalSizeMeters) || layer.physicalSizeMeters <= 0
            || !validImage(layer.albedo, true) || !validImage(layer.normal, false)
            || !validImage(layer.roughness, false) || !validImage(layer.occlusion, false)
            || !std::isfinite((tile.originX + tile.worldSize) / layer.physicalSizeMeters)
            || !std::isfinite((tile.originZ + tile.worldSize) / layer.physicalSizeMeters)
            || !std::isfinite(tile.originX / layer.physicalSizeMeters)
            || !std::isfinite(tile.originZ / layer.physicalSizeMeters))
            return out;
    for (std::size_t p = 0; p < count; ++p) {
        unsigned sum = 0;
        for (std::size_t c = 0; c < 6; ++c)
            sum += c < 4 ? tile.pageA[p * 4 + c] : tile.pageB[p * 4 + c - 4];
        if (!sum || !std::isfinite(tile.heights[p]))
            return out;
    }
    for (std::size_t i = 0; i < out.textures.size(); ++i) {
        auto& texture = out.textures[i];
        texture.width = texture.height = tile.outputResolution;
        texture.srgb = i == 0 || i == 4;
        texture.rgba8.resize(pixels * 4);
    }
    for (std::uint32_t z = 0; z < tile.outputResolution; ++z)
        for (std::uint32_t x = 0; x < tile.outputResolution; ++x) {
            // Lattice endpoints map to output texel centers; the mesh applies
            // the matching half-texel UV transform for seam-free edge samples.
            const float u = float(x) / (tile.outputResolution - 1);
            const float v = float(z) / (tile.outputResolution - 1);
            const float gx = u * (tile.samplesPerEdge - 1), gz = v * (tile.samplesPerEdge - 1);
            const auto x0 = std::min(std::uint32_t(gx), tile.samplesPerEdge - 2);
            const auto z0 = std::min(std::uint32_t(gz), tile.samplesPerEdge - 2);
            const float fx = gx - x0, fz = gz - z0;
            std::array<float, 8> weights{};
            float height = 0;
            for (std::uint32_t dz = 0; dz < 2; ++dz)
                for (std::uint32_t dx = 0; dx < 2; ++dx) {
                    const auto p = std::size_t(z0 + dz) * tile.samplesPerEdge + x0 + dx;
                    const float w = (dx ? fx : 1 - fx) * (dz ? fz : 1 - fz);
                    height += tile.heights[p] * w;
                    for (std::size_t c = 0; c < 8; ++c)
                        weights[c] += (c < 4 ? tile.pageA[p * 4 + c] : tile.pageB[p * 4 + c - 4])
                            / 255.0f * w;
                }
            float sum = 0;
            for (std::size_t i = 0; i < 6; ++i)
                sum += weights[i];
            for (std::size_t i = 0; i < 6; ++i)
                weights[i] /= sum;
            // editor.terrain-preview/v1 pins the shared terrain_surface.slang
            // overlay weight equation; game-specific lighting is excluded.
            const float snow = weights[6]
                * std::clamp((height - input.snowLine) / input.snowFeather, 0.0f, 1.0f) * 0.7f;
            weights[5] += snow;
            weights[0] *= 1 - snow;
            weights[4] *= 1 - snow * 0.8f;
            const float wet = weights[7] * 0.3f;
            weights[2] += wet;
            weights[0] *= 1 - wet * 0.5f;
            sum = 0;
            for (std::size_t i = 0; i < 6; ++i)
                sum += weights[i];
            std::array<float, 3> color{}, normal{};
            float roughness = 0, occlusion = 0;
            for (std::size_t i = 0; i < 6; ++i) {
                const auto& layer = input.layers[i];
                const float su = (tile.originX + u * tile.worldSize) / layer.physicalSizeMeters;
                const float sv = (tile.originZ + v * tile.worldSize) / layer.physicalSizeMeters;
                const auto albedo = sample(layer.albedo, su, sv);
                const auto n = sample(layer.normal, su, sv);
                const float w = weights[i] / sum;
                for (std::size_t c = 0; c < 3; ++c) {
                    color[c] += albedo[c] * w;
                    normal[c] += (n[c] * 2 - 1) * w;
                }
                roughness += sample(layer.roughness, su, sv)[0] * w;
                occlusion += sample(layer.occlusion, su, sv)[0] * w;
            }
            const float length =
                std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            if (!std::isfinite(length) || length <= 0) {
                out.diagnostic = "EDITOR-TERRAIN-PREVIEW-NORMAL";
                out.textures = {};
                return out;
            }
            const auto p = (std::size_t(z) * tile.outputResolution + x) * 4;
            for (std::size_t c = 0; c < 3; ++c) {
                out.textures[0].rgba8[p + c] = encode(color[c], true);
                // TerrainStreamer's +V is +Z, while cross(N,T) is -Z.
                const float sign = c == 1 ? -1.0f : 1.0f;
                out.textures[1].rgba8[p + c] = encode(normal[c] / length * sign * 0.5f + 0.5f);
                out.textures[2].rgba8[p + c] = c == 1 ? encode(roughness) : 0;
                out.textures[3].rgba8[p + c] = encode(occlusion);
                out.textures[4].rgba8[p + c] = 0;
            }
            for (auto& texture : out.textures)
                texture.rgba8[p + 3] = 255;
        }
    out.success = true;
    out.diagnostic.clear();
    return out;
}
} // namespace arp::assets
