#include "scene/game_object.h"

#include "scene/scene.h"

#include <algorithm>

namespace arp::scene {

GameObject::GameObject(GameObjectId id, arp::core::pmr::String name, Scene* scene)
    : id_(id), name_(std::move(name)), scene_(scene),
      reflectionObject_(arp::reflection::object::native_reference(this))
{}

GameObject::~GameObject()
{
    reflectionObject_.invalidate_native_reference();
}

Transform GameObject::world_transform() const
{
    return scene_ ? scene_->computeWorldTransform(id_) : transform().localTransform();
}

Transform TransformComponent::world_transform() const
{
    return gameObject() ? gameObject()->world_transform() : localTransform_;
}

bool GameObject::activeInHierarchy() const
{
    if (!active_ || pendingDestroy_) {
        return false;
    }
    if (!parentId_.valid() || scene_ == nullptr) {
        return true;
    }
    const auto* parent = scene_->findObject(parentId_);
    return parent != nullptr && parent->activeInHierarchy();
}

GameObject* GameObject::parent_object() const
{
    if (scene_ == nullptr || !parentId_.valid())
        return nullptr;
    return scene_->findObject(parentId_);
}

GameObject* GameObject::child_at(std::uint32_t index) const
{
    if (scene_ == nullptr || index >= childIds_.size())
        return nullptr;
    return scene_->findObject(childIds_[index]);
}

Component* GameObject::component_at(std::uint32_t index) const
{
    return index < components_.size() ? components_[index].get() : nullptr;
}

Component* GameObject::create_component(const arp::core::pmr::String& type)
{
    return scene_ != nullptr ? scene_->createComponentByName(*this, type) : nullptr;
}

arp::reflection::object GameObject::add_component(const arp::core::pmr::String& type)
{
    auto* component = create_component(type);
    if (!component)
        return {};
    auto instance = component->scriptInstance();
    return !instance.is_undefined() ? instance : component->nativeReflectionObject();
}

arp::reflection::object GameObject::get_component(const arp::core::pmr::String& type) const
{
    for (const auto& component : components_) {
        if (!component->pendingDestroy() && type == component->scriptTypeName()) {
            auto instance = component->scriptInstance();
            return !instance.is_undefined() ? instance : component->nativeReflectionObject();
        }
    }
    return {};
}

bool GameObject::send_event(
    const arp::core::pmr::String& name, const arp::reflection::object& payload)
{
    return scene_ && scene_->queueEvent({id_, name, payload});
}

bool GameObject::set_world_transform(const Transform& value)
{
    return scene_ != nullptr && scene_->setWorldTransform(id_, value).ok();
}

bool GameObject::destroy_self()
{
    return scene_ != nullptr && scene_->destroyObject(id_).ok();
}

bool GameObject::set_parent_object(GameObject* parent)
{
    return scene_ != nullptr && parent != nullptr && scene_->reparent(id_, parent->id_).ok();
}

TransformComponent& GameObject::transform()
{
    assert(transform_ != nullptr);
    return *transform_;
}

const TransformComponent& GameObject::transform() const
{
    assert(transform_ != nullptr);
    return *transform_;
}

Component* GameObject::findComponent(ComponentId componentId)
{
    for (auto& component : components_) {
        if (component->id() == componentId)
            return component.get();
    }
    return nullptr;
}

const Component* GameObject::findComponent(ComponentId componentId) const
{
    for (const auto& component : components_) {
        if (component->id() == componentId)
            return component.get();
    }
    return nullptr;
}

bool GameObject::removeComponent(ComponentId componentId)
{
    if (!componentId.valid() || transform_ == findComponent(componentId))
        return false;
    const auto found =
        std::find_if(components_.begin(), components_.end(), [componentId](const auto& component) {
            return component->id() == componentId;
        });
    if (found == components_.end())
        return false;
    if ((*found)->pendingDestroy())
        return true;
    if (scene_ && scene_->dispatching_) {
        (*found)->pendingDestroy_ = true;
        return true;
    }
    auto owned = std::move(*found);
    componentIds_.erase(
        std::remove(componentIds_.begin(), componentIds_.end(), componentId), componentIds_.end());
    components_.erase(found);
    const bool wasDispatching = scene_ && scene_->dispatching_;
    if (scene_)
        scene_->dispatching_ = true;
    SceneUpdateContext context;
    if (owned->activeInScene()) {
        owned->setActiveInScene(false);
        owned->onDisable(context);
    }
    owned->onDestroy(context);
    if (scene_) {
        if (!wasDispatching)
            scene_->flushDestroyed(context);
        scene_->dispatching_ = wasDispatching;
    }
    return true;
}

void GameObject::initializeMandatoryTransform(ComponentId componentId)
{
    auto transform = arp::core::makeUnique<TransformComponent>();
    transform->bindReflectionObject(arp::reflection::object::native_reference(transform.get()));
    transform->bind(componentId, id_, this);
    transform_ = transform.get();
    componentIds_.push_back(componentId);
    components_.push_back(std::move(transform));
}

Component& GameObject::attachComponent(arp::core::UniquePtr<Component> component)
{
    assert(scene_ != nullptr);
    const auto componentId = scene_->allocateComponentId();
    component->bind(componentId, id_, this);
    auto* attached = component.get();
    componentIds_.push_back(componentId);
    components_.push_back(std::move(component));
    return *attached;
}

void GameObject::appendChild(GameObjectId childId)
{
    if (std::find(childIds_.begin(), childIds_.end(), childId) == childIds_.end()) {
        childIds_.push_back(childId);
    }
}

bool GameObject::insertChildAt(GameObjectId childId, std::size_t index)
{
    if (index > childIds_.size()
        || std::find(childIds_.begin(), childIds_.end(), childId) != childIds_.end())
        return false;
    childIds_.insert(childIds_.begin() + static_cast<std::ptrdiff_t>(index), childId);
    return true;
}

bool GameObject::reorderChild(GameObjectId childId, std::size_t index)
{
    if (index >= childIds_.size())
        return false;
    const auto found = std::find(childIds_.begin(), childIds_.end(), childId);
    if (found == childIds_.end())
        return false;
    const auto currentIndex = static_cast<std::size_t>(found - childIds_.begin());
    if (currentIndex == index)
        return true;
    childIds_.erase(found);
    childIds_.insert(childIds_.begin() + static_cast<std::ptrdiff_t>(index), childId);
    return true;
}

void GameObject::removeChild(GameObjectId childId)
{
    childIds_.erase(std::remove(childIds_.begin(), childIds_.end(), childId), childIds_.end());
}

} // namespace arp::scene
