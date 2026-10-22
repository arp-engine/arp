#pragma once

#include "reflection/serialization.h"
#include "scene/component_factory.h"
#include "scene/script_component_module.h"

namespace arp::scriptvm {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ScriptComponentIdentity {
    APROP(Serialize)
    core::pmr::String scriptResourceId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    core::pmr::String scriptType{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    std::uint32_t moduleSchemaVersion = 0;
};

bool registerScriptComponentPersistence(
    scene::SceneComponentFactory&, core::SharedPtr<ScriptComponentModule>);

// TypeNode describes access and type identity only. All YAML walking, numeric
// checks, budgets and references are handled by SerializationArchive.

} // namespace arp::scriptvm
