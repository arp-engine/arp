#include "scene/script_component_persistence.h"

#include "scriptvm/runtime/serialization_descriptor.h"

#include <stdexcept>

namespace arp::reflection::generated {
bool register_script_component_authoring_reflection(arp::scriptvm::ModuleRegistration*);
}

namespace arp::scriptvm {
using Descriptor = reflection::SerializationDescriptor;

core::SharedPtr<Descriptor> scriptComponentFieldDescriptor(
    const ScriptComponentModule& module, const ScriptComponentModule::Type& type)
{
    auto record = core::makeShared<Descriptor>();
    record->kind = Descriptor::Kind::Record;
    record->get = [](const reflection::object& state, reflection::NameView name) {
        if (state.is<reflection::instance>())
            return state.ptr<reflection::instance>()->__getitem__(name);
        if (!state.is<ScriptComponentInspectionValues>())
            throw std::invalid_argument("ARP-SCRIPT-COMPONENT-FIELD-STORAGE");
        const auto& fields = state.ptr<ScriptComponentInspectionValues>()->fields;
        const auto found = fields.find(core::pmr::String(name));
        return found == fields.end() ? reflection::object::undefined() : found->second;
    };
    record->set = [](const reflection::object& state,
                      reflection::NameView name,
                      const reflection::object& value) {
        if (state.is<reflection::instance>()) {
            state.ptr<reflection::instance>()->slot(name) = value;
            return;
        }
        if (!state.is<ScriptComponentInspectionValues>())
            throw std::invalid_argument("ARP-SCRIPT-COMPONENT-FIELD-STORAGE");
        state.ptr<ScriptComponentInspectionValues>()->fields[core::pmr::String(name)] = value;
    };
    const auto supported = [&](const auto& self, const TypeNode& field) -> bool {
        if (field.kind == TypeKind::Optional || field.kind == TypeKind::List)
            return field.elem && self(self, *field.elem);
        if (field.kind != TypeKind::Class)
            return true;
        if (field.reflectedClass)
            return field.reflectedClass == reflection::class_of<scene::GameObject>()
                || field.reflectedClass == reflection::class_of<scene::Component>()
                || reflection::class_of<scene::Component>()->is_base_of(field.reflectedClass);
        return module.find(field.name) != nullptr;
    };
    for (const auto& field : type.fields) {
        if (field.name == "game_object" || field.name == "component")
            continue;
        if (!field.type || !supported(supported, *field.type))
            return {};
        auto descriptor = serializationDescriptor(*field.type);
        if (!descriptor)
            return {};
        record->fields.push_back({field.name, std::move(descriptor)});
    }
    return record;
}

namespace {
// Immutable module membership; sessions/counters remain owned by the existing
// ScriptComponentModule. This is registration composition, not a script host.
class PersistenceModules final {
public:
    explicit PersistenceModules(core::pmr::Vector<core::SharedPtr<ScriptComponentModule>> modules)
        : modules_(std::move(modules))
    {}

    ScriptComponentModule* find(const ScriptComponentIdentity& identity) const
    {
        for (const auto& module : modules_)
            if (module->resourceId() == identity.scriptResourceId
                && module->schemaVersion() == identity.moduleSchemaVersion
                && module->find(identity.scriptType))
                return module.get();
        return nullptr;
    }

    ScriptComponentModule* find(const scene::Component& value) const
    {
        const auto* component = dynamic_cast<const scene::ScriptBehaviorComponent*>(&value);
        if (!component)
            return nullptr;
        for (const auto& module : modules_)
            if (module->resourceId() == component->scriptResourceId()
                && module->schemaVersion() == component->schemaVersion()
                && module->find(component->scriptTypeName()))
                return module.get();
        return nullptr;
    }

    bool bind(scene::Scene& scene) const
    {
        if (modules_.size() == 1)
            return modules_.front()->bind(scene);
        // An existing bare alias must never survive into an ambiguous set.
        // Do not replace it: the enclosing Scene restoration owns rollback.
        for (const auto& module : modules_)
            for (const auto& type : module->types())
                if (typeCount(type->name) > 1 && scene.hasRegisteredScriptComponentType(type->name))
                    return false;
        for (const auto& module : modules_) {
            core::pmr::Vector<core::pmr::String> qualified, unqualified;
            for (const auto& type : module->types()) {
                qualified.push_back(type->name);
                if (typeCount(type->name) == 1)
                    unqualified.push_back(type->name);
            }
            if (!module->bind(
                    scene, qualified, ScriptComponentModule::RegistrationScope::ResourceQualified)
                || (!unqualified.empty()
                    && !module->bind(
                        scene, unqualified, ScriptComponentModule::RegistrationScope::Unqualified)))
                return false;
        }
        return true;
    }

private:
    std::size_t typeCount(const core::pmr::String& name) const
    {
        std::size_t matches = 0;
        for (const auto& module : modules_)
            matches += module->find(name) != nullptr;
        return matches;
    }

    const core::pmr::Vector<core::SharedPtr<ScriptComponentModule>> modules_;
};
} // namespace

bool registerScriptComponentPersistence(
    scene::SceneComponentFactory& factory, core::SharedPtr<ScriptComponentModule> module)
{
    core::pmr::Vector<core::SharedPtr<ScriptComponentModule>> modules;
    modules.push_back(std::move(module));
    core::pmr::String diagnostic;
    return registerScriptComponentPersistence(factory, std::move(modules), 1, diagnostic);
}

bool registerScriptComponentPersistence(scene::SceneComponentFactory& factory,
    core::pmr::Vector<core::SharedPtr<ScriptComponentModule>> modules,
    std::size_t maxModules,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](const char* text) {
        diagnostic = text;
        return false;
    };
    static const bool registered =
        reflection::generated::register_script_component_authoring_reflection(nullptr);
    if (!registered || !maxModules || modules.empty() || modules.size() > maxModules)
        return fail("ARP-SCRIPT-COMPONENT-PERSISTENCE-BUDGET-OR-SCHEMA");
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto& module = modules[i];
        if (!module || module->resourceId().empty() || !module->schemaVersion()
            || module->types().empty())
            return fail("ARP-SCRIPT-COMPONENT-PERSISTENCE-MODULE");
        for (std::size_t j = 0; j < i; ++j)
            if (modules[j]->resourceId() == module->resourceId())
                return fail("ARP-SCRIPT-COMPONENT-PERSISTENCE-DUPLICATE-MODULE");
        for (const auto& type : module->types())
            if (module->qualifiedTypeName(type->name).empty())
                return fail("ARP-SCRIPT-COMPONENT-PERSISTENCE-TYPE-IDENTITY");
    }
    const auto snapshot = core::makeShared<PersistenceModules>(std::move(modules));
    scene::SceneComponentFactory::Persistence entry;
    entry.type = "arp.script-component/1";
    entry.accepts = [snapshot](const scene::Component& value) {
        return snapshot->find(value) != nullptr;
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
    entry.construct = [snapshot](scene::GameObject& owner,
                          const reflection::object& value) -> core::UniquePtr<scene::Component> {
        if (!value.is<ScriptComponentIdentity>())
            return {};
        const auto& identity = value.cast<const ScriptComponentIdentity&>();
        auto* module = snapshot->find(identity);
        return module ? module->create(owner, identity.scriptType, false) : nullptr;
    };
    entry.state = [](const scene::Component& component) {
        return component.scriptInstance();
    };
    entry.descriptor = [snapshot](
                           const scene::Component& component) -> core::SharedPtr<Descriptor> {
        const auto* module = snapshot->find(component);
        return module
            ? scriptComponentFieldDescriptor(*module, *module->find(component.scriptTypeName()))
            : nullptr;
    };
    entry.inspectConfiguration = [snapshot](const reflection::object& value,
                                     scene::SceneComponentFactory::Inspection& output,
                                     core::pmr::String& error) {
        if (!value.is<ScriptComponentIdentity>()) {
            error = "ARP-SCRIPT-COMPONENT-INSPECTION-CONFIGURATION";
            return false;
        }
        const auto& identity = value.cast<const ScriptComponentIdentity&>();
        const auto* module = snapshot->find(identity);
        if (!module) {
            error = "ARP-SCRIPT-COMPONENT-INSPECTION-MODULE-TYPE-OR-SCHEMA";
            return false;
        }
        scene::SceneComponentFactory::Inspection candidate;
        candidate.descriptor =
            scriptComponentFieldDescriptor(*module, *module->find(identity.scriptType));
        if (!candidate.descriptor) {
            error = "ARP-SCRIPT-COMPONENT-INSPECTION-FIELD-TYPE";
            return false;
        }
        candidate.state = reflection::object(ScriptComponentInspectionValues{});
        candidate.componentType = identity.scriptType;
        output = std::move(candidate);
        return true;
    };
    entry.prepare = [snapshot](scene::Component& component, const reflection::object&) {
        return snapshot->find(component) != nullptr;
    };
    entry.publish = [snapshot](scene::Component& component) {
        return component.gameObject() && component.gameObject()->scene()
            && snapshot->bind(*component.gameObject()->scene());
    };
    if (!factory.addPersistence(std::move(entry)))
        return fail("ARP-SCRIPT-COMPONENT-PERSISTENCE-REGISTRATION");
    return true;
}
} // namespace arp::scriptvm
