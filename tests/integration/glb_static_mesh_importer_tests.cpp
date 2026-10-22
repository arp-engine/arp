#include "assets/glb_importer_common.h"
#include "assets/glb_static_mesh_importer.h"
#include "core/encoding/base64.h"

#include <cassert>
#include <cmath>
#include <filesystem>

namespace {

std::filesystem::path findProjectRoot()
{
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "artifacts" / "project-lane" / "assets")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}

bool unitLength3(const float* value)
{
    const auto length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    return std::fabs(length - 1.0f) < 0.001f;
}

} // namespace

int main()
{
    const char* materialJson = R"json({
        "asset": { "version": "2.0" },
        "materials": [
            {
                "name": "MaskedLeaves",
                "alphaMode": "MASK",
                "alphaCutoff": 0.37,
                "doubleSided": true,
                "emissiveFactor": [0.1, 0.2, 0.3],
                "normalTexture": { "index": 1 },
                "occlusionTexture": { "index": 4, "strength": 0.62 },
                "emissiveTexture": { "index": 3 },
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.7, 0.8, 0.9, 0.6],
                    "baseColorTexture": { "index": 0 },
                    "metallicRoughnessTexture": { "index": 2 },
                    "metallicFactor": 0.25,
                    "roughnessFactor": 0.75
                },
                "extensions": {
                    "KHR_materials_clearcoat": {
                        "clearcoatTexture": { "index": 5 },
                        "clearcoatRoughnessTexture": { "index": 6 },
                        "clearcoatNormalTexture": { "index": 7 }
                    },
                    "KHR_materials_specular": {
                        "specularTexture": { "index": 8 },
                        "specularColorTexture": { "index": 9 }
                    },
                    "KHR_materials_sheen": {
                        "sheenColorTexture": { "index": 10 },
                        "sheenRoughnessTexture": { "index": 11 }
                    },
                    "KHR_materials_transmission": {
                        "transmissionTexture": { "index": 12 }
                    },
                    "KHR_materials_volume": {
                        "thicknessTexture": { "index": 13 }
                    },
                    "KHR_materials_iridescence": {
                        "iridescenceTexture": { "index": 14 },
                        "iridescenceThicknessTexture": { "index": 15 }
                    }
                }
            }
        ]
    })json";
    arp::assets::detail::JsonValue rootJson;
    assert(arp::assets::detail::parseJson(materialJson, rootJson));
    const auto materialAssets =
        arp::assets::detail::parseImportedGlbMaterialAssets(rootJson, "", {});
    assert(materialAssets.materials.size() == 1);
    assert(materialAssets.materials[0].name == "MaskedLeaves");
    assert(materialAssets.materials[0].alphaMode == arp::assets::ImportedAlphaMode::Mask);
    assert(materialAssets.materials[0].alphaCutoff == 0.37f);
    assert(materialAssets.materials[0].doubleSided);
    assert(materialAssets.materials[0].baseColorTexture == 0);
    assert(materialAssets.materials[0].normalTexture == 1);
    assert(materialAssets.materials[0].metallicRoughnessTexture == 2);
    assert(materialAssets.materials[0].emissiveTexture == 3);
    assert(materialAssets.materials[0].occlusionTexture == 4);
    assert(materialAssets.materials[0].clearcoatTexture == 5);
    assert(materialAssets.materials[0].clearcoatRoughnessTexture == 6);
    assert(materialAssets.materials[0].clearcoatNormalTexture == 7);
    assert(materialAssets.materials[0].specularTexture == 8);
    assert(materialAssets.materials[0].specularColorTexture == 9);
    assert(materialAssets.materials[0].sheenColorTexture == 10);
    assert(materialAssets.materials[0].sheenRoughnessTexture == 11);
    assert(materialAssets.materials[0].transmissionTexture == 12);
    assert(materialAssets.materials[0].thicknessTexture == 13);
    assert(materialAssets.materials[0].iridescenceTexture == 14);
    assert(materialAssets.materials[0].iridescenceThicknessTexture == 15);
    assert(materialAssets.materials[0].occlusionStrength == 0.62f);

    const auto parseImageSource = [](const char* json) {
        arp::assets::detail::JsonValue imageRoot;
        assert(arp::assets::detail::parseJson(json, imageRoot));
        return arp::assets::detail::parseImportedGlbMaterialAssets(imageRoot, "", {});
    };
    const auto dataImage =
        parseImageSource(R"({"images":[{"uri":"data:image/png;base64,AQIDBA=="}]})");
    assert(dataImage.success && dataImage.images.size() == 1u);
    assert(dataImage.images[0].mimeType == "image/png");
    assert((dataImage.images[0].encodedBytes == arp::core::ByteBuffer{1u, 2u, 3u, 4u}));
    const auto badData = parseImageSource(R"({"images":[{"uri":"data:image/png;base64,AQ?="}]})");
    assert(!badData.success && badData.diagnostics == "ASSET-GLB-IMAGE-DATA-URI-INVALID");
    const auto mimeConflict = parseImageSource(
        R"({"images":[{"uri":"data:image/png;base64,AQID","mimeType":"image/jpeg"}]})");
    assert(!mimeConflict.success && mimeConflict.diagnostics == "ASSET-GLB-IMAGE-MIME-CONFLICT");
    const auto externalImage = parseImageSource(R"({"images":[{"uri":"leaves.png"}]})");
    assert(
        !externalImage.success && externalImage.diagnostics == "ASSET-GLB-IMAGE-URI-UNSUPPORTED");
    const auto ambiguousImage =
        parseImageSource(R"({"images":[{"uri":"data:image/png;base64,AQID","bufferView":0}]})");
    assert(
        !ambiguousImage.success && ambiguousImage.diagnostics == "ASSET-GLB-IMAGE-SOURCE-INVALID");
    const auto missingImage = parseImageSource(R"({"images":[{}]})");
    assert(!missingImage.success && missingImage.diagnostics == "ASSET-GLB-IMAGE-SOURCE-INVALID");

    const auto root = findProjectRoot();
    const auto imported = arp::assets::importStaticMeshFromGlbFile(
        root / "artifacts/project-lane/assets/khronos/Box/Box.glb");

    assert(imported.success);
    assert(imported.mesh.validForIndexedDraw());
    assert(imported.mesh.vertices.size() == 24);
    assert(imported.mesh.indices.size() == 36);
    assert(imported.mesh.submeshes.size() == 1);
    assert(imported.mesh.submeshes[0].materialSlot == 0);
    assert(imported.materials.size() == 1);
    assert(imported.materials[0].name == "Red");
    assert(imported.materials[0].baseColorFactor[0] > 0.79f);
    assert(imported.materials[0].baseColorFactor[1] == 0.0f);
    assert(imported.materials[0].baseColorTexture == arp::assets::invalidImportedTextureIndex);
    assert(imported.mesh.vertices[0].normal[0] != 0.0f
        || imported.mesh.vertices[0].normal[1] != 0.0f
        || imported.mesh.vertices[0].normal[2] != 0.0f);
    assert(unitLength3(imported.mesh.vertices[0].tangent));
    assert(imported.mesh.vertices[0].tangent[3] == 1.0f);

    const auto lantern = arp::assets::importStaticMeshFromGlbFile(
        root / "artifacts/project-lane/assets/khronos/Lantern/Lantern.glb");
    assert(lantern.success);
    assert(lantern.mesh.validForIndexedDraw());
    assert(lantern.mesh.vertices.size() == 4145);
    assert(lantern.mesh.indices.size() == 16182);
    assert(lantern.mesh.submeshes.size() == 3);
    assert(lantern.mesh.submeshes[0].materialSlot == 0);
    assert(lantern.mesh.submeshes[1].materialSlot == 0);
    assert(lantern.mesh.submeshes[2].materialSlot == 0);
    assert(lantern.materials.size() == 1);
    assert(lantern.textures.size() == 4);
    assert(lantern.images.size() == 4);
    assert(lantern.materials[0].baseColorTexture == 0);

    const auto unsupported = arp::assets::importStaticMeshFromGlbFile(
        root / "artifacts/project-lane/assets/khronos/Fox/Fox.glb");
    assert(!unsupported.success);
    assert(!unsupported.diagnostics.empty());

    return 0;
}
