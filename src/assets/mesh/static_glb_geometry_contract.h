#pragma once

#include "assets/glb_static_mesh_importer.h"
#include "assets/mesh/glb_geometry_schema.h"

#include <cmath>
#include <initializer_list>

namespace arp::assets::detail {

// static-glb-geometry/v1: a bounded, embedded, identity-node geometry source.
// Validates the complete accepted schema before the existing decoder allocates.
class StaticGlbGeometryContract final : private GlbGeometrySchema {

public:
    static const char* validate(const Json& root,
        const core::pmr::String& binary,
        const StaticMeshSourceImportLimits& limits)
    {
        try {
            keys(root,
                {"asset",
                    "scene",
                    "scenes",
                    "nodes",
                    "meshes",
                    "buffers",
                    "bufferViews",
                    "accessors",
                    "materials"});
            const auto& asset = field(root, "asset");
            keys(asset, {"version", "generator"});
            require(text(asset, "version", "2.0"));
            if (const auto* generator = asset.find("generator"))
                require(generator->type == Json::Type::String);
            validateLayout(root, binary);
            const auto& accessors = array(root, "accessors");
            const auto& nodes = array(root, "nodes");
            const auto& scenes = array(root, "scenes");
            const auto& meshes = array(root, "meshes");
            require(nodes.size() == 1u && scenes.size() == 1u && meshes.size() == 1u
                && number(root, "scene") == 0u);
            keys(nodes[0], {"mesh", "name"});
            require(number(nodes[0], "mesh") == 0u);
            keys(scenes[0], {"nodes", "name"});
            const auto& roots = array(scenes[0], "nodes");
            require(roots.size() == 1u && integer(roots[0]) == 0u);
            const auto& materials = array(root, "materials");
            require(!materials.empty());
            for (const auto& material : materials)
                keys(material, {"name"});
            keys(meshes[0], {"primitives", "name"});
            const auto& primitives = array(meshes[0], "primitives");
            require(!primitives.empty() && primitives.size() <= limits.maxPrimitives,
                "ASSET-GLB-GEOMETRY-BUDGET");
            std::uint64_t vertices = 0, indices = 0;
            for (const auto& primitive : primitives) {
                keys(primitive, {"attributes", "indices", "material", "mode"});
                require(optional(primitive, "mode", 4u) == 4u
                    && number(primitive, "material") < materials.size());
                const auto indexId = number(primitive, "indices");
                require(indexId < accessors.size() && text(accessors[indexId], "type", "SCALAR"));
                const auto indexCount = number(accessors[indexId], "count");
                require(indexCount % 3u == 0u);
                const auto& attributes = field(primitive, "attributes");
                keys(attributes, {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"});
                const auto positionId = number(attributes, "POSITION");
                require(positionId < accessors.size());
                const auto count = number(accessors[positionId], "count");
                for (const auto* semantic : {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT"}) {
                    const auto id = number(attributes, semantic);
                    require(id < accessors.size());
                    const auto expected = std::string_view(semantic) == "TANGENT" ? "VEC4"
                        : std::string_view(semantic) == "TEXCOORD_0"              ? "VEC2"
                                                                                  : "VEC3";
                    require(text(accessors[id], "type", expected)
                        && number(accessors[id], "componentType") == 5126u
                        && number(accessors[id], "count") == count);
                }
                vertices += count;
                indices += indexCount;
                require(vertices <= limits.maxVertices && indices <= limits.maxIndices,
                    "ASSET-GLB-GEOMETRY-BUDGET");
            }
            return nullptr;
        } catch (Failure failure) {
            return failure.diagnostic;
        }
    }
};
} // namespace arp::assets::detail
