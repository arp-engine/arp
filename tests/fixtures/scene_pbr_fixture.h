#pragma once
#include "renderer/pipeline/scene_pbr_producer.h"
#include "tests/fixtures/scene_opaque_fixture.h"

namespace arp::tests {
// Unit contract only. The representative terrain/vegetation scene is separate.
class ScenePbrFixture final {
public:
    SceneOpaqueFixture opaque;
    renderer::ScenePbrGeometryBudget budget{
        1u, true, 2u, 12u, 4u, 65536u, core::makeShared<core::CpuMemoryQuota>(65536u)};
    static constexpr std::array<const char*, 5u> textureIds{"00000000-0000-4000-8000-000000000011",
        "00000000-0000-4000-8000-000000000012",
        "00000000-0000-4000-8000-000000000013",
        "00000000-0000-4000-8000-000000000014",
        "00000000-0000-4000-8000-000000000015"};

    ScenePbrFixture()
    {
        auto& m = *opaque.material;
        m.surfaceModel = opaque.shader.materialInterfaceContract =
            resource::kPbrMetallicRoughnessContract;
        m.alphaMode = "OPAQUE";
        m.textureBindingCount = 5;
        m.baseColorTextureId = textureIds[0];
        m.normalTextureId = textureIds[1];
        m.metallicRoughnessTextureId = textureIds[2];
        m.occlusionTextureId = textureIds[3];
        m.emissiveTextureId = textureIds[4];
        for (auto& v : opaque.mesh->vertices) {
            const std::array normal{0.f, 0.f, 1.f};
            const std::array tangent{1.f, 0.f, 0.f, 1.f};
            std::copy(normal.begin(), normal.end(), v.normal);
            std::copy(tangent.begin(), tangent.end(), v.tangent);
        }
    }

    const char* create(std::shared_ptr<const renderer::ScenePbrGeometry>& output) const
    {
        const std::array bindings{opaque.binding()};
        return renderer::ScenePbrGeometry::create(
            opaque.queue(), bindings, opaque.shader, opaque.shaderId, budget, output);
    }
};
} // namespace arp::tests
