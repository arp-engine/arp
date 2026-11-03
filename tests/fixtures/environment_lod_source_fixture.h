#pragma once

#include "resource/environment_asset_resources.h"
#include "tests/fixtures/named_glb_source_fixture.h"

namespace arp::tests {
class EnvironmentLodSourceFixture final {
public:
    static constexpr const char* plantId = "00000000-0000-4000-8000-000000000080";
    static constexpr const char* rockId = "00000000-0000-4000-8000-000000000081";
    static constexpr std::size_t sourceBudget = NamedGlbSourceFixture::sourceBudget;
    static constexpr std::size_t decodedBudget = 128 * 1024;
    core::pmr::Vector<resource::ResourceSourceBuffer> files;

    EnvironmentLodSourceFixture()
    {
        const char* paths[] = {"plants/close.glb",
            "plants/distant.glb",
            "plants/shared.glb",
            "rocks/close.glb",
            "rocks/distant.glb",
            "rocks/shared.glb"};
        for (unsigned i = 0; i < 6; ++i) {
            auto json = NamedGlbSourceFixture::document();
            if (i >= 3)
                NamedGlbSourceFixture::replace(json, ",\"COLOR_0\":3", "");
            if (i == 1 || i == 4)
                NamedGlbSourceFixture::replace(json, "\"scale\":[2,3,4]", "\"scale\":[1,1,1]");
            resource::ResourceSourceBuffer file;
            file.path = paths[i];
            file.bytes = StaticGlbGeometryFixture::bytes(json);
            files.push_back(std::move(file));
        }
    }

    resource::EnvironmentLodSourceDesc descriptor(
        resource::ResourceFileOrigin origin, bool plant, std::uint32_t resident = 1) const
    {
        resource::EnvironmentLodSourceDesc result;
        result.origin = origin;
        result.residentLevels = resident;
        result.variantNames.push_back("leaf\xf0\x9f\x8c\xbf");
        result.maxTotalSourceBytes = 3 * sourceBudget;
        result.maxDecodedBytes = decodedBudget;
        result.maxTotalImportBytes = 8 * 1024 * 1024;
        result.geometrySpace = assets::NamedVariantGeometrySpace::RootRelative;
        for (unsigned i = 0; i < 3; ++i) {
            const auto& file = files[(plant ? 0 : 3) + i];
            resource::EnvironmentLodSource level;
            level.path = file.path;
            level.byteCount = file.bytes.size();
            level.version.kind = core::io::FileVersionKind::ContentSha256;
            const auto digest = core::Sha256::digest(
                {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()});
            std::copy(digest.begin(), digest.end(), level.version.digest.begin());
            level.limits = NamedGlbSourceFixture::limits();
            level.limits.colorPolicy = plant ? assets::NamedVariantColorPolicy::PreserveRgbaWind
                                             : assets::NamedVariantColorPolicy::RejectColor0;
            if (i == 2)
                level.sharedLod = 0;
            result.levels.push_back(std::move(level));
        }
        return result;
    }

    resource::ResourceSourceSet sources(resource::ResourceFileOrigin origin) const
    {
        resource::ResourceSourceSet result;
        for (auto file : files) {
            file.pathScope = origin.pathScope;
            file.mount = origin.mount;
            assert(result.add(std::move(file)));
        }
        return result;
    }
};
} // namespace arp::tests
