#include "scene/script_component_module.h"

namespace arp::scriptvm {
core::pmr::String ScriptComponentModule::compose(const core::pmr::String& source)
{
    return core::pmr::String("# ARP ScriptComponent SDK contract 1\n"
                             "class ScriptComponent:\n"
                             "    game_object: GameObject\n"
                             "    component: Component\n\n")
        + source;
}

bool ScriptComponentModule::load(const core::pmr::String& source,
    const core::pmr::String& moduleId,
    const core::pmr::String& resourceId,
    std::uint32_t schemaVersion,
    ModuleRegistry& modules,
    const core::pmr::Vector<core::pmr::String>& dependencies,
    AotExecutionBudgetV5 budget,
    std::uint32_t maximum,
    const AotPackageDescriptorV5* nativePackage)
{
    if (!types_.empty() || resourceId.empty() || !schemaVersion || !maximum)
        return fail("ARP-SCRIPT-COMPONENT-MODULE-CONTRACT");
    auto registry = core::makeShared<ModuleRegistry>();
    for (const auto& dependency : dependencies) {
        auto exports = modules.loadModule(dependency);
        if (!exports || !registry->installPluginModule(dependency, std::move(exports)))
            return fail("ARP-SCRIPT-COMPONENT-DEPENDENCY");
    }
    session_->retainModules(registry);
    auto composed = compose(source);
    auto parsed = ScriptFrontend::compile(composed, moduleId + ".arp", moduleId, registry.get());
    if (!parsed.ok()) {
        diagnostic_ = parsed.error.message;
        return false;
    }
    if (nativePackage) {
        if (!session_->bindNative(*nativePackage, moduleId.c_str(), *registry, composed)) {
            diagnostic_ = session_->diagnostic();
            return false;
        }
    } else if (!session_->build(composed, moduleId, *registry, dependencies, budget, "")) {
        diagnostic_ = session_->diagnostic();
        return false;
    }
    const auto& declarations = parsed.module.syntax->decls;
    core::pmr::Map<core::pmr::String, const reflection::class_info*> importedTypes;
    for (const auto& statement : declarations) {
        if (statement->kind != StmtKind::ImportDecl || statement->importIsModuleImport)
            continue;
        auto exports = registry->loadModule(statement->importModule);
        if (!exports)
            continue;
        for (const auto& imported : statement->importNames) {
            const auto found = exports->symbols.find(imported.name);
            if (found != exports->symbols.end() && found->second.is<ReflectedTypeBinding>())
                importedTypes[imported.alias.empty() ? imported.name : imported.alias] =
                    found->second.ptr<ReflectedTypeBinding>()->classInfo;
        }
    }
    const auto resolveType = [&](const auto& self, const TypePtr& original) -> TypePtr {
        if (!original)
            return {};
        auto result = core::makeShared<TypeNode>(*original);
        const auto found = importedTypes.find(original->name);
        if (found != importedTypes.end())
            result->reflectedClass = found->second;
        if (original->elem)
            result->elem = self(self, original->elem);
        return result;
    };
    const auto declaration = [&](const core::pmr::String& name) -> const Stmt* {
        for (const auto& value : declarations)
            if (value->kind == StmtKind::ClassDecl && value->className == name)
                return value.get();
        return nullptr;
    };
    const auto isComponent = [&](const Stmt& value) {
        auto* base = &value;
        while (base && !base->baseClassName.empty()) {
            if (base->baseClassName == "ScriptComponent")
                return true;
            base = declaration(base->baseClassName);
        }
        return false;
    };
    for (const auto& value : declarations) {
        if (value->kind != StmtKind::ClassDecl || !isComponent(*value))
            continue;
        auto type = core::makeShared<Type>();
        type->name = value->className;
        core::pmr::Vector<const Stmt*> chain;
        for (const auto* item = value.get(); item; item = declaration(item->baseClassName))
            chain.push_back(item);
        core::pmr::Map<core::pmr::String, const Stmt*> methods;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            for (const auto& field : (*it)->fields) {
                type->fieldNames.push_back(field.name);
                type->fields.push_back({field.name, resolveType(resolveType, field.type)});
            }
            for (const auto& method : (*it)->methods)
                methods[method->funcName] = *it;
        }
        const char* names[] = {"on_enable",
            "on_start",
            "on_update",
            "on_fixed_update",
            "on_late_update",
            "on_disable",
            "on_destroy"};
        for (const auto* name : names) {
            const auto found = methods.find(core::pmr::String(name));
            if (found == methods.end())
                continue;
            const Stmt* method = nullptr;
            for (const auto& candidate : found->second->methods)
                if (candidate->funcName == name)
                    method = candidate.get();
            const bool delta = core::StringView(name) == "on_update"
                || core::StringView(name) == "on_fixed_update"
                || core::StringView(name) == "on_late_update";
            if (!method || method->isGenerator || method->variadicParam
                || method->paramNames.size() != (delta ? 2u : 1u)
                || (delta && (!method->paramTypes[1] || method->paramTypes[1]->name != "float")))
                return fail("ARP-SCRIPT-COMPONENT-HOOK-SIGNATURE");
            Hook hook;
            hook.name = name;
            hook.delta = delta;
            if (!session_->resolve(
                    "arp-fn:" + moduleId + "/" + found->second->className + "." + name, hook.entry))
                return fail("ARP-SCRIPT-COMPONENT-HOOK-ENTRY");
            type->hooks.push_back(std::move(hook));
        }
        const char* physicsNames[] = {
            "on_collision_enter", "on_collision_exit", "on_trigger_enter", "on_trigger_exit"};
        for (const auto* name : physicsNames) {
            const auto found = methods.find(core::pmr::String(name));
            if (found == methods.end())
                continue;
            const Stmt* method = nullptr;
            for (const auto& candidate : found->second->methods)
                if (candidate->funcName == name)
                    method = candidate.get();
            if (!method || method->isGenerator || method->variadicParam
                || method->paramNames.size() != 2 || method->paramTypes.size() != 2
                || !method->paramTypes[1] || method->paramTypes[1]->name != "GameObject")
                return fail("ARP-SCRIPT-COMPONENT-PHYSICS-HOOK-SIGNATURE");
            PhysicsHook hook;
            hook.name = name;
            if (!session_->resolve(
                    "arp-fn:" + moduleId + "/" + found->second->className + "." + name, hook.entry))
                return fail("ARP-SCRIPT-COMPONENT-PHYSICS-HOOK-ENTRY");
            type->physicsHooks.push_back(std::move(hook));
        }
        for (auto* item : chain)
            for (const auto& method : item->methods)
                if (method->isConstructor) {
                    if (type->hasConstructor)
                        continue;
                    if (method->paramNames.size() != 1 || method->isGenerator)
                        return fail("ARP-SCRIPT-COMPONENT-CONSTRUCTOR-SIGNATURE");
                    if (!session_->resolve(
                            "arp-fn:" + moduleId + "/" + item->className + "." + method->funcName,
                            type->constructor))
                        return fail("ARP-SCRIPT-COMPONENT-CONSTRUCTOR-ENTRY");
                    type->hasConstructor = true;
                }
        const auto event = methods.find(core::pmr::String("on_event"));
        if (event != methods.end()) {
            for (const auto& method : event->second->methods)
                if (method->funcName == "on_event") {
                    if (method->paramNames.size() != 3 || method->isGenerator
                        || method->variadicParam || !method->paramTypes[1]
                        || method->paramTypes[1]->name != "string" || !method->paramTypes[2]
                        || method->paramTypes[2]->name != "any")
                        return fail("ARP-SCRIPT-COMPONENT-EVENT-SIGNATURE");
                }
            if (!session_->resolve(
                    "arp-fn:" + moduleId + "/" + event->second->className + ".on_event",
                    type->event))
                return fail("ARP-SCRIPT-COMPONENT-EVENT-ENTRY");
            type->hasEvent = true;
        }
        types_.push_back(std::move(type));
    }
    if (types_.empty())
        return fail("ARP-SCRIPT-COMPONENT-NO-TYPES");
    resourceId_ = resourceId;
    schemaVersion_ = schemaVersion;
    counters_->maximum = maximum;
    return true;
}

const ScriptComponentModule::Type* ScriptComponentModule::find(const core::pmr::String& name) const
{
    for (const auto& type : types_)
        if (type->name == name)
            return type.get();
    return nullptr;
}

bool ScriptComponentModule::bind(scene::Scene& scene)
{
    auto registration = core::makeShared<ScriptComponentModule>(*this);
    core::pmr::Vector<core::pmr::String> names;
    for (const auto& type : types_)
        names.push_back(type->name);
    if (!scene.registerScriptComponentTypes(
            resourceId_,
            names,
            [registration](scene::GameObject& object, const core::pmr::String& type) {
                return registration->create(object, type);
            },
            session_.get()))
        return fail("ARP-SCRIPT-COMPONENT-REGISTRATION-CONFLICT");
    return true;
}

core::UniquePtr<scene::Component> ScriptComponentModule::create(
    scene::GameObject& owner, const core::pmr::String& name, bool runConstructor)
{
    core::SharedPtr<Type> type;
    for (const auto& candidate : types_)
        if (candidate->name == name)
            type = candidate;
    if (!type)
        return {};
    if (!owner.get_component(name).is_undefined()) {
        fail("ARP-SCRIPT-COMPONENT-DUPLICATE-TYPE");
        return {};
    }
    for (const auto& pending : counters_->constructing)
        if (pending.owner == &owner && pending.type == type.get()) {
            fail("ARP-SCRIPT-COMPONENT-REENTRANT-CONSTRUCTION");
            return {};
        }
    if (counters_->live == counters_->maximum) {
        fail("ARP-SCRIPT-COMPONENT-CAPACITY");
        return {};
    }

    struct InstanceLease {
        explicit InstanceLease(core::SharedPtr<Counters> value) : counters(std::move(value))
        {
            ++counters->live;
        }

        ~InstanceLease() { --counters->live; }

        core::SharedPtr<Counters> counters;
    };

    auto lease = core::makeShared<InstanceLease>(counters_);
    counters_->constructing.push_back({&owner, type.get()});

    struct ConstructionLease {
        Counters& counters;

        ~ConstructionLease() { counters.constructing.pop_back(); }
    } construction{*counters_};

    auto made = ScriptSemanticRuntime::makeScriptInstance(type->name, type->fieldNames);
    if (!made.ok()) {
        diagnostic_ = made.diagnostic.message;
        return {};
    }
    auto receiver = std::move(made.value);
    auto* instance = receiver.ptr<reflection::instance>();
    instance->slot("game_object") = owner.nativeReflectionObject();
    if (runConstructor && type->hasConstructor) {
        Value result = Value::undefined();
        if (!session_->invoke(type->constructor, {receiver}, result)) {
            instance->invalidate();
            diagnostic_ = session_->diagnostic();
            return {};
        }
    }
    core::pmr::Vector<Value> arguments;
    arguments.reserve(2);
    // The callback owns the shared module and type, never the module binder.
    // Thus components remain safe if host-side registration is released first.
    auto invoke = [session = session_, type, receiver, lease, arguments = std::move(arguments)](
                      const char* name, const scene::SceneUpdateContext& context) mutable {
        bool ok = true;
        for (const auto& hook : type->hooks)
            if (hook.name == name) {
                arguments.clear();
                arguments.push_back(receiver);
                if (hook.delta)
                    arguments.push_back(makeFloat(context.deltaSeconds));
                Value result = Value::undefined();
                ok = session->invoke(hook.entry, arguments, result);
                break;
            }
        return ok;
    };
    auto component =
        core::makeUnique<scene::ScriptBehaviorComponent>(type->name, receiver, std::move(invoke));
    component->setDiagnosticProvider([session = session_]() {
        return session->diagnostic();
    });
    component->setScriptIdentity(resourceId_, schemaVersion_);
    if (type->hasEvent) {
        core::pmr::Vector<Value> eventArgs;
        eventArgs.reserve(3);
        component->setEventInvoke(
            [session = session_, type, receiver, eventArgs = std::move(eventArgs)](
                const scene::SceneEvent& event) mutable {
                eventArgs.clear();
                eventArgs.push_back(receiver);
                eventArgs.push_back(makeString(event.name));
                eventArgs.push_back(event.payload);
                Value result = Value::undefined();
                return session->invoke(type->event, eventArgs, result);
            });
    }
    if (!type->physicsHooks.empty()) {
        core::pmr::Vector<Value> physicsArgs;
        physicsArgs.reserve(2);
        component->setPhysicsEventInvoke(
            [session = session_, type, receiver, physicsArgs = std::move(physicsArgs)](
                const char* name, scene::GameObject* other) mutable {
                if (other == nullptr)
                    return false;
                for (const auto& hook : type->physicsHooks)
                    if (hook.name == name) {
                        physicsArgs.clear();
                        physicsArgs.push_back(receiver);
                        physicsArgs.push_back(other->nativeReflectionObject());
                        Value result = Value::undefined();
                        return session->invoke(hook.entry, physicsArgs, result);
                    }
                return true;
            });
    }
    instance->slot("component") = component->nativeReflectionObject();
    return component;
}
} // namespace arp::scriptvm
