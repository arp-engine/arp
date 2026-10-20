#include "scene/script_component_codec.h"

namespace arp::scriptvm {
using Y = core::YamlContract;
using Node = Y::Node;

namespace {
bool fail(core::pmr::String& diagnostic, const char* message)
{
    diagnostic = message;
    return false;
}

bool reserved(const core::pmr::String& name)
{
    return name == "game_object" || name == "component";
}
} // namespace

bool ScriptComponentCodec::attach(scene::GameObject& object,
    const Node& node,
    const scene::SceneObjectBindings&,
    resource::ResourceManager&,
    core::pmr::String& diagnostic) const
{
    try {
        if (Y::text(node, "scriptResourceId") != module_->resourceId()
            || Y::uint32(node, "schemaVersion") != module_->schemaVersion())
            return fail(diagnostic, "ARP-SCRIPT-COMPONENT-RESOURCE-VERSION");
        const auto name = Y::text(node, "scriptType");
        if (!module_->find(name))
            return fail(diagnostic, "ARP-SCRIPT-COMPONENT-UNKNOWN-TYPE");
        if (!module_->bind(*object.scene()))
            return fail(diagnostic, module_->diagnostic().c_str());
        const auto& fields = Y::required(node, "fields");
        if (!fields.is_mapping())
            return fail(diagnostic, "ARP-SCRIPT-COMPONENT-FIELDS");
        const bool enabled = Y::boolean(node, "enabled");
        // Deserialization restores a complete field snapshot. Constructors may
        // have external effects, so they must not run in a candidate transaction.
        auto component = module_->create(object, name, false);
        if (!component)
            return fail(diagnostic, module_->diagnostic().c_str());
        component->setEnabled(enabled);
        object.addComponent(std::move(component));
        return true;
    } catch (...) {
        return fail(diagnostic, "ARP-SCRIPT-COMPONENT-YAML");
    }
}

bool ScriptComponentCodec::restore(scene::GameObject& object,
    const Node& node,
    const scene::SceneObjectBindings& bindings,
    core::pmr::String& diagnostic) const
{
    try {
        const auto name = Y::text(node, "scriptType");
        const auto* type = module_->find(name);
        auto value = object.get_component(name);
        if (!type || !value.is<reflection::instance>())
            return fail(diagnostic, "ARP-SCRIPT-COMPONENT-RESTORE-TYPE");
        const auto& fields = Y::required(node, "fields");
        ScriptValueYaml codec(maxValues_, maxDepth_);
        core::pmr::Vector<std::pair<core::pmr::String, Value>> pending;
        for (const auto& item : fields.get_value_ref<const Node::mapping_type&>()) {
            const auto fieldName = Y::text(item.first);
            const TypeNode* fieldType = nullptr;
            for (const auto& field : type->fields)
                if (field.name == fieldName)
                    fieldType = field.type.get();
            if (!fieldType || reserved(fieldName))
                return fail(diagnostic, "ARP-SCRIPT-COMPONENT-UNKNOWN-FIELD");
            Value field;
            if (!codec.decode(*fieldType,
                    item.second,
                    field,
                    [&](const TypeNode& t, const Node& n, Value& v) {
                        return reference(t, n, v, bindings);
                    }))
                return fail(diagnostic, "ARP-SCRIPT-COMPONENT-FIELD-TYPE-OR-REFERENCE");
            pending.emplace_back(fieldName, std::move(field));
        }
        std::size_t expected = 0;
        for (const auto& field : type->fields)
            if (!reserved(field.name))
                ++expected;
        if (pending.size() != expected)
            return fail(diagnostic, "ARP-SCRIPT-COMPONENT-MISSING-FIELD");
        auto* instance = value.ptr<reflection::instance>();
        for (auto& field : pending)
            instance->slot(field.first) = std::move(field.second);
        return true;
    } catch (...) {
        return fail(diagnostic, "ARP-SCRIPT-COMPONENT-RESTORE");
    }
}

bool ScriptComponentCodec::encode(const scene::Component& c,
    const scene::SceneObjectBindings& bindings,
    Node& node,
    core::pmr::String& diagnostic) const
{
    const auto* component = dynamic_cast<const scene::ScriptBehaviorComponent*>(&c);
    if (!component || component->schemaVersion() != module_->schemaVersion())
        return fail(diagnostic, "ARP-SCRIPT-COMPONENT-ENCODE-IDENTITY");
    const auto* type = module_->find(core::pmr::String(component->scriptTypeName()));
    auto value = component->scriptInstance();
    if (!type || !value.is<reflection::instance>() || !value.ptr<reflection::instance>()->alive())
        return fail(diagnostic, "ARP-SCRIPT-COMPONENT-ENCODE-INSTANCE");
    node["scriptResourceId"] = module_->resourceId().c_str();
    node["schemaVersion"] = module_->schemaVersion();
    node["scriptType"] = type->name.c_str();
    node["enabled"] = component->enabled();
    node["fields"] = Node::mapping();
    ScriptValueYaml codec(maxValues_, maxDepth_);
    for (const auto& field : type->fields) {
        if (reserved(field.name))
            continue;
        Node encoded;
        if (!field.type
            || !codec.encode(*field.type,
                value.ptr<reflection::instance>()->__getitem__(field.name),
                encoded,
                [&](const TypeNode& t, const Value& v, Node& n) {
                    return reference(t, v, n, bindings);
                })) {
            diagnostic = "ARP-SCRIPT-COMPONENT-ENCODE-FIELD: " + type->name + "." + field.name;
            return false;
        }
        node["fields"][field.name.c_str()] = std::move(encoded);
    }
    return true;
}

bool ScriptComponentCodec::reference(const TypeNode& type,
    const Value& value,
    Node& node,
    const scene::SceneObjectBindings& bindings) const
{
    if (value.is_native_reference() && !value.native_reference_alive())
        return false;
    if (value.is_native_reference() && value.can_safe_cast<scene::GameObject*>()) {
        if (type.name != "GameObject"
            && type.reflectedClass != reflection::class_of<scene::GameObject>())
            return false;
        const auto id = bindings.local(value.cast<scene::GameObject*>()->id());
        if (!id)
            return false;
        node = Node::mapping();
        node["object"] = id;
        return true;
    }
    for (const auto& object : bindings.scene().objects()) {
        core::pmr::Map<core::pmr::String, std::uint32_t> ordinals;
        for (const auto& component : object->components()) {
            core::pmr::String name(component->scriptTypeName());
            const auto ordinal = ordinals[name]++;
            const auto receiver = component->scriptInstance();
            const bool matches = value.is<reflection::instance>() && !receiver.is_undefined()
                ? value.ptr<reflection::instance>() == receiver.ptr<reflection::instance>()
                : value.is_native_reference()
                    && component->nativeReflectionObject().native_reference_alive()
                    && value.can_safe_cast<scene::Component*>()
                    && value.cast<scene::Component*>() == component.get();
            if (!matches)
                continue;
            if (type.name != name && type.name != "Component"
                && (!type.reflectedClass || !component->nativeReflectionObject().class_()
                    || !type.reflectedClass->is_base_of(
                        component->nativeReflectionObject().class_())))
                return false;
            const auto id = bindings.local(object->id());
            if (!id)
                return false;
            node = Node::mapping();
            node["object"] = id;
            node["componentType"] = name.c_str();
            node["ordinal"] = ordinal;
            return true;
        }
    }
    return false;
}

bool ScriptComponentCodec::reference(const TypeNode& type,
    const Node& node,
    Value& value,
    const scene::SceneObjectBindings& bindings) const
{
    if (!node.is_mapping())
        return false;
    const auto* object = bindings.findObject(Y::uint64(Y::required(node, "object")));
    if (!object)
        return false;
    if (type.name == "GameObject"
        || type.reflectedClass == reflection::class_of<scene::GameObject>()) {
        if (node.contains("componentType"))
            return false;
        value = object->nativeReflectionObject();
        return true;
    }
    const auto name = Y::text(node, "componentType");
    auto ordinal = Y::uint32(node, "ordinal");
    if (type.name != "Component" && type.name != name && !type.reflectedClass)
        return false;
    for (const auto& component : object->components())
        if (name == component->scriptTypeName()) {
            if (ordinal-- != 0)
                continue;
            value = component->scriptInstance();
            if (value.is_undefined()) {
                value = component->nativeReflectionObject();
                if (!type.reflectedClass || !value.class_()
                    || !type.reflectedClass->is_base_of(value.class_()))
                    return false;
            } else if (type.name != name)
                return false;
            return true;
        }
    return false;
}
} // namespace arp::scriptvm
