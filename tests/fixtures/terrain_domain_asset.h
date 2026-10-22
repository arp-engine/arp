#pragma once

#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_resource.h"

namespace arp::tests {

class TerrainDomainAssetFixture final {
public:
    static constexpr const char* resourceId = "b445d8f1-4ccc-42fd-8444-36b787f91037";
    static constexpr const char* catalogId = "3ea4c197-d012-4e64-8f5f-6a415ec2c109";

    static resource::TerrainAssetBuildDesc description()
    {
        resource::TerrainAssetBuildDesc desc;
        desc.resourceId = resourceId;
        desc.alias = "terrain/domain-smoke";
        desc.bounds = {-64.0f, 64.0f, -64.0f, 64.0f};
        desc.tileCountX = 2u;
        desc.tileCountZ = 2u;
        desc.tileWorldSize = 64.0f;
        desc.samplesPerEdge = 9u;
        desc.vegetationCatalogResourceId = catalogId;
        constexpr const char* names[] = {"grassMoss",
            "forestFloor",
            "wetMud",
            "rockyPath",
            "mossyRock",
            "dirtySnow",
            "snowOverlay",
            "wetnessOverlay"};
        for (std::size_t index = 0; index < desc.layers.size(); ++index) {
            auto& layer = desc.layers[index];
            layer.layerId = names[index];
            layer.semantic = names[index];
            layer.materialResourceId = "33c5dcdf-4f06-482a-94eb-26baf7c937fb";
            layer.page = static_cast<std::uint32_t>(index / 4u);
            layer.channel = static_cast<std::uint32_t>(index % 4u);
        }
        return desc;
    }

    static resource::ResourceReference reference()
    {
        resource::ResourceReference ref;
        ref.id = resourceId;
        ref.type = resource::TerrainAssetResource::resourceTypeName();
        ref.alias = "terrain/domain-smoke";
        ref.status = "Contract Complete";
        return ref;
    }
};

// Explicit empty catalog for CPU resource-contract smoke only. This does not
// represent an imported/native vegetation renderer or substitute GPU payloads.
class EmptyTerrainCatalogFixture final : public resource::VegetationCatalogResource {
public:
    EmptyTerrainCatalogFixture() : VegetationCatalogResource(description()) {}

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        data = core::makeShared<resource::VegetationCatalogResourceData>();
        return resource::ResourceBase::load(manager, context);
    }

private:
    static resource::VegetationCatalogResourceLoadDesc description()
    {
        resource::VegetationCatalogResourceLoadDesc desc;
        desc.resourceId = TerrainDomainAssetFixture::catalogId;
        desc.alias = "vegetation/explicit-empty-test-catalog";
        desc.status = "Contract Complete";
        return desc;
    }
};

} // namespace arp::tests
