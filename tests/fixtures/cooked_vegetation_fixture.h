#pragma once

#include "resource/vegetation_catalog_resource.h"

namespace arp::tests {

// One explicit triangle, one material slot and non-default vertex attributes.
// Shared by cooked-codec and routed IO tests; source is provenance, not a file.
class CookedVegetationFixture final {
public:
    static constexpr const char* catalogId = "3ea4c197-d012-4e64-8f5f-6a415ec2c109";
    static constexpr const char* materialId = "33c5dcdf-4f06-482a-94eb-26baf7c937fb";
    static constexpr std::size_t geometryBytes = 204;

    static resource::VegetationCatalogResourceData catalog()
    {
        resource::VegetationCatalogResourceData data;
        data.templateConfig.hasEnvironmentAssets = true;
        data.templateConfig.grassCardCount = 1;
        resource::VegetationCatalogAssetEntry asset;
        asset.id = "grass-a";
        asset.source = "explicit-provenance-without-host-file.glb";
        asset.role = "ground_cover";
        asset.category = "ground_cover";
        asset.heightClass = "low";
        asset.templateCategory = vegetation::TemplateCategory::FoliageGrass;
        asset.templateId = 1;
        asset.materials.push_back({7, materialId});
        data.assets.push_back(std::move(asset));
        vegetation::VegetationTemplate mesh;
        mesh.id = 1;
        mesh.category = vegetation::TemplateCategory::FoliageGrass;
        mesh.vertices.resize(3);
        mesh.vertices[0].position[0] = 1.25f;
        mesh.vertices[1].position[1] = 2.5f;
        mesh.vertices[2].position[2] = -0.75f;
        for (auto& vertex : mesh.vertices) {
            vertex.normal[1] = 1;
            vertex.tangent[0] = vertex.tangent[3] = 1;
            vertex.primaryBend = 0.25f;
            vertex.flutter = 0.5f;
            vertex.stiffness = 0.75f;
        }
        mesh.indices = {0, 1, 2};
        mesh.sections.push_back({0, 3, 7});
        mesh.materialIndex = 7;
        mesh.baseColor[0] = 0.125f;
        data.templates.push_back(std::move(mesh));
        return data;
    }
};

} // namespace arp::tests
