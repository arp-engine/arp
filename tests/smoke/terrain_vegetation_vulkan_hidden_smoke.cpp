#include "platform/render_system.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/terrain_vegetation_gpu_upload.h"

#include <chrono>
#include <cstdio>

int main()
{
    using namespace arp;
    platform::WindowSystem platform;
    const auto initialized = platform.initialize();
    if (!initialized.success)
        return 77;
    auto window = platform.createWindow({"TerrainVegetationVulkanUpload", 320, 180, true, true});
    if (!window.valid())
        return 77;
    auto system =
        platform::createDefaultRenderSystem(platform, window, "TerrainVegetationVulkanUpload");
    if (!system.valid() || system.backend() != rhi::Backend::Vulkan) {
        std::fprintf(stderr, "Vulkan upload unavailable: %s\n", system.diagnostics().c_str());
        return 77;
    }
    auto& device = *system.gpuDevice();
    tests::RenderDocCaptureScope capture;
    const auto before = rhi_vulkan::inspectVulkanRhiNativeResourceStats(device);
    const auto source = tests::terrainVegetationDrawFixture();
    auto desc = tests::terrainVegetationUploadDescFixture();
    renderer::TerrainVegetationGpuUpload ticket;
    const auto prepared = ticket.prepare(device, source, desc);
    if (!prepared.success) {
        std::fprintf(stderr, "Vulkan upload prepare failed: %s\n", prepared.diagnostics.c_str());
        return 1;
    }
    assert(ticket.decision().executable && !ticket.decision().validatorOnly);
    assert(!ticket.snapshot() && !ticket.poll());
    const auto submitted = ticket.submit();
    rhi::semantics::ShutdownDrainRequest request;
    request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto drained = device.drainForShutdown(request);
    if (!submitted.success || !drained.completed()) {
        std::fprintf(stderr,
            "Vulkan submit/drain failed: %s / %s\n",
            submitted.diagnostics.c_str(),
            drained.diagnosticId.c_str());
        if (drained.completed())
            (void)ticket.cancel();
        return 1;
    }
    assert(ticket.poll());
    auto reader = ticket.snapshot();
    tests::checkTerrainVegetationGpuSnapshot(*reader, source);
    assert(!reader->validatorOnly());
    const auto after = rhi_vulkan::inspectVulkanRhiNativeResourceStats(device);
    assert(after.runtimeConnected);
    assert(after.nativeBufferCreateCount == before.nativeBufferCreateCount + 6u);
    assert(after.nativeBufferCopyRecordCount == before.nativeBufferCopyRecordCount + 4u);
    assert(after.nativeBufferBarrierRecordCount >= before.nativeBufferBarrierRecordCount + 3u);
    assert(after.nativeQueueSubmitCount == before.nativeQueueSubmitCount + 1u);
    assert(
        ticket.cancel() && desc.liveGenerations->used() == 1u && desc.liveInstances->used() == 4u);
    reader.reset();
    assert(!desc.liveGenerations->used() && !desc.liveInstances->used());
    capture.end();
    std::printf(
        "Terrain vegetation Vulkan upload PASS: native buffers=6 copies=4 barriers=%u submissions=1; exact fence completed; retained readers charged/released\n",
        after.nativeBufferBarrierRecordCount - before.nativeBufferBarrierRecordCount);
}
