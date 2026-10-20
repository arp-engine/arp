#pragma once

#include "renderer/render_graph/compiled_frame_graph.h"

namespace arp::render_graph {

class FrameGraph;

class FrameGraphCompiler {
public:
    CompiledFrameGraph compile(const FrameGraph& graph) const;

private:
    void validateGraph(const FrameGraph& graph) const;
};

} // namespace arp::render_graph
