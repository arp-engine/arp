#pragma once

#include "resource/vegetation_catalog_resource.h"
#include "tests/fixtures/named_glb_source_fixture.h"

namespace arp::tests {
class VegetationCatalogSourceFixture final {
public:
    static constexpr const char* id = "00000000-0000-4000-8000-000000000085";
    static constexpr std::size_t sourceBudget = 65536, decodedBudget = 128 * 1024;
    core::pmr::String source, runtime;
    core::pmr::Vector<std::uint8_t> geometry;

    VegetationCatalogSourceFixture()
    {
        source = document(false);
        runtime = document(true);
        auto json = NamedGlbSourceFixture::document();
        NamedGlbSourceFixture::replace(json, "\"name\":\"leaf\\uD83C\\uDF3F\",", "");
        geometry = StaticGlbGeometryFixture::bytes(json);
    }

    static core::pmr::String document(bool runtime)
    {
        core::pmr::String text =
            "type: VegetationSourceCatalog\nfields:\n  contractVersion: 2\n"
            "  runtimeCatalog: runtime/catalog.yaml\n  status: Contract Complete\n";
        if (runtime)
            text += "  imported:\n    type: VegetationCatalogImportedInfo\n"
                    "    fields: {format: glb, generatedBy: source-contract-fixture}\n";
        text += "  validation:\n    type: VegetationCatalogValidationInfo\n"
                "    fields:\n      required: [bounds, scale, wind, slots, texture, mesh]\n";
        text += "  assets:\n";
        // Reversed family order, two assets sharing the exact same input path.
        for (const auto rock : {true, false}) {
            text += "    - type: VegetationCatalogAssetRecord\n      fields:\n        id: ";
            text += rock ? "rock-b\n" : "grass-a\n";
            text += "        source: ";
            text += runtime ? "models/plant.glb\n" : "originals/plant.glb\n";
            text += "        role: ground_cover\n        category: ground_cover\n"
                    "        heightClass: low\n        templateCategory: ";
            text += rock ? "rock_mossy_01\n" : "foliage_grass\n";
        }
        return text;
    }

    static resource::VegetationCatalogResourceLoadDesc descriptor(
        resource::ResourceFileOrigin origin)
    {
        resource::VegetationCatalogResourceLoadDesc desc;
        desc.catalogPath = "source.yaml";
        desc.resourceId = id;
        desc.alias = "vegetation/source";
        desc.status = "Contract Complete";
        desc.input = resource::VegetationCatalogInput::SourceCatalogV2;
        desc.sourceOrigin = origin;
        desc.sourceLimits.emplace();
        auto& limits = *desc.sourceLimits;
        limits.maxCatalogBytes = sourceBudget;
        limits.maxAssets = 4;
        limits.maxMetadataDepth = 32;
        limits.maxMetadataValues = 4096;
        limits.maxMetadataCollectionLength = 64;
        limits.maxDecodedBytes = decodedBudget;
        limits.maxTotalSourceBytes = 3 * sourceBudget;
        limits.maxImportBytes = 20 * 1024 * 1024;
        limits.mesh = {{sourceBudget, 12, 12, 4, false, 32, 2048},
            4 * 1024 * 1024,
            decodedBudget,
            16,
            16,
            assets::StaticMeshVertexColorPolicy::DiscardValidatedRgba};
        return desc;
    }

    resource::ResourceSourceSet sources(resource::ResourceFileOrigin origin) const
    {
        resource::ResourceSourceSet result;
        const auto add = [&](const char* path, const std::uint8_t* bytes, std::size_t count) {
            resource::ResourceSourceBuffer source;
            source.path = path;
            source.pathScope = origin.pathScope;
            source.mount = origin.mount;
            source.bytes.assign(bytes, bytes + count);
            assert(result.add(std::move(source)));
        };
        add("source.yaml", reinterpret_cast<const std::uint8_t*>(source.data()), source.size());
        add("runtime/catalog.yaml",
            reinterpret_cast<const std::uint8_t*>(runtime.data()),
            runtime.size());
        add("runtime/models/plant.glb", geometry.data(), geometry.size());
        return result;
    }
};
} // namespace arp::tests
