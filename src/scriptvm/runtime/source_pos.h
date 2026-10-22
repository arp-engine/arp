#pragma once

// ArpScript: source position tracking for diagnostics and debugging.
// Pure header-only; backed by core::pmr for the filename string.

#include "core/memory/cpu_allocator.h"

#include <cstdint>

namespace arp::scriptvm {

struct SourcePos {
    core::pmr::String file{&core::defaultCpuMemoryResource()};
    int line = 0;   // 1-based; 0 = unknown
    int column = 0; // 1-based; 0 = unknown

    SourcePos() = default;

    SourcePos(core::pmr::String f, int ln, int col) : file(std::move(f)), line(ln), column(col) {}

    bool valid() const { return line > 0; }

    bool operator==(const SourcePos& rhs) const
    {
        return line == rhs.line && column == rhs.column && file == rhs.file;
    }
};

} // namespace arp::scriptvm
