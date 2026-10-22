#pragma once

#include "core/memory/cpu_allocator.h"
#include "renderer/render_graph/render_graph.h"

namespace arp::render_graph {

class FrameGraphBuilder {
public:
    explicit FrameGraphBuilder(FrameGraph& graph) : graph_(graph) {}

    PassBuilder addPass(std::pmr::string name) { return graph_.addPass(std::move(name)); }

private:
    FrameGraph& graph_;
};

} // namespace arp::render_graph
