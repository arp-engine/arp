#include "platform/render_system.h"
#include "renderer/terrain_vegetation_gpu_publication.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/terrain_vegetation_gpu_upload.h"

#include <chrono>
#include <cstdio>

int main()
{
    using namespace arp;
    platform::WindowSystem platform;
    if (!platform.initialize().success)
        return 77;
    auto window =
        platform.createWindow({"TerrainVegetationVulkanPublication", 320, 180, true, true});
    if (!window.valid())
        return 77;
    auto system =
        platform::createDefaultRenderSystem(platform, window, "TerrainVegetationVulkanPublication");
    if (!system.valid() || system.backend() != rhi::Backend::Vulkan) {
        std::fprintf(stderr, "Vulkan publication unavailable: %s\n", system.diagnostics().c_str());
        return 77;
    }
    auto& device = *system.gpuDevice();
    tests::RenderDocCaptureScope capture;
    const auto before = rhi_vulkan::inspectVulkanRhiNativeResourceStats(device);
    auto desc = tests::terrainVegetationUploadDescFixture();
    renderer::TerrainVegetationGpuPublication owner(device, desc);
    const auto source = tests::terrainVegetationDrawSourceFixture();
    auto replacement = core::makeShared<renderer::TerrainVegetationSnapshot>(*source);
    ++replacement->terrainResourceGeneration;
    replacement->translation.x += 13.0f;
    for (auto& tile : replacement->tiles)
        for (auto& instance : tile.instances)
            instance.position.x += 13.0f;
    using Status = renderer::TerrainVegetationGpuPublication::Status;
    const auto complete = [&](const auto& candidate) {
        const auto staged = owner.stage(candidate);
        if (staged.status == Status::Failed) {
            std::fprintf(stderr, "Vulkan publication failed: %s\n", staged.diagnostics.c_str());
            return false;
        }
        rhi::semantics::ShutdownDrainRequest request;
        request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto drained = device.drainForShutdown(request);
        if (!drained.completed()) {
            std::fprintf(stderr, "Vulkan completion failed: %s\n", drained.diagnosticId.c_str());
            return false;
        }
        // Recreated, value-equivalent CPU candidate must retain the first exact source.
        auto retry = core::makeShared<renderer::TerrainVegetationSnapshot>(*candidate);
        return owner.stage(retry).status == Status::Ready && owner.preparedSource() == candidate;
    };
    assert(complete(source) && !owner.snapshot());
    owner.publishPrepared();
    auto first = owner.snapshot();
    assert(first && !first->validatorOnly() && first->source()->source == source);
    assert(complete(replacement) && owner.snapshot() == first);
    owner.publishPrepared();
    auto second = owner.snapshot();
    assert(second != first && second->source()->source == replacement);
    assert(desc.liveGenerations->used() == 2u && desc.liveInstances->used() == 8u);
    const auto after = rhi_vulkan::inspectVulkanRhiNativeResourceStats(device);
    assert(after.runtimeConnected);
    assert(after.nativeBufferCreateCount == before.nativeBufferCreateCount + 12u);
    assert(after.nativeBufferCopyRecordCount == before.nativeBufferCopyRecordCount + 8u);
    assert(after.nativeBufferBarrierRecordCount >= before.nativeBufferBarrierRecordCount + 6u);
    assert(after.nativeQueueSubmitCount == before.nativeQueueSubmitCount + 2u);
    assert(owner.stage(nullptr).status == Status::Ready);
    owner.publishPrepared();
    assert(!owner.snapshot() && owner.drainAndClear({}).success);
    assert(desc.liveGenerations->used() == 2u); // Both external readers remain charged.
    capture.end();
    first.reset();
    second.reset();
    assert(!desc.liveGenerations->used() && !desc.liveInstances->used());
    std::puts(
        "Terrain vegetation Vulkan publication PASS: two exact generations, buffers=12 copies=8 submissions=2; no consuming draws");
}
