#pragma once

#include "renderer/terrain_vegetation_submission.h"

#include <cassert>

namespace arp::tests {

inline renderer::TerrainVegetationSubmission::Desc terrainVegetationSubmissionDescFixture()
{
    renderer::TerrainVegetationSubmission::Desc desc;
    desc.maxCpuBytes = 4096u;
    desc.liveSubmissions = core::makeShared<core::AtomicCapacityBudget>(2u);
    return desc;
}

// Transfer-only consuming probe. Outputs remain explicitly caller-owned until
// completion; only the input generation is retained by the submission owner.
class TerrainVegetationTransferProbe final {
public:
    core::pmr::Vector<std::shared_ptr<rhi::GpuBuffer>> outputs{&core::defaultCpuMemoryResource()};

    void addTo(render_graph::FrameGraph& graph,
        rhi::GpuDevice& device,
        renderer::TerrainVegetationSubmission& submission,
        bool failRecording = false,
        render_graph::PassSubmissionObserver* observer = nullptr)
    {
        const auto& snapshot = submission.snapshot();
        const auto token = submission.token();
        bool observed = false;
        for (const auto& tile : snapshot->tiles())
            for (const auto& prototype : tile.prototypes) {
                const auto bytes = prototype.buffer->desc().sizeBytes;
                auto owned = core::makeShared<core::UniquePtr<rhi::GpuBuffer>>(
                    device.createBuffer({"TerrainVegetation.ConsumptionReadback",
                        bytes,
                        rhi::ResourceUsage::CopyDst,
                        rhi::MemoryUsage::GpuToCpu,
                        rhi::ResourceState::CopyDst}));
                assert(*owned);
                outputs.emplace_back(owned, owned->get());
                const auto input = graph.importBufferRef({snapshot, prototype.buffer.get()});
                const auto output = graph.importBufferRef(outputs.back());
                auto& builder = graph.addPass("TerrainVegetation.ConsumeInstances")
                                    .kind(render_graph::PassKind::Copy)
                                    .read(input, rhi::ResourceState::CopySrc)
                                    .write(output, rhi::ResourceState::CopyDst)
                                    .externalSideEffects();
                if (!observed) {
                    builder.onSubmitted(observer ? *observer : submission, token);
                    observed = true;
                }
                builder.execute([&graph, &submission, token, input, output, bytes, failRecording](
                                    render_graph::RenderContext& context) {
                    const auto before = core::defaultCpuAllocator().stats();
                    const auto* diagnostic = submission.beginUse(context, token);
                    assert(before.allocationCount
                        == core::defaultCpuAllocator().stats().allocationCount);
                    if (diagnostic)
                        throw core::DiagnosticError(diagnostic);
                    if (failRecording)
                        throw core::DiagnosticError("FIXTURE-CONSUMING-RECORD-FAILED");
                    auto pass = context.commands().beginTransferPass(
                        {"TerrainVegetation.ConsumeInstances"});
                    assert(pass);
                    const auto copied = pass->copyBufferToBuffer(
                        {graph.buffer(input).id(), graph.buffer(output).id(), 0u, 0u, bytes});
                    assert(copied.success && pass->end().success);
                });
            }
    }
};

} // namespace arp::tests
