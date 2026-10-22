#pragma once

#include "assets/asset_pipeline.h"
#include "core/math/triangle_rect_clipper.h"

namespace arp::assets {

class MeshRectClipper final {
public:
    // Clip rendered heightfield triangles in world XZ. Collision data is separate.
    static MeshAsset subtractXZ(const MeshAsset& source, const core::Rect2<float>& rect)
    {
        if (!source.validForIndexedDraw())
            throw std::invalid_argument("MESH-RECT-CLIP-SOURCE-INVALID");
        auto* memory = &core::defaultCpuMemoryResource();
        MeshAsset result{core::pmr::Vector<MeshVertex>(memory),
            core::pmr::Vector<std::uint32_t>(memory),
            core::pmr::Vector<Submesh>(memory)};
        result.vertices = source.vertices;
        result.indices.reserve(source.indices.size());
        for (const auto& section : source.submeshes) {
            if (section.indexCount % 3u != 0u)
                throw std::invalid_argument("MESH-RECT-CLIP-TOPOLOGY-INVALID");
            const auto start = static_cast<std::uint32_t>(result.indices.size());
            for (std::uint32_t index = section.firstIndex;
                index < section.firstIndex + section.indexCount;
                index += 3u) {
                const std::array<std::uint32_t, 3> ids{
                    source.indices[index], source.indices[index + 1], source.indices[index + 2]};
                std::array<core::TriangleRectClipper::Point, 3> points{};
                for (std::size_t i = 0; i < 3; ++i) {
                    const auto& v = source.vertices[ids[i]];
                    points[i] = {v.position[0], v.position[2]};
                }
                core::TriangleRectClipper::subtract(points, rect, [&](const auto& triangle) {
                    for (const auto& weights : triangle) {
                        std::uint32_t vertexIndex = UINT32_MAX;
                        for (std::size_t i = 0; i < 3; ++i) {
                            if (weights[i] == 1.0f)
                                vertexIndex = ids[i];
                        }
                        if (vertexIndex == UINT32_MAX) {
                            MeshVertex vertex{};
                            std::fill(std::begin(vertex.normal), std::end(vertex.normal), 0.0f);
                            std::fill(std::begin(vertex.tangent), std::end(vertex.tangent), 0.0f);
                            for (std::size_t i = 0; i < 3; ++i) {
                                const auto& v = source.vertices[ids[i]];
                                for (std::size_t j = 0; j < 3; ++j) {
                                    vertex.position[j] += weights[i] * v.position[j];
                                    vertex.normal[j] += weights[i] * v.normal[j];
                                }
                                for (std::size_t j = 0; j < 2; ++j)
                                    vertex.uv[j] += weights[i] * v.uv[j];
                                for (std::size_t j = 0; j < 4; ++j)
                                    vertex.tangent[j] += weights[i] * v.tangent[j];
                            }
                            vertexIndex = static_cast<std::uint32_t>(result.vertices.size());
                            result.vertices.push_back(vertex);
                        }
                        result.indices.push_back(vertexIndex);
                    }
                });
            }
            const auto count = static_cast<std::uint32_t>(result.indices.size()) - start;
            if (count != 0u)
                result.submeshes.push_back({start, count, section.materialSlot});
        }
        return result;
    }
};

} // namespace arp::assets
