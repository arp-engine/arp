#pragma once

#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"
#include "rhi/frontend/original_prepared_command_device.h"

namespace arp::render_graph {

// Immutable semantic commands shared by the graph callback and its cold
// original-Program projection. No callback is invoked during projection.
class OriginalFrameGraphPass final {
public:
    enum class Kind { Clear, Compute, Graphics, TextureToBuffer };
    std::shared_ptr<const rhi::OriginalCommandResourceSources> sources;
    Kind kind = Kind::Clear;
    std::size_t pipeline = 0u, descriptors = 0u;
    rhi::Extent3D groups;
    rhi::ViewportState viewport;
    rhi::ScissorState scissor;
    rhi::CommandScalarArguments::Draw draw{3u, 1u, 0u, 0u};
    rhi::TextureToBufferCopyDesc copy;
};

struct OriginalFrameGraphBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    rhi::OriginalCommandInputsBudget inputs;
    rhi::OriginalCommandStateAdmissionBudget states;
    rhi::OriginalCommandStreamBudget stream;
    rhi::OriginalRenderPassPreparationRequest preparation;
    std::uint64_t maxCpuIntentBytes = 0u;
};

// Single-use, explicit Prototype projection into the existing FrameGraph
// executor. The original Program remains the only RHI ownership closure.
// The graph and device must outlive this cold preparation and its execution.
class ARP_API OriginalFrameGraphExecution final {
public:
    OriginalFrameGraphExecution(FrameGraph& graph,
        std::shared_ptr<const rhi::OriginalCommandResourceSources> sources,
        std::shared_ptr<core::CpuMemoryQuota> intentQuota);

    const char* bindAttachment(TextureHandle handle, std::size_t viewSource) noexcept;
    PassBuilder clear(core::pmr::String name);
    PassBuilder compute(core::pmr::String name,
        std::size_t pipeline,
        std::size_t descriptors,
        rhi::Extent3D groups);
    PassBuilder graphics(core::pmr::String name,
        std::size_t pipeline,
        std::size_t descriptors,
        rhi::ViewportState viewport,
        rhi::ScissorState scissor,
        rhi::CommandScalarArguments::Draw draw);
    PassBuilder copy(core::pmr::String name, const rhi::TextureToBufferCopyDesc& copy);

    const char* prepare(const OriginalFrameGraphBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& intentQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& stateQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& recordingQuota);
    void execute();
    const char* cancel() noexcept;

    const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program() const noexcept
    {
        return program_;
    }

    FrameGraph& graph() const noexcept { return graph_; }

    const rhi::OriginalCommandResourceSources& sources() const noexcept { return *sources_; }

private:
    PassBuilder add(core::pmr::String name, const std::shared_ptr<const OriginalFrameGraphPass>&);
    FrameGraph& graph_;
    std::shared_ptr<const rhi::OriginalCommandResourceSources> sources_;
    std::shared_ptr<core::CpuMemoryQuota> intentQuota_;
    core::UniquePtr<CompiledFrameGraph> compiled_;
    std::shared_ptr<const rhi::OriginalCommandStreamProgram> program_;
    core::UniquePtr<rhi::CommandEncoder> encoder_;
    std::shared_ptr<rhi::GpuFence> completion_;
    core::LifetimeEpoch::Ticket graphEpoch_;
    core::pmr::String diagnostic_{&core::defaultCpuMemoryResource()};
    bool executed_ = false;
};

} // namespace arp::render_graph
