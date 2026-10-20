#pragma once

#include "reflection/reflection.h"

namespace arp::reflection {

struct ReflectionYamlResult {
    bool success = false;
    core::pmr::String yaml{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct ReflectionObjectResult {
    bool success = false;
    object value;
    core::pmr::Vector<object> ownedObjects = core::pmr::Vector<object>(
        core::pmr::Vector<object>::allocator_type(&core::defaultCpuMemoryResource()));
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

ARP_API ReflectionYamlResult serialize_to_yaml(const object& value);
ARP_API ReflectionObjectResult deserialize_from_yaml(NameView yamlText);

} // namespace arp::reflection
