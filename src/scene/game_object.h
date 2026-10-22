#pragma once

#include "core/memory/cpu_allocator.h"
#include "reflection/object.h"
#include "reflection/reflection_macros.h"
#include "scene/component.h"
#include "scene/diagnostics.h"
#include "scene/ids.h"
#include "scene/transform_component.h"

#include <cassert>
#include <type_traits>
#include <utility>

namespace arp::scene {

class Scene;

ACLASS(Name = "GameObject", Ownership = Engine, Lifecycle = Explicit)

class GameObject {
public:
    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;
    virtual ~GameObject();

    GameObjectId id() const { return id_; }

    const arp::core::pmr::String& name() const { return name_; }

    void setName(const arp::core::pmr::String& name) { name_ = name; }

    AFUNC()

    std::uint64_t object_id() const { return id_.value(); }
    AFUNC()

    arp::core::pmr::String object_name() const
    {
        return arp::core::pmr::String(name_, &arp::core::defaultCpuMemoryResource());
    }
    AFUNC()

    void set_object_name(const arp::core::pmr::String& value) { setName(value); }

    GameObjectId parentId() const { return parentId_; }

    const arp::core::pmr::Vector<GameObjectId>& childIds() const { return childIds_; }

    bool active() const { return active_; }

    void setActive(bool active) { active_ = active; }

    bool activeInHierarchy() const;

    AFUNC()

    bool is_active() const { return active(); }
    AFUNC()

    bool is_active_in_hierarchy() const { return activeInHierarchy(); }
    AFUNC()

    void set_active(bool value) { setActive(value); }
    AFUNC()
    GameObject* parent_object() const;
    AFUNC()

    std::uint32_t child_count() const { return static_cast<std::uint32_t>(childIds_.size()); }
    AFUNC()
    GameObject* child_at(std::uint32_t index) const;

    TransformComponent& transform();
    const TransformComponent& transform() const;

    AFUNC()

    Transform local_transform() const { return transform().localTransform(); }
    AFUNC()
    Transform world_transform() const;
    AFUNC()

    void set_local_transform(const Transform& value) { transform().setLocalTransform(value); }
    AFUNC()
    bool set_world_transform(const Transform& value);

    const arp::core::pmr::Vector<ComponentId>& componentIds() const { return componentIds_; }

    const arp::core::pmr::Vector<arp::core::UniquePtr<Component>>& components() const
    {
        return components_;
    }

    AFUNC()

    std::uint32_t component_count() const { return static_cast<std::uint32_t>(components_.size()); }
    AFUNC()
    Component* component_at(std::uint32_t index) const;
    AFUNC()
    Component* create_component(const arp::core::pmr::String& type);
    AFUNC()
    arp::reflection::object add_component(const arp::core::pmr::String& type);
    AFUNC()
    arp::reflection::object get_component(const arp::core::pmr::String& type) const;
    AFUNC()
    bool send_event(const arp::core::pmr::String& name, const arp::reflection::object& payload);

    Scene* scene() const { return scene_; }

    bool pendingDestroy() const { return pendingDestroy_; }

    Component& addComponent(arp::core::UniquePtr<Component> component)
    {
        return attachComponent(std::move(component));
    }

    Component* findComponent(ComponentId componentId);
    const Component* findComponent(ComponentId componentId) const;
    // TransformComponent is mandatory for every object and cannot be removed.
    bool removeComponent(ComponentId componentId);

    AFUNC()

    bool remove_component(std::uint64_t componentId)
    {
        return removeComponent(ComponentId::fromValue(componentId));
    }
    AFUNC()
    bool destroy_self();
    AFUNC()
    bool set_parent_object(GameObject* parent);

    // Native return boxing uses this canonical object so every script copy
    // shares the same invalidation control as the engine-owned registry entry.
    const arp::reflection::object& nativeReflectionObject() const { return reflectionObject_; }

    template <typename ComponentType> ComponentType* getComponent()
    {
        static_assert(std::is_base_of<Component, ComponentType>::value,
            "ComponentType must derive from Component");
        for (auto& component : components_) {
            if (auto* typed = dynamic_cast<ComponentType*>(component.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    template <typename ComponentType> const ComponentType* getComponent() const
    {
        static_assert(std::is_base_of<Component, ComponentType>::value,
            "ComponentType must derive from Component");
        for (const auto& component : components_) {
            if (const auto* typed = dynamic_cast<const ComponentType*>(component.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    template <typename ComponentType, typename... Args> ComponentType& addComponent(Args&&... args)
    {
        static_assert(std::is_base_of<Component, ComponentType>::value,
            "ComponentType must derive from Component");
        auto component = arp::core::makeUnique<ComponentType>(std::forward<Args>(args)...);
        auto& attached = attachComponent(std::move(component));
        attached.bindReflectionObject(
            arp::reflection::object::native_reference(static_cast<ComponentType*>(&attached)));
        return static_cast<ComponentType&>(attached);
    }

protected:
    GameObject(GameObjectId id, arp::core::pmr::String name, Scene* scene);

private:
    template <class T, class... Args>
    friend arp::core::UniquePtr<T> arp::core::makeUnique(Args&&... args);
    friend class Scene;

    void bindScene(Scene* scene) { scene_ = scene; }

    void bindReflectionObject(arp::reflection::object value)
    {
        reflectionObject_ = std::move(value);
    }

    void initializeMandatoryTransform(ComponentId componentId);
    Component& attachComponent(arp::core::UniquePtr<Component> component);
    void appendChild(GameObjectId childId);
    bool insertChildAt(GameObjectId childId, std::size_t index);
    bool reorderChild(GameObjectId childId, std::size_t index);
    void removeChild(GameObjectId childId);

    void setParentId(GameObjectId parentId) { parentId_ = parentId; }

    GameObjectId id_;
    GameObjectId parentId_;
    arp::core::pmr::String name_;
    Scene* scene_ = nullptr;
    bool active_ = true;
    bool pendingDestroy_ = false;
    TransformComponent* transform_ = nullptr;
    arp::core::pmr::Vector<ComponentId> componentIds_;
    arp::core::pmr::Vector<arp::core::UniquePtr<Component>> components_;
    arp::core::pmr::Vector<GameObjectId> childIds_;
    arp::reflection::object reflectionObject_;
};

} // namespace arp::scene
