#include "assets/asset_pipeline.h"

#include <cassert>
#include <string>

int main()
{
    const auto mesh = arp::assets::MeshAsset::debugTriangle();
    assert(mesh.validForIndexedDraw());
    assert(mesh.vertices.size() == 3);
    assert(mesh.indices.size() == 3);

    const auto material = arp::assets::importSceneMaterialFromJson(R"json(
    {
      "type": "SceneStylizedPbr",
      "name": "MeadowRock",
      "baseColorTexture": "textures/meadow_rock_base.ktx2",
      "normalTexture": "textures/meadow_rock_n.ktx2",
      "lightWrap": 0.55
    }
    )json");
    assert(material.success);
    assert(material.material.name == "MeadowRock");
    assert(material.material.parameters.lightWrap == 0.55f);

    const auto importedMesh = arp::assets::importStaticMeshFromGltfJson(R"json(
    {
      "asset": { "version": "2.0" },
      "meshes": [
        {
          "primitives": [
            {
              "attributes": {
                "POSITION": [0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0],
                "NORMAL": [0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0],
                "TEXCOORD_0": [0.0, 0.0, 1.0, 0.0, 0.0, 1.0]
              },
              "indices": [0, 1, 2],
              "material": 2
            }
          ]
        }
      ]
    }
    )json");
    assert(importedMesh.success);
    assert(importedMesh.mesh.submeshes[0].materialSlot == 2);

    arp::assets::RuntimeAssetPackageDesc package;
    package.name = "mobile_demo";
    package.meshes.push_back({"hero", "assets/meshes/hero.fbx", true, true, true});
    package.materials.push_back({"hero_body", "assets/materials/hero_body.material.json"});
    package.textures.push_back({"hero_albedo",
        "assets/textures/hero_albedo.png",
        true,
        true,
        true,
        arp::assets::MobileTextureCompression::Astc});
    package.shaderReferences.push_back({"stylized_scene",
        "shaders/stylized/scene.slang",
        "stylized_scene.spv",
        "stylized_scene.metal",
        "stylized_scene.reflection.yaml"});

    const auto packagePlan = arp::assets::buildRuntimeAssetPackagePlan(package);
    assert(packagePlan.valid);
    assert(packagePlan.runtimeReady);
    assert(packagePlan.entries.size() == 4);
    assert(packagePlan.textureCompression == arp::assets::MobileTextureCompression::Astc);

    const auto dependencies = arp::assets::buildDefaultAssetDependencyPlan();
    assert(dependencies.assimpBroadImportEnabled());
    assert(dependencies.dependencies[0].packageName == "assimp");
    assert(dependencies.supportsSourceFormat(arp::assets::SourceAssetFormat::Fbx));
    assert(dependencies.supportsSourceFormat(arp::assets::SourceAssetFormat::Obj));
    assert(dependencies.supportsSourceFormat(arp::assets::SourceAssetFormat::Gltf));
    for (const auto& dependency : dependencies.dependencies) {
        if (dependency.packageName == "assimp") {
            assert(!dependency.runtimeDependency);
        }
    }

    return 0;
}
