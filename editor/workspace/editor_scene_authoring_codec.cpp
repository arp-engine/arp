#include "editor/workspace/editor_scene_authoring_codec.h"

#include "core/yaml_contract.h"
#include "scene/transform.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace arp::editor {
namespace {
using Node = core::YamlContract::Node;
using Yaml = core::YamlContract;

double readNumber(const Node& value)
{
    if (!value.is_integer() && !value.is_float_number())
        Yaml::fail();
    const auto result = value.get_value<double>();
    if (!std::isfinite(result))
        Yaml::fail();
    return result;
}

bool exactMappingSize(const Node& value, std::size_t expected)
{
    return value.is_mapping()
        && value.get_value_ref<const Node::mapping_type&>().size() == expected;
}

void setVector(Node& node, const core::Vec3& value)
{
    node = Node::mapping();
    node["x"] = value.x;
    node["y"] = value.y;
    node["z"] = value.z;
}

core::Vec3 readVector(const Node& node)
{
    if (!exactMappingSize(node, 3u))
        Yaml::fail();
    return {static_cast<float>(Yaml::number(node, "x")),
        static_cast<float>(Yaml::number(node, "y")),
        static_cast<float>(Yaml::number(node, "z"))};
}

void setVector(Node& node, const core::Vec2& value)
{
    node = Node::mapping();
    node["x"] = value.x;
    node["y"] = value.y;
}

core::Vec2 readVector2(const Node& node)
{
    if (!exactMappingSize(node, 2u))
        Yaml::fail();
    return {static_cast<float>(Yaml::number(node, "x")),
        static_cast<float>(Yaml::number(node, "y"))};
}

void setColor(Node& node, const core::Vec4& value)
{
    node = Node::mapping();
    node["r"] = value.x;
    node["g"] = value.y;
    node["b"] = value.z;
    node["a"] = value.w;
}

core::Vec4 readColor(const Node& node)
{
    if (!exactMappingSize(node, 4u))
        Yaml::fail();
    return {static_cast<float>(Yaml::number(node, "r")),
        static_cast<float>(Yaml::number(node, "g")),
        static_cast<float>(Yaml::number(node, "b")),
        static_cast<float>(Yaml::number(node, "a"))};
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

bool encodeProperty(const SceneInspectorProperty& property,
    Node& output,
    std::uint64_t sceneObjectLocalId = 0)
{
    const auto* kind = kindName(property.kind);
    if (!kind || !property.editable)
        return false;
    output = Node::mapping();
    output["kind"] = kind;
    switch (property.kind) {
    case SceneInspectorPropertyKind::Boolean:
        output["value"] = property.booleanValue;
        break;
    case SceneInspectorPropertyKind::Integer:
    case SceneInspectorPropertyKind::Enumeration:
        output["value"] = property.integerValue;
        break;
    case SceneInspectorPropertyKind::UnsignedInteger:
        output["value"] = property.unsignedIntegerValue;
        break;
    case SceneInspectorPropertyKind::SceneObjectReference:
        if (sceneObjectLocalId == 0u && !property.resourceOptional)
            return false;
        output["value"] = sceneObjectLocalId;
        break;
    case SceneInspectorPropertyKind::Number:
        if (!std::isfinite(property.numberValue))
            return false;
        output["value"] = property.numberValue;
        break;
    case SceneInspectorPropertyKind::Vector2:
        if (!std::isfinite(property.vector2Value.x) || !std::isfinite(property.vector2Value.y))
            return false;
        setVector(output["value"], property.vector2Value);
        break;
    case SceneInspectorPropertyKind::Vector2List:
        if (property.vector2ListValue.size() > kMaxSceneInspectorVector2ListElements)
            return false;
        output["value"] = Node::sequence();
        for (const auto& point : property.vector2ListValue) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return false;
            Node encodedPoint;
            setVector(encodedPoint, point);
            output["value"].get_value_ref<Node::sequence_type&>().push_back(
                std::move(encodedPoint));
        }
        break;
    case SceneInspectorPropertyKind::SceneObjectLinkList:
        if (property.sceneObjectLinkListValue.size() > kMaxSceneInspectorObjectLinks)
            return false;
        output["value"] = Node::sequence();
        for (const auto& link : property.sceneObjectLinkListValue) {
            if (link.name.empty() || link.name.size() > 128u || link.targetObjectId == 0u)
                return false;
            Node encodedLink = Node::mapping();
            encodedLink["name"] = std::string(link.name.data(), link.name.size());
            encodedLink["target"] = link.targetObjectId;
            output["value"].get_value_ref<Node::sequence_type&>().push_back(
                std::move(encodedLink));
        }
        break;
    case SceneInspectorPropertyKind::Vector3:
        if (!std::isfinite(property.vector3Value.x) || !std::isfinite(property.vector3Value.y)
            || !std::isfinite(property.vector3Value.z))
            return false;
        setVector(output["value"], property.vector3Value);
        break;
    case SceneInspectorPropertyKind::Color4:
        if (!std::isfinite(property.color4Value.x) || !std::isfinite(property.color4Value.y)
            || !std::isfinite(property.color4Value.z) || !std::isfinite(property.color4Value.w))
            return false;
        setColor(output["value"], property.color4Value);
        break;
    case SceneInspectorPropertyKind::String:
    case SceneInspectorPropertyKind::ResourceReference:
        if (property.stringValue.size() > 512u)
            return false;
        output["value"] = Node::mapping();
        if (property.stringValue.empty())
            output["value"]["empty"] = true;
        else
            output["value"]["text"] =
                std::string(property.stringValue.data(), property.stringValue.size());
        break;
    default:
        return false;
    }
    return true;
}

bool kindMatches(std::string_view name, SceneInspectorPropertyKind kind)
{
    const auto* expected = kindName(kind);
    return expected && name == expected;
}

struct ObjectRecord {
    std::uint64_t localId = 0;
    std::uint64_t parentId = 0;
    std::size_t childIndex = 0;
    scene::GameObjectId runtimeId;
};

const SceneInspectorProperty* findProperty(
    const core::pmr::Vector<SceneInspectorProperty>& properties,
    std::string_view type,
    std::string_view name)
{
    for (const auto& property : properties)
        if (std::string_view(property.componentType.data(), property.componentType.size()) == type
            && std::string_view(property.name.data(), property.name.size()) == name)
            return &property;
    return nullptr;
}

bool writeProperty(EditorSceneDocument& document,
    scene::GameObjectId objectId,
    scene::ComponentId componentId,
    const SceneInspectorProperty& property,
    const Node& encoded,
    EditorSceneResourceValidator validator,
    void* context,
    bool validateOnly,
    const core::pmr::Vector<ObjectRecord>& records)
{
    const auto kind = Yaml::text(encoded, "kind");
    if (!exactMappingSize(encoded, 2u)
        || !kindMatches(std::string_view(kind.data(), kind.size()), property.kind))
        return false;
    const auto& value = Yaml::required(encoded, "value");
    bool booleanValue = false;
    int integerValue = 0;
    std::uint32_t unsignedValue = 0;
    float numberValue = 0.0f;
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
        if (!value.is_boolean())
            return false;
        booleanValue = value.get_value<bool>();
        break;
    case SceneInspectorPropertyKind::Integer:
    case SceneInspectorPropertyKind::Enumeration:
        if (!value.is_integer())
            return false;
        {
            const auto raw = value.get_value<Node::integer_type>();
            if (raw < INT32_MIN || raw > INT32_MAX)
                return false;
            integerValue = static_cast<int>(raw);
        }
        break;
    case SceneInspectorPropertyKind::UnsignedInteger:
        if (!value.is_integer())
            return false;
        {
            const auto raw = Yaml::uint64(value);
            if (raw > UINT32_MAX)
                return false;
            unsignedValue = static_cast<std::uint32_t>(raw);
        }
        break;
    case SceneInspectorPropertyKind::SceneObjectReference:
        if (!value.is_integer())
            return false;
        {
            const auto localId = Yaml::uint64(value);
            if (localId == 0u && !property.resourceOptional)
                return false;
            if (localId != 0u) {
                const auto target = std::find_if(records.begin(), records.end(),
                    [localId](const auto& record) { return record.localId == localId; });
                if (target == records.end())
                    return false;
                sceneObjectReferenceValue = target->runtimeId.value();
            }
        }
        break;
    case SceneInspectorPropertyKind::Number:
        numberValue = static_cast<float>(readNumber(value));
        break;
    case SceneInspectorPropertyKind::Vector2:
        vector2Value = readVector2(value);
        break;
    case SceneInspectorPropertyKind::Vector2List:
        if (!value.is_sequence()
            || value.get_value_ref<const Node::sequence_type&>().size()
                > kMaxSceneInspectorVector2ListElements)
            return false;
        vector2ListValue.reserve(value.get_value_ref<const Node::sequence_type&>().size());
        for (const auto& point : value.get_value_ref<const Node::sequence_type&>())
            vector2ListValue.push_back(readVector2(point));
        break;
    case SceneInspectorPropertyKind::SceneObjectLinkList:
        if (!value.is_sequence()
            || value.get_value_ref<const Node::sequence_type&>().size()
                > kMaxSceneInspectorObjectLinks)
            return false;
        for (const auto& encodedLink : value.get_value_ref<const Node::sequence_type&>()) {
            if (!exactMappingSize(encodedLink, 2u))
                return false;
            const auto name = Yaml::text(encodedLink, "name");
            const auto localId = Yaml::uint64(Yaml::required(encodedLink, "target"));
            const auto target = std::find_if(records.begin(), records.end(),
                [localId](const auto& record) { return record.localId == localId; });
            if (name.empty() || name.size() > 128u || localId == 0u || target == records.end())
                return false;
            sceneObjectLinkListValue.emplace_back();
            auto& link = sceneObjectLinkListValue.back();
            link.name = name;
            link.targetObjectId = target->runtimeId.value();
        }
        break;
    case SceneInspectorPropertyKind::Vector3:
        vectorValue = readVector(value);
        break;
    case SceneInspectorPropertyKind::Color4:
        colorValue = readColor(value);
        break;
    case SceneInspectorPropertyKind::String:
    case SceneInspectorPropertyKind::ResourceReference:
        if (!value.is_mapping() || value.get_value_ref<const Node::mapping_type&>().size() != 1u)
            return false;
        if (value.contains("empty")) {
            if (!Yaml::boolean(value, "empty"))
                return false;
        } else if (value.contains("text")) {
            stringValue = Yaml::text(value, "text");
            if (stringValue.empty())
                return false;
        } else
            return false;
        if (stringValue.size() > 512u)
            return false;
        if (property.kind == SceneInspectorPropertyKind::ResourceReference
            && !stringValue.empty()) {
            if (!validator
                || !validator(context,
                    std::string_view(property.resourceType.data(), property.resourceType.size()),
                    std::string_view(stringValue.data(), stringValue.size())))
                return false;
        }
        break;
    default:
        return false;
    }
    return validateOnly || document.setComponentProperty(objectId,
               componentId,
               std::string_view(property.name.data(), property.name.size()),
               property.kind,
               booleanValue,
               integerValue,
               unsignedValue,
               numberValue,
               vectorValue,
               colorValue,
               std::string_view(stringValue.data(), stringValue.size()),
               vector2Value,
               vector2ListValue,
               sceneObjectReferenceValue,
               sceneObjectLinkListValue)
        != SceneInspectorWriteResult::Rejected;
}
} // namespace

bool EditorSceneAuthoringCodec::encode(
    const EditorSceneDocument& document, core::pmr::String& yaml, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        const auto entries = document.hierarchyEntries();
        if (entries.empty() || entries.size() - 1u > maxObjects) {
            diagnostic = "EDITOR-SCENE-OBJECT-LIMIT";
            return false;
        }
        Node root = Node::mapping();
        root["type"] = "arp.editor-scene";
        root["contractVersion"] = contractVersion;
        root["name"] = document.scene().name().c_str();
        root["objects"] = Node::sequence();
        core::pmr::Vector<std::pair<std::uint64_t, std::uint64_t>> ids{
            &core::defaultCpuMemoryResource()};
        for (std::size_t i = 1; i < entries.size(); ++i)
            ids.push_back({entries[i].objectId.value(), i});
        const auto local = [&ids](scene::GameObjectId id) -> std::uint64_t {
            for (const auto& entry : ids)
                if (entry.first == id.value())
                    return entry.second;
            return 0;
        };
        root["selected"] = local(document.selectedObject());
        std::size_t componentCount = 0;
        for (std::size_t i = 1; i < entries.size(); ++i) {
            const auto* object = document.scene().findObject(entries[i].objectId);
            if (!object) {
                diagnostic = "EDITOR-SCENE-OBJECT-MISSING";
                return false;
            }
            Node node = Node::mapping();
            node["id"] = static_cast<std::uint64_t>(i);
            node["name"] = object->name().c_str();
            node["active"] = object->active();
            node["parent"] =
                object->parentId() == document.scene().rootId() ? 0u : local(object->parentId());
            const auto* parent = document.scene().findObject(object->parentId());
            std::size_t childIndex = 0;
            if (parent) {
                const auto& children = parent->childIds();
                const auto it = std::find(children.begin(), children.end(), object->id());
                if (it == children.end()) {
                    diagnostic = "EDITOR-SCENE-HIERARCHY-INVALID";
                    return false;
                }
                childIndex = static_cast<std::size_t>(it - children.begin());
            }
            node["childIndex"] = static_cast<std::uint64_t>(childIndex);
            const auto transform = object->transform().localTransform();
            Node t = Node::mapping();
            setVector(t["translation"], transform.translation);
            setVector(t["scale"], transform.scale);
            Node rotation = Node::mapping();
            rotation["x"] = transform.rotation.x;
            rotation["y"] = transform.rotation.y;
            rotation["z"] = transform.rotation.z;
            rotation["w"] = transform.rotation.w;
            t["rotation"] = std::move(rotation);
            node["transform"] = std::move(t);
            Node components = Node::sequence();
            const auto inspector = document.inspectorProperties(object->id());
            for (const auto& component : object->components()) {
                const std::string_view type(component->scriptTypeName());
                if (type == "TransformComponent")
                    continue;
                bool known = false;
                for (const auto& entry : EditorSceneDocument::componentCatalog())
                    known = known || entry.typeName == type;
                if (!known || ++componentCount > maxComponents) {
                    diagnostic = "EDITOR-SCENE-COMPONENT-UNSUPPORTED";
                    return false;
                }
                Node componentNode = Node::mapping();
                componentNode["type"] = component->scriptTypeName();
                Node properties = Node::mapping();
                std::size_t propertyCount = 0;
                for (const auto& property : inspector) {
                    if (property.componentId != component->id().value() || !property.editable)
                        continue;
                    if (++propertyCount > maxPropertiesPerComponent) {
                        diagnostic = "EDITOR-SCENE-PROPERTY-LIMIT";
                        return false;
                    }
                    Node value;
                    const auto objectReferenceLocalId =
                        property.kind == SceneInspectorPropertyKind::SceneObjectReference
                        ? local(scene::GameObjectId::fromValue(
                              property.sceneObjectReferenceValue))
                        : 0u;
                    auto encodedProperty = property;
                    if (property.kind == SceneInspectorPropertyKind::SceneObjectLinkList) {
                        for (auto& link : encodedProperty.sceneObjectLinkListValue) {
                            const auto localId = local(
                                scene::GameObjectId::fromValue(link.targetObjectId));
                            if (localId == 0u) {
                                diagnostic = "EDITOR-SCENE-OBJECT-LINK-EXTERNAL";
                                return false;
                            }
                            link.targetObjectId = localId;
                        }
                    }
                    if ((property.kind == SceneInspectorPropertyKind::SceneObjectReference
                            && property.sceneObjectReferenceValue != 0u
                            && objectReferenceLocalId == 0u)
                        || !encodeProperty(encodedProperty, value, objectReferenceLocalId)) {
                        diagnostic = "EDITOR-SCENE-PROPERTY-UNSUPPORTED";
                        return false;
                    }
                    properties[std::string(property.name.data(), property.name.size())] =
                        std::move(value);
                }
                componentNode["properties"] = std::move(properties);
                components.get_value_ref<Node::sequence_type&>().push_back(
                    std::move(componentNode));
            }
            node["components"] = std::move(components);
            root["objects"].get_value_ref<Node::sequence_type&>().push_back(std::move(node));
        }
        const auto serialized = Node::serialize(root);
        if (serialized.size() > maxDocumentBytes) {
            diagnostic = "EDITOR-SCENE-DOCUMENT-LIMIT";
            return false;
        }
        yaml.assign(serialized.data(), serialized.size());
        return true;
    } catch (...) {
        diagnostic = "EDITOR-SCENE-ENCODE-INVALID";
        return false;
    }
}

bool EditorSceneAuthoringCodec::decode(std::string_view yaml,
    EditorSceneDocument& destination,
    EditorSceneResourceValidator resourceValidator,
    void* resourceContext,
    core::pmr::String& diagnostic,
    bool validateOnly)
{
    diagnostic.clear();
    if (yaml.empty() || yaml.size() > maxDocumentBytes) {
        diagnostic = "EDITOR-SCENE-DOCUMENT-LIMIT";
        return false;
    }
    const char* stage = "root";
    try {
        const auto root = Node::deserialize(core::pmr::String(yaml.data(), yaml.size()));
        if (!exactMappingSize(root, 5u)) {
            diagnostic = "EDITOR-SCENE-ROOT-STRUCTURE";
            return false;
        }
        if (Yaml::text(root, "type") != "arp.editor-scene"
            || Yaml::uint32(root, "contractVersion") != contractVersion) {
            diagnostic = "EDITOR-SCENE-CONTRACT-UNSUPPORTED";
            return false;
        }
        const auto& objects = Yaml::sequence(root, "objects");
        if (objects.size() > maxObjects) {
            diagnostic = "EDITOR-SCENE-OBJECT-LIMIT";
            return false;
        }
        EditorSceneDocument candidate(false);
        candidate.setResourceManagerProvider(destination.resourceManagerProvider());
    candidate.setPhysicsWorld2DProvider(destination.physicsWorld2DProvider());
        candidate.setSchemaOnlyComponentCreation(validateOnly);
        candidate.scene().setName(Yaml::text(root, "name"));
        core::pmr::Vector<ObjectRecord> records{&core::defaultCpuMemoryResource()};
        records.reserve(objects.size());
        struct PendingObjectReference {
            std::uint64_t objectId = 0;
            std::uint64_t componentId = 0;
            core::pmr::String componentType{&core::defaultCpuMemoryResource()};
            core::pmr::String propertyName{&core::defaultCpuMemoryResource()};
            const Node* encoded = nullptr;
        };
        core::pmr::Vector<PendingObjectReference> pendingObjectReferences{
            &core::defaultCpuMemoryResource()};
        std::size_t componentCount = 0;
        stage = "object";
        for (const auto& node : objects) {
            ObjectRecord record;
            record.localId = Yaml::uint64(Yaml::required(node, "id"));
            record.parentId = Yaml::uint64(Yaml::required(node, "parent"));
            const auto childIndex = Yaml::uint64(Yaml::required(node, "childIndex"));
            if (record.localId == 0 || childIndex > maxObjects
                || std::any_of(records.begin(), records.end(), [&](const auto& r) {
                       return r.localId == record.localId;
                   })) {
                diagnostic = "EDITOR-SCENE-OBJECT-IDENTITY";
                return false;
            }
            record.childIndex = static_cast<std::size_t>(childIndex);
            const auto name = Yaml::text(node, "name");
            if (!exactMappingSize(node, 7u) || name.empty() || name.size() > 128u) {
                diagnostic = "EDITOR-SCENE-OBJECT-NAME";
                return false;
            }
            record.runtimeId = candidate.createObject(std::string_view(name.data(), name.size()));
            auto* object = candidate.scene().findObject(record.runtimeId);
            object->setActive(Yaml::boolean(node, "active"));
            const auto& t = Yaml::required(node, "transform");
            if (!exactMappingSize(t, 3u)) {
                diagnostic = "EDITOR-SCENE-TRANSFORM-STRUCTURE";
                return false;
            }
            auto transform = scene::Transform::identity();
            transform.translation = readVector(Yaml::required(t, "translation"));
            transform.scale = readVector(Yaml::required(t, "scale"));
            const auto& rotation = Yaml::required(t, "rotation");
            if (!exactMappingSize(rotation, 4u)) {
                diagnostic = "EDITOR-SCENE-ROTATION-STRUCTURE";
                return false;
            }
            transform.rotation = {static_cast<float>(Yaml::number(rotation, "x")),
                static_cast<float>(Yaml::number(rotation, "y")),
                static_cast<float>(Yaml::number(rotation, "z")),
                static_cast<float>(Yaml::number(rotation, "w"))};
            const auto finiteVec = [](const core::Vec3& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            if (!finiteVec(transform.translation) || !finiteVec(transform.scale)
                || !std::isfinite(transform.rotation.x) || !std::isfinite(transform.rotation.y)
                || !std::isfinite(transform.rotation.z) || !std::isfinite(transform.rotation.w)) {
                diagnostic = "EDITOR-SCENE-TRANSFORM-NONFINITE";
                return false;
            }
            object->transform().setLocalTransform(transform);
            records.push_back(record);
        }
        stage = "hierarchy";
        for (const auto& record : records) {
            if (record.parentId == 0)
                continue;
            const auto parent = std::find_if(records.begin(), records.end(), [&](const auto& r) {
                return r.localId == record.parentId;
            });
            if (parent == records.end() || parent->localId == record.localId
                || !candidate.reparentObject(
                    record.runtimeId, parent->runtimeId, record.childIndex)) {
                diagnostic = "EDITOR-SCENE-HIERARCHY-INVALID";
                return false;
            }
        }
        stage = "component";
        for (std::size_t index = 0; index < objects.size(); ++index) {
            const auto& node = objects[index];
            auto* object = candidate.scene().findObject(records[index].runtimeId);
            const auto& components = Yaml::sequence(node, "components");
            for (const auto& componentNode : components) {
                if (++componentCount > maxComponents) {
                    diagnostic = "EDITOR-SCENE-COMPONENT-LIMIT";
                    return false;
                }
                const auto type = Yaml::text(componentNode, "type");
                if (!exactMappingSize(componentNode, 2u)) {
                    diagnostic = "EDITOR-SCENE-COMPONENT-STRUCTURE";
                    return false;
                }
                bool known = false;
                for (const auto& entry : EditorSceneDocument::componentCatalog())
                    known = known || entry.typeName == std::string_view(type.data(), type.size());
                if (!known || type == "TransformComponent") {
                    diagnostic = "EDITOR-SCENE-COMPONENT-UNKNOWN";
                    return false;
                }
                const auto componentType = std::string_view(type.data(), type.size());
                auto* component = object->create_component(type);
                if (!component) {
                    diagnostic = "EDITOR-SCENE-COMPONENT-FACTORY-FAILED";
                    diagnostic.append(": ");
                    diagnostic.append(type.data(), type.size());
                    return false;
                }
                const auto& properties = Yaml::required(componentNode, "properties");
                if (!properties.is_mapping()) {
                    diagnostic = "EDITOR-SCENE-PROPERTIES-INVALID";
                    return false;
                }
                const auto inspector = candidate.inspectorProperties(object->id());
                const auto& mapping = properties.get_value_ref<const Node::mapping_type&>();
                if (mapping.size() > maxPropertiesPerComponent) {
                    diagnostic = "EDITOR-SCENE-PROPERTY-LIMIT";
                    return false;
                }
                for (const auto& [rawName, encoded] : mapping) {
                    const auto ownedPropertyName = Yaml::text(rawName);
                    const std::string_view propertyName(
                        ownedPropertyName.data(), ownedPropertyName.size());
                    const auto* property = findProperty(inspector, componentType, propertyName);
                    if (!property || !property->editable
                        || property->componentId != component->id().value()) {
                        diagnostic = "EDITOR-SCENE-PROPERTY-INVALID: ";
                        diagnostic.append(type.data(), type.size());
                        diagnostic.push_back('.');
                        diagnostic.append(propertyName.data(), propertyName.size());
                        return false;
                    }
                    if (property->kind == SceneInspectorPropertyKind::SceneObjectReference) {
                        pendingObjectReferences.emplace_back();
                        auto& pending = pendingObjectReferences.back();
                        pending.objectId = object->id().value();
                        pending.componentId = component->id().value();
                        pending.componentType = componentType;
                        pending.propertyName = propertyName;
                        pending.encoded = &encoded;
                        continue;
                    }
                    if (!writeProperty(candidate,
                            object->id(),
                            component->id(),
                            *property,
                            encoded,
                            resourceValidator,
                            resourceContext,
                            validateOnly,
                            records)) {
                        diagnostic = "EDITOR-SCENE-PROPERTY-INVALID: ";
                        diagnostic.append(type.data(), type.size());
                        diagnostic.push_back('.');
                        diagnostic.append(propertyName.data(), propertyName.size());
                        return false;
                    }
                }
            }
        }
        for (const auto& pending : pendingObjectReferences) {
            const auto properties = candidate.inspectorProperties(
                scene::GameObjectId::fromValue(pending.objectId));
            const auto* property = findProperty(properties,
                std::string_view(pending.componentType.data(), pending.componentType.size()),
                std::string_view(pending.propertyName.data(), pending.propertyName.size()));
            if (!property || property->componentId != pending.componentId || !writeProperty(candidate,
                    scene::GameObjectId::fromValue(pending.objectId),
                    scene::ComponentId::fromValue(pending.componentId),
                    *property,
                    *pending.encoded,
                    resourceValidator,
                    resourceContext,
                    validateOnly,
                    records)) {
                diagnostic = "EDITOR-SCENE-OBJECT-REFERENCE-INVALID";
                return false;
            }
        }
        stage = "selection";
        const auto selected = Yaml::uint64(Yaml::required(root, "selected"));
        if (selected == 0)
            candidate.select(candidate.scene().rootId());
        else {
            const auto it = std::find_if(records.begin(), records.end(), [&](const auto& r) {
                return r.localId == selected;
            });
            if (it == records.end() || !candidate.select(it->runtimeId)) {
                diagnostic = "EDITOR-SCENE-SELECTION-INVALID";
                return false;
            }
        }
        if (!validateOnly && !destination.replaceWith(std::move(candidate))) {
            diagnostic = "EDITOR-SCENE-REPLACE-FAILED";
            return false;
        }
        return true;
    } catch (const core::YamlContractError&) {
        diagnostic = "EDITOR-SCENE-";
        diagnostic += stage;
        diagnostic += "-FIELD-INVALID";
        return false;
    } catch (...) {
        diagnostic = "EDITOR-SCENE-YAML-INVALID";
        return false;
    }
}
} // namespace arp::editor
