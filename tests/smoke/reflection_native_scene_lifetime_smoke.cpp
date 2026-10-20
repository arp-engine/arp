#include "scene/scene.h"

#include <cassert>

int main()
{
    using namespace arp::scene;

    Scene scene;
    auto& child =
        scene.createObject(arp::core::pmr::String("child", &arp::core::defaultCpuMemoryResource()));
    auto reference = child.nativeReflectionObject();
    auto copy = reference;

    assert(reference.is_native_reference());
    assert(reference.native_reference_alive());
    assert(reference.can_safe_cast<GameObject*>());
    assert(reference.cast<GameObject*>() == &child);

    const auto result = scene.destroyObject(child.id());
    assert(result.ok());
    assert(!reference.native_reference_alive());
    assert(!copy.native_reference_alive());
    assert(copy.cast<GameObject*>() == nullptr);
    assert(copy.ptr<GameObject>() == nullptr);
    return 0;
}
