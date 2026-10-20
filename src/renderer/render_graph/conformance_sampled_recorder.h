#pragma once

#include "renderer/render_graph/conformance_recorder.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/conformance/sampled_execution.h"
#include "rhi/conformance/sampled_workload.h"
#include "rhi/gpu_pipeline.h"

namespace arp::render_graph {
using ConformanceSampledRecorderDesc = ConformanceRecorderDesc;

// Cold, bounded conformance preparation. Recording shares immutable original
// resources; no CPU output oracle or native handle participates in execution.
class ConformanceSampledRecorder final {
    class ConstructionKey {
        friend class ConformanceSampledRecorder;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    struct PreparedFrame;

public:
    ConformanceSampledRecorder(ConstructionKey,
        std::shared_ptr<rhi::RenderPipeline>,
        std::shared_ptr<core::CpuMemoryQuota>);
    ~ConformanceSampledRecorder();
    static core::UniquePtr<ConformanceSampledRecorder> prepare(rhi::GpuDevice&,
        std::shared_ptr<rhi::RenderPipeline>,
        const rhi::ConformanceSampledWorkload&,
        const ConformanceSampledRecorderDesc&,
        core::pmr::String& diagnostic,
        rhi::OriginalConformanceSampledExecution* original = nullptr,
        std::shared_ptr<rhi::RenderPipeline> intermediate = {});
    static rhi::RenderPipelineDesc pipelineDefinition(
        const rhi::PipelineLayout&, const rhi::ShaderPackageDesc&, rhi::TextureFormat);
    void record(std::size_t frameIndex, FrameGraph&, TextureHandle);

private:
    std::shared_ptr<rhi::RenderPipeline> pipeline_, intermediate_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::pmr::Vector<std::shared_ptr<PreparedFrame>> frames_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::render_graph
