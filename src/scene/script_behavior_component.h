#pragma once

#include "core/concurrency/callback.h"
#include "scene/component.h"
#include "scene/script_behavior_binding.h"

#include <cstdint>
#include <utility>

namespace arp::scene {

/// Bridges native component lifecycle to a script instance. The callback is
/// deliberately name-based so interpreter and AOT dispatchers share the same
/// hook contract. Hooks cannot suspend: returning false fails the current
/// world deterministically and prevents later hooks from running.
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ScriptBehaviorComponent final : public Component {
public:
    const char* scriptTypeName() const override
    {
        return type_.empty() ? "ScriptBehaviorComponent" : type_.c_str();
    }

    using Invoke = core::SharedCallback<bool(const char*, const SceneUpdateContext&)>;
    using InvokeEvent = core::SharedCallback<bool(const SceneEvent&)>;
    using InvokePhysicsEvent = core::SharedCallback<bool(const char*, GameObject*)>;

    explicit ScriptBehaviorComponent(Invoke invoke = {}) : invoke_(std::move(invoke)) {}

    ScriptBehaviorComponent(
        arp::core::pmr::String type, arp::reflection::object instance, Invoke invoke)
        : invoke_(std::move(invoke)), type_(std::move(type)), instance_(std::move(instance))
    {}

    arp::reflection::object scriptInstance() const override { return instance_; }

    void setScriptIdentity(arp::core::pmr::String resource, std::uint32_t version)
    {
        resourceId_ = std::move(resource);
        schemaVersion_ = version;
    }

    const auto& scriptResourceId() const { return resourceId_; }

    std::uint32_t schemaVersion() const { return schemaVersion_; }

    void setEventInvoke(InvokeEvent invoke) { eventInvoke_ = std::move(invoke); }

    void setPhysicsEventInvoke(InvokePhysicsEvent invoke)
    {
        physicsEventInvoke_ = std::move(invoke);
    }

    bool dispatchPhysicsEvent(const char* hook, GameObject* other);

    void setDiagnosticProvider(core::SharedCallback<arp::core::pmr::String()> provider)
    {
        diagnosticProvider_ = std::move(provider);
    }

    const auto& failureDiagnostic() const { return failureDiagnostic_; }

    void onEvent(const SceneEvent& event) override
    {
        if (!failed_ && eventInvoke_ && !eventInvoke_(event)) {
            failed_ = true;
            failureHook_ = "on_event";
            if (diagnosticProvider_)
                failureDiagnostic_ = diagnosticProvider_();
        }
    }

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }
    AFUNC()

    bool failed() const { return failed_; }
    AFUNC()

    arp::core::pmr::String failure_hook() const
    {
        return failureHook_ != nullptr
            ? arp::core::pmr::String(failureHook_, &arp::core::defaultCpuMemoryResource())
            : arp::core::pmr::String(&arp::core::defaultCpuMemoryResource());
    }

    void onEnable(const SceneUpdateContext& context) override;
    void onStart(const SceneUpdateContext& context) override;
    void onUpdate(const SceneUpdateContext& context) override;
    void onFixedUpdate(const SceneUpdateContext& context) override;
    void onLateUpdate(const SceneUpdateContext& context) override;
    void onDisable(const SceneUpdateContext& context) override;
    void onDestroy(const SceneUpdateContext& context) override;

    const char* failureHook() const { return failureHook_; }

private:
    void invoke(const char* hook, const SceneUpdateContext& context);

    Invoke invoke_;
    InvokeEvent eventInvoke_;
    InvokePhysicsEvent physicsEventInvoke_;
    core::SharedCallback<arp::core::pmr::String()> diagnosticProvider_;
    arp::core::pmr::String failureDiagnostic_;
    bool failed_ = false;
    const char* failureHook_ = nullptr;
    arp::core::pmr::String type_;
    arp::reflection::object instance_;
    arp::core::pmr::String resourceId_;
    std::uint32_t schemaVersion_ = 0;
};

} // namespace arp::scene
