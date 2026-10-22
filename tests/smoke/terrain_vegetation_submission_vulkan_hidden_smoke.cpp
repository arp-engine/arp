#include "platform/render_system.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/terrain_vegetation_gpu_upload.h"
#include "tests/fixtures/terrain_vegetation_submission.h"

#include <chrono>
#include <cstdio>

int main()
{
    using namespace arp;
    platform::WindowSystem platform;
    if (!platform.initialize().success)
        return 77;
    auto window = platform.createWindow({"TerrainVegetationSubmission", 320, 180, true, true});
    if (!window.valid())
        return 77;
    auto system =
        platform::createDefaultRenderSystem(platform, window, "TerrainVegetationSubmission");
    if (!system.valid() || system.backend() != rhi::Backend::Vulkan) {
        std::fprintf(stderr, "Vulkan consumption unavailable: %s\n", system.diagnostics().c_str());
        return 77;
    }
    auto& device = *system.gpuDevice();
    tests::RenderDocCaptureScope capture;
    const auto before = rhi_vulkan::inspectVulkanRhiNativeResourceStats(device);
    const auto drain = [&] {
        rhi::semantics::ShutdownDrainRequest request;
        request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto report = device.drainForShutdown(request);
        if (!report.completed())
            std::fprintf(stderr, "Vulkan drain failed: %s\n", report.diagnosticId.c_str());
        assert(report.completed());
    };
    auto uploadDesc = tests::terrainVegetationUploadDescFixture();
    renderer::TerrainVegetationGpuUpload ticket;
    assert(ticket.prepare(device, tests::terrainVegetationDrawFixture(), uploadDesc).success);
    assert(ticket.submit().success);
    drain();
    assert(ticket.poll());
    auto snapshot = ticket.snapshot();
    assert(snapshot && !snapshot->validatorOnly());
    const std::weak_ptr<const renderer::TerrainVegetationGpuSnapshot> weak = snapshot;
    auto desc = tests::terrainVegetationSubmissionDescFixture();
    renderer::TerrainVegetationSubmission submission(device);
    assert(submission.prepare(snapshot, desc).success);
    tests::TerrainVegetationTransferProbe probe;
    {
        render_graph::FrameGraph graph(device);
        probe.addTo(graph, device, submission);
        graph.execute();
        assert(graph.lastExecutionSubmitted());
        assert(submission.state() == renderer::TerrainVegetationSubmission::State::Accepted);
    } // Graph no longer provides any input/output ownership.
    assert(ticket.cancel());
    snapshot.reset();
    assert(!weak.expired() && uploadDesc.liveInstances->used() == 4u);
    assert(desc.liveSubmissions->used() == 1u);
    const auto after = rhi_vulkan::inspectVulkanRhiNativeResourceStats(device);
    assert(after.runtimeConnected);
    assert(after.nativeBufferCreateCount == before.nativeBufferCreateCount + 9u);
    assert(after.nativeBufferCopyRecordCount == before.nativeBufferCopyRecordCount + 7u);
    assert(after.nativeBufferBarrierRecordCount >= before.nativeBufferBarrierRecordCount + 6u);
    assert(after.nativeQueueSubmitCount == before.nativeQueueSubmitCount + 2u);
    drain();
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    assert(submission.poll());
    assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
    assert(weak.expired() && !uploadDesc.liveInstances->used());
    assert(!uploadDesc.liveGenerations->used() && !desc.liveSubmissions->used());
    capture.end();
    std::puts(
        "Terrain vegetation Vulkan consumption PASS: buffers=9 copies=7 submissions=2; graph/upload released before consuming completion retirement; no draws");
}
