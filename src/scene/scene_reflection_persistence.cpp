#include "core/algorithm/dependency_order.h"
#include "core/text/number_format.h"
#include "resource/serialization_reference_resolver.h"
#include "scene/scene_asset_resource.h"

namespace arp::scriptvm {
class ModuleRegistration;
}

namespace arp::reflection::generated {
bool register_scene_authoring_reflection(arp::scriptvm::ModuleRegistration*);
}

namespace arp::scene {
using Y = core::YamlContract;
using Node = Y::Node;

bool initializeSceneAuthoringReflection()
{
    static const bool registered =
        reflection::generated::register_scene_authoring_reflection(nullptr);
    return registered;
}

namespace {
class SceneReferenceContext final : public reflection::SerializationContext {
public:
    SceneReferenceContext(const SceneObjectBindings& objects, resource::ResourceManager* resources)
        : objects_(objects), resources_(resources)
    {}

    bool writeReference(const reflection::property& property,
        const reflection::object& value,
        Node& node,
        core::pmr::String& diagnostic) const override
    {
        const auto& metadata = property.reference();
        if (metadata.kind == reflection::ReferenceKind::Resource) {
            const auto& id = value.cast<const core::pmr::String&>();
            if (!resourceValid(metadata, id, diagnostic))
                return false;
            node = core::toStdString(id);
            return true;
        }
        const auto id = GameObjectId::fromValue(value.cast<std::uint64_t>());
        const auto local = objects_.local(id);
        if ((!local || !objectValid(metadata, id)) && (metadata.required || id.valid())) {
            diagnostic = "ARP-SCENE-REFERENCE-EXTERNAL-DANGLING-OR-TYPE";
            return false;
        }
        node = static_cast<Node::integer_type>(local);
        return true;
    }

    bool readReference(const reflection::property& property,
        const Node& node,
        reflection::object& value,
        core::pmr::String& diagnostic) const override
    {
        const auto& metadata = property.reference();
        if (metadata.kind == reflection::ReferenceKind::Resource) {
            const auto id = Y::text(node);
            if (!resourceValid(metadata, id, diagnostic))
                return false;
            value = reflection::object(id);
            return true;
        }
        const auto local = Y::uint64(node);
        const auto id = objects_.object(local);
        if ((!id.valid() || !objectValid(metadata, id)) && (metadata.required || local)) {
            diagnostic = "ARP-SCENE-REFERENCE-DANGLING-OR-TYPE";
            return false;
        }
        value = reflection::object(id.value());
        return true;
    }

    bool writeReference(const reflection::SerializationDescriptor& type,
        const reflection::object& value,
        Node& node,
        core::pmr::String& diagnostic) const override
    {
        if (value.is_native_reference() && !value.native_reference_alive())
            return false;
        if (value.is_native_reference() && value.can_safe_cast<GameObject*>()) {
            if (type.referenceType != "GameObject"
                && type.referenceClass != reflection::class_of<GameObject>())
                return false;
            const auto id = objects_.local(value.cast<GameObject*>()->id());
            if (!id)
                return false;
            node = Node::mapping();
            node["object"] = static_cast<Node::integer_type>(id);
            return true;
        }
        for (const auto& object : objects_.scene().objects()) {
            core::pmr::Map<core::pmr::String, std::uint32_t> ordinals{
                &core::defaultCpuMemoryResource()};
            for (const auto& component : object->components()) {
                const core::pmr::String name(component->scriptTypeName());
                const auto ordinal = ordinals[name]++;
                const auto instance = component->scriptInstance();
                const bool same =
                    value.is<reflection::instance>() && instance.is<reflection::instance>()
                    ? value.ptr<reflection::instance>() == instance.ptr<reflection::instance>()
                    : value.is_native_reference() && value.can_safe_cast<Component*>()
                        && value.cast<Component*>() == component.get();
                if (!same)
                    continue;
                if (type.referenceType != name && type.referenceType != "Component"
                    && (!type.referenceClass || !component->nativeReflectionObject().class_()
                        || !type.referenceClass->is_base_of(
                            component->nativeReflectionObject().class_())))
                    return false;
                const auto id = objects_.local(object->id());
                if (!id)
                    return false;
                node = Node::mapping();
                node["object"] = static_cast<Node::integer_type>(id);
                node["componentType"] = core::toStdString(name);
                node["ordinal"] = ordinal;
                return true;
            }
        }
        diagnostic = "ARP-SCENE-COMPONENT-REFERENCE-EXTERNAL";
        return false;
    }

    bool readReference(const reflection::SerializationDescriptor& type,
        const Node& node,
        reflection::object& value,
        core::pmr::String& diagnostic) const override
    {
        if (!node.is_mapping())
            return false;
        const auto* object = objects_.findObject(Y::uint64(Y::required(node, "object")));
        if (!object) {
            diagnostic = "ARP-SCENE-COMPONENT-REFERENCE-DANGLING";
            return false;
        }
        if (type.referenceType == "GameObject"
            || type.referenceClass == reflection::class_of<GameObject>()) {
            if (node.size() != 1)
                return false;
            value = object->nativeReflectionObject();
            return true;
        }
        if (node.size() != 3)
            return false;
        const auto name = Y::text(node, "componentType");
        auto ordinal = Y::uint32(node, "ordinal");
        if (type.referenceType != "Component" && type.referenceType != name && !type.referenceClass)
            return false;
        for (const auto& component : object->components())
            if (name == component->scriptTypeName() && ordinal-- == 0) {
                value = component->scriptInstance();
                if (value.is_undefined()) {
                    value = component->nativeReflectionObject();
                    if (!type.referenceClass || !value.class_()
                        || !type.referenceClass->is_base_of(value.class_()))
                        return false;
                } else if (type.referenceType != name && type.referenceType != "Component")
                    return false;
                return true;
            }
        return false;
    }

private:
    bool objectValid(const reflection::ReferenceMetadata& metadata, GameObjectId id) const
    {
        const auto* object = objects_.scene().findObject(id);
        if (!object)
            return false;
        if (metadata.targetType == "*")
            return true;
        std::size_t matches = 0;
        for (const auto& component : object->components())
            if (metadata.targetType == component->scriptTypeName())
                ++matches;
        return matches == 1;
    }

    bool resourceValid(const reflection::ReferenceMetadata& metadata,
        const core::pmr::String& id,
        core::pmr::String& diagnostic) const
    {
        return resource::SerializationReferenceResolver::validate(
            resources_, metadata, id, diagnostic);
    }

    const SceneObjectBindings& objects_;
    resource::ResourceManager* resources_;
};

bool envelope(const Node& node, std::initializer_list<const char*> keys)
{
    if (!node.is_mapping() || node.size() != keys.size())
        return false;
    for (auto* key : keys)
        if (!node.contains(key))
            return false;
    return true;
}

bool archiveFailure(const reflection::SerializationArchive& archive, core::pmr::String& diagnostic)
{
    diagnostic = archive.diagnostics();
    return false;
}
} // namespace

core::UniquePtr<Scene> SceneDocumentResource::instantiate(resource::ResourceManager& resources,
    const SceneComponentFactory& factory,
    core::pmr::String& diagnostic) const
{
    return instantiate(resources, factory, diagnostic, SceneExecutionMode::Runtime);
}

core::UniquePtr<Scene> SceneDocumentResource::instantiate(resource::ResourceManager& resources,
    const SceneComponentFactory& factory,
    core::pmr::String& diagnostic,
    SceneExecutionMode mode) const
{
    diagnostic.clear();
    try {
        const auto root = Y::parseSingleDocument(yaml_);
        auto candidate =
            core::makeUnique<Scene>(Y::text(root, "name"), SceneId::fromValue(1), mode);
        Scene::RestorationTransaction transaction(*candidate);
        if (!populate(*candidate, candidate->rootId(), resources, factory, diagnostic))
            return {};
        transaction.commit();
        return candidate;
    } catch (...) {
        diagnostic = "ARP-SCENE-REFLECTION-INSTANTIATE";
        return {};
    }
}

GameObjectId SceneDocumentResource::instantiateInto(Scene& target,
    GameObjectId parent,
    resource::ResourceManager& resources,
    const SceneComponentFactory& factory,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (!target.findObject(parent)) {
        diagnostic = "ARP-SCENE-ASSET-INSTANCE-PARENT";
        return {};
    }
    GameObjectId instance;
    try {
        Scene::RestorationTransaction transaction(target);
        const auto root = Y::parseSingleDocument(yaml_);
        instance = target.createObject(Y::text(root, "name")).id();
        if (target.reparent(instance, parent).ok()
            && populate(target, instance, resources, factory, diagnostic)) {
            transaction.commit();
            return instance;
        }
    } catch (...) {
        diagnostic = "ARP-SCENE-REFLECTION-INSTANCE";
    }
    return {};
}

bool SceneDocumentResource::populate(Scene& target,
    GameObjectId instanceRoot,
    resource::ResourceManager& resources,
    const SceneComponentFactory& factory,
    core::pmr::String& diagnostic) const
{
    if (!initializeSceneAuthoringReflection()) {
        diagnostic = "ARP-SCENE-REFLECTION-SCHEMA";
        return false;
    }
    try {
        const auto root = Y::parseSingleDocument(yaml_);
        if (Y::uint32(root, "contractVersion") != 2) {
            diagnostic = "ARP-SCENE-MIGRATION-REQUIRED: run tools/migrate_scene_v1.py";
            return false;
        }
        core::pmr::Vector<SceneObjectBinding> bindings{&core::defaultCpuMemoryResource()};
        reflection::SerializationArchive objectArchive;
        for (const auto& node : Y::sequence(root, "objects")) {
            core::pmr::String objectPath("$.objects.", &core::defaultCpuMemoryResource());
            core::appendUnsigned(objectPath, Y::uint64(Y::required(node, "id")));
            objectArchive.setPath(objectPath);
            auto properties = reflection::object(SceneObjectAuthoringProperties{});
            auto transform = reflection::object(Transform{});
            if (!objectArchive.readFields(Y::required(node, "properties"), properties)
                || !objectArchive.readFields(Y::required(node, "transform"), transform))
                return archiveFailure(objectArchive, diagnostic);
            const auto& state = properties.cast<const SceneObjectAuthoringProperties&>();
            auto& object = target.createObject(state.name);
            if (!target.reparent(object.id(), instanceRoot).ok())
                return false;
            object.setActive(state.active);
            object.set_local_transform(transform.cast<const Transform&>());
            bindings.push_back({Y::uint64(Y::required(node, "id")), object.id()});
        }
        SceneObjectBindings objects(bindings, target);
        for (const auto& node : Y::sequence(root, "objects")) {
            const auto id = objects.object(Y::uint64(Y::required(node, "id")));
            const auto parent = Y::uint64(Y::required(node, "parent"));
            if (parent && !target.reparent(id, objects.object(parent)).ok()) {
                diagnostic = "ARP-SCENE-ASSET-PARENT";
                return false;
            }
        }

        struct Pending {
            Component* component;
            const SceneComponentFactory::Persistence* entry;
            reflection::object state;
            const Node* fields;
            core::pmr::String path;
        };

        core::pmr::Vector<Pending> pending{&core::defaultCpuMemoryResource()};
        SceneReferenceContext references(objects, &resources);
        reflection::SerializationArchive archive({}, &references);
        // Allocate all identities without running scripts or preparing external resources.
        for (const auto& node : Y::sequence(root, "objects")) {
            auto* owner = target.findObject(objects.object(Y::uint64(Y::required(node, "id"))));
            core::pmr::String objectPath("$.objects.", &core::defaultCpuMemoryResource());
            core::appendUnsigned(objectPath, Y::uint64(Y::required(node, "id")));
            objectPath += ".components.";
            std::size_t ordinal = 0;
            for (const auto& data : Y::sequence(node, "components")) {
                if (pending.size() >= 4096) {
                    diagnostic = "ARP-SCENE-COMPONENT-BUDGET";
                    return false;
                }
                auto componentPath = objectPath;
                core::appendUnsigned(componentPath, ordinal++);
                archive.setPath(componentPath + ".configuration");
                const auto* entry = factory.persistence(Y::text(data, "type"));
                if (!entry || Y::uint32(data, "schemaVersion") != 1
                    || !envelope(
                        data, {"type", "schemaVersion", "enabled", "configuration", "fields"})) {
                    diagnostic = "ARP-SCENE-COMPONENT-SCHEMA-OR-FACTORY";
                    return false;
                }
                reflection::object configuration;
                if (entry->configuration) {
                    configuration = entry->configuration(nullptr);
                    if (!archive.readFields(Y::required(data, "configuration"), configuration))
                        return archiveFailure(archive, diagnostic);
                } else if (!Y::required(data, "configuration").is_null()) {
                    diagnostic = "ARP-SCENE-CONSTRUCTION-CONFIGURATION";
                    return false;
                }
                auto component = entry->construct(*owner, configuration);
                if (!component) {
                    diagnostic = "ARP-SCENE-COMPONENT-CONSTRUCTION";
                    return false;
                }
                component->setRestoring(true);
                component->setEnabled(Y::boolean(data, "enabled"));
                auto* pointer = component.get();
                owner->addComponent(std::move(component));
                pending.push_back({pointer,
                    entry,
                    entry->state(*pointer),
                    &Y::required(data, "fields"),
                    componentPath + ".fields"});
            }
        }
        // All local object/component references, including cycles, now have identities.
        for (auto& item : pending) {
            archive.setPath(item.path);
            const auto descriptor =
                item.entry->descriptor ? item.entry->descriptor(*item.component) : nullptr;
            if (item.entry->descriptor && !descriptor) {
                diagnostic = "ARP-SCENE-FIELD-DESCRIPTOR";
                return false;
            }
            const bool restored = descriptor
                ? archive.readValue(*descriptor, *item.fields, item.state)
                : archive.readFields(*item.fields, item.state);
            if (!restored)
                return archiveFailure(archive, diagnostic);
        }
        core::DependencyOrder::Dependencies edges{&core::defaultCpuMemoryResource()};
        edges.resize(pending.size());
        for (std::size_t i = 0; i < pending.size(); ++i)
            for (const auto& required : pending[i].entry->after)
                for (std::size_t j = 0; j < pending.size(); ++j)
                    if (pending[j].entry->type == required)
                        edges[i].push_back(j);
        core::pmr::Vector<std::size_t> order{&core::defaultCpuMemoryResource()};
        if (!core::DependencyOrder::resolve(edges, order)) {
            diagnostic = "ARP-SCENE-INITIALIZATION-DEPENDENCY-CYCLE";
            return false;
        }
        // A failed preparation leaves components restoring; transaction teardown
        // releases external resources and suppresses script destruction callbacks.
        for (auto index : order) {
            auto& item = pending[index];
            if (!item.entry->prepare(*item.component, item.state)) {
                diagnostic = "ARP-SCENE-COMPONENT-PREPARE: " + item.entry->type;
                return false;
            }
        }
        for (auto& item : pending)
            if (item.entry->publish && !item.entry->publish(*item.component)) {
                diagnostic = "ARP-SCENE-COMPONENT-PUBLISH";
                return false;
            }
        for (auto& item : pending)
            item.component->setRestoring(false);
        return true;
    } catch (...) {
        diagnostic = "ARP-SCENE-REFLECTION-RESTORE";
        return false;
    }
}

bool SceneDocumentResource::encode(const Scene& scene,
    const SceneComponentFactory& factory,
    const char* type,
    const core::pmr::String& id,
    core::pmr::String& yaml,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!type
        || (core::StringView(type) != "arp.scene-asset"
            && core::StringView(type) != "arp.scene-prefab")
        || !resource::isResourceUuid(id) || !initializeSceneAuthoringReflection()) {
        diagnostic = "ARP-SCENE-ASSET-ENCODE-CONFIG";
        return false;
    }
    try {
        Node root = Node::mapping();
        root["type"] = type;
        root["contractVersion"] = 2;
        root["resourceId"] = core::toStdString(id);
        root["name"] = core::toStdString(scene.name());
        root["objects"] = Node::sequence();
        core::pmr::Vector<SceneObjectBinding> bindings{&core::defaultCpuMemoryResource()};
        for (const auto& object : scene.objects())
            bindings.push_back({bindings.size() + 1, object->id()});
        SceneObjectBindings objects(bindings, scene);
        SceneReferenceContext references(objects, nullptr);
        reflection::SerializationArchive archive({}, &references);
        std::size_t count = 0;
        for (const auto& object : scene.objects()) {
            Node node = Node::mapping();
            node["id"] = static_cast<Node::integer_type>(objects.local(object->id()));
            node["parent"] = static_cast<Node::integer_type>(
                object->parentId() == scene.rootId() ? 0 : objects.local(object->parentId()));
            if (!archive.writeFields(reflection::object(SceneObjectAuthoringProperties{
                                         object->name(), object->active()}),
                    node["properties"])
                || !archive.writeFields(
                    reflection::object(object->local_transform()), node["transform"]))
                return archiveFailure(archive, diagnostic);
            node["components"] = Node::sequence();

            for (const auto& component : object->components()) {
                if (dynamic_cast<const TransformComponent*>(component.get()))
                    continue;
                const auto* entry = factory.persistence(*component);
                if (!entry || ++count > 4096) {
                    diagnostic = "ARP-SCENE-COMPONENT-FACTORY-OR-BUDGET";
                    return false;
                }
                Node data = Node::mapping();
                data["type"] = core::toStdString(entry->type);
                data["schemaVersion"] = 1;
                data["enabled"] = component->enabled();
                data["configuration"] = Node();
                if (entry->configuration
                    && !archive.writeFields(
                        entry->configuration(component.get()), data["configuration"]))
                    return archiveFailure(archive, diagnostic);
                const auto descriptor = entry->descriptor ? entry->descriptor(*component) : nullptr;
                if (entry->descriptor && !descriptor) {
                    diagnostic = "ARP-SCENE-FIELD-DESCRIPTOR";
                    return false;
                }
                const auto state = entry->state(*component);
                if (entry->validateState && !entry->validateState(state)) {
                    diagnostic = "ARP-SCENE-COMPONENT-INVALID-STATE: " + entry->type;
                    return false;
                }
                const bool written = descriptor
                    ? archive.writeValue(*descriptor, state, data["fields"])
                    : archive.writeFields(state, data["fields"]);
                if (!written)
                    return archiveFailure(archive, diagnostic);
                node["components"].get_value_ref<Node::sequence_type&>().push_back(std::move(data));
            }
            root["objects"].get_value_ref<Node::sequence_type&>().push_back(std::move(node));
        }
        yaml = Y::serialize(root);
        return true;
    } catch (...) {
        diagnostic = "ARP-SCENE-REFLECTION-ENCODE";
        return false;
    }
}
} // namespace arp::scene
