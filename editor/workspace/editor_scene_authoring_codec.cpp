#include "editor/workspace/editor_scene_authoring_codec.h"

#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "editor/workspace/editor_scene_property_codec.h"
#include "reflection/serialization.h"
#include "scene/scene_asset_resource.h"
#include "scene/transform.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace arp::editor {
namespace {
using Node = core::YamlContract::Node;
using Yaml = core::YamlContract;

bool exactMappingSize(const Node& value, std::size_t expected)
{
    return value.is_mapping() && value.size() == expected;
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
    SceneInspectorProperty value;
    const EditorScenePropertyCodec::ObjectResolver resolve = [&records](std::uint64_t id) {
        for (const auto& record : records)
            if (record.localId == id)
                return record.runtimeId.value();
        return std::uint64_t{};
    };
    if (!EditorScenePropertyCodec::decode(property, encoded, value, validator, context, resolve))
        return false;
    return validateOnly
        || document.setComponentProperty(objectId,
               componentId,
               property.name,
               property.kind,
               value.booleanValue,
               value.integerValue,
               value.unsignedIntegerValue,
               value.numberValue,
               value.vector3Value,
               value.color4Value,
               value.stringValue,
               value.vector2Value,
               value.vector2ListValue,
               value.sceneObjectReferenceValue,
               value.sceneObjectLinkListValue)
        != SceneInspectorWriteResult::Rejected;
}

} // namespace

bool EditorSceneAuthoringCodec::contentHash(
    const EditorSceneDocument& document, core::pmr::String& hash, core::pmr::String& diagnostic)
{
    core::pmr::String yaml;
    if (!encode(document, yaml, diagnostic))
        return false;
    try {
        auto root = Yaml::parseSingleDocument(yaml);
        root["selected"] = 0;
        const auto content = Yaml::serialize(root);
        hash = core::Sha256::hexDigest(core::StringView(content));
        return true;
    } catch (...) {
        diagnostic = "EDITOR-SCENE-CONTENT-HASH";
        return false;
    }
}

bool EditorSceneAuthoringCodec::isContentHash(std::string_view value) noexcept
{
    constexpr std::string_view prefix = "sha256:";
    return value.substr(0, prefix.size()) == prefix
        && core::Sha256::isHexDigest(value.substr(prefix.size()));
}

bool EditorSceneAuthoringCodec::encode(
    const EditorSceneDocument& document, core::pmr::String& yaml, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        // Version 3 encodes child objects only. Reject extra root components
        // until the authoring format has an explicit representation for them.
        for (const auto& component : document.scene().components())
            if (component.get() != &document.scene().transform()) {
                diagnostic = "EDITOR-SCENE-ROOT-COMPONENT-UNSUPPORTED";
                return false;
            }
        const auto entries = document.hierarchyEntries();
        if (entries.empty() || entries.size() - 1u > maxObjects) {
            diagnostic = "EDITOR-SCENE-OBJECT-LIMIT";
            return false;
        }
        Node root = Node::mapping();
        root["type"] = "arp.editor-scene";
        root["contractVersion"] = contractVersion;
        root["runtimeSceneId"] =
            document.runtimeSceneId().empty() ? Node() : Node(document.runtimeSceneId());
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
            Node t;
            reflection::SerializationArchive transformArchive;
            if (!transformArchive.writeFields(reflection::object(object->local_transform()), t))
                return false;
            node["transform"] = std::move(t);
            Node components = Node::sequence();
            const auto inspector = document.inspectorProperties(object->id());
            for (const auto& component : object->components()) {
                // Script names are not native identities, even when their
                // spelling matches a static catalog entry or Transform.
                if (!component->scriptInstance().is_undefined()) {
                    diagnostic = "EDITOR-SCENE-SCRIPT-COMPONENT-UNSUPPORTED";
                    return false;
                }
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
                    if (property.componentId != component->id().value() || !property.editable
                        || !property.serializable)
                        continue;
                    if (++propertyCount > maxPropertiesPerComponent) {
                        diagnostic = "EDITOR-SCENE-PROPERTY-LIMIT";
                        return false;
                    }
                    Node value;
                    const auto objectReferenceLocalId =
                        property.kind == SceneInspectorPropertyKind::SceneObjectReference
                        ? local(scene::GameObjectId::fromValue(property.sceneObjectReferenceValue))
                        : 0u;
                    auto encodedProperty = property;
                    if (property.kind == SceneInspectorPropertyKind::SceneObjectLinkList) {
                        for (auto& link : encodedProperty.sceneObjectLinkListValue) {
                            const auto localId =
                                local(scene::GameObjectId::fromValue(link.targetObjectId));
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
                        || !EditorScenePropertyCodec::encode(
                            encodedProperty, value, objectReferenceLocalId)) {
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
        const auto serialized = Yaml::serialize(root);
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

bool EditorSceneAuthoringCodec::isResourceReady(
    void* context, std::string_view type, std::string_view id)
{
    auto* resources = static_cast<resource::ResourceManager*>(context);
    if (!resources)
        return false;
    const auto* record = resources->recordSnapshot(id);
    return record && record->state == resource::ResourceState::Ready
        && std::string_view(record->type.data(), record->type.size()) == type;
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
        const auto root = Yaml::parseSingleDocument(yaml);
        if (Yaml::text(root, "type") != "arp.editor-scene") {
            diagnostic = "EDITOR-SCENE-CONTRACT-UNSUPPORTED";
            return false;
        }
        if (Yaml::uint32(root, "contractVersion") == 1) {
            diagnostic =
                "ARP-SCENE-MIGRATION-REQUIRED: run python tools/migrate_scene_v1.py source destination";
            return false;
        }
        if (Yaml::uint32(root, "contractVersion") == 2) {
            diagnostic =
                "EDITOR-SCENE-MIGRATION-REQUIRED: run arp_editor_scene_migrate source new-output-directory --runtime-scene <uuid|none>";
            return false;
        }
        if (Yaml::uint32(root, "contractVersion") != contractVersion) {
            diagnostic = "EDITOR-SCENE-CONTRACT-UNSUPPORTED";
            return false;
        }
        if (!exactMappingSize(root, 6u)) {
            diagnostic = "EDITOR-SCENE-ROOT-STRUCTURE";
            return false;
        }
        const auto& objects = Yaml::sequence(root, "objects");
        if (objects.size() > maxObjects) {
            diagnostic = "EDITOR-SCENE-OBJECT-LIMIT";
            return false;
        }
        EditorSceneDocument candidate(false);
        candidate.copySceneServicesFrom(destination);
        candidate.setSchemaOnlyComponentCreation(validateOnly);
        candidate.scene().setName(Yaml::text(root, "name"));
        const auto& identity = Yaml::required(root, "runtimeSceneId");
        if (!identity.is_null()
            && (!identity.is_string() || Yaml::text(identity).empty()
                || !candidate.setRuntimeSceneId(Yaml::text(identity)))) {
            diagnostic = "EDITOR-SCENE-RUNTIME-IDENTITY-INVALID";
            return false;
        }
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
            reflection::SerializationArchive transformArchive;
            if (!transformArchive.readFields(t, reflection::object(&transform)))
                return false;
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
                std::size_t expectedProperties = 0;
                for (const auto& property : inspector)
                    if (property.componentId == component->id().value() && property.editable
                        && property.serializable)
                        ++expectedProperties;
                if (mapping.size() != expectedProperties) {
                    diagnostic = "EDITOR-SCENE-MISSING-AUTHORING-FIELD";
                    return false;
                }
                for (const auto& [rawName, encoded] : mapping) {
                    const auto ownedPropertyName = Yaml::text(rawName);
                    const std::string_view propertyName(
                        ownedPropertyName.data(), ownedPropertyName.size());
                    const auto* property = findProperty(inspector, componentType, propertyName);
                    if (!property || !property->serializable || !property->editable
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
            const auto properties =
                candidate.inspectorProperties(scene::GameObjectId::fromValue(pending.objectId));
            const auto* property = findProperty(properties,
                std::string_view(pending.componentType.data(), pending.componentType.size()),
                std::string_view(pending.propertyName.data(), pending.propertyName.size()));
            if (!property || property->componentId != pending.componentId
                || !writeProperty(candidate,
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

bool EditorSceneAuthoringCodec::validateStructure(
    std::string_view yaml, core::pmr::String& diagnostic)
{
    EditorSceneDocument candidate(false);
    return decode(
        yaml,
        candidate,
        [](void*, std::string_view, std::string_view) {
            return true;
        },
        nullptr,
        diagnostic,
        true);
}

bool EditorSceneAuthoringCodec::migrateVersion2(std::string_view yaml,
    std::string_view runtimeSceneId,
    core::pmr::String& migrated,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (yaml.empty() || yaml.size() > maxDocumentBytes) {
        diagnostic = "EDITOR-SCENE-DOCUMENT-LIMIT";
        return false;
    }
    if (!runtimeSceneId.empty() && !resource::isResourceUuid(runtimeSceneId)) {
        diagnostic = "EDITOR-SCENE-RUNTIME-IDENTITY-INVALID";
        return false;
    }
    try {
        auto root = Yaml::parseSingleDocument(yaml);
        Yaml::onlyKeys(root, {"type", "contractVersion", "name", "objects", "selected"});
        if (Yaml::text(root, "type") != "arp.editor-scene"
            || Yaml::uint32(root, "contractVersion") != 2 || !exactMappingSize(root, 5)) {
            diagnostic = "EDITOR-SCENE-MIGRATION-SOURCE-CONTRACT";
            return false;
        }
        root["contractVersion"] = contractVersion;
        root["runtimeSceneId"] =
            runtimeSceneId.empty() ? Node() : Node(core::pmr::String(runtimeSceneId));
        auto output = Yaml::serialize(root);
        if (!validateStructure(output, diagnostic))
            return false;
        migrated = std::move(output);
        return true;
    } catch (...) {
        diagnostic = "EDITOR-SCENE-MIGRATION-SOURCE-INVALID";
        return false;
    }
}
} // namespace arp::editor
