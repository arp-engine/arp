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

// Explicit, bounded registration snapshot. The stable persistence key remains
// arp.script-component/1; resource ID/schema/type determine the selected module.
bool registerScriptComponentPersistence(scene::SceneComponentFactory&,
    core::pmr::Vector<core::SharedPtr<ScriptComponentModule>>,
    std::size_t maxModules,
    core::pmr::String& diagnostic);

// Cold fields have no VM receiver or initialized/default gameplay values.
struct ScriptComponentInspectionValues {
    core::pmr::Map<core::pmr::String, reflection::object> fields;
};

core::SharedPtr<reflection::SerializationDescriptor> scriptComponentFieldDescriptor(
    const ScriptComponentModule&, const ScriptComponentModule::Type&);

// TypeNode describes access and type identity only. All YAML walking, numeric
// checks, budgets and references are handled by SerializationArchive.

} // namespace arp::scriptvm
