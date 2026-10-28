#include "assets/cloud/cloud_asset.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "resource/cloud_asset_cooked.h"
#include "resource/cloud_asset_resource.h"
#include "resource/resource_contract.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
using namespace arp;
using namespace assets::cloud;

class CookContract final {
    static bool same(const CloudAsset& a, const CloudAsset& b)
    {
        return a.cells.size() == b.cells.size()
            && std::memcmp(a.cells.data(), b.cells.data(), a.cells.size() * sizeof(core::Vec4))
            == 0;
    }

    static void rejected(core::StringView yaml, const char* diagnostic)
    {
        CloudCookDesc output;
        const auto result = CloudAssetCooker::parse(yaml, 8192u, output);
        assert(!result.success && std::string_view(result.diagnostic) == diagnostic
            && output.algorithmVersion == 0u);
    }

public:
    static void run()
    {
        CloudCookDesc desc;
        desc.algorithmVersion = kCloudCookAlgorithmVersion;
        desc.seed = 137u;
        desc.weatherDimension = desc.shapeDimension = 4u;
        desc.detailDimension = 2u;
        desc.weatherFrequency = desc.shapeFrequency = 4u;
        desc.detailFrequency = 6u;
        desc.octaves = 3u;
        desc.maxOutputBytes = 65536u;
        desc.maxNoiseEvaluations = 65536u;
        desc.heightProfiles.fill({0.f, .125f, .75f, 1.f});
        CloudAsset a, b;
        assert(CloudAssetCooker::cook(desc, a).success && CloudAssetCooker::validate(a).success);
        assert(CloudAssetCooker::cook(desc, b).success && same(a, b));
        for (unsigned channel = 0u; channel < 3u; ++channel) {
            float minimum = 1.f, maximum = 0.f;
            bool distinct = false;
            for (unsigned cell = 3u; cell < 19u; ++cell) {
                const auto& value = a.cells[cell];
                const std::array lanes{value.x, value.y, value.z};
                minimum = std::min(minimum, lanes[channel]);
                maximum = std::max(maximum, lanes[channel]);
                distinct |= lanes[channel] != lanes[(channel + 1u) % 3u];
            }
            assert(distinct && maximum - minimum > .01f);
        }
        // Previously the high 24 seed bits were invisible. Each probe must
        // alter the complete cooked field, rather than only source metadata.
        for (const auto mask : {1u, 256u, 65536u, 16777216u, UINT32_MAX}) {
            auto changed = desc;
            changed.seed ^= mask;
            assert(CloudAssetCooker::cook(changed, b).success && !same(a, b));
        }
        for (const auto uv : {core::Vec2{0.f, 0.f}, core::Vec2{-.25f, 1.375f}}) {
            const auto x = CloudAssetCooker::sampleWeather(a, uv);
            const auto y = CloudAssetCooker::sampleWeather(a, {uv.x + 1.f, uv.y - 1.f});
            assert(x.x == y.x && x.y == y.y && x.z == y.z);
        }
        using Y = core::YamlContract;
        core::pmr::String recipe;
        assert(CloudAssetCooker::serialize(desc, recipe).success);
        auto metadata = Y::parseSingleDocument(recipe);
        metadata["precision"] = "unknown";
        rejected(Y::serialize(metadata), "CLOUD-ASSET-PRECISION");
        metadata = Y::parseSingleDocument(recipe);
        metadata["seedRouting"] = "unknown";
        rejected(Y::serialize(metadata), "CLOUD-ASSET-SEED-ROUTING");
        metadata = Y::parseSingleDocument(recipe);
        metadata["algorithmVersion"] = 4u;
        rejected(Y::serialize(metadata), "CLOUD-ASSET-ALGORITHM-VERSION");
        metadata["algorithmVersion"] = kCloudLegacyCookAlgorithmVersion;
        rejected(Y::serialize(metadata), "CLOUD-ASSET-LEGACY-METADATA");
        metadata["algorithmVersion"] = 1u;
        rejected(Y::serialize(metadata), "CLOUD-ASSET-ALGORITHM-VERSION");

        auto legacy = desc;
        legacy.algorithmVersion = kCloudLegacyCookAlgorithmVersion;
        core::pmr::String legacyRecipe;
        assert(CloudAssetCooker::serialize(legacy, legacyRecipe).success);
        metadata = Y::parseSingleDocument(legacyRecipe);
        metadata["algorithmVersion"] = kCloudCookAlgorithmVersion;
        rejected(Y::serialize(metadata), "CLOUD-ASSET-PRECISION");
        metadata["precision"] = kCloudCookPrecision;
        rejected(Y::serialize(metadata), "CLOUD-ASSET-SEED-ROUTING");
        assert(CloudAssetCooker::cook(legacy, b).success && !same(a, b));

        const auto rounding = std::fegetround();
        assert(rounding == FE_TONEAREST && std::fesetround(FE_DOWNWARD) == 0);
        const auto invalidRounding = CloudAssetCooker::cook(desc, b);
        const auto retainedRounding = std::fegetround();
        assert(std::fesetround(rounding) == 0);
        assert(!invalidRounding.success
            && std::string_view(invalidRounding.diagnostic) == "CLOUD-ASSET-ROUNDING-MODE"
            && b.cells.empty() && retainedRounding == FE_DOWNWARD);

        core::pmr::String cooked;
        assert(resource::CloudAssetCookedCodec::encode(a, 65536u, cooked).success);
        assert(resource::CloudAssetCookedCodec::decode(cooked, 65536u, 65536u, b).success
            && same(a, b) && b.source.algorithmVersion == 3u);
        resource::CloudAssetResourceDesc load;
        load.reference = {resource::deriveResourceUuid("CloudPeriodicSimplex/3", "contract"),
            "CloudAssetResource",
            "cloud.v3",
            "Contract Complete",
            "Cloud v3"};
        load.cookedPath = "virtual/v3.cooked.yaml";
        load.maxDocumentBytes = load.maxPayloadBytes = 65536u;
        resource::ResourceSourceSet sources;
        resource::ResourceSourceBuffer source;
        source.path = load.cookedPath;
        source.role = "yaml-resource";
        source.bytes.assign(cooked.begin(), cooked.end());
        assert(sources.add(std::move(source)));
        resource::ResourceLoadContext context;
        context.sources = &sources;
        context.requirePreloadedSources = true;
        resource::ResourceManager manager;
        const auto loaded = manager.load<resource::CloudAssetResource>(context, load);
        assert(loaded.success);
        const auto handle = manager.find<resource::CloudAssetResource>(load.reference.id);
        assert(handle.ready() && same(a, *handle.get()->asset()));
        auto limited = desc;
        limited.maxNoiseEvaluations = 1u;
        assert(!CloudAssetCooker::cook(limited, b).success && b.cells.empty());
        // Pinned after independent MinGW/Clang complete-document comparison.
        // A changed numeric stream/schema requires an explicit version review.
        const auto golden = core::Sha256::hexDigest(cooked);
        assert(golden == "sha256:576d1431e459687188f6c97a80617953bcc78e8cd17549286235cbeb09d180d0");
        std::printf("CLOUD_COOK_V3_CONTRACT_PASS %s\n", golden.c_str());
    }
};
} // namespace

int main()
{
    CookContract::run();
}
