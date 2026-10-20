#pragma once

#include "assets/asset_pipeline.h"
#include "core/array_view.h"
#include "core/cpu_allocator.h"
#include "renderer/gi/ddgi_renderer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace arp::renderer {

inline void appendDdgiMeshTriangles(const assets::MeshAsset& mesh,
    const std::array<float, 3>& translation,
    std::uint32_t material,
    std::uint64_t objectId,
    core::pmr::Vector<DdgiSceneBvhTriangle>& output,
    arp::core::ArrayView<std::uint32_t> materialSlots = {})
{
    if (!mesh.validForIndexedDraw() || objectId > 0xffffffffull) {
        throw std::runtime_error("DDGI-GEOMETRY-SOURCE-INVALID");
    }
    for (std::size_t index = 0; index + 2u < mesh.indices.size(); index += 3u) {
        DdgiSceneBvhTriangle triangle{};
        const auto& a = mesh.vertices[mesh.indices[index]];
        const auto& b = mesh.vertices[mesh.indices[index + 1u]];
        const auto& c = mesh.vertices[mesh.indices[index + 2u]];
        for (std::size_t axis = 0; axis < 3u; ++axis) {
            triangle.v0[axis] = a.position[axis] + translation[axis];
            triangle.v1[axis] = b.position[axis] + translation[axis];
            triangle.v2[axis] = c.position[axis] + translation[axis];
        }
        std::copy_n(a.uv, 2u, triangle.uv0);
        std::copy_n(b.uv, 2u, triangle.uv1);
        std::copy_n(c.uv, 2u, triangle.uv2);
        triangle.materialIdx = material;
        if (!materialSlots.empty()) {
            bool found = false;
            for (const auto& section : mesh.submeshes) {
                if (index < section.firstIndex || index >= section.firstIndex + section.indexCount)
                    continue;
                if (found || section.materialSlot >= materialSlots.size())
                    throw std::runtime_error("DDGI-SECTION-MATERIAL-INVALID");
                triangle.materialIdx = materialSlots[section.materialSlot];
                found = true;
            }
            if (!found)
                throw std::runtime_error("DDGI-SECTION-MATERIAL-MISSING");
        }
        triangle.stableId = (objectId << 32u) | (index / 3u);
        output.push_back(triangle);
    }
}

struct DdgiStaticMeshSource {
    std::string_view resourceId;
    std::uint64_t resourceGeneration = 0u;
    std::uint64_t objectId = 0u;
    std::array<float, 3> translation{};
    std::uint32_t material = 0u;
    const assets::MeshAsset* mesh = nullptr;
    arp::core::ArrayView<std::uint32_t> materialSlots{};
};

struct DdgiStaticGeometrySnapshot {
    core::pmr::Vector<DdgiSceneBvhTriangle> triangles{&core::defaultCpuMemoryResource()};
    std::uint32_t revision = 0u;
};

// Callers admit only immutable resources. Resource generations and transforms
// invalidate extraction; submitted snapshots outlive source removal/reload.
class DdgiSceneGeometryCache {
public:
    std::shared_ptr<const DdgiStaticGeometrySnapshot> update(
        const core::pmr::Vector<DdgiStaticMeshSource>& sources)
    {
        bool unchanged = snapshot_ && sources.size() == keys_.size();
        for (std::size_t i = 0u; unchanged && i < sources.size(); ++i) {
            const auto& source = sources[i];
            const auto& key = keys_[i];
            unchanged = source.resourceId == std::string_view(key.resourceId)
                && source.resourceGeneration == key.resourceGeneration
                && source.objectId == key.objectId && source.translation == key.translation
                && source.material == key.material
                && std::equal(source.materialSlots.begin(),
                    source.materialSlots.end(),
                    key.materialSlots.begin(),
                    key.materialSlots.end());
        }
        if (unchanged)
            return snapshot_;
        if (revision_ == std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("DDGI-STATIC-REVISION-EXHAUSTED");
        }
        auto candidate = core::makeShared<DdgiStaticGeometrySnapshot>();
        core::pmr::Vector<Key> keys(&core::defaultCpuMemoryResource());
        std::size_t triangleCount = 0u;
        for (const auto& source : sources) {
            if (!source.mesh)
                throw std::runtime_error("DDGI-STATIC-MESH-MISSING");
            triangleCount += source.mesh->indices.size() / 3u;
            if (triangleCount > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("DDGI-STATIC-PRIMITIVE-CAPACITY-EXCEEDED");
        }
        candidate->triangles.reserve(triangleCount);
        for (const auto& source : sources) {
            if (!source.mesh || source.resourceId.empty() || source.resourceGeneration == 0u) {
                throw std::runtime_error("DDGI-STATIC-IDENTITY-INVALID");
            }
            appendDdgiMeshTriangles(*source.mesh,
                source.translation,
                source.material,
                source.objectId,
                candidate->triangles,
                source.materialSlots);
            keys.push_back({core::pmr::String(source.resourceId, &core::defaultCpuMemoryResource()),
                source.resourceGeneration,
                source.objectId,
                source.translation,
                source.material,
                core::pmr::Vector<std::uint32_t>(source.materialSlots.begin(),
                    source.materialSlots.end(),
                    &core::defaultCpuMemoryResource())});
        }
        std::sort(candidate->triangles.begin(),
            candidate->triangles.end(),
            [](const auto& a, const auto& b) {
                return a.stableId < b.stableId;
            });
        for (std::size_t i = 1u; i < candidate->triangles.size(); ++i) {
            if (candidate->triangles[i - 1u].stableId == candidate->triangles[i].stableId) {
                throw std::runtime_error("DDGI-STATIC-IDENTITY-DUPLICATE");
            }
        }
        candidate->revision = ++revision_;
        keys_ = std::move(keys);
        snapshot_ = std::move(candidate);
        return snapshot_;
    }

private:
    struct Key {
        core::pmr::String resourceId;
        std::uint64_t resourceGeneration;
        std::uint64_t objectId;
        std::array<float, 3> translation;
        std::uint32_t material;
        core::pmr::Vector<std::uint32_t> materialSlots;
    };

    core::pmr::Vector<Key> keys_{&core::defaultCpuMemoryResource()};
    std::shared_ptr<const DdgiStaticGeometrySnapshot> snapshot_;
    std::uint32_t revision_ = 0u;
};

} // namespace arp::renderer
