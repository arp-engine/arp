#pragma once
#include "renderer/pipeline/scene_opaque_producer.h"

namespace arp::tests {
class SceneOpaqueFixture final {
public:
    static constexpr const char* shaderId = "00000000-0000-4000-8000-000000000008";
    std::shared_ptr<assets::MeshAsset> mesh =
        core::makeShared<assets::MeshAsset>(assets::MeshAsset::debugTriangle());
    std::shared_ptr<resource::MaterialResource> material =
        core::makeShared<resource::MaterialResource>();
    resource::ShaderPackageResource shader;
    std::array<std::shared_ptr<const resource::MaterialResource>, 1u> materials;
    scene3d::Scene3DRenderItem item;
    renderer::BasicWorldMeshProfile profile{1u,
        false,
        renderer::BasicWorldMeshIndexPolicy::Uint16Exact,
        3u,
        3u,
        1u,
        1u,
        1u,
        3u * sizeof(assets::MeshVertex),
        6u};
    renderer::SceneOpaqueGeometryBudget budget{
        1u, true, 2u, 6u, 65536u, core::makeShared<core::CpuMemoryQuota>(65536u)};

    SceneOpaqueFixture()
    {
        material->materialContractVersion = shader.materialInterfaceVersion = 1u;
        material->surfaceModel = shader.materialInterfaceContract =
            renderer::kBasicForwardMaterialContract;
        material->shaderPackageId = shaderId;
        material->metallicFactor = 0.f;
        material->roughnessFactor = 1.f;
        material->alphaMode = "Opaque";
        material->baseColorR = .5f;
        material->baseColorG = .25f;
        material->baseColorB = .125f;
        materials[0] = material;
        item.objectId = scene::GameObjectId::fromValue(1u);
        item.mesh = {1u};
        item.material = {1u};
    }

    scene3d::Scene3DRenderQueue queue() const
    {
        scene3d::Scene3DRenderQueue q;
        q.addItem(item);
        return q;
    }

    renderer::SceneOpaqueMeshBinding binding() const { return {0u, mesh, profile, materials}; }

    const char* create(std::shared_ptr<const renderer::SceneOpaqueGeometry>& output) const
    {
        const std::array bindings{binding()};
        return renderer::SceneOpaqueGeometry::create(
            queue(), bindings, shader, shaderId, budget, output);
    }
};
} // namespace arp::tests
