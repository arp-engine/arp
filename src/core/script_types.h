#pragma once

#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"

#include <cstdint>

namespace arp::core {

// Shared value-level error contract for domain plugins. Native services may
// retain richer diagnostics internally, but script modules exchange this
// stable projection instead of leaking native error classes.
ASTRUCT(Name = "ScriptResult", Construct = Default, Ownership = Value, Lifecycle = Value)

struct ScriptResult {
    APROP(Runtime)
    bool success = false;
    APROP(Runtime)
    std::int32_t code = 0;
    APROP(Runtime)
    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::core
