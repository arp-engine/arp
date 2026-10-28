#include "assets/cloud/cloud_noise.h"
#include "core/hash/stable_hash.h"

#include <cfenv>
#include <cfloat>
#include <limits>

#ifdef __FAST_MATH__
#error CloudPeriodicSimplex/3 cannot be compiled with fast-math
#endif

namespace arp::assets::cloud::detail {
namespace {
class CloudCornerHash final {
    core::StableHash64 stream_;

public:
    CloudCornerHash(std::uint32_t seed, std::uint32_t channel, std::uint32_t octave) noexcept
    {
        stream_.appendString("CloudPeriodicSimplex/3.seed");
        stream_.appendU64(seed);
        stream_.appendU64(channel);
        stream_.appendU64(octave);
    }

    int operator()(int i, int j, int k) const noexcept
    {
        auto hash = stream_;
        hash.appendU64(static_cast<std::uint32_t>(i));
        hash.appendU64(static_cast<std::uint32_t>(j));
        hash.appendU64(static_cast<std::uint32_t>(k));
        const auto value = hash.value();
        return pcg::simplex_detail::perm[(value ^ (value >> 32u)) & 255u];
    }
};

class CloudNoiseV3 final {
    CloudCornerHash hash_;

public:
    CloudNoiseV3(std::uint32_t seed, std::uint32_t channel, std::uint32_t octave) noexcept
        : hash_(seed, channel, octave)
    {}

    static CloudNoiseV3 stream(
        std::uint32_t seed, std::uint32_t channel, std::uint32_t octave) noexcept
    {
        return {seed, channel, octave};
    }

    float sample(float x, float y, float z) const noexcept
    {
        return pcg::Simplex3D<CloudCornerHash>::sample(x, y, z, hash_);
    }
};
} // namespace

CloudAssetResult cookVersion3(const CloudCookDesc& desc, CloudAsset& asset)
{
    static_assert(sizeof(float) == 4u && std::numeric_limits<float>::is_iec559
        && std::numeric_limits<float>::digits == 24 && FLT_EVAL_METHOD == 0);
#ifndef ARP_CLOUD_COOK_V3_PRECISION
    return CloudAssetResult::failure("CLOUD-ASSET-PRECISION-UNAVAILABLE");
#else
    if (std::fegetround() != FE_TONEAREST)
        return CloudAssetResult::failure("CLOUD-ASSET-ROUNDING-MODE");
    CloudNoiseField<CloudNoiseV3>::append(desc, asset);
    return {};
#endif
}

} // namespace arp::assets::cloud::detail
