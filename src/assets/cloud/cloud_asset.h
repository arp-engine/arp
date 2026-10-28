#pragma once

#include "core/api.h"
#include "core/encoding/base64.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"

#include <array>

namespace arp::assets::cloud {

inline constexpr std::uint32_t kCloudAssetVersion = 1u;
inline constexpr std::uint32_t kCloudCookAlgorithmVersion = 3u;
inline constexpr std::uint32_t kCloudLegacyCookAlgorithmVersion = 2u;
inline constexpr const char* kCloudCookPrecision = "ieee754-binary32-nearest-even-no-contract/1";
inline constexpr const char* kCloudCookSeedRouting = "fnv1a64-le-u64-corner/1";
inline constexpr std::uint32_t kCloudHeightProfileCount = 3u;

// /3 defines explicit binary32 precision and full seed/channel hash.
// /2 retains its platform-dependent arithmetic and original seed semantics.
// /1 is rejected. Explicit versioned recipe and recook, never silent conversion.
// See docs/cloud-cook-contract-v3.md.
struct CloudCookDesc {
    std::uint32_t algorithmVersion = 0u;
    std::uint32_t seed = 0u;
    std::uint32_t weatherDimension = 0u;
    std::uint32_t shapeDimension = 0u;
    std::uint32_t detailDimension = 0u;
    std::uint32_t weatherFrequency = 0u;
    std::uint32_t shapeFrequency = 0u;
    std::uint32_t detailFrequency = 0u;
    std::uint32_t octaves = 0u;
    std::uint64_t maxOutputBytes = 0u;
    std::uint64_t maxNoiseEvaluations = 0u;
    std::array<core::Vec4, kCloudHeightProfileCount> heightProfiles{};
};

struct CloudAssetResult {
    bool success = true;
    const char* diagnostic = "";

    static CloudAssetResult failure(const char* id) noexcept { return {false, id}; }
};

// Canonical linear float4 buffer: heightProfiles[3], weather[W*W],
// shape[S*S*S], detail[D*D*D]. Weather xyz = coverage/type/density; noise x =
// density noise. Reserved lanes are zero. Explicit repeat linear filtering.
struct CloudAsset {
    CloudCookDesc source;
    core::pmr::Vector<core::Vec4> cells;
};

class ARP_API CloudAssetCooker final {
public:
    static CloudAssetResult parse(
        core::StringView yaml, std::uint64_t maxSourceBytes, CloudCookDesc& output);
    static CloudAssetResult serialize(const CloudCookDesc& desc, core::pmr::String& output);
    static CloudAssetResult plan(const CloudCookDesc& desc, std::uint32_t& cells) noexcept;
    static CloudAssetResult cook(const CloudCookDesc& desc, CloudAsset& output);
    static CloudAssetResult validate(const CloudAsset& asset) noexcept;
    static core::Vec4 sampleWeather(const CloudAsset& asset, core::Vec2 uv) noexcept;
    static float sampleShape(const CloudAsset& asset, core::Vec3 uvw) noexcept;
    static float sampleDetail(const CloudAsset& asset, core::Vec3 uvw) noexcept;
};

} // namespace arp::assets::cloud
