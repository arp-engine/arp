#pragma once

#include "core/memory/cpu_allocator.h"
#include "scriptvm/runtime/value.h"

#include <memory>

namespace arp::scriptvm {

struct ScriptModuleGlobalStorage {
    core::pmr::Vector<Value> values{&core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm
