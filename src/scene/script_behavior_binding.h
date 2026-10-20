#pragma once

#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"

namespace arp::scene {

// Stable, name-based binding metadata shared by the interpreter and AOT
// lifecycle dispatchers. It is not an object carrier and does not resolve
// Scene/GameObject/Component references.
ASTRUCT(Construct = Default)

struct ScriptBehaviorBinding {
    APROP(Runtime)
    arp::core::pmr::String functionPrefix{&arp::core::defaultCpuMemoryResource()};
    APROP(Runtime)
    arp::core::pmr::String receiverType{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scene
