#include "editor/workspace/editor_scene_property_codec.h"

#include "reflection/serialization.h"
#include "scene/scene_asset_resource.h"

namespace arp::editor {
namespace {
using Node = core::YamlContract::Node;
using Yaml = core::YamlContract;

// Inspector references have already been remapped to document-local identities.
// This context preserves those identities through the common field archive.
class LocalReferenceContext final : public reflection::SerializationContext {
public:
    bool writeReference(const reflection::property& property,
        const reflection::object& value,
        Node& output,
        core::pmr::String&) const override
    {
        if (property.reference().kind != reflection::ReferenceKind::SceneObject)
            return false;
        const auto id = value.cast<std::uint64_t>();
        if (!id || id > INT64_MAX)
            return false;
        output = static_cast<Node::integer_type>(id);
        return true;
    }

    bool readReference(const reflection::property& property,
        const Node& input,
        reflection::object& value,
        core::pmr::String&) const override
    {
        if (property.reference().kind != reflection::ReferenceKind::SceneObject)
            return false;
        const auto id = Yaml::uint64(input);
        if (!id)
            return false;
        value = reflection::object(id);
        return true;
    }
};

template <class T> bool readValue(const Node& input, T& output)
{
    LocalReferenceContext references;
    reflection::SerializationArchive archive({}, &references);
    reflection::object value;
    if (!archive.readValue(input, reflection::type_of<T>(), value))
        return false;
    output = value.cast<T>();
    return true;
}

const char* kindName(SceneInspectorPropertyKind kind)
{
    switch (kind) {
    case SceneInspectorPropertyKind::Boolean:
        return "boolean";
    case SceneInspectorPropertyKind::Integer:
        return "integer";
    case SceneInspectorPropertyKind::UnsignedInteger:
        return "unsigned";
    case SceneInspectorPropertyKind::Number:
        return "number";
    case SceneInspectorPropertyKind::Enumeration:
        return "enum";
    case SceneInspectorPropertyKind::Vector2:
        return "vector2";
    case SceneInspectorPropertyKind::Vector2List:
        return "vector2List";
    case SceneInspectorPropertyKind::SceneObjectLinkList:
        return "sceneObjectLinkList";
    case SceneInspectorPropertyKind::Vector3:
        return "vector3";
    case SceneInspectorPropertyKind::Color4:
        return "color4";
    case SceneInspectorPropertyKind::String:
        return "string";
    case SceneInspectorPropertyKind::ResourceReference:
        return "resource";
    case SceneInspectorPropertyKind::SceneObjectReference:
        return "sceneObjectReference";
    default:
        return nullptr;
    }
}

} // namespace

bool EditorScenePropertyCodec::encode(
    const SceneInspectorProperty& property, Node& output, std::uint64_t sceneObjectLocalId)
{
    const auto* kind = kindName(property.kind);
    if (!kind || !property.editable)
        return false;
    output = Node::mapping();
    output["kind"] = kind;

    reflection::object value;
    switch (property.kind) {
    case SceneInspectorPropertyKind::Boolean:
        value = reflection::object(property.booleanValue);
        break;
    case SceneInspectorPropertyKind::Integer:
    case SceneInspectorPropertyKind::Enumeration:
        value = reflection::object(property.integerValue);
        break;
    case SceneInspectorPropertyKind::UnsignedInteger:
        value = reflection::object(property.unsignedIntegerValue);
        break;
    case SceneInspectorPropertyKind::Number:
        value = reflection::object(property.numberValue);
        break;
    case SceneInspectorPropertyKind::Vector2:
        value = reflection::object(property.vector2Value);
        break;
    case SceneInspectorPropertyKind::Vector3:
        value = reflection::object(property.vector3Value);
        break;
    case SceneInspectorPropertyKind::Color4:
        value = reflection::object(property.color4Value);
        break;
    case SceneInspectorPropertyKind::Vector2List:
        value = reflection::object(property.vector2ListValue);
        break;
    case SceneInspectorPropertyKind::String:
    case SceneInspectorPropertyKind::ResourceReference:
        value = reflection::object(property.stringValue);
        break;
    case SceneInspectorPropertyKind::SceneObjectReference:
        if ((!sceneObjectLocalId && !property.resourceOptional) || sceneObjectLocalId > INT64_MAX)
            return false;
        value = reflection::object(static_cast<std::int64_t>(sceneObjectLocalId));
        break;
    case SceneInspectorPropertyKind::SceneObjectLinkList: {
        core::pmr::Vector<scene::ObjectLinksEditorEntry> links{&core::defaultCpuMemoryResource()};
        for (const auto& item : property.sceneObjectLinkListValue)
            links.push_back({item.name, item.targetObjectId});
        value = reflection::object(std::move(links));
        break;
    }
    default:
        return false;
    }
    LocalReferenceContext references;
    reflection::SerializationArchive archive({}, &references);
    if (!archive.writeValue(value, output["value"]))
        return false;

    return true;
}

bool EditorScenePropertyCodec::decode(const SceneInspectorProperty& property,
    const Node& encoded,
    SceneInspectorProperty& output,
    EditorSceneResourceValidator validator,
    void* context,
    const ObjectResolver& resolveObject)
{
    const auto kind = Yaml::text(encoded, "kind");
    if ((!encoded.is_mapping() || encoded.size() != 2u)
        || (!kindName(property.kind) || kind != kindName(property.kind)))
        return false;
    const auto& value = Yaml::required(encoded, "value");
    bool booleanValue = false;
    int integerValue = 0;
    std::uint64_t unsignedValue = 0;
    double numberValue = 0.0f;
    core::Vec2 vector2Value{};
    core::pmr::Vector<core::Vec2> vector2ListValue(&core::defaultCpuMemoryResource());
    core::pmr::Vector<SceneInspectorObjectLink> sceneObjectLinkListValue{
        &core::defaultCpuMemoryResource()};
    core::Vec3 vectorValue{};
    core::Vec4 colorValue{};
    core::pmr::String stringValue{&core::defaultCpuMemoryResource()};
    std::uint64_t sceneObjectReferenceValue = 0;

    switch (property.kind) {
    case SceneInspectorPropertyKind::Boolean:
        if (!readValue(value, booleanValue))
            return false;
        break;
    case SceneInspectorPropertyKind::Integer:
    case SceneInspectorPropertyKind::Enumeration:
        if (!readValue(value, integerValue))
            return false;
        break;
    case SceneInspectorPropertyKind::UnsignedInteger:
        if (!readValue(value, unsignedValue))
            return false;
        break;
    case SceneInspectorPropertyKind::Number:
        if (!readValue(value, numberValue))
            return false;
        break;
    case SceneInspectorPropertyKind::Vector2:
        if (!readValue(value, vector2Value))
            return false;
        break;
    case SceneInspectorPropertyKind::Vector3:
        if (!readValue(value, vectorValue))
            return false;
        break;
    case SceneInspectorPropertyKind::Color4:
        if (!readValue(value, colorValue))
            return false;
        break;
    case SceneInspectorPropertyKind::Vector2List:
        if (!readValue(value, vector2ListValue)
            || vector2ListValue.size() > kMaxSceneInspectorVector2ListElements)
            return false;
        break;
    case SceneInspectorPropertyKind::SceneObjectReference: {
        std::int64_t id = 0;
        if (!readValue(value, id) || id < 0 || (!id && !property.resourceOptional))
            return false;
        if (id) {
            if (!resolveObject || !(sceneObjectReferenceValue = resolveObject(std::uint64_t(id))))
                return false;
        }
        break;
    }
    case SceneInspectorPropertyKind::SceneObjectLinkList: {
        core::pmr::Vector<scene::ObjectLinksEditorEntry> links{&core::defaultCpuMemoryResource()};
        if (!readValue(value, links) || links.size() > kMaxSceneInspectorObjectLinks)
            return false;
        for (const auto& link : links) {
            const auto target = resolveObject ? resolveObject(link.targetObjectId) : 0;
            if (!target || link.name.empty() || link.name.size() > 128)
                return false;
            sceneObjectLinkListValue.push_back({link.name, target});
        }
        break;
    }
    case SceneInspectorPropertyKind::String:
    case SceneInspectorPropertyKind::ResourceReference:
        if (!readValue(value, stringValue) || stringValue.size() > 512)
            return false;
        if (property.kind == SceneInspectorPropertyKind::ResourceReference && !stringValue.empty()
            && (!validator || !validator(context, property.resourceType, stringValue)))
            return false;
        break;
    default:
        return false;
    }
    auto candidate = property;
    candidate.booleanValue = booleanValue;
    candidate.integerValue = integerValue;
    candidate.unsignedIntegerValue = unsignedValue;
    candidate.numberValue = numberValue;
    candidate.vector3Value = vectorValue;
    candidate.color4Value = colorValue;
    candidate.stringValue = std::move(stringValue);
    candidate.vector2Value = vector2Value;
    candidate.vector2ListValue = std::move(vector2ListValue);
    candidate.sceneObjectReferenceValue = sceneObjectReferenceValue;
    candidate.sceneObjectLinkListValue = std::move(sceneObjectLinkListValue);
    output = std::move(candidate);
    return true;
}
} // namespace arp::editor
