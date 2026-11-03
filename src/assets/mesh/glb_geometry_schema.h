#pragma once

#include "assets/glb_importer_common.h"
#include "core/math/math.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace arp::assets::detail {

enum class GlbLayoutProfile { StrictStatic, StaticScene, Animated };

// Shared typed glTF layout validation; no file IO or string-search extraction.
class GlbGeometrySchema {
protected:
    using Json = JsonValue;

    struct Failure final {
        const char* diagnostic;
    };

    static void require(bool condition, const char* diagnostic = "ASSET-GLB-GEOMETRY-CONTRACT")
    {
        if (!condition)
            throw Failure{diagnostic};
    }

    // Descriptor/work accounting shared by the cold glTF preparation stages.
    static void chargeBudget(
        std::size_t& remaining, std::size_t count, std::size_t width, const char* diagnostic)
    {
        require(count <= remaining / width, diagnostic);
        remaining -= count * width;
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

    // Shared glTF node numeric/matrix contract. The animated profile also
    // requires a normalizable quaternion; static legacy acceptance is unchanged.
    static void validateNodeTransform(const Json& node,
        bool checkedRotation,
        const char* diagnostic = "ASSET-GLB-GEOMETRY-CONTRACT")
    {
        require(!node.find("matrix")
                || (!node.find("translation") && !node.find("rotation") && !node.find("scale")),
            diagnostic);
        for (const auto* name : {"matrix", "translation", "rotation", "scale"}) {
            if (!node.find(name))
                continue;
            const auto& values = array(node, name);
            const auto count = std::string_view(name) == "matrix" ? 16u
                : std::string_view(name) == "rotation"            ? 4u
                                                                  : 3u;
            require(values.size() == count, diagnostic);
            for (const auto& value : values)
                require(value.type == Json::Type::Number && std::isfinite(value.numberValue)
                        && std::abs(value.numberValue) <= std::numeric_limits<float>::max(),
                    diagnostic);
            if (count == 16)
                require(values[3].numberValue == 0 && values[7].numberValue == 0
                        && values[11].numberValue == 0 && values[15].numberValue == 1,
                    diagnostic);
            if (checkedRotation && count == 4) {
                core::Quat normalized;
                require(core::tryNormalize({static_cast<float>(values[0].numberValue),
                                               static_cast<float>(values[1].numberValue),
                                               static_cast<float>(values[2].numberValue),
                                               static_cast<float>(values[3].numberValue)},
                            normalized),
                    diagnostic);
            }
        }
    }

    static std::uint32_t dimensions(const Json& accessor, GlbLayoutProfile profile)
    {
        if (text(accessor, "type", "SCALAR"))
            return 1u;
        if (text(accessor, "type", "VEC2"))
            return 2u;
        if (text(accessor, "type", "VEC3"))
            return 3u;
        if (text(accessor, "type", "VEC4"))
            return 4u;
        return profile == GlbLayoutProfile::Animated && text(accessor, "type", "MAT4") ? 16u : 0u;
    }

    static std::uint32_t componentBytes(std::uint32_t type)
    {
        switch (type) {
        case 5121u:
            return 1u;
        case 5123u:
            return 2u;
        case 5125u:
        case 5126u:
            return 4u;
        default:
            require(false);
            return 0u;
        }
    }

    static void validateLayout(const Json& root,
        const core::pmr::String& binary,
        GlbLayoutProfile profile = GlbLayoutProfile::StrictStatic)
    {
        require(profile == GlbLayoutProfile::StrictStatic
            || profile == GlbLayoutProfile::StaticScene || profile == GlbLayoutProfile::Animated);
        const bool named = profile != GlbLayoutProfile::StrictStatic;
        const bool animated = profile == GlbLayoutProfile::Animated;
        const auto& buffers = array(root, "buffers");
        require(buffers.size() == 1u);
        keys(buffers[0], {"byteLength", "name"});
        const auto binaryBytes = number(buffers[0], "byteLength");
        require(binaryBytes && binaryBytes <= binary.size() && binary.size() - binaryBytes <= 3u);
        for (std::size_t i = binaryBytes; i < binary.size(); ++i)
            require(binary[i] == 0);
        const auto& views = array(root, "bufferViews");
        const auto& accessors = array(root, "accessors");
        require(!views.empty() && !accessors.empty());
        for (const auto& view : views) {
            if (named)
                keys(view, {"buffer", "byteOffset", "byteLength", "byteStride", "target", "name"});
            else
                keys(view, {"buffer", "byteOffset", "byteLength", "target", "name"});
            require(number(view, "buffer") == 0u);
            const auto start = optional(view, "byteOffset", 0u);
            const auto size = number(view, "byteLength");
            require(size && start <= binaryBytes && size <= binaryBytes - start);
            if (const auto* target = view.find("target"))
                require(integer(*target) == 34962u || integer(*target) == 34963u);
        }
        for (const auto& accessor : accessors) {
            if (named)
                keys(accessor,
                    {"bufferView",
                        "byteOffset",
                        "componentType",
                        "count",
                        "type",
                        "min",
                        "max",
                        "name",
                        "normalized"});
            else
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
            const auto dimensionCount = dimensions(accessor, profile);
            require(count && dimensionCount
                && (dimensionCount == 16u ? component == 5126u
                        : scalar          ? component == 5123u || component == 5125u
                            || (animated && (component == 5121u || component == 5126u))
                                 : component == 5126u
                            || (named && (component == 5121u || component == 5123u))));
            if (named && accessor.find("normalized"))
                require(field(accessor, "normalized").type == Json::Type::Bool);
            // glTF normalized is defined only for integer components. Keep the
            // existing static profiles unchanged; the animated profile is strict.
            if (animated && component == 5126u && accessor.find("normalized"))
                require(!field(accessor, "normalized").boolValue);
            if (animated && accessor.find("normalized") && field(accessor, "normalized").boolValue)
                require(component == 5121u || component == 5123u);
            const auto bytes = componentBytes(component);
            const auto offset = optional(accessor, "byteOffset", 0u);
            const auto& view = views[viewIndex];
            const auto available = number(view, "byteLength");
            const auto elementBytes = dimensionCount * bytes;
            const auto stride = named ? optional(view, "byteStride", elementBytes) : elementBytes;
            require(stride >= elementBytes
                && (!view.find("byteStride") || (stride >= 4 && stride <= 252 && stride % 4 == 0)));
            require(offset <= available
                && std::uint64_t(count - 1u) * stride + elementBytes <= available - offset
                && (std::uint64_t(optional(view, "byteOffset", 0u)) + offset) % bytes == 0u);
            for (const auto* bound : {"min", "max"})
                if (accessor.find(bound)) {
                    const auto& values = array(accessor, bound);
                    require(values.size() == dimensionCount);
                    for (const auto& value : values)
                        require(
                            value.type == Json::Type::Number && std::isfinite(value.numberValue));
                }
        }
    }
};
} // namespace arp::assets::detail
