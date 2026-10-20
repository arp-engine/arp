#include "scene/component.h"

#include "reflection/db.h"

namespace arp::scene {

Component::Component() : reflectionObject_(arp::reflection::object::native_reference(this))
{}

Component::~Component()
{
    reflectionObject_.invalidate_native_reference();
}

void Component::bindNativeReflectionObject()
{
    auto* mostDerived = dynamic_cast<void*>(this);
    auto* dynamicClass = arp::reflection::db::instance().find_class(scriptTypeName());
    if (dynamicClass == nullptr) {
        dynamicClass = arp::reflection::class_of<Component>();
    }
    bindReflectionObject(arp::reflection::object::native_reference_dynamic(
        mostDerived, dynamicClass, arp::reflection::type_of<Component*>()));
}

void Component::bind(ComponentId id, GameObjectId ownerId, GameObject* owner)
{
    id_ = id;
    ownerId_ = ownerId;
    owner_ = owner;
}

} // namespace arp::scene
