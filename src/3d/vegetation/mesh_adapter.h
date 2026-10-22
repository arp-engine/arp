#pragma once
#include "3d/vegetation/instance_layout.h"
#include "assets/asset_pipeline.h"

namespace arp::vegetation {
/// Convert a MeshAsset (MeshVertex) + optional wind weights to a vector of
/// InstanceVertex. Used for tree/rock variant meshes from GLBs.
inline arp::core::pmr::Vector<InstanceVertex> convertToInstanceVertices(
    const assets::MeshAsset& mesh,
    const float* windWeights4, // nullptr or float[vertices.size()*4]
    bool hasWindData)
{
    arp::core::pmr::Vector<InstanceVertex> out{&arp::core::defaultCpuMemoryResource()};
    out.reserve(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& mv = mesh.vertices[i];
        InstanceVertex iv;
        iv.position[0] = mv.position[0];
        iv.position[1] = mv.position[1];
        iv.position[2] = mv.position[2];
        iv.normal[0] = mv.normal[0];
        iv.normal[1] = mv.normal[1];
        iv.normal[2] = mv.normal[2];
        iv.uv[0] = mv.uv[0];
        iv.uv[1] = mv.uv[1];
        iv.tangent[0] = mv.tangent[0];
        iv.tangent[1] = mv.tangent[1];
        iv.tangent[2] = mv.tangent[2];
        iv.tangent[3] = mv.tangent[3];
        if (hasWindData && windWeights4) {
            iv.primaryBend = windWeights4[i * 4 + 0];
            iv.flutter = windWeights4[i * 4 + 1];
            iv.stiffness = windWeights4[i * 4 + 2];
        } else {
            iv.primaryBend = 0.0f;
            iv.flutter = 0.0f;
            iv.stiffness = 1.0f; // rigid (rocks, wood)
        }
        out.push_back(iv);
    }
    return out;
}

} // namespace arp::vegetation
