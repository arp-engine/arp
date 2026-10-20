#pragma once
#include "renderer/render_graph/conformance_recorder.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/conformance/storage_mrt_execution.h"
#include "rhi/conformance/storage_mrt_workload.h"

namespace arp::render_graph {
// One cold preparation owns original resources; each graph retains its frame.
class ConformanceStorageMrtRecorder final {
    class ConstructionKey {
        friend class ConformanceStorageMrtRecorder;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };
    struct PreparedFrame;

public:
    ConformanceStorageMrtRecorder(ConstructionKey,
        std::shared_ptr<rhi::RenderPipeline>,
        std::shared_ptr<rhi::ComputePipeline>,
        std::shared_ptr<core::CpuMemoryQuota>);
    ~ConformanceStorageMrtRecorder();
    static rhi::PipelineLayoutDesc layoutDefinition(const rhi::ShaderPackageDesc&);
    static rhi::RenderPipelineDesc pipelineDefinition(
        const rhi::PipelineLayout&, const rhi::ShaderPackageDesc&, rhi::TextureFormat);
    static core::UniquePtr<ConformanceStorageMrtRecorder> prepare(rhi::GpuDevice&,
        std::shared_ptr<rhi::RenderPipeline>,
        std::shared_ptr<rhi::ComputePipeline>,
        const rhi::ConformanceStorageMrtWorkload&,
        const ConformanceRecorderDesc&,
        core::pmr::String& diagnostic,
        rhi::OriginalConformanceStorageMrtExecution* original = nullptr);
    void record(std::size_t frameIndex, FrameGraph&, TextureHandle target);

private:
    std::shared_ptr<rhi::RenderPipeline> pipeline_;
    std::shared_ptr<rhi::ComputePipeline> compute_;
    std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::pmr::Vector<std::shared_ptr<PreparedFrame>> frames_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::render_graph
