#pragma once

#include "core/encoding/yaml_contract.h"
#include "tests/fixtures/static_glb_geometry.h"

namespace arp::tests {
// Shared metadata for cold package and acquired-source authoring contracts.
class VegetationAuthoringFixture final {
public:
    using Yaml = core::YamlContract;

    static Yaml::Node document()
    {
        return Yaml::parseSingleDocument(
            "type: VegetationCatalogAuthoring\ncontractVersion: vegetation.catalog.authoring/v1\n"
            "resourceId: 3ea4c197-d012-4e64-8f5f-6a415ec2c109\nalias: vegetation/authored\n"
            "inputContract: static-glb-geometry/v1\nmaxSourceBytes: 1048576\n"
            "maxTotalSourceBytes: 2097152\nmaxImportBytes: 4194304\nmaxDecodedBytes: 204\n"
            "maxTemplates: 4\nmaxVertices: 32\nmaxIndices: 64\nmaxPrimitives: 4\n"
            "maxJsonDepth: 16\nmaxJsonValues: 256\ndependencyPackage: dependencies\n"
            "packageBudget:\n  maxResources: 16\n  maxFiles: 64\n  maxFileBytes: 1048576\n"
            "  maxTotalFileBytes: 4194304\n  maxDecodedBytes: 1048576\nassets:\n"
            "  - assetId: grass-a\n    source: plant.glb\n    role: ground_cover\n"
            "    category: ground_cover\n    heightClass: low\n    templateCategory: foliage_grass\n"
            "    templateId: 1\n    variantIndex: 0\n    lodIndex: 0\n    metersPerUnit: 2.0\n"
            "    baseColor: [0.125, 0.25, 0.5, 1.0]\n    wind: [0.25, 0.5, 0.75]\n"
            "    materials:\n      - slot: 0\n        resourceId: 33c5dcdf-4f06-482a-94eb-26baf7c937fb\n");
    }
};
} // namespace arp::tests
