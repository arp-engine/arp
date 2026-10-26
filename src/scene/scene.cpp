#include "scene/scene.h"

#include "scene/script_behavior_component.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace arp::scene {

namespace {

SceneUpdateResult scriptFailureResult(const ScriptBehaviorComponent* component)
{
    arp::core::pmr::String message{
        "script lifecycle hook failed", &arp::core::defaultCpuMemoryResource()};
    if (component != nullptr && component->failureHook() != nullptr) {
        message += ": ";
        message += component->failureHook();
    }
    if (component && !component->failureDiagnostic().empty()) {
        message += ": ";
        message += component->failureDiagnostic();
    }
    return SceneUpdateResult::failure(
        SceneDiagnosticCode::ScriptExecutionFailed, std::move(message));
}

} // namespace

SceneUpdateResult::SceneUpdateResult(SceneDiagnostic diagnostic, size_t visitedObjectCount)
    : diagnostic_(std::move(diagnostic)), visitedObjectCount_(visitedObjectCount)
{}

SceneUpdateResult SceneUpdateResult::success(size_t visitedObjectCount)
{
    return SceneUpdateResult(SceneDiagnostic{}, visitedObjectCount);
}

SceneUpdateResult SceneUpdateResult::failure(
    SceneDiagnosticCode code, arp::core::pmr::String message)
{
    return SceneUpdateResult(SceneDiagnostic{code, std::move(message)}, 0);
}

bool Scene::destroy_object_ref(GameObject* object)
{
    return object != nullptr && destroyObject(object->id()).ok();
}

bool Scene::reparent_objects(GameObject* child, GameObject* parent)
{
    return child != nullptr && parent != nullptr && reparent(child->id(), parent->id()).ok();
}

Scene* Scene::create_scene(const arp::core::pmr::String& name)
{
    return mainSceneRegistry_ != nullptr ? mainSceneRegistry_->createScene(name) : nullptr;
}

bool Scene::destroy_scene(Scene* scene)
{
    return mainSceneRegistry_ != nullptr && scene != nullptr
        && mainSceneRegistry_->destroyScene(*scene);
}

bool Scene::request_main_scene()
{
    return mainSceneRegistry_ != nullptr && mainSceneRegistry_->request(*this);
}

bool Scene::is_main_scene() const
{
    return mainSceneRegistry_ != nullptr && mainSceneRegistry_->active() == this;
}

std::uint64_t Scene::main_scene_generation() const
{
    return mainSceneRegistry_ != nullptr ? mainSceneRegistry_->generation() : 0u;
}

Component* Scene::createComponentByName(GameObject& object, const arp::core::pmr::String& type)
{
    requireIdentityIdle();
    if (&object != this && findObject(object.id()) != &object)
        return nullptr;
    if (shuttingDown_ || object.pendingDestroy())
        return nullptr;
    const auto script = scriptFactories_.find(type);
    auto component = script != scriptFactories_.end() ? script->second.create(object, type)
                                                      : arp::core::UniquePtr<Component>{};
    if (script == scriptFactories_.end() && componentFactory_)
        component = componentFactory_(object, type);
    if (!component)
        return nullptr;
    auto& attached = object.attachComponent(std::move(component));
    // Custom script types have a script receiver, not a native C++ class.
    if (attached.scriptInstance().is_undefined())
        attached.bindNativeReflectionObject();
    return &attached;
}

bool Scene::hasRegisteredScriptComponentType(const core::pmr::String& type) const noexcept
{
    return scriptFactories_.find(type) != scriptFactories_.end();
}

bool Scene::registerScriptComponentTypes(const core::pmr::String& module,
    const core::pmr::Vector<core::pmr::String>& types,
    ScriptComponentFactory factory,
    const void* executionIdentity)
{
    requireIdentityIdle();
    if (shuttingDown_ || module.empty() || types.empty() || !factory)
        return false;
    for (const auto& type : types) {
        const auto found = scriptFactories_.find(type);
        if (type.empty()
            || (found != scriptFactories_.end()
                && (found->second.module != module || !executionIdentity
                    || found->second.executionIdentity != executionIdentity)))
            return false;
    }
    for (const auto& type : types)
        scriptFactories_.try_emplace(type, ScriptFactory{module, factory, executionIdentity});
    return true;
}

Scene::Scene(arp::core::pmr::String name, SceneId sceneId)
    : Scene(std::move(name), sceneId, SceneExecutionMode::Runtime)
{}

Scene::Scene(arp::core::pmr::String name, SceneId sceneId, SceneExecutionMode mode)
    : GameObject(GameObjectId::fromValue(1), std::move(name), this), sceneId_(sceneId),
      executionMode_(mode)
{
    if (mode != SceneExecutionMode::Runtime && mode != SceneExecutionMode::Passive)
        throw std::invalid_argument("ARP-SCENE-EXECUTION-MODE-INVALID");
    bindScene(this);
    bindReflectionObject(arp::reflection::object::native_reference(this));
    objectLookup_[id().value()] = this;
    initializeMandatoryTransform(allocateComponentId());
}

Scene::~Scene()
{
    dispatching_ = true;
    const SceneUpdateContext context{};
    shuttingDown_ = true;
    for (auto& object : objects_)
        object->pendingDestroy_ = true;
    flushDestroyed(context);
    while (!components_.empty()) {
        auto owned = std::move(components_.back());
        components_.pop_back();
        dispatchComponentDestruction(*owned, context);
    }
}

GameObject& Scene::createObject(arp::core::pmr::String name)
{
    requireIdentityIdle();
    if (shuttingDown_)
        throw std::logic_error("ARP-SCENE-SHUTTING-DOWN");
    if (objects_.size() >= maxObjects_)
        throw std::length_error("ARP-SCENE-OBJECT-CAPACITY");
    if (nextObjectId_ == UINT64_MAX)
        throw std::overflow_error("ARP-SCENE-OBJECT-ID-EXHAUSTED");
    const auto objectId = GameObjectId::fromValue(nextObjectId_++);
    auto object = arp::core::makeUnique<GameObject>(objectId, std::move(name), this);
    object->initializeMandatoryTransform(allocateComponentId());
    auto& result = *object;
    // Newly created script objects belong to the canonical Scene root by
    // default. Scripts may still reparent them explicitly; keeping the
    // default rooted makes an object immediately participate in traversal.
    result.setParentId(id_);
    appendChild(result.id());
    objectLookup_[objectId.value()] = &result;
    objects_.push_back(std::move(object));
    return result;
}

GameObject* Scene::findObject(GameObjectId objectId)
{
    const auto found = objectLookup_.find(objectId.value());
    return found != objectLookup_.end() ? found->second : nullptr;
}

const GameObject* Scene::findObject(GameObjectId objectId) const
{
    const auto found = objectLookup_.find(objectId.value());
    return found != objectLookup_.end() ? found->second : nullptr;
}

GameObject* Scene::findObjectByName(const arp::core::pmr::String& name)
{
    if (this->name() == name)
        return this;
    for (const auto& object : objects_) {
        if (object->name() == name)
            return object.get();
    }
    return nullptr;
}

const GameObject* Scene::findObjectByName(const arp::core::pmr::String& name) const
{
    if (this->name() == name)
        return this;
    for (const auto& object : objects_) {
        if (object->name() == name)
            return object.get();
    }
    return nullptr;
}

SceneResult Scene::reparent(GameObjectId childId, GameObjectId parentId)
{
    requireIdentityIdle();
    const auto* child = findObject(childId);
    const auto* parent = findObject(parentId);
    if (child && child->parentId() == parentId)
        return SceneResult::success();
    if (!parent)
        return SceneResult::failure(SceneDiagnosticCode::InvalidParentId,
            "Cannot reparent to an unknown parent GameObject id.");
    return reparent(childId, parentId, parent->childIds().size());
}

SceneResult Scene::reparent(GameObjectId childId, GameObjectId parentId, std::size_t childIndex)
{
    requireIdentityIdle();
    if (childId == rootId()) {
        return SceneResult::failure(
            SceneDiagnosticCode::CannotReparentSceneRoot, "Scene root cannot be reparented.");
    }

    auto* child = findObject(childId);
    if (child == nullptr) {
        return SceneResult::failure(
            SceneDiagnosticCode::InvalidObjectId, "Cannot reparent an unknown GameObject id.");
    }

    auto* parent = findObject(parentId);
    if (parent == nullptr) {
        return SceneResult::failure(SceneDiagnosticCode::InvalidParentId,
            "Cannot reparent to an unknown parent GameObject id.");
    }

    if (parentChainContains(parentId, childId)) {
        return SceneResult::failure(SceneDiagnosticCode::HierarchyCycle,
            "Cannot create a cycle in the GameObject hierarchy.");
    }

    const auto oldParentId = child->parentId();
    const auto oldParent = oldParentId.valid() ? findObject(oldParentId) : nullptr;
    if (oldParentId.valid() && !oldParent)
        return SceneResult::failure(SceneDiagnosticCode::InvalidParentId,
            "Child's current parent is missing from the scene.");
    if (oldParent
        && std::count(oldParent->childIds().begin(), oldParent->childIds().end(), childId) != 1)
        return SceneResult::failure(SceneDiagnosticCode::InvalidObjectId,
            "Child is missing or duplicated in its parent's sibling list.");
    const auto targetChildren = parent->childIds().size();
    const auto maximumIndex =
        oldParentId == parentId && oldParent ? targetChildren - 1u : targetChildren;
    if (childIndex > maximumIndex)
        return SceneResult::failure(SceneDiagnosticCode::InvalidChildIndex,
            "Child insertion index is outside the target sibling list.");

    auto success = SceneResult::success();
    if (oldParentId == parentId && oldParent) {
        if (!oldParent->reorderChild(childId, childIndex))
            return SceneResult::failure(SceneDiagnosticCode::InvalidObjectId,
                "Child is missing from its parent's sibling list.");
        return success;
    }

    // No hierarchy write may precede the target's potentially allocating prepare.
    if (!parent->prepareChildInsertion(childId, childIndex))
        return SceneResult::failure(SceneDiagnosticCode::InvalidChildIndex,
            "Child insertion index is outside the target sibling list.");
    if (oldParentId.valid() && oldParent)
        oldParent->removeChild(childId);
    child->setParentId(parentId);
    parent->insertPreparedChildAt(childId, childIndex);
    return success;
}

SceneResult Scene::destroyObject(GameObjectId objectId)
{
    requireIdentityIdle();
    if (objectId == rootId() || !objectId.valid()) {
        return SceneResult::failure(SceneDiagnosticCode::InvalidObjectId,
            "Cannot destroy the scene root or an invalid GameObject id.");
    }

    auto* object = findObject(objectId);
    if (object == nullptr) {
        return SceneResult::failure(
            SceneDiagnosticCode::InvalidObjectId, "Cannot destroy an unknown GameObject id.");
    }

    if (dispatching_) {
        markDestroyed(*object);
        return SceneResult::success();
    }
    markDestroyed(*object);
    dispatching_ = true;
    flushDestroyed({});
    dispatching_ = false;
    return SceneResult::success();
}

SceneResult Scene::setWorldTransform(GameObjectId objectId, const Transform& worldTransform)
{
    auto* object = findObject(objectId);
    if (object == nullptr) {
        return SceneResult::failure(SceneDiagnosticCode::InvalidObjectId,
            "Cannot set world transform for an unknown GameObject id.");
    }

    const auto parentWorld = object->parentId().valid() ? computeWorldTransform(object->parentId())
                                                        : Transform::identity();
    object->transform().setLocalTransform(deriveLocalTransform(parentWorld, worldTransform));
    object->transform().setWorldTransformForTraversal(worldTransform);
    return SceneResult::success();
}

SceneUpdateResult Scene::update(const SceneUpdateOptions& options)
{
    return dispatch(options, false);
}

SceneUpdateResult Scene::fixedUpdate(const SceneUpdateOptions& options)
{
    return dispatch(options, true);
}

SceneUpdateResult Scene::dispatch(const SceneUpdateOptions& options, bool fixed)
{
    if (executionMode_ == SceneExecutionMode::Passive)
        return SceneUpdateResult::failure(
            SceneDiagnosticCode::PassiveExecutionRejected, "ARP-SCENE-PASSIVE-EXECUTION");
    if (dispatching_)
        return SceneUpdateResult::failure(
            SceneDiagnosticCode::ScriptExecutionFailed, "ARP-SCENE-REENTRANT-DISPATCH");
    auto& objects = updateObjects_;
    objects.clear();
    objects.reserve(options.maxObjectCount);
    if (!collectUpdateOrder(*this, options, objects))
        return SceneUpdateResult::failure(SceneDiagnosticCode::TraversalBudgetExceeded,
            "Scene update exceeded the configured object budget.");
    // Snapshot pointer ownership, never iterators into mutable component arrays.
    // New attachments become eligible at the next scheduler boundary.
    updateComponents_.clear();
    updateComponents_.reserve(options.maxComponentCount);
    for (auto* object : objects)
        for (const auto& component : object->components()) {
            if (updateComponents_.size() >= options.maxComponentCount)
                return SceneUpdateResult::failure(
                    SceneDiagnosticCode::TraversalBudgetExceeded, "ARP-SCENE-COMPONENT-BUDGET");
            updateComponents_.push_back(component.get());
        }
    dispatching_ = true;
    const SceneUpdateContext context{
        options.deltaSeconds, options.frameIndex, options.unscaledDeltaSeconds};
    const auto eligible = [](const Component& component) {
        return !component.pendingDestroy() && component.gameObject()->activeInHierarchy()
            && component.enabled();
    };
    SceneUpdateResult result = SceneUpdateResult::success(objects.size());
    const auto failed = [&](Component* component) {
        const auto* script = dynamic_cast<ScriptBehaviorComponent*>(component);
        if (script && script->failed()) {
            result = scriptFailureResult(script);
            return true;
        }
        return false;
    };
    try {
        for (auto* component : updateComponents_) {
            const bool active = eligible(*component);
            if (active != component->activeInScene()) {
                component->setActiveInScene(active);
                if (active)
                    component->onEnable(context);
                else
                    component->onDisable(context);
            }
            if (failed(component))
                break;
        }
        if (result.ok())
            for (auto* component : updateComponents_) {
                if (eligible(*component) && component->activeInScene() && !component->started()) {
                    component->setStarted(true);
                    component->onStart(context);
                }
                if (failed(component))
                    break;
            }
        if (result.ok())
            for (auto* component : updateComponents_) {
                if (eligible(*component) && component->activeInScene() && component->started()) {
                    if (fixed)
                        component->onFixedUpdate(context);
                    else
                        component->onUpdate(context);
                }
                if (failed(component))
                    break;
            }
        if (!fixed && result.ok())
            for (auto* component : updateComponents_) {
                if (eligible(*component) && component->activeInScene() && component->started())
                    component->onLateUpdate(context);
                if (failed(component))
                    break;
            }
    } catch (const std::exception& error) {
        result = SceneUpdateResult::failure(
            SceneDiagnosticCode::ScriptExecutionFailed, core::pmr::String(error.what()));
    }
    updateComponents_.clear();
    flushDestroyed(context);
    dispatching_ = false;
    return result;
}

bool Scene::queueEvent(SceneEvent event)
{
    requireIdentityIdle();
    if (executionMode_ == SceneExecutionMode::Passive)
        return false;
    const auto* target = findObject(event.target);
    if (!target || target->pendingDestroy() || event.name.empty() || events_.size() >= maxEvents_)
        return false;
    events_.push_back(std::move(event));
    return true;
}

SceneUpdateResult Scene::dispatchEvents(const SceneUpdateOptions& options)
{
    if (executionMode_ == SceneExecutionMode::Passive)
        return SceneUpdateResult::failure(
            SceneDiagnosticCode::PassiveExecutionRejected, "ARP-SCENE-PASSIVE-EXECUTION");
    if (dispatching_)
        return SceneUpdateResult::failure(
            SceneDiagnosticCode::ScriptExecutionFailed, "ARP-SCENE-REENTRANT-DISPATCH");
    dispatching_ = true;
    dispatchEvents_.clear();
    dispatchEvents_.swap(events_);
    auto result = SceneUpdateResult::success(dispatchEvents_.size());
    try {
        for (const auto& event : dispatchEvents_) {
            auto* object = findObject(event.target);
            if (!object || !object->activeInHierarchy())
                continue;
            updateComponents_.clear();
            if (object->components().size() > options.maxComponentCount) {
                result = SceneUpdateResult::failure(
                    SceneDiagnosticCode::TraversalBudgetExceeded, "ARP-SCENE-COMPONENT-BUDGET");
                break;
            }
            for (const auto& component : object->components())
                updateComponents_.push_back(component.get());
            for (auto* component : updateComponents_) {
                if (!component->pendingDestroy() && !object->pendingDestroy()
                    && component->enabled() && component->activeInScene() && component->started())
                    component->onEvent(event);
                auto* script = dynamic_cast<ScriptBehaviorComponent*>(component);
                if (script && script->failed()) {
                    result = scriptFailureResult(script);
                    break;
                }
            }
            if (!result.ok())
                break;
        }
    } catch (const std::exception& error) {
        result = SceneUpdateResult::failure(
            SceneDiagnosticCode::ScriptExecutionFailed, core::pmr::String(error.what()));
    }
    updateComponents_.clear();
    dispatchEvents_.clear();
    flushDestroyed({options.deltaSeconds, options.frameIndex});
    dispatching_ = false;
    return result;
}

SceneUpdateResult Scene::dispatchPhysicsEvents(
    const arp::core::pmr::Vector<ScriptPhysicsEvent>& events, const SceneUpdateOptions& options)
{
    if (executionMode_ == SceneExecutionMode::Passive)
        return SceneUpdateResult::failure(
            SceneDiagnosticCode::PassiveExecutionRejected, "ARP-SCENE-PASSIVE-EXECUTION");
    if (dispatching_)
        return SceneUpdateResult::failure(
            SceneDiagnosticCode::ScriptExecutionFailed, "ARP-SCENE-PHYSICS-EVENT-CONTRACT");
    dispatching_ = true;
    std::size_t invoked = 0;
    auto result = SceneUpdateResult::success(0);
    try {
        for (const auto& event : events) {
            if (event.hook == nullptr) {
                result = SceneUpdateResult::failure(
                    SceneDiagnosticCode::ScriptExecutionFailed, "ARP-SCENE-PHYSICS-EVENT-CONTRACT");
                break;
            }
            auto* object = findObject(event.target);
            auto* other = findObject(event.other);
            if (object == nullptr || other == nullptr || object->pendingDestroy()
                || other->pendingDestroy() || !object->activeInHierarchy())
                continue;
            if (object->components().size() > options.maxComponentCount) {
                result = SceneUpdateResult::failure(
                    SceneDiagnosticCode::TraversalBudgetExceeded, "ARP-SCENE-COMPONENT-BUDGET");
                break;
            }
            updateComponents_.clear();
            for (const auto& component : object->components())
                updateComponents_.push_back(component.get());
            for (auto* component : updateComponents_) {
                if (!component->pendingDestroy() && !object->pendingDestroy()
                    && component->enabled() && component->activeInScene() && component->started()) {
                    auto* script = dynamic_cast<ScriptBehaviorComponent*>(component);
                    if (script) {
                        if (!script->dispatchPhysicsEvent(event.hook, other)) {
                            result = scriptFailureResult(script);
                            break;
                        }
                        ++invoked;
                    }
                }
            }
            if (!result.ok())
                break;
        }
    } catch (const std::exception& error) {
        result = SceneUpdateResult::failure(
            SceneDiagnosticCode::ScriptExecutionFailed, core::pmr::String(error.what()));
    }
    updateComponents_.clear();
    if (result.ok())
        result = SceneUpdateResult::success(invoked);
    flushDestroyed({options.deltaSeconds, options.frameIndex});
    dispatching_ = false;
    return result;
}

void Scene::markDestroyed(GameObject& object)
{
    object.pendingDestroy_ = true;
    for (const auto child : object.childIds())
        if (auto* value = findObject(child))
            markDestroyed(*value);
}

void Scene::dispatchComponentDestruction(Component& component, const SceneUpdateContext& context)
{
    const bool active = component.activeInScene();
    component.setActiveInScene(false);
    if (executionMode_ == SceneExecutionMode::Passive)
        return;
    if (active)
        component.onDisable(context);
    component.onDestroy(context);
}

void Scene::flushDestroyed(const SceneUpdateContext& context)
{
    const auto destroyComponent = [&](core::UniquePtr<Component> owned) {
        dispatchComponentDestruction(*owned, context);
    };
    // Restart after every hook: callbacks may mark an earlier object for
    // deletion. Children finish before parents, independent of creation order.
    for (;;) {
        bool changed = false;
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            auto* object = objects_[i].get();
            if (!object->pendingDestroy())
                continue;
            bool hasPendingChild = false;
            for (const auto child : object->childIds()) {
                const auto* value = findObject(child);
                if (value && value->pendingDestroy()) {
                    hasPendingChild = true;
                    break;
                }
            }
            if (hasPendingChild)
                continue;
            if (auto* parent = findObject(object->parentId()))
                parent->removeChild(object->id());
            auto owned = std::move(objects_[i]);
            objects_.erase(objects_.begin() + i);
            objectLookup_.erase(owned->id().value());
            while (!owned->components_.empty()) {
                auto component = std::move(owned->components_.back());
                owned->components_.pop_back();
                destroyComponent(std::move(component));
            }
            changed = true;
            break;
        }
        if (changed)
            continue;
        const auto removeOne = [&](GameObject& object) {
            for (std::size_t i = 0; i < object.components_.size(); ++i) {
                if (!object.components_[i]->pendingDestroy())
                    continue;
                const auto id = object.components_[i]->id();
                auto owned = std::move(object.components_[i]);
                object.components_.erase(object.components_.begin() + i);
                object.componentIds_.erase(
                    std::remove(object.componentIds_.begin(), object.componentIds_.end(), id),
                    object.componentIds_.end());
                destroyComponent(std::move(owned));
                return true;
            }
            return false;
        };
        if (removeOne(*this))
            continue;
        for (std::size_t i = 0; i < objects_.size(); ++i)
            if (removeOne(*objects_[i])) {
                changed = true;
                break;
            }
        if (!changed)
            break;
    }
}

SceneTraversalSnapshot Scene::extractTraversalSnapshot(const SceneTraversalOptions& options)
{
    arp::core::pmr::Vector<SceneTraversalNode> nodes;
    nodes.reserve(options.maxNodeCount);
    if (!collectTraversal(*this, Transform::identity(), true, options, nodes)) {
        return SceneTraversalSnapshot::failure(SceneDiagnosticCode::TraversalBudgetExceeded,
            "Scene traversal exceeded the configured node budget.");
    }
    return SceneTraversalSnapshot::success(std::move(nodes));
}

void Scene::requireIdentityIdle() const
{
    if (restoringIdentity_)
        throw std::logic_error("ARP-SCENE-IDENTITY-BUSY");
}

ComponentId Scene::allocateComponentId()
{
    requireIdentityIdle();
    if (nextComponentId_ == UINT64_MAX)
        throw std::overflow_error("ARP-SCENE-COMPONENT-ID-EXHAUSTED");
    return ComponentId::fromValue(nextComponentId_++);
}

GameObject* Scene::requireMutableObject(GameObjectId objectId)
{
    return findObject(objectId);
}

bool Scene::parentChainContains(GameObjectId startParentId, GameObjectId childId) const
{
    auto current = startParentId;
    while (current.valid()) {
        if (current == childId) {
            return true;
        }
        const auto* object = findObject(current);
        if (object == nullptr) {
            return false;
        }
        current = object->parentId();
    }
    return false;
}

Transform Scene::computeWorldTransform(GameObjectId objectId) const
{
    const auto* object = findObject(objectId);
    if (object == nullptr) {
        return Transform::identity();
    }
    if (!object->parentId().valid()) {
        return object->transform().localTransform();
    }
    return composeTransforms(
        computeWorldTransform(object->parentId()), object->transform().localTransform());
}

bool Scene::collectTraversal(GameObject& object,
    const Transform& parentWorld,
    bool parentActiveInHierarchy,
    const SceneTraversalOptions& options,
    arp::core::pmr::Vector<SceneTraversalNode>& nodes)
{
    if (nodes.size() >= options.maxNodeCount) {
        return false;
    }

    const auto worldTransform = object.parentId().valid()
        ? composeTransforms(parentWorld, object.transform().localTransform())
        : object.transform().localTransform();
    object.transform().setWorldTransformForTraversal(worldTransform);

    const auto activeInHierarchy = parentActiveInHierarchy && object.active();
    SceneTraversalNode node;
    node.objectId = object.id();
    node.parentId = object.parentId();
    node.transformComponentId = object.transform().id();
    node.localTransform = object.transform().localTransform();
    node.worldTransform = worldTransform;
    node.active = object.active();
    node.activeInHierarchy = activeInHierarchy;
    node.componentIds = object.componentIds();
    nodes.push_back(std::move(node));

    for (const auto childId : object.childIds()) {
        auto* child = requireMutableObject(childId);
        if (child != nullptr
            && !collectTraversal(*child, worldTransform, activeInHierarchy, options, nodes)) {
            return false;
        }
    }
    return true;
}

bool Scene::collectUpdateOrder(GameObject& object,
    const SceneUpdateOptions& options,
    arp::core::pmr::Vector<GameObject*>& objects)
{
    if (objects.size() >= options.maxObjectCount) {
        return false;
    }
    objects.push_back(&object);
    for (const auto childId : object.childIds()) {
        auto* child = requireMutableObject(childId);
        if (child != nullptr && !collectUpdateOrder(*child, options, objects)) {
            return false;
        }
    }
    return true;
}

Scene::RestorationTransaction::RestorationTransaction(Scene& scene)
    : scene_(scene), factories_(scene.scriptFactories_), objects_(scene.objects_.size()),
      objectId_(scene.nextObjectId_), componentId_(scene.nextComponentId_)
{
    if (scene.dispatching_ || scene.shuttingDown_ || scene.restoringIdentity_)
        throw std::logic_error("ARP-SCENE-RESTORATION-DURING-DISPATCH");
    ++scene.restorationDepth_;
}

Scene::RestorationTransaction::~RestorationTransaction()
{
    --scene_.restorationDepth_;
    if (committed_)
        return;
    while (scene_.objects_.size() > objects_)
        scene_.destroyObject(scene_.objects_.back()->id());
    scene_.scriptFactories_.swap(factories_);
    scene_.nextObjectId_ = objectId_;
    scene_.nextComponentId_ = componentId_;
}

} // namespace arp::scene
