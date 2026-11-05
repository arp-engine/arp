#pragma once

#include "core/memory/cpu_allocator.h"

namespace arp::rhi {
class GpuDevice;
}

namespace arp::render_graph {
class FrameGraph;
class CompiledFrameGraph;

// FrameGraphCompilationCache/1. CPU metadata memoization for exact flat,
// ordinary graph recipes on one live device. No callback, submission token,
// native resource or completion authority is retained. Non-cacheable graphs
// use the unchanged compiler and all execution checks remain mandatory.
class ARP_API FrameGraphCompileCache final {
public:
    explicit FrameGraphCompileCache(rhi::GpuDevice& device, std::size_t maxEntries);
    core::SharedPtr<const CompiledFrameGraph> compile(FrameGraph& graph);

    std::uint64_t hits() const { return hits_; }

    std::uint64_t misses() const { return misses_; }

    std::uint64_t uncached() const { return uncached_; }

    std::size_t entryCount() const { return entries_.size(); }

    void clear();

private:
    struct Entry {
        core::pmr::Vector<std::uint8_t> key{&core::defaultCpuMemoryResource()};
        core::SharedPtr<const CompiledFrameGraph> compiled;
        std::uint64_t touched = 0;
    };

    bool makeKey(const FrameGraph& graph);
    rhi::GpuDevice& device_;
    std::size_t capacity_ = 0;
    std::uint64_t clock_ = 0, hits_ = 0, misses_ = 0, uncached_ = 0;
    core::pmr::Vector<std::uint8_t> key_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::render_graph
