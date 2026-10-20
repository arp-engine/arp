#pragma once
#include "3d/bvh.h"
#include "core/ibl_bake.h"
#include "core/ray_geometry.h"
#include "renderer/gi/ddgi_renderer.h"

namespace arp::renderer {
// StaticSceneRadiance/1: initialization/offline only, real textured triangle
// occlusion, direct sun + one diffuse bounce. No dynamic or DDGI-history capture.
class StaticSceneRadiance final {
public:
    StaticSceneRadiance(core::ArrayView<DdgiSceneBvhTriangle> triangles,
        core::ArrayView<std::uint32_t> materials,
        core::Vec3 sunDirection,
        core::Vec3 sunRadiance,
        core::Vec3 skyRadiance)
        : triangles_(triangles), materials_(materials),
          sunDirection_(core::normalize(sunDirection)), sunRadiance_(sunRadiance),
          skyRadiance_(skyRadiance)
    {
        if (triangles.empty() || triangles.size() > 1000000u
            || !DdgiSceneMaterials::valid(materials))
            throw std::runtime_error("REFLECTION-STATIC-SCENE-INVALID");
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            const auto& t = triangles[i];
            if ((t.materialIdx & 0xc0000000u) != kDdgiSceneMaterialFlag
                || (t.materialIdx & 0x3fffffffu) >= materials[0])
                throw std::runtime_error("REFLECTION-STATIC-MATERIAL-UNSUPPORTED");
            core::Vec3 lo{std::min({t.v0[0], t.v1[0], t.v2[0]}),
                std::min({t.v0[1], t.v1[1], t.v2[1]}),
                std::min({t.v0[2], t.v1[2], t.v2[2]})};
            core::Vec3 hi{std::max({t.v0[0], t.v1[0], t.v2[0]}),
                std::max({t.v0[1], t.v1[1], t.v2[1]}),
                std::max({t.v0[2], t.v1[2], t.v2[2]})};
            bvh_.insert({lo, hi}, static_cast<std::uint32_t>(i), 0u);
        }
        bvh_.build();
    }

    core::Vec3 sample(core::Vec3 origin, core::Vec3 direction) const
    {
        Query hit(*this, origin, direction);
        bvh_.query(hit, hit);
        if (!hit.found)
            return skyRadiance_;
        auto radiance = direct(hit);
        const auto n = hit.normal;
        const auto tangent = core::normalize(
            core::cross(std::abs(n.y) < .99f ? core::Vec3{0, 1, 0} : core::Vec3{1, 0, 0}, n));
        const auto bitangent = core::cross(n, tangent);
        core::Vec3 indirect{};
        // Fixed cosine-weighted sample budget. Deterministic bake, no per-frame work.
        for (unsigned i = 0; i < 16; ++i) {
            const float u = (i + .5f) / 16.f, phi = 6.28318530718f * float(i) * .61803398875f;
            const float r = std::sqrt(u), z = std::sqrt(1 - u);
            const auto d = core::scale(tangent, r * std::cos(phi))
                + core::scale(bitangent, r * std::sin(phi)) + core::scale(n, z);
            Query bounce(*this, hit.position + core::scale(n, .002f), d);
            bvh_.query(bounce, bounce);
            const auto light = bounce.found ? direct(bounce) : skyRadiance_;
            indirect = indirect + core::scale(light, 1.f / 16.f);
        }
        return radiance
            + core::Vec3{
                hit.color[0] * indirect.x, hit.color[1] * indirect.y, hit.color[2] * indirect.z};
    }

private:
    struct Query final : scene3d::BvhQuery, scene3d::BvhQuerySink {
        const StaticSceneRadiance& owner;
        core::Vec3 origin, direction;
        float distance = 1000.f;
        bool found = false;
        core::Vec3 position{}, normal{};
        std::array<float, 4> color{};
        core::Vec3 emissive{};

        Query(const StaticSceneRadiance& o, core::Vec3 p, core::Vec3 d)
            : owner(o), origin(p), direction(d)
        {}

        bool intersectsAabb(const scene3d::Aabb& b) const override
        {
            return core::intersectRayBox(origin, direction, b.min, b.max, distance);
        }

        void accept(const scene3d::BvhLeaf& leaf) override
        {
            const auto& t = owner.triangles_[leaf.handle];
            const core::Vec3 a{t.v0[0], t.v0[1], t.v0[2]}, b{t.v1[0], t.v1[1], t.v1[2]},
                c{t.v2[0], t.v2[1], t.v2[2]};
            core::RayTriangleHit h;
            if (!core::intersectRayTriangle(origin, direction, a, b, c, .001f, distance, h))
                return;
            const float w = 1.f - h.u - h.v;
            const auto sampled = DdgiSceneMaterials::sample(owner.materials_,
                t.materialIdx,
                t.uv0[0] * w + t.uv1[0] * h.u + t.uv2[0] * h.v,
                t.uv0[1] * w + t.uv1[1] * h.u + t.uv2[1] * h.v);
            if (!DdgiSceneMaterials::visible(owner.materials_, t.materialIdx, sampled[3]))
                return;
            found = true;
            distance = h.distance;
            position = origin + core::scale(direction, distance);
            color = sampled;
            normal = core::normalize(core::cross(b - a, c - a));
            if (core::dot(normal, direction) > 0)
                normal = core::scale(normal, -1.f);
            emissive = {t.emissiveRadiance[0], t.emissiveRadiance[1], t.emissiveRadiance[2]};
        }
    };

    core::Vec3 direct(const Query& hit) const
    {
        const float cosine = std::max(core::dot(hit.normal, sunDirection_), 0.f);
        if (cosine <= 0.f)
            return hit.emissive;
        Query shadow(*this, hit.position + core::scale(hit.normal, .002f), sunDirection_);
        bvh_.query(shadow, shadow);
        if (shadow.found)
            return hit.emissive;
        return hit.emissive
            + core::Vec3{hit.color[0] * sunRadiance_.x * cosine,
                hit.color[1] * sunRadiance_.y * cosine,
                hit.color[2] * sunRadiance_.z * cosine};
    }

    core::ArrayView<DdgiSceneBvhTriangle> triangles_;
    core::ArrayView<std::uint32_t> materials_;
    scene3d::Bvh bvh_;
    core::Vec3 sunDirection_, sunRadiance_, skyRadiance_;
};
} // namespace arp::renderer
