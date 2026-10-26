#include "reflection/instance.h"
#include "scene/script_component_persistence.h"

#include <stdexcept>

namespace arp::scene {

ScriptBehaviorComponent& Scene::IdentityRestoration::createScriptComponent(GameObjectId owner,
    scriptvm::ScriptComponentModule& module,
    const scriptvm::ScriptComponentIdentity& identity,
    std::size_t componentIndex)
{
    return restoreScriptComponent(
        owner, ComponentId::fromValue(nextComponent_), module, identity, componentIndex);
}

ScriptBehaviorComponent& Scene::IdentityRestoration::restoreScriptComponent(GameObjectId ownerId,
    ComponentId id,
    scriptvm::ScriptComponentModule& module,
    const scriptvm::ScriptComponentIdentity& identity,
    std::size_t componentIndex)
{
    requirePreparing();
    try {
        auto& owner = validateComponentRequest(ownerId, id, componentIndex);
        if (identity.scriptResourceId != module.resourceId()
            || identity.moduleSchemaVersion != module.schemaVersion()
            || !module.find(identity.scriptType))
            throw std::invalid_argument("ARP-SCENE-IDENTITY-SCRIPT-IDENTITY");
        // This private module path is available only to this Passive transaction.
        // A live source may coexist during validation, but prepare rejects it.
        auto component = module.createInstance(owner, identity.scriptType, false, true);
        auto* script = dynamic_cast<ScriptBehaviorComponent*>(component.get());
        if (!script || script->id().valid() || script->gameObject()
            || script->scriptResourceId() != identity.scriptResourceId
            || script->schemaVersion() != identity.moduleSchemaVersion
            || identity.scriptType != script->scriptTypeName())
            throw std::invalid_argument("ARP-SCENE-IDENTITY-SCRIPT-CANDIDATE");
        auto receiver = script->scriptInstance();
        auto* instance =
            receiver.is<reflection::instance>() ? receiver.ptr<reflection::instance>() : nullptr;
        if (!instance || !instance->alive())
            throw std::invalid_argument("ARP-SCENE-IDENTITY-SCRIPT-CANDIDATE");
        // All binding writes are preparation work. Preserve the canonical native
        // reference already stored by the module; rebinding would orphan copies.
        instance->slot("game_object") = owner.nativeReflectionObject();
        instance->slot("component") = script->nativeReflectionObject();
        stageComponent(owner, id, componentIndex, std::move(component));
        return *script;
    } catch (...) {
        failed_ = true;
        throw;
    }
}

} // namespace arp::scene
