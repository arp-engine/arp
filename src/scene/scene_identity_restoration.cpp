#include "scene/scene.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace arp::scene {

Scene::IdentityRestoration::IdentityRestoration(
    Scene& scene, Limits limits, ComponentFactory isolatedFactory)
    : scene_(scene), limits_(limits), factory_(std::move(isolatedFactory)),
      nextObject_(scene.nextObjectId_), nextComponent_(scene.nextComponentId_)
{
    if (scene.executionMode_ != SceneExecutionMode::Passive)
        throw std::logic_error("ARP-SCENE-IDENTITY-PASSIVE-ONLY");
    if (scene.dispatching_ || scene.shuttingDown_ || scene.restoringIdentity_
        || scene.restorationDepth_)
        throw std::logic_error("ARP-SCENE-IDENTITY-BUSY");
    scene.restoringIdentity_ = true;
}

Scene::IdentityRestoration::~IdentityRestoration()
{
    // Retire candidates while ordinary mutation is still excluded. Their native
    // ownership must be rollback-safe; no lifecycle hook is dispatched here.
    components_.clear();
    candidates_.clear();
    scene_.restoringIdentity_ = false;
}

void Scene::IdentityRestoration::requirePreparing() const
{
    if (prepared_ || committed_ || failed_)
        throw std::logic_error("ARP-SCENE-IDENTITY-CONSUMED");
}

GameObject* Scene::IdentityRestoration::findCandidate(GameObjectId id) const noexcept
{
    for (const auto& object : candidates_)
        if (object->id() == id)
            return object.get();
    return nullptr;
}

GameObject& Scene::IdentityRestoration::createObject(
    core::pmr::String name, GameObjectId parent, std::size_t childIndex)
{
    return restoreObject(GameObjectId::fromValue(nextObject_),
        ComponentId::fromValue(nextComponent_),
        std::move(name),
        parent,
        childIndex);
}

Component& Scene::IdentityRestoration::createComponent(
    GameObjectId owner, const core::pmr::String& type, std::size_t componentIndex)
{
    return restoreComponent(owner, ComponentId::fromValue(nextComponent_), type, componentIndex);
}

GameObject* Scene::IdentityRestoration::resolveObject(GameObjectId id) const noexcept
{
    auto* candidate = findCandidate(id);
    return candidate ? candidate : scene_.findObject(id);
}

bool Scene::IdentityRestoration::componentIdUsed(ComponentId id) const noexcept
{
    if (scene_.findComponent(id))
        return true;
    for (const auto& object : scene_.objects_)
        if (object->findComponent(id))
            return true;
    for (const auto& object : candidates_)
        if (object->findComponent(id))
            return true;
    for (const auto& item : components_)
        if (item.component->id() == id)
            return true;
    return false;
}

bool Scene::IdentityRestoration::worldTransform(GameObjectId id, Transform& result) const noexcept
{
    const auto read =
        [&](const auto& self, GameObjectId current, std::size_t depth, Transform& value) -> bool {
        const auto* object = resolveObject(current);
        if (!object || depth > scene_.objects_.size() + candidates_.size())
            return false;
        if (!object->parentId().valid()) {
            value = object->local_transform();
            return true;
        }
        Transform parent;
        if (!self(self, object->parentId(), depth + 1, parent))
            return false;
        value = composeTransforms(parent, object->local_transform());
        return true;
    };
    Transform prepared;
    if (!read(read, id, 0, prepared))
        return false;
    result = prepared;
    return true;
}

void Scene::IdentityRestoration::validateComponentId(ComponentId id) const
{
    if (!id.valid() || id.value() == std::numeric_limits<std::uint64_t>::max()
        || componentIdUsed(id))
        throw std::invalid_argument("ARP-SCENE-IDENTITY-COMPONENT-ID");
    if (componentCount_ >= limits_.components)
        throw std::length_error("ARP-SCENE-IDENTITY-COMPONENT-CAPACITY");
}

GameObject& Scene::IdentityRestoration::restoreObject(GameObjectId id,
    ComponentId transformId,
    core::pmr::String name,
    GameObjectId parentId,
    std::size_t childIndex)
{
    requirePreparing();
    try {
        if (!id.valid() || id == scene_.rootId()
            || id.value() == std::numeric_limits<std::uint64_t>::max() || scene_.findObject(id)
            || findCandidate(id))
            throw std::invalid_argument("ARP-SCENE-IDENTITY-OBJECT-ID");
        validateComponentId(transformId);
        if (candidates_.size() >= limits_.objects || scene_.objects_.size() >= scene_.maxObjects_
            || candidates_.size() >= scene_.maxObjects_ - scene_.objects_.size())
            throw std::length_error("ARP-SCENE-IDENTITY-OBJECT-CAPACITY");
        // Parents must already exist in this transaction or the live scene. This
        // topological authoring order excludes cycles without a guessed repair.
        auto* parent = findCandidate(parentId);
        if (!parent)
            parent = scene_.findObject(parentId);
        if (!parent || parent->pendingDestroy())
            throw std::invalid_argument("ARP-SCENE-IDENTITY-PARENT");
        auto children =
            std::find_if(children_.begin(), children_.end(), [parent](const auto& item) {
                return item.owner == parent;
            });
        if (children == children_.end()) {
            children_.push_back({parent, parent->childIds_});
            children = children_.end() - 1;
        }
        if (childIndex > children->ids.size())
            throw std::invalid_argument("ARP-SCENE-IDENTITY-CHILD-INDEX");
        auto object = core::makeUnique<GameObject>(id, std::move(name), &scene_);
        object->initializeMandatoryTransform(transformId);
        object->setParentId(parentId);
        auto* result = object.get();
        children->ids.insert(children->ids.begin() + static_cast<std::ptrdiff_t>(childIndex), id);
        candidates_.push_back(std::move(object));
        ++componentCount_;
        nextObject_ = std::max(nextObject_, id.value() + 1);
        nextComponent_ = std::max(nextComponent_, transformId.value() + 1);
        return *result;
    } catch (...) {
        failed_ = true;
        throw;
    }
}

Component& Scene::IdentityRestoration::restoreComponent(
    GameObjectId ownerId, ComponentId id, const core::pmr::String& type, std::size_t componentIndex)
{
    requirePreparing();
    try {
        validateComponentId(id);
        auto* candidate = findCandidate(ownerId);
        auto* owner = candidate ? candidate : scene_.findObject(ownerId);
        if (!owner || owner->pendingDestroy() || !factory_ || type.empty()
            || type == "TransformComponent"
            || scene_.scriptFactories_.find(type) != scene_.scriptFactories_.end())
            throw std::invalid_argument("ARP-SCENE-IDENTITY-FACTORY-UNSUPPORTED");
        std::size_t count = owner->components_.size();
        for (const auto& item : components_)
            count += item.owner == owner;
        if (!componentIndex || componentIndex > count)
            throw std::invalid_argument("ARP-SCENE-IDENTITY-COMPONENT-INDEX");
        auto component = factory_(*owner, type);
        if (!component || component->id().valid() || component->gameObject()
            || type != component->scriptTypeName() || !component->scriptInstance().is_undefined())
            throw std::invalid_argument("ARP-SCENE-IDENTITY-FACTORY-UNSUPPORTED");
        component->bind(id, ownerId, owner);
        component->bindNativeReflectionObject();
        auto* result = component.get();
        if (candidate) {
            owner->componentIds_.insert(
                owner->componentIds_.begin() + static_cast<std::ptrdiff_t>(componentIndex), id);
            owner->components_.insert(
                owner->components_.begin() + static_cast<std::ptrdiff_t>(componentIndex),
                std::move(component));
        } else {
            components_.push_back({owner, componentIndex, std::move(component)});
        }
        ++componentCount_;
        nextComponent_ = std::max(nextComponent_, id.value() + 1);
        return *result;
    } catch (...) {
        failed_ = true;
        throw;
    }
}

void Scene::IdentityRestoration::prepare()
{
    if (prepared_ && !committed_)
        return;
    requirePreparing();
    try {
        // Copying the map prepares actual nodes, not just bucket capacity. No
        // unordered_map insertion (and therefore no node allocation) follows.
        lookup_ = scene_.objectLookup_;
        for (const auto& object : candidates_)
            if (!lookup_.emplace(object->id().value(), object.get()).second)
                throw std::invalid_argument("ARP-SCENE-IDENTITY-OBJECT-ID");
        scene_.objects_.reserve(scene_.objects_.size() + candidates_.size());
        for (const auto& item : components_) {
            std::size_t count = item.owner->components_.size();
            for (const auto& other : components_)
                count += item.owner == other.owner;
            item.owner->components_.reserve(count);
            item.owner->componentIds_.reserve(count);
        }
        prepared_ = true;
    } catch (...) {
        failed_ = true;
        throw;
    }
}

void Scene::IdentityRestoration::commit()
{
    prepare();
    for (auto& item : components_) {
        auto* owner = item.owner;
        owner->componentIds_.insert(
            owner->componentIds_.begin() + static_cast<std::ptrdiff_t>(item.index),
            item.component->id());
        owner->components_.insert(
            owner->components_.begin() + static_cast<std::ptrdiff_t>(item.index),
            std::move(item.component));
    }
    for (auto& item : children_)
        item.owner->childIds_.swap(item.ids);
    for (auto& object : candidates_)
        scene_.objects_.push_back(std::move(object));
    candidates_.clear();
    scene_.objectLookup_.swap(lookup_);
    scene_.nextObjectId_ = nextObject_;
    scene_.nextComponentId_ = nextComponent_;
    committed_ = true;
}

} // namespace arp::scene
