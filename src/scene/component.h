#pragma once

#include "reflection/object.h"
#include "reflection/reflection_macros.h"
#include "scene/ids.h"
#include "scene/render_extract.h"

#include <utility>

namespace arp::scene {

class GameObject;

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SceneUpdateContext {
    APROP(Runtime)
    float deltaSeconds = 0.0f;
    APROP(Runtime)
    uint64_t frameIndex = 0;
    APROP(Runtime)
    float unscaledDeltaSeconds = 0;
};

struct SceneEvent {
    GameObjectId target;
    arp::core::pmr::String name;
    arp::reflection::object payload;
};

struct ScriptPhysicsEvent {
    GameObjectId target;
    GameObjectId other;
    const char* hook = nullptr;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ComponentEditorProperties {
    APROP(Serialize, Editor)
    bool enabled = true;
};

ACLASS(Name = "Component", Ownership = Engine, Lifecycle = Explicit)

class Component {
public:
    virtual ~Component();

    virtual const char* scriptTypeName() const { return "Component"; }

    ComponentId id() const { return id_; }

    GameObjectId ownerId() const { return ownerId_; }

    AFUNC()

    std::uint64_t component_id() const { return id_.value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId_.value(); }

    bool enabled() const { return enabled_; }

    bool pendingDestroy() const { return pendingDestroy_; }

    GameObject* gameObject() const { return owner_; }

    virtual arp::reflection::object scriptInstance() const { return {}; }
    AFUNC()

    bool is_enabled() const { return enabled(); }

    void setEnabled(bool enabled) { enabled_ = enabled; }
    AFUNC()

    void set_enabled(bool enabled) { setEnabled(enabled); }

    virtual arp::reflection::object editorProperties() const
    {
        return arp::reflection::object(ComponentEditorProperties{enabled_});
    }

    virtual bool applyEditorProperties(const arp::reflection::object& value)
    {
        if (!value.is<ComponentEditorProperties>())
            return false;
        setEnabled(value.cast<const ComponentEditorProperties&>().enabled);
        return true;
    }

    virtual arp::reflection::object editorAuthoringProperties() const { return {}; }

    virtual bool applyEditorAuthoringProperties(const arp::reflection::object& value)
    {
        (void)value;
        return false;
    }

    // Native return boxing uses this canonical object so every script copy
    // shares the same invalidation control as the engine-owned registry entry.
    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    // Rebind through the runtime concrete type after a component has been
    // created by the domain factory. The script-visible object remains the
    // same engine-owned canonical reference, but calls dispatch to the
    // generated derived class instead of the static Component base.
    void bindNativeReflectionObject();

    virtual void onEnable(const SceneUpdateContext&) {}

    virtual void onStart(const SceneUpdateContext&) {}

    virtual void onUpdate(const SceneUpdateContext&) {}

    virtual void onFixedUpdate(const SceneUpdateContext&) {}

    virtual void onEvent(const SceneEvent&) {}

    virtual void onLateUpdate(const SceneUpdateContext&) {}

    virtual void onDisable(const SceneUpdateContext&) {}

    virtual void onDestroy(const SceneUpdateContext&) {}

    /// Polymorphic render extraction hook.
    ///
    /// Each renderable component overrides this to push a RenderExtractPayload
    /// into `sink`, self-describing its render data (mesh handle, resourceId,
    /// rect/color, text, ...). Extractors walk the scene graph and dispatch
    /// `extract()` on every active+enabled component instead of performing
    /// `dynamic_cast` type checks on concrete component types.
    ///
    /// Default implementation is a no-op: data/logic components that do not
    /// contribute render data (e.g. TerrainComponent heightfield owner,
    /// AnimationComponent driver) need not override this.
    ///
    /// `ctx` is populated by the extractor (owner, worldTransform,
    /// activeInHierarchy). The component may read `ctx.owner` to resolve
    /// sibling components if needed.
    virtual void extract(const RenderExtractContext& ctx, RenderExtractSink& sink) const
    {
        (void)ctx;
        (void)sink;
    }

protected:
    Component();

private:
    friend class GameObject;
    friend class Scene;

    void bindReflectionObject(arp::reflection::object value)
    {
        reflectionObject_ = std::move(value);
    }

    void bind(ComponentId id, GameObjectId ownerId, GameObject* owner);

    bool activeInScene() const { return activeInScene_; }

    bool started() const { return started_; }

    void setActiveInScene(bool active) { activeInScene_ = active; }

    void setStarted(bool started) { started_ = started; }

    ComponentId id_;
    GameObjectId ownerId_;
    bool enabled_ = true;
    bool activeInScene_ = false;
    bool started_ = false;
    bool pendingDestroy_ = false;
    GameObject* owner_ = nullptr;
    arp::reflection::object reflectionObject_;
};

} // namespace arp::scene
