#pragma once
#include "assets/cloud/cloud_asset.h"
#include "pcg/simplex_noise.h"

#include <algorithm>

namespace arp::assets::cloud::detail {
// Assets own periodic fields, channels, fBm and cell order; PCG owns simplex.
template <class Noise> class CloudNoiseField final {
    static float periodicSimplex(core::Vec3 uvw, float frequency, const Noise& noise) noexcept
    {
        const auto smooth = [](float t) {
            return t * t * (3 - 2 * t);
        };
        const core::Vec3 f{smooth(uvw.x), smooth(uvw.y), smooth(uvw.z)};
        float result = 0;
        for (int z = 0; z != 2; ++z)
            for (int y = 0; y != 2; ++y)
                for (int x = 0; x != 2; ++x) {
                    const float weight =
                        (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y) * (z ? f.z : 1 - f.z);
                    result += weight
                        * noise.sample((uvw.x - x) * frequency,
                            (uvw.y - y) * frequency,
                            (uvw.z - z) * frequency);
                }
        return result;
    }

    static float fractal(core::Vec3 uvw,
        std::uint32_t frequency,
        const CloudCookDesc& d,
        std::uint32_t channel) noexcept
    {
        float sum = 0, total = 0, weight = 1, scale = float(frequency);
        for (std::uint32_t octave = 0; octave < d.octaves; ++octave) {
            const auto noise = Noise::stream(d.seed, channel, octave);
            sum += weight * periodicSimplex(uvw, scale, noise);
            total += weight;
            weight *= 0.5f;
            scale *= 2;
        }
        // pcg::simplex3D already returns [0,1]. Only the normalized weighted sum
        // belongs here; applying another signed-to-unorm remap biases every field.
        return std::clamp(sum / total, 0.0f, 1.0f);
    }

public:
    static void append(const CloudCookDesc& d, CloudAsset& asset)
    {
        const auto w = d.weatherDimension;
        for (std::uint32_t y = 0; y < w; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                const core::Vec3 uvw{(x + 0.5f) / w, (y + 0.5f) / w, 0};
                asset.cells.push_back({fractal(uvw, d.weatherFrequency, d, 0u),
                    fractal(uvw, d.weatherFrequency, d, 1u),
                    fractal(uvw, d.weatherFrequency, d, 2u),
                    0});
            }
        std::uint32_t channel = 3u;
        for (auto spec : {std::array<std::uint32_t, 2>{d.shapeDimension, d.shapeFrequency},
                 std::array<std::uint32_t, 2>{d.detailDimension, d.detailFrequency}}) {
            const auto n = spec[0];
            for (std::uint32_t z = 0; z < n; ++z)
                for (std::uint32_t y = 0; y < n; ++y)
                    for (std::uint32_t x = 0; x < n; ++x)
                        asset.cells.push_back(
                            {fractal({(x + 0.5f) / n, (y + 0.5f) / n, (z + 0.5f) / n},
                                 spec[1],
                                 d,
                                 channel),
                                0,
                                0,
                                0});
            ++channel;
        }
    }
};

CloudAssetResult cookVersion3(const CloudCookDesc& desc, CloudAsset& asset);
} // namespace arp::assets::cloud::detail
