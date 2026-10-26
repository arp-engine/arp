#pragma once

#include "core/concurrency/callback.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/game_object.h"
#include "scene/main_scene_registry.h"
#include "scene/scene_traversal_snapshot.h"

#include <initializer_list>

namespace arp::scriptvm {
class ScriptComponentModule;
struct ScriptComponentIdentity;
} // namespace arp::scriptvm

namespace arp::scene {

class ScriptBehaviorComponent;

inline constexpr const char* kMainSceneService = "arp.scene.main-scene";
inline constexpr std::uint32_t kMainSceneServiceVersion = 1u;
inline constexpr const char* kMainSceneRegistryService = "arp.scene.main-scene-registry";
inline constexpr std::uint32_t kMainSceneRegistryServiceVersion = 1u;

// Construction-time policy. Authoring snapshots never become live worlds in place.
enum class SceneExecutionMode { Runtime, Passive };

struct SceneUpdateOptions {
    float deltaSeconds = 0.0f;
    uint64_t frameIndex = 0;
    size_t maxObjectCount = 1024;
    size_t maxComponentCount = 4096;
    float unscaledDeltaSeconds = 0;
};

class SceneUpdateResult {
public:
    static SceneUpdateResult success(size_t visitedObjectCount);
    static SceneUpdateResult failure(SceneDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == SceneDiagnosticCode::None; }

    const SceneDiagnostic& diagnostic() const { return diagnostic_; }

    size_t visitedObjectCount() const { return visitedObjectCount_; }

private:
    SceneUpdateResult(SceneDiagnostic diagnostic, size_t visitedObjectCount);

    SceneDiagnostic diagnostic_;
    size_t visitedObjectCount_ = 0;
};

ACLASS(Name = "Scene", Export = "main_scene", Ownership = Engine, Lifecycle = Explicit)

class Scene final : public GameObject {
public:
    class RestorationTransaction;
    class IdentityRestoration;
    using ComponentFactory = arp::core::SharedCallback<arp::core::UniquePtr<Component>(
        GameObject& owner, const arp::core::pmr::String& type)>;

    explicit Scene(arp::core::pmr::String name = "scene", SceneId sceneId = SceneId::fromValue(1));
    Scene(arp::core::pmr::String name, SceneId sceneId, SceneExecutionMode mode);
    ~Scene() override;

    SceneExecutionMode executionMode() const noexcept { return executionMode_; }

    // Domain plugins install an allow-listed native component factory during
    // runtime setup. Script calls still invoke GameObject directly; this is
    // not a script service/facade dispatch path.
    void configureObjectCapacity(std::size_t maximum)
    {
        requireIdentityIdle();
        maxObjects_ = maximum;
        objects_.reserve(maximum);
        objectLookup_.reserve(maximum + 1);
    }

    void setComponentFactory(ComponentFactory factory)
    {
        requireIdentityIdle();
        componentFactory_ = std::move(factory);
    }

    using ScriptComponentFactory = arp::core::SharedCallback<arp::core::UniquePtr<Component>(
        GameObject&, const arp::core::pmr::String&)>;
    bool registerScriptComponentTypes(const arp::core::pmr::String& module,
        const arp::core::pmr::Vector<arp::core::pmr::String>& types,
        ScriptComponentFactory factory,
        const void* executionIdentity = nullptr);
    // Read-only admission for composed registries; never materializes a type.
    bool hasRegisteredScriptComponentType(const core::pmr::String& type) const noexcept;

    SceneId sceneId() const { return sceneId_; }

    GameObjectId rootId() const { return id(); }

    AFUNC()

    std::uint64_t scene_id_value() const { return sceneId_.value(); }

    // The Scene is already the canonical root object.  Keep this native
    // convenience method for engine callers, but do not expose a second
    // script-level root API; scripts receive the main Scene directly.
    GameObject* root_object() { return this; }
    AFUNC()

    GameObject* create_object_ref(const arp::core::pmr::String& name)
    {
        return shuttingDown_
            ? nullptr
            : &createObject(arp::core::pmr::String(name, &arp::core::defaultCpuMemoryResource()));
    }
    AFUNC()

    GameObject* find_object_by_name_ref(const arp::core::pmr::String& name)
    {
        return findObjectByName(name);
    }
    // Keep component creation available on the canonical Scene static type so
    // the AOT direct-object binder does not need an implicit upcast.
    AFUNC()

    GameObject* find_object(std::uint64_t id) { return findObject(GameObjectId::fromValue(id)); }
    AFUNC()

    Component* create_component(const arp::core::pmr::String& type)
    {
        return GameObject::create_component(type);
    }
    AFUNC()

    arp::reflection::object add_component(const arp::core::pmr::String& type)
    {
        return GameObject::add_component(type);
    }
    AFUNC()

    arp::reflection::object get_component(const arp::core::pmr::String& type) const
    {
        return GameObject::get_component(type);
    }
    AFUNC()
    bool destroy_object_ref(GameObject* object);
    AFUNC()
    bool reparent_objects(GameObject* child, GameObject* parent);
    AFUNC()
    Scene* create_scene(const arp::core::pmr::String& name);
    AFUNC()
    bool destroy_scene(Scene* scene);
    AFUNC()
    bool request_main_scene();
    AFUNC()
    bool is_main_scene() const;
    AFUNC()
    std::uint64_t main_scene_generation() const;

    // Host-only binding performed during Plugin registration. It does not
    // expose the registry object or a service callable to script.
    void bindMainSceneRegistry(MainSceneRegistry* registry) noexcept
    {
        mainSceneRegistry_ = registry;
    }

    Component* createComponentByName(GameObject& object, const arp::core::pmr::String& type);

    GameObject& createObject(arp::core::pmr::String name);
    GameObject* findObject(GameObjectId objectId);
    const GameObject* findObject(GameObjectId objectId) const;
    GameObject* findObjectByName(const arp::core::pmr::String& name);
    const GameObject* findObjectByName(const arp::core::pmr::String& name) const;

    // Native scene systems use this read-only view to bind resources to
    // script-created components after a preparation phase. It is not marked
    // for reflection and therefore is not part of the script API.
    const arp::core::pmr::Vector<arp::core::UniquePtr<GameObject>>& objects() const
    {
        return objects_;
    }

    SceneResult reparent(GameObjectId childId, GameObjectId parentId);
    SceneResult reparent(GameObjectId childId, GameObjectId parentId, std::size_t childIndex);
    SceneResult destroyObject(GameObjectId objectId);
    SceneResult setWorldTransform(GameObjectId objectId, const Transform& worldTransform);

    SceneUpdateResult update(const SceneUpdateOptions& options);
    SceneUpdateResult fixedUpdate(const SceneUpdateOptions& options);

    void configureEventCapacity(std::size_t maximum)
    {
        requireIdentityIdle();
        maxEvents_ = maximum;
        events_.reserve(maximum);
        dispatchEvents_.reserve(maximum);
    }

    bool queueEvent(SceneEvent event);
    SceneUpdateResult dispatchEvents(const SceneUpdateOptions& options);
    SceneUpdateResult dispatchPhysicsEvents(
        const arp::core::pmr::Vector<ScriptPhysicsEvent>& events,
        const SceneUpdateOptions& options);
    SceneTraversalSnapshot extractTraversalSnapshot(const SceneTraversalOptions& options);

private:
    friend class GameObject;

    void requireIdentityIdle() const;
    ComponentId allocateComponentId();

    GameObject* requireMutableObject(GameObjectId objectId);
    bool parentChainContains(GameObjectId startParentId, GameObjectId childId) const;
    Transform computeWorldTransform(GameObjectId objectId) const;
    bool collectTraversal(GameObject& object,
        const Transform& parentWorld,
        bool parentActiveInHierarchy,
        const SceneTraversalOptions& options,
        arp::core::pmr::Vector<SceneTraversalNode>& nodes);
    bool collectUpdateOrder(GameObject& object,
        const SceneUpdateOptions& options,
        arp::core::pmr::Vector<GameObject*>& objects);
    SceneUpdateResult dispatch(const SceneUpdateOptions& options, bool fixed);
    void flushDestroyed(const SceneUpdateContext& context);
    void markDestroyed(GameObject& object);
    void dispatchComponentDestruction(Component& component, const SceneUpdateContext& context);

    SceneId sceneId_;
    const SceneExecutionMode executionMode_;
    MainSceneRegistry* mainSceneRegistry_ = nullptr;
    std::size_t maxObjects_ = SIZE_MAX;
    uint64_t nextObjectId_ = 2;
    uint64_t nextComponentId_ = 1;
    arp::core::pmr::Vector<arp::core::UniquePtr<GameObject>> objects_;
    arp::core::pmr::UnorderedMap<uint64_t, GameObject*> objectLookup_;
    ComponentFactory componentFactory_ = nullptr;

    struct ScriptFactory {
        arp::core::pmr::String module;
        ScriptComponentFactory create;
        const void* executionIdentity = nullptr;
    };

    arp::core::pmr::Map<arp::core::pmr::String, ScriptFactory> scriptFactories_;
    bool dispatching_ = false, shuttingDown_ = false;
    bool restoringIdentity_ = false;
    std::size_t restorationDepth_ = 0;
    arp::core::pmr::Vector<GameObject*> updateObjects_;
    arp::core::pmr::Vector<Component*> updateComponents_;
    std::size_t maxEvents_ = 0;
    arp::core::pmr::Vector<SceneEvent> events_, dispatchEvents_;
};

// A restoration transaction may append candidate objects and bind script types.
// It must run outside scene dispatch; rollback restores identity allocation and
// published factories after tearing down all candidate components.
class Scene::RestorationTransaction final {
public:
    explicit RestorationTransaction(Scene& scene);
    ~RestorationTransaction();
    RestorationTransaction(const RestorationTransaction&) = delete;
    RestorationTransaction& operator=(const RestorationTransaction&) = delete;

    void commit() { committed_ = true; }

private:
    Scene& scene_;
    core::pmr::Map<core::pmr::String, ScriptFactory> factories_;
    std::size_t objects_;
    std::uint64_t objectId_, componentId_;
    bool committed_ = false;
};

// Explicit Passive-only candidate transaction. The caller supplies an allow-listed
// factory whose constructors/destructors have isolated, rollback-safe ownership.
// Ordinary scene lookup never exposes candidates; only this object resolves them.
class Scene::IdentityRestoration final {
public:
    struct Limits {
        std::size_t objects;
        std::size_t components; // Includes mandatory transforms.
    };

    IdentityRestoration(Scene&, Limits, ComponentFactory isolatedFactory = {});
    ~IdentityRestoration();
    IdentityRestoration(const IdentityRestoration&) = delete;
    IdentityRestoration& operator=(const IdentityRestoration&) = delete;

    GameObject& restoreObject(GameObjectId,
        ComponentId transformId,
        core::pmr::String name,
        GameObjectId parent,
        std::size_t childIndex);
    Component& restoreComponent(
        GameObjectId owner, ComponentId, const core::pmr::String& type, std::size_t componentIndex);
    GameObject* findCandidate(GameObjectId) const noexcept;
    // Allocates registry nodes, validates all identities and reserves live vectors.
    // Calling commit after prepare performs no allocation.
    GameObject& createObject(core::pmr::String name, GameObjectId parent, std::size_t childIndex);
    Component& createComponent(
        GameObjectId owner, const core::pmr::String& type, std::size_t componentIndex);
    // Script candidates use the prepared module's cold path, never the native
    // factory. Identity is explicit; ordinary create keeps its duplicate gate.
    ScriptBehaviorComponent& restoreScriptComponent(GameObjectId owner,
        ComponentId,
        scriptvm::ScriptComponentModule&,
        const scriptvm::ScriptComponentIdentity&,
        std::size_t componentIndex);
    ScriptBehaviorComponent& createScriptComponent(GameObjectId owner,
        scriptvm::ScriptComponentModule&,
        const scriptvm::ScriptComponentIdentity&,
        std::size_t componentIndex);
    GameObject* resolveObject(GameObjectId) const noexcept;
    Component* resolveComponent(GameObjectId owner, ComponentId) const noexcept;
    // Same TRS composition as Scene, including unpublished candidate parents.
    bool worldTransform(GameObjectId, Transform&) const noexcept;
    void prepare();
    void commit();

    bool committed() const noexcept { return committed_; }

private:
    struct ComponentCandidate {
        GameObject* owner;
        std::size_t index;
        core::UniquePtr<Component> component;
    };

    struct ChildrenCandidate {
        GameObject* owner;
        core::pmr::Vector<GameObjectId> ids;
    };

    void requirePreparing() const;
    bool componentIdUsed(ComponentId) const noexcept;
    void validateComponentId(ComponentId) const;
    GameObject& validateComponentRequest(GameObjectId, ComponentId, std::size_t) const;
    Component& stageComponent(GameObject&, ComponentId, std::size_t, core::UniquePtr<Component>);
    Scene& scene_;
    Limits limits_;
    ComponentFactory factory_;
    core::pmr::Vector<core::UniquePtr<GameObject>> candidates_;
    core::pmr::Vector<ComponentCandidate> components_;
    core::pmr::Vector<ChildrenCandidate> children_;
    core::pmr::UnorderedMap<std::uint64_t, GameObject*> lookup_;
    std::uint64_t nextObject_, nextComponent_;
    std::size_t componentCount_ = 0;
    bool prepared_ = false, committed_ = false, failed_ = false;
};

} // namespace arp::scene
