#include "scene/script_behavior_component.h"

#include "reflection/instance.h"

namespace arp::scene {

void ScriptBehaviorComponent::invoke(const char* hook, const SceneUpdateContext& context)
{
    if (failed_ || !invoke_)
        return;
    if (!invoke_(hook, context)) {
        failed_ = true;
        failureHook_ = hook;
        if (diagnosticProvider_)
            failureDiagnostic_ = diagnosticProvider_();
    }
}

bool ScriptBehaviorComponent::dispatchPhysicsEvent(const char* hook, GameObject* other)
{
    if (failed_ || !physicsEventInvoke_ || !physicsEventInvoke_(hook, other)) {
        if (!failed_ && physicsEventInvoke_) {
            failed_ = true;
            failureHook_ = hook;
            if (diagnosticProvider_)
                failureDiagnostic_ = diagnosticProvider_();
        }
        return !failed_;
    }
    return true;
}

void ScriptBehaviorComponent::onEnable(const SceneUpdateContext& context)
{
    invoke("on_enable", context);
}

void ScriptBehaviorComponent::onStart(const SceneUpdateContext& context)
{
    invoke("on_start", context);
}

void ScriptBehaviorComponent::onUpdate(const SceneUpdateContext& context)
{
    invoke("on_update", context);
}

void ScriptBehaviorComponent::onFixedUpdate(const SceneUpdateContext& context)
{
    invoke("on_fixed_update", context);
}

void ScriptBehaviorComponent::onLateUpdate(const SceneUpdateContext& context)
{
    invoke("on_late_update", context);
}

void ScriptBehaviorComponent::onDisable(const SceneUpdateContext& context)
{
    invoke("on_disable", context);
}

void ScriptBehaviorComponent::onDestroy(const SceneUpdateContext& context)
{
    invoke("on_destroy", context);
    if (instance_.is<arp::reflection::instance>()) {
        auto* instance = instance_.ptr<arp::reflection::instance>();
        instance->invalidate();
    }
}

} // namespace arp::scene
