#pragma once

#include "assets/mesh/glb_geometry_schema.h"

#include <limits>

namespace arp::assets::detail {
// Shared scene schema: embedded geometry, explicit nodes and PBR metadata.
// Animated is an explicit profile; default static acceptance/diagnostics remain
// unchanged. Skin topology and clip semantics are validated by their own stages.
class GlbSceneSourceContract final : private GlbGeometrySchema {
    static bool supportedMetadataExtension(std::string_view name)
    {
        for (const auto supported : {"KHR_materials_emissive_strength",
                 "KHR_materials_clearcoat",
                 "KHR_materials_specular",
                 "KHR_materials_sheen",
                 "KHR_materials_transmission",
                 "KHR_materials_volume",
                 "KHR_materials_ior",
                 "KHR_materials_iridescence",
                 "EXT_texture_webp"})
            if (name == supported)
                return true;
        return false;
    }

    enum class ExtensionOwner { Root, Materials, Material, Textures, Texture, Other };

    static void rejectExtensions(const Json& value, ExtensionOwner owner = ExtensionOwner::Root)
    {
        for (const auto& pair : value.objectValue) {
            if (pair.first == "extensions") {
                require(pair.second.type == Json::Type::Object
                        && (owner == ExtensionOwner::Material || owner == ExtensionOwner::Texture),
                    "ASSET-NAMED-VARIANT-EXTENSION-UNSUPPORTED");
                for (const auto& extension : pair.second.objectValue)
                    require(supportedMetadataExtension(extension.first)
                            && extension.second.type == Json::Type::Object
                            && (owner == ExtensionOwner::Texture
                                    ? extension.first == "EXT_texture_webp"
                                    : extension.first != "EXT_texture_webp"),
                        "ASSET-NAMED-VARIANT-EXTENSION-UNSUPPORTED");
            }
            if (pair.first != "extras") {
                const auto childOwner = owner == ExtensionOwner::Root && pair.first == "materials"
                    ? ExtensionOwner::Materials
                    : owner == ExtensionOwner::Root && pair.first == "textures"
                    ? ExtensionOwner::Textures
                    : ExtensionOwner::Other;
                rejectExtensions(pair.second, childOwner);
            }
        }
        for (const auto& child : value.arrayValue)
            rejectExtensions(child,
                owner == ExtensionOwner::Materials      ? ExtensionOwner::Material
                    : owner == ExtensionOwner::Textures ? ExtensionOwner::Texture
                                                        : ExtensionOwner::Other);
    }

    static bool normalized(const Json& accessor)
    {
        const auto* value = accessor.find("normalized");
        return value && value->boolValue;
    }

public:
    static const char* validate(const Json& root,
        const core::pmr::String& binary,
        bool allowColor0,
        GlbLayoutProfile profile = GlbLayoutProfile::StaticScene)
    {
        try {
            require(
                profile == GlbLayoutProfile::StaticScene || profile == GlbLayoutProfile::Animated);
            const bool animated = profile == GlbLayoutProfile::Animated;
            rejectExtensions(root);
            keys(root,
                {"asset",
                    "scene",
                    "scenes",
                    "nodes",
                    "meshes",
                    "buffers",
                    "bufferViews",
                    "accessors",
                    "skins",
                    "animations",
                    "materials",
                    "images",
                    "textures",
                    "samplers",
                    "extras",
                    "extensionsUsed",
                    "extensionsRequired"});
            require(animated || (!root.find("skins") && !root.find("animations")));
            for (const auto* declarations : {"extensionsUsed", "extensionsRequired"})
                if (root.find(declarations))
                    for (const auto& extension : array(root, declarations))
                        require(extension.type == Json::Type::String
                                && supportedMetadataExtension(extension.stringValue),
                            "ASSET-NAMED-VARIANT-EXTENSION-UNSUPPORTED");
            const auto& asset = field(root, "asset");
            keys(asset, {"version", "generator", "copyright", "minVersion", "extras"});
            require(text(asset, "version", "2.0"));
            if (asset.find("minVersion"))
                require(text(asset, "minVersion", "2.0"));
            validateLayout(root, binary, profile);
            const auto& accessors = array(root, "accessors");
            const auto& views = array(root, "bufferViews");
            const auto& materials = array(root, "materials");
            const auto& meshes = array(root, "meshes");
            require(!meshes.empty() && !materials.empty());
            for (const auto& mesh : meshes) {
                keys(mesh, {"name", "primitives", "extras"});
                const auto& primitives = array(mesh, "primitives");
                require(!primitives.empty());
                bool colorPresence = false;
                bool first = true;
                for (const auto& primitive : primitives) {
                    keys(primitive, {"attributes", "indices", "material", "mode", "extras"});
                    require(optional(primitive, "mode", 4) == 4
                        && number(primitive, "material") < materials.size());
                    const auto indexId = number(primitive, "indices");
                    require(indexId < accessors.size());
                    const auto& index = accessors[indexId];
                    require(text(index, "type", "SCALAR") && !normalized(index)
                        && (number(index, "componentType") == 5121
                            || number(index, "componentType") == 5123
                            || number(index, "componentType") == 5125)
                        && number(index, "count") % 3 == 0
                        && !views[number(index, "bufferView")].find("byteStride"));
                    const auto& attributes = field(primitive, "attributes");
                    keys(attributes,
                        {"POSITION",
                            "NORMAL",
                            "TEXCOORD_0",
                            "TANGENT",
                            "COLOR_0",
                            "JOINTS_0",
                            "WEIGHTS_0"});
                    require(animated
                        || (!attributes.find("JOINTS_0") && !attributes.find("WEIGHTS_0")));
                    const auto position = number(attributes, "POSITION");
                    require(position < accessors.size());
                    const auto vertices = number(accessors[position], "count");
                    for (const auto* semantic :
                        {"POSITION", "NORMAL", "TEXCOORD_0", "TANGENT", "COLOR_0"}) {
                        const auto* binding = attributes.find(semantic);
                        if (!binding)
                            continue;
                        const auto id = integer(*binding);
                        require(id < accessors.size());
                        const auto& accessor = accessors[id];
                        const bool color = std::string_view(semantic) == "COLOR_0";
                        const auto shape = std::string_view(semantic) == "TEXCOORD_0" ? "VEC2"
                            : color || std::string_view(semantic) == "TANGENT"        ? "VEC4"
                                                                                      : "VEC3";
                        require(
                            text(accessor, "type", shape) && number(accessor, "count") == vertices);
                        const auto component = number(accessor, "componentType");
                        require(color ? (component == 5126 && !normalized(accessor))
                                    || ((component == 5121 || component == 5123)
                                        && normalized(accessor))
                                      : component == 5126 && !normalized(accessor));
                        if (color)
                            require(allowColor0, "ASSET-NAMED-VARIANT-COLOR-POLICY");
                    }
                    require(bool(attributes.find("JOINTS_0")) == bool(attributes.find("WEIGHTS_0")),
                        "ASSET-ANIMATED-PRIMITIVE-INFLUENCES");
                    for (const auto* semantic : {"JOINTS_0", "WEIGHTS_0"}) {
                        const auto* binding = attributes.find(semantic);
                        if (!binding)
                            continue;
                        const auto id = integer(*binding);
                        require(id < accessors.size(), "ASSET-ANIMATED-PRIMITIVE-INFLUENCES");
                        const auto& accessor = accessors[id];
                        require(
                            text(accessor, "type", "VEC4") && number(accessor, "count") == vertices,
                            "ASSET-ANIMATED-PRIMITIVE-INFLUENCES");
                        const auto component = number(accessor, "componentType");
                        const bool joints = std::string_view(semantic) == "JOINTS_0";
                        require(joints
                                ? (component == 5121 || component == 5123) && !normalized(accessor)
                                : (component == 5126 && !normalized(accessor))
                                    || ((component == 5121 || component == 5123)
                                        && normalized(accessor)),
                            "ASSET-ANIMATED-PRIMITIVE-INFLUENCES");
                    }
                    const bool present = attributes.find("COLOR_0") != nullptr;
                    require(first || present == colorPresence, "ASSET-NAMED-VARIANT-COLOR-MIXED");
                    first = false;
                    colorPresence = present;
                }
            }
            const auto& nodes = array(root, "nodes");
            const auto& scenes = array(root, "scenes");
            require(
                !nodes.empty() && !scenes.empty() && optional(root, "scene", 0) < scenes.size());
            for (const auto& scene : scenes) {
                keys(scene, {"name", "nodes", "extras"});
                for (const auto& node : array(scene, "nodes"))
                    require(integer(node) < nodes.size());
            }
            for (const auto& node : nodes) {
                keys(node,
                    {"name",
                        "mesh",
                        "skin",
                        "children",
                        "matrix",
                        "translation",
                        "rotation",
                        "scale",
                        "extras"});
                require(animated || !node.find("skin"));
                if (const auto* name = node.find("name"))
                    require(name->stringValue.find('\0') == core::pmr::String::npos,
                        "ASSET-NAMED-VARIANT-NAME-INVALID");
                if (node.find("mesh"))
                    require(number(node, "mesh") < meshes.size());
                if (node.find("children"))
                    for (const auto& child : array(node, "children"))
                        require(integer(child) < nodes.size());
                validateNodeTransform(node, animated);
            }
            return nullptr;
        } catch (Failure error) {
            return error.diagnostic;
        }
    }
};
} // namespace arp::assets::detail
