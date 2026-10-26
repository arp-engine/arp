#include "scene/script_behavior_component.h"

#include "reflection/instance.h"
#include "scene/scene.h"

namespace arp::scene {

ScriptBehaviorComponent::~ScriptBehaviorComponent()
{
    // Receiver invalidation is ownership cleanup, independent of lifecycle execution.
    invalidateInstance();
}

void ScriptBehaviorComponent::invalidateInstance() noexcept
{
    if (instance_.is<arp::reflection::instance>())
        instance_.ptr<arp::reflection::instance>()->invalidate();
}

bool ScriptBehaviorComponent::rejectPassiveExecution(const char* hook)
{
    const auto* owner = gameObject();
    if (!owner || !owner->scene() || owner->scene()->executionMode() != SceneExecutionMode::Passive)
        return false;
    failed_ = true;
    failureHook_ = hook;
    failureDiagnostic_ = "ARP-SCENE-PASSIVE-EXECUTION";
    return true;
}

void ScriptBehaviorComponent::invoke(const char* hook, const SceneUpdateContext& context)
{
    if (rejectPassiveExecution(hook))
        return;
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
    if (rejectPassiveExecution(hook))
        return false;
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
    if (!restoring())
        invoke("on_destroy", context);
    invalidateInstance();
}

} // namespace arp::scene
