#pragma once

#include "assets/glb_importer_common.h"
#include "assets/glb_static_mesh_importer.h"

#include <cmath>
#include <initializer_list>

namespace arp::assets::detail {

// static-glb-geometry/v1: a bounded, embedded, identity-node geometry source.
// Validates the complete accepted schema before the existing decoder allocates.
class StaticGlbGeometryContract final {
    using Json = JsonValue;

    struct Failure final {
        const char* diagnostic;
    };

    static void require(bool condition, const char* diagnostic = "ASSET-GLB-GEOMETRY-CONTRACT")
    {
        if (!condition)
            throw Failure{diagnostic};
    }

    static const Json& field(const Json& object, const char* name)
    {
        const auto* value = object.find(name);
        require(value != nullptr);
        return *value;
    }

    static void keys(const Json& object, std::initializer_list<std::string_view> allowed)
    {
        require(object.type == Json::Type::Object);
        for (const auto& item : object.objectValue)
            require(std::any_of(allowed.begin(), allowed.end(), [&](auto key) {
                return item.first == key;
            }));
        if (const auto* name = object.find("name"))
            require(name->type == Json::Type::String);
    }

    static std::uint32_t integer(const Json& value)
    {
        require(value.type == Json::Type::Number && std::isfinite(value.numberValue)
            && value.numberValue >= 0 && value.numberValue <= UINT32_MAX
            && std::floor(value.numberValue) == value.numberValue);
        return static_cast<std::uint32_t>(value.numberValue);
    }

    static std::uint32_t number(const Json& object, const char* key)
    {
        return integer(field(object, key));
    }

    static std::uint32_t optional(const Json& object, const char* key, std::uint32_t defaultValue)
    {
        const auto* value = object.find(key);
        return value ? integer(*value) : defaultValue;
    }

    static const core::pmr::Vector<Json>& array(const Json& object, const char* key)
    {
        const auto& value = field(object, key);
        require(value.type == Json::Type::Array);
        return value.arrayValue;
    }

    static bool text(const Json& object, const char* key, std::string_view expected)
    {
        const auto& value = field(object, key);
        return value.type == Json::Type::String && value.stringValue == expected;
    }

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
            const auto& buffers = array(root, "buffers");
            require(buffers.size() == 1u);
            keys(buffers[0], {"byteLength", "name"});
            const auto binaryBytes = number(buffers[0], "byteLength");
            require(
                binaryBytes && binaryBytes <= binary.size() && binary.size() - binaryBytes <= 3u);
            for (std::size_t i = binaryBytes; i < binary.size(); ++i)
                require(binary[i] == 0);
            const auto& views = array(root, "bufferViews");
            const auto& accessors = array(root, "accessors");
            require(!views.empty() && !accessors.empty());
            for (const auto& view : views) {
                keys(view, {"buffer", "byteOffset", "byteLength", "target", "name"});
                require(number(view, "buffer") == 0u);
                const auto start = optional(view, "byteOffset", 0u);
                const auto size = number(view, "byteLength");
                require(size && start <= binaryBytes && size <= binaryBytes - start);
                if (const auto* target = view.find("target"))
                    require(integer(*target) == 34962u || integer(*target) == 34963u);
            }
            for (const auto& accessor : accessors) {
                keys(accessor,
                    {"bufferView",
                        "byteOffset",
                        "componentType",
                        "count",
                        "type",
                        "min",
                        "max",
                        "name"});
                const auto viewIndex = number(accessor, "bufferView");
                require(viewIndex < views.size());
                const auto count = number(accessor, "count");
                const auto component = number(accessor, "componentType");
                const bool scalar = text(accessor, "type", "SCALAR");
                const auto dimensions = scalar       ? 1u
                    : text(accessor, "type", "VEC2") ? 2u
                    : text(accessor, "type", "VEC3") ? 3u
                    : text(accessor, "type", "VEC4") ? 4u
                                                     : 0u;
                require(count && dimensions
                    && (scalar ? component == 5123u || component == 5125u : component == 5126u));
                const auto bytes = component == 5123u ? 2u : 4u;
                const auto offset = optional(accessor, "byteOffset", 0u);
                const auto& view = views[viewIndex];
                const auto available = number(view, "byteLength");
                require(offset <= available
                    && std::uint64_t(count) * dimensions * bytes <= available - offset
                    && (std::uint64_t(optional(view, "byteOffset", 0u)) + offset) % bytes == 0u);
                for (const auto* bound : {"min", "max"})
                    if (accessor.find(bound)) {
                        const auto& values = array(accessor, bound);
                        require(values.size() == dimensions);
                        for (const auto& value : values)
                            require(value.type == Json::Type::Number
                                && std::isfinite(value.numberValue));
                    }
            }
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
