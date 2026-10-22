#include "assets/cloud/cloud_asset.h"

#include "core/encoding/yaml_contract.h"
#include "pcg/simplex_noise.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace arp::assets::cloud {
namespace {

bool profileValid(core::Vec4 p) noexcept
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w)
        && p.x >= 0 && p.x < p.y && p.y <= p.z && p.z < p.w && p.w <= 1;
}

float periodicSimplex(core::Vec3 uvw, float frequency, std::uint32_t seed) noexcept
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
                    * pcg::simplex3D((uvw.x - x) * frequency,
                        (uvw.y - y) * frequency,
                        (uvw.z - z) * frequency,
                        seed);
            }
    return result;
}

float fractal(
    core::Vec3 uvw, std::uint32_t frequency, const CloudCookDesc& d, std::uint32_t channel) noexcept
{
    float sum = 0, total = 0, weight = 1, scale = float(frequency);
    for (std::uint32_t octave = 0; octave < d.octaves; ++octave) {
        // Seed streams use distinct integer lanes; wrap is part of version 1.
        sum += weight * periodicSimplex(uvw, scale, d.seed + channel + octave);
        total += weight;
        weight *= 0.5f;
        scale *= 2;
    }
    // pcg::simplex3D already returns [0,1]. Only the normalized weighted sum
    // belongs here; applying another signed-to-unorm remap biases every field.
    return std::clamp(sum / total, 0.0f, 1.0f);
}

std::uint32_t wrap(std::int64_t index, std::uint32_t extent) noexcept
{
    const auto rem = index % std::int64_t(extent);
    return std::uint32_t(rem < 0 ? rem + extent : rem);
}

core::Vec4 sample(const CloudAsset& asset,
    core::Vec3 uvw,
    std::uint32_t offset,
    std::uint32_t extent,
    bool planar) noexcept
{
    // Validated assets and finite coordinates are required by the renderer.
    const core::Vec3 grid{(uvw.x - std::floor(uvw.x)) * extent - 0.5f,
        (uvw.y - std::floor(uvw.y)) * extent - 0.5f,
        planar ? 0.0f : (uvw.z - std::floor(uvw.z)) * extent - 0.5f};
    const core::Vec3 f{
        grid.x - std::floor(grid.x), grid.y - std::floor(grid.y), grid.z - std::floor(grid.z)};
    core::Vec4 result{};
    for (int z = 0; z < (planar ? 1 : 2); ++z)
        for (int y = 0; y != 2; ++y)
            for (int x = 0; x != 2; ++x) {
                const auto ix = wrap(std::int64_t(std::floor(grid.x)) + x, extent);
                const auto iy = wrap(std::int64_t(std::floor(grid.y)) + y, extent);
                const auto iz = planar ? 0u : wrap(std::int64_t(std::floor(grid.z)) + z, extent);
                const auto& cell = asset.cells[offset + (iz * extent + iy) * extent + ix];
                const float weight = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y)
                    * (planar ? 1.0f : (z ? f.z : 1 - f.z));
                result.x += weight * cell.x;
                result.y += weight * cell.y;
                result.z += weight * cell.z;
                result.w += weight * cell.w;
            }
    return result;
}

} // namespace

CloudAssetResult CloudAssetCooker::parse(
    core::StringView yaml, std::uint64_t maxSourceBytes, CloudCookDesc& output)
{
    output = {};
    if (yaml.empty() || yaml.size() > maxSourceBytes)
        return CloudAssetResult::failure("CLOUD-ASSET-SOURCE-BUDGET");
    try {
        using Y = core::YamlContract;
        const auto n = Y::parseSingleDocument(yaml);
        Y::onlyKeys(n,
            {"type",
                "contractVersion",
                "algorithm",
                "algorithmVersion",
                "colorSpace",
                "addressMode",
                "filter",
                "weatherChannels",
                "noiseChannels",
                "seed",
                "weatherDimension",
                "shapeDimension",
                "detailDimension",
                "weatherFrequency",
                "shapeFrequency",
                "detailFrequency",
                "octaves",
                "maxOutputBytes",
                "maxNoiseEvaluations",
                "heightProfiles"});
        if (Y::text(n, "type") != "arp.cloud.cook"
            || Y::uint32(n, "contractVersion") != kCloudAssetVersion
            || Y::text(n, "algorithm") != "CloudPeriodicSimplex")
            return CloudAssetResult::failure("CLOUD-ASSET-VERSION");
        if (Y::uint32(n, "algorithmVersion") != kCloudCookAlgorithmVersion)
            return CloudAssetResult::failure("CLOUD-ASSET-ALGORITHM-VERSION");
        if (Y::text(n, "colorSpace") != "linear" || Y::text(n, "addressMode") != "repeat"
            || Y::text(n, "filter") != "linear"
            || Y::text(n, "weatherChannels") != "coverage,type,density,zero"
            || Y::text(n, "noiseChannels") != "noise,zero,zero,zero")
            return CloudAssetResult::failure("CLOUD-ASSET-SEMANTICS");
        CloudCookDesc d;
        d.algorithmVersion = Y::uint32(n, "algorithmVersion");
        d.seed = Y::uint32(n, "seed");
        d.weatherDimension = Y::uint32(n, "weatherDimension");
        d.shapeDimension = Y::uint32(n, "shapeDimension");
        d.detailDimension = Y::uint32(n, "detailDimension");
        d.weatherFrequency = Y::uint32(n, "weatherFrequency");
        d.shapeFrequency = Y::uint32(n, "shapeFrequency");
        d.detailFrequency = Y::uint32(n, "detailFrequency");
        d.octaves = Y::uint32(n, "octaves");
        d.maxOutputBytes = Y::uint64(Y::required(n, "maxOutputBytes"));
        d.maxNoiseEvaluations = Y::uint64(Y::required(n, "maxNoiseEvaluations"));
        const auto& profiles = Y::sequence(n, "heightProfiles");
        if (profiles.size() != kCloudHeightProfileCount)
            Y::fail();
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            const auto& p = profiles[i];
            Y::onlyKeys(p, {"ramp"});
            const auto v = Y::floatArray<4>(p, "ramp");
            d.heightProfiles[i] = {v[0], v[1], v[2], v[3]};
        }
        std::uint32_t cells = 0;
        const auto valid = plan(d, cells);
        if (valid.success)
            output = d;
        return valid;
    } catch (const std::exception&) {
        return CloudAssetResult::failure("CLOUD-ASSET-YAML");
    } catch (const core::YamlContractError&) {
        return CloudAssetResult::failure("CLOUD-ASSET-YAML");
    }
}

CloudAssetResult CloudAssetCooker::plan(const CloudCookDesc& d, std::uint32_t& cells) noexcept
{
    cells = 0;
    if (d.algorithmVersion != kCloudCookAlgorithmVersion)
        return CloudAssetResult::failure("CLOUD-ASSET-ALGORITHM-VERSION");
    if (d.weatherDimension < 2 || d.shapeDimension < 2 || d.detailDimension < 2
        || !d.weatherFrequency || !d.shapeFrequency || !d.detailFrequency || !d.octaves
        || d.octaves > 31)
        return CloudAssetResult::failure("CLOUD-ASSET-DIMENSIONS");
    for (auto frequency : {d.weatherFrequency, d.shapeFrequency, d.detailFrequency})
        if ((std::uint64_t(frequency) << (d.octaves - 1u)) > std::uint64_t(INT32_MAX / 8))
            return CloudAssetResult::failure("CLOUD-ASSET-LATTICE-RANGE");
    for (const auto p : d.heightProfiles)
        if (!profileValid(p))
            return CloudAssetResult::failure("CLOUD-ASSET-HEIGHT-PROFILE");
    const std::uint64_t weather = std::uint64_t(d.weatherDimension) * d.weatherDimension;
    std::uint64_t total = kCloudHeightProfileCount;
    if (weather > UINT32_MAX - total)
        return CloudAssetResult::failure("CLOUD-ASSET-INDEX-OVERFLOW");
    total += weather;
    for (const auto dim : {d.shapeDimension, d.detailDimension}) {
        const auto square = std::uint64_t(dim) * dim;
        if (square > (UINT32_MAX - total) / dim)
            return CloudAssetResult::failure("CLOUD-ASSET-INDEX-OVERFLOW");
        total += square * dim;
    }
    if (total * sizeof(core::Vec4) > d.maxOutputBytes || total > SIZE_MAX / sizeof(core::Vec4))
        return CloudAssetResult::failure("CLOUD-ASSET-MEMORY-BUDGET");
    const auto evaluations = (total - kCloudHeightProfileCount + 2u * weather) * 8ull * d.octaves;
    if (evaluations > d.maxNoiseEvaluations)
        return CloudAssetResult::failure("CLOUD-ASSET-WORK-BUDGET");
    cells = std::uint32_t(total);
    return {};
}

CloudAssetResult CloudAssetCooker::serialize(const CloudCookDesc& d, core::pmr::String& output)
{
    output.clear();
    std::uint32_t cells = 0;
    const auto valid = plan(d, cells);
    if (!valid.success)
        return valid;
    if (d.maxOutputBytes > INT64_MAX || d.maxNoiseEvaluations > INT64_MAX)
        return CloudAssetResult::failure("CLOUD-ASSET-YAML-INTEGER-RANGE");
    using Y = core::YamlContract;
    auto n = Y::Node::mapping();
    n["type"] = "arp.cloud.cook";
    n["contractVersion"] = kCloudAssetVersion;
    n["algorithm"] = "CloudPeriodicSimplex";
    n["algorithmVersion"] = d.algorithmVersion;
    n["colorSpace"] = "linear";
    n["addressMode"] = "repeat";
    n["filter"] = "linear";
    n["weatherChannels"] = "coverage,type,density,zero";
    n["noiseChannels"] = "noise,zero,zero,zero";
    n["seed"] = d.seed;
    n["weatherDimension"] = d.weatherDimension;
    n["shapeDimension"] = d.shapeDimension;
    n["detailDimension"] = d.detailDimension;
    n["weatherFrequency"] = d.weatherFrequency;
    n["shapeFrequency"] = d.shapeFrequency;
    n["detailFrequency"] = d.detailFrequency;
    n["octaves"] = d.octaves;
    n["maxOutputBytes"] = d.maxOutputBytes;
    n["maxNoiseEvaluations"] = d.maxNoiseEvaluations;
    auto profiles = Y::Node::sequence();
    for (const auto p : d.heightProfiles) {
        auto profile = Y::Node::mapping();
        profile["ramp"] = Y::Node::sequence({p.x, p.y, p.z, p.w});
        profiles.get_value_ref<Y::Node::sequence_type&>().push_back(std::move(profile));
    }
    n["heightProfiles"] = std::move(profiles);
    output = Y::serialize(n);
    return {};
}

CloudAssetResult CloudAssetCooker::cook(const CloudCookDesc& d, CloudAsset& output)
{
    output = {};
    std::uint32_t count = 0;
    const auto planned = plan(d, count);
    if (!planned.success)
        return planned;
    CloudAsset asset;
    asset.source = d;
    asset.cells.reserve(count);
    asset.cells.insert(asset.cells.end(), d.heightProfiles.begin(), d.heightProfiles.end());
    const auto w = d.weatherDimension;
    for (std::uint32_t y = 0; y < w; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const core::Vec3 uvw{(x + 0.5f) / w, (y + 0.5f) / w, 0};
            asset.cells.push_back({fractal(uvw, d.weatherFrequency, d, 0u),
                fractal(uvw, d.weatherFrequency, d, 0x10000u),
                fractal(uvw, d.weatherFrequency, d, 0x20000u),
                0});
        }
    std::uint32_t channel = 0x30000u;
    for (auto spec : {std::array<std::uint32_t, 2>{d.shapeDimension, d.shapeFrequency},
             std::array<std::uint32_t, 2>{d.detailDimension, d.detailFrequency}}) {
        const auto n = spec[0];
        for (std::uint32_t z = 0; z < n; ++z)
            for (std::uint32_t y = 0; y < n; ++y)
                for (std::uint32_t x = 0; x < n; ++x)
                    asset.cells.push_back(
                        {fractal(
                             {(x + 0.5f) / n, (y + 0.5f) / n, (z + 0.5f) / n}, spec[1], d, channel),
                            0,
                            0,
                            0});
        channel += 0x10000u;
    }
    output = std::move(asset);
    return {};
}

CloudAssetResult CloudAssetCooker::validate(const CloudAsset& asset) noexcept
{
    std::uint32_t count = 0;
    const auto planned = plan(asset.source, count);
    if (!planned.success)
        return planned;
    if (asset.cells.size() != count)
        return CloudAssetResult::failure("CLOUD-ASSET-PAYLOAD-SIZE");
    for (std::uint32_t i = 0; i < kCloudHeightProfileCount; ++i) {
        const auto p = asset.source.heightProfiles[i], v = asset.cells[i];
        if (p.x != v.x || p.y != v.y || p.z != v.z || p.w != v.w)
            return CloudAssetResult::failure("CLOUD-ASSET-PROFILE-BINDING");
    }
    const auto weatherEnd = kCloudHeightProfileCount
        + std::uint64_t(asset.source.weatherDimension) * asset.source.weatherDimension;
    for (std::uint32_t i = kCloudHeightProfileCount; i < count; ++i) {
        const auto v = asset.cells[i];
        for (const float value : {v.x, v.y, v.z, v.w})
            if (!std::isfinite(value) || value < 0 || value > 1)
                return CloudAssetResult::failure("CLOUD-ASSET-PAYLOAD-RANGE");
        if (v.w != 0 || (i >= weatherEnd && (v.y != 0 || v.z != 0)))
            return CloudAssetResult::failure("CLOUD-ASSET-RESERVED-CHANNEL");
    }
    return {};
}

core::Vec4 CloudAssetCooker::sampleWeather(const CloudAsset& a, core::Vec2 uv) noexcept
{
    return sample(a, {uv.x, uv.y, 0}, kCloudHeightProfileCount, a.source.weatherDimension, true);
}

float CloudAssetCooker::sampleShape(const CloudAsset& a, core::Vec3 uvw) noexcept
{
    const auto offset =
        kCloudHeightProfileCount + a.source.weatherDimension * a.source.weatherDimension;
    return sample(a, uvw, offset, a.source.shapeDimension, false).x;
}

float CloudAssetCooker::sampleDetail(const CloudAsset& a, core::Vec3 uvw) noexcept
{
    const auto s = a.source.shapeDimension;
    const auto offset = kCloudHeightProfileCount
        + a.source.weatherDimension * a.source.weatherDimension + s * s * s;
    return sample(a, uvw, offset, a.source.detailDimension, false).x;
}

} // namespace arp::assets::cloud
