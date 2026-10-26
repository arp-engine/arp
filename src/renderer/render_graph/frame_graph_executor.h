#pragma once

#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/ordered_graphics_measurements.h"
#include "rhi/frontend/ordered_graphics_execution.h"

namespace arp::render_graph {

class FrameGraph;

class FrameGraphExecutor {
public:
    explicit FrameGraphExecutor(FrameGraph& graph);
    void execute(const CompiledFrameGraph& compiled);
    void executeConformance(
        const CompiledFrameGraph&, rhi::OriginalConformanceClearExecution&, rhi::GpuTexture&);

private:
    friend class FrameGraph;
    friend class OriginalFrameGraphExecution;
    void executeImpl(const CompiledFrameGraph& compiled,
        core::UniquePtr<rhi::CommandEncoder> encoder,
        rhi::OriginalOrderedGraphicsExecution* ordered,
        OrderedGraphicsMeasurements* measurements = nullptr,
        rhi::OriginalConformanceClearExecution* conformance = nullptr,
        std::shared_ptr<rhi::GpuFence> preparedCompletion = {},
        bool preparedTrace = false);
    FrameGraph& graph_;
};

} // namespace arp::render_graph
