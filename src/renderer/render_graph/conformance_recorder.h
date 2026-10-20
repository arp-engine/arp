#pragma once

#include "core/cpu_allocator.h"

namespace arp::render_graph {
struct ConformanceRecorderDesc {
    std::uint32_t contractVersion = 0, maxPreparedFrames = 0;
    bool prototypeOptIn = false;
    std::uint64_t maxCpuMetadataBytes = 0;
    std::shared_ptr<core::CpuMemoryQuota> quota;
};
} // namespace arp::render_graph
