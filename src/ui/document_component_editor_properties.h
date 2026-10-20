#pragma once

#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"

#include <cstdint>

namespace arp {
namespace ui {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct DocumentComponentEditorProperties {
    APROP(Editor)
    core::pmr::String documentSource{&core::defaultCpuMemoryResource()};
    APROP(Editor)
    std::uint32_t layer = 0;
};
} // namespace ui
} // namespace arp
