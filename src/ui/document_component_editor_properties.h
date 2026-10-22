#pragma once

#include "core/memory/cpu_allocator.h"
#include "reflection/reflection_macros.h"

#include <cstdint>

namespace arp {
namespace ui {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct DocumentComponentEditorProperties {
    APROP(Serialize, Editor)
    core::pmr::String documentSource{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor)
    std::uint32_t layer = 0;
};
} // namespace ui
} // namespace arp
