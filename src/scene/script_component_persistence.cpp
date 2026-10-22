#include "scene/script_component_persistence.h"

#include "scriptvm/runtime/serialization_descriptor.h"

namespace arp::reflection::generated {
bool register_script_component_authoring_reflection(arp::scriptvm::ModuleRegistration*);
}

namespace arp::scriptvm {
using Descriptor = reflection::SerializationDescriptor;

bool registerScriptComponentPersistence(
    scene::SceneComponentFactory& factory, core::SharedPtr<ScriptComponentModule> module)
{
    static const bool registered =
        reflection::generated::register_script_component_authoring_reflection(nullptr);
    if (!registered || !module)
        return false;
    scene::SceneComponentFactory::Persistence entry;
    entry.type = "arp.script-component/1";
    entry.accepts = [module](const scene::Component& value) {
        const auto* component = dynamic_cast<const scene::ScriptBehaviorComponent*>(&value);
        return component && component->scriptResourceId() == module->resourceId();
    };
    entry.configuration = [](const scene::Component* value) {
        ScriptComponentIdentity identity;
        if (const auto* component = dynamic_cast<const scene::ScriptBehaviorComponent*>(value)) {
            identity.scriptResourceId = component->scriptResourceId();
            identity.scriptType = component->scriptTypeName();
            identity.moduleSchemaVersion = component->schemaVersion();
        }
        return reflection::object(std::move(identity));
    };
    entry.construct = [module](scene::GameObject& owner,
                          const reflection::object& value) -> core::UniquePtr<scene::Component> {
        const auto& identity = value.cast<const ScriptComponentIdentity&>();
        if (identity.scriptResourceId != module->resourceId()
            || identity.moduleSchemaVersion != module->schemaVersion()
            || !module->find(identity.scriptType))
            return {};
        return module->create(owner, identity.scriptType, false);
    };
    entry.state = [](const scene::Component& component) {
        return component.scriptInstance();
    };
    entry.descriptor = [module](const scene::Component& component) -> core::SharedPtr<Descriptor> {
        const auto* type = module->find(component.scriptTypeName());
        if (!type)
            return {};
        auto record = core::makeShared<Descriptor>();
        record->kind = Descriptor::Kind::Record;
        record->get = [](const reflection::object& instance, reflection::NameView name) {
            return instance.ptr<reflection::instance>()->__getitem__(name);
        };
        record->set = [](const reflection::object& instance,
                          reflection::NameView name,
                          const reflection::object& value) {
            instance.ptr<reflection::instance>()->slot(name) = value;
        };
        for (const auto& field : type->fields) {
            if (field.name == "game_object" || field.name == "component")
                continue;
            if (!field.type)
                return {};
            auto descriptor = serializationDescriptor(*field.type);
            if (!descriptor)
                return {};
            record->fields.push_back({field.name, std::move(descriptor)});
        }
        return record;
    };
    entry.prepare = [module](scene::Component& component, const reflection::object&) {
        const auto* script = dynamic_cast<const scene::ScriptBehaviorComponent*>(&component);
        return script && script->schemaVersion() == module->schemaVersion();
    };
    entry.publish = [module](scene::Component& component) {
        return module->bind(*component.gameObject()->scene());
    };
    return factory.addPersistence(std::move(entry));
}
} // namespace arp::scriptvm
