#include "../fixtures/renderdoc_capture.h"
#include "../fixtures/vulkan_wsi_failure_retention_fixture.h"
#include "platform/render_system.h"
#include "renderer/render_graph/render_graph_resource_management_smoke.h"
#include "rhi/backends/vulkan/vulkan_device.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using arp::tests::RenderDocCaptureScope;

int main(int argc, char** argv)
{
    {
        arp::platform::RenderSystemDesc unsupported;
        unsupported.backendPreference = arp::platform::BackendPreference::WebGL;
        auto refused = arp::platform::createRenderSystem(unsupported);
        assert(!refused.valid() && refused.backend() == arp::rhi::Backend::Null);
        assert(!refused.capabilities().canRender);
        assert(refused.diagnostics() == "RHI-WEBGL1-RENDERSYSTEM-UNAVAILABLE");
        assert(!refused.gpuDevice() && !refused.swapchain());
    }
    if (argc == 2 && std::strcmp(argv[1], "--feature-gate-only") == 0) {
        std::puts("WebGl1Graphics/1 original production factory refusal: PASS");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--wsi-failure-retention") == 0)
        return arp::tests::VulkanWsiFailureRetentionFixture::run();
    if (argc == 2 && std::strcmp(argv[1], "--wsi-maintenance-failure-retention") == 0)
        return arp::tests::VulkanWsiFailureRetentionFixture::run(true);
    assert(argc == 1);
    arp::platform::WindowSystem platform;
    const auto initialized = platform.initialize();
    if (!initialized.success) {
        std::fprintf(
            stderr, "Vulkan hidden smoke unavailable: %s\n", initialized.diagnostics.c_str());
        return 0;
    }
    auto window =
        platform.createWindow({"RenderGraphResourceManagementVulkanSmoke", 320, 180, true, true});
    if (!window.valid()) {
        std::fprintf(stderr, "Vulkan hidden smoke unavailable: window creation failed\n");
        return 0;
    }
    auto renderSystem = arp::platform::createDefaultRenderSystem(
        platform, window, "RenderGraphResourceManagementVulkanSmoke");
    if (!renderSystem.valid() || renderSystem.backend() != arp::rhi::Backend::Vulkan) {
        std::fprintf(
            stderr, "Vulkan hidden smoke unavailable: %s\n", renderSystem.diagnostics().c_str());
        return 0;
    }
    auto* device = renderSystem.gpuDevice();
    auto* swapchain = renderSystem.swapchain();
    assert(device);
    assert(swapchain);
    const auto capabilities = device->capabilities();
    assert(capabilities.core.graphics);
    assert(capabilities.explicitFeatures.explicitBarriers);
    assert(capabilities.memory.deviceLocal && capabilities.memory.hostVisible);
    assert(capabilities.swapchain.requestedImageCount > 0u);
    assert(capabilities.swapchain.maxFramesInFlight > 0u);
    assert(!capabilities.features.computeDispatch || capabilities.core.compute);
    assert(capabilities.features.transientAliasing);
    std::fprintf(stderr,
        "[device-capability-seed] graphics=%u compute=%u deviceLocal=%u hostVisible=%u request=%u slots=%u\n",
        unsigned(capabilities.core.graphics),
        unsigned(capabilities.core.compute),
        unsigned(capabilities.memory.deviceLocal),
        unsigned(capabilities.memory.hostVisible),
        capabilities.swapchain.requestedImageCount,
        capabilities.swapchain.maxFramesInFlight);
    RenderDocCaptureScope capture;
    arp::render_graph::RenderGraphResourceManagementSmokeRunner runner(*device, swapchain);
    const auto report = runner.run({2, {64, 64, 1}, arp::rhi::TextureFormat::Rgba8Unorm});
    if (!report.success) {
        std::fprintf(stderr,
            "Vulkan resource-management smoke failed: %s frames=%u handoffs=%llu "
            "premature=%llu barriers=%llu fallbacks=%llu final=%d budget=%d "
            "hits=%llu evictions=%llu pending=%u cached=%llu budgetBytes=%llu\n",
            report.diagnostics.c_str(),
            report.completedFrameCount,
            static_cast<unsigned long long>(report.aliasHandoffCount),
            static_cast<unsigned long long>(report.prematureReuseCount),
            static_cast<unsigned long long>(report.barrierFailureCount),
            static_cast<unsigned long long>(report.fallbackCount),
            report.externalFinalStateCorrect ? 1 : 0,
            report.budgetWithinBounds ? 1 : 0,
            static_cast<unsigned long long>(report.pool.hitCount),
            static_cast<unsigned long long>(report.pool.evictionCount),
            report.pool.pendingResourceCount,
            static_cast<unsigned long long>(report.pool.cachedBytes),
            static_cast<unsigned long long>(report.pool.byteBudget));
        return 1;
    }
    assert(report.aliasHandoffCount == report.requestedFrameCount);
    assert(report.prematureReuseCount == 0);
    assert(report.barrierFailureCount == 0);
    assert(report.fallbackCount == 0);
    assert(report.budgetWithinBounds);
    const auto nativeStats = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*device);
    assert(nativeStats.nativeSwapchainSubmitWaitCount == report.requestedFrameCount);
    assert(nativeStats.nativeSwapchainSubmitSignalCount == report.requestedFrameCount);
    assert(nativeStats.nativeSwapchainPresentCount == report.requestedFrameCount);
    arp::rhi::semantics::ShutdownDrainRequest drainRequest;
    drainRequest.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto drain = renderSystem.drainGpuForShutdown(drainRequest);
    if (!drain.completed()) {
        std::fprintf(
            stderr, "Vulkan bounded shutdown drain failed: %s\n", drain.diagnosticId.c_str());
        return 1;
    }
    assert(drain.outcome == arp::rhi::semantics::ShutdownDrainOutcome::Native);
    renderSystem.waitGpuIdle();
    capture.end();

    window.resize(400, 225);
    const auto resized = renderSystem.resize({
        static_cast<std::uint32_t>(window.pixelWidth()),
        static_cast<std::uint32_t>(window.pixelHeight()),
    });
    if (!resized.success) {
        std::fprintf(stderr, "Vulkan bounded resize failed: %s\n", resized.diagnostics.c_str());
        return 1;
    }
    arp::render_graph::RenderGraphResourceManagementSmokeRunner resizedRunner(
        *device, renderSystem.swapchain());
    const auto resizedReport =
        resizedRunner.run({2, {64, 64, 1}, arp::rhi::TextureFormat::Rgba8Unorm});
    if (!resizedReport.success) {
        std::fprintf(
            stderr, "Vulkan post-resize render failed: %s\n", resizedReport.diagnostics.c_str());
        return 1;
    }
    window.resize(480, 270);
    const auto busyResize = renderSystem.resize({
        static_cast<std::uint32_t>(window.pixelWidth()),
        static_cast<std::uint32_t>(window.pixelHeight()),
    });
    if (!busyResize.success) {
        std::fprintf(
            stderr, "Vulkan in-flight resize failed: %s\n", busyResize.diagnostics.c_str());
        return 1;
    }
    arp::render_graph::RenderGraphResourceManagementSmokeRunner finalRunner(
        *device, renderSystem.swapchain());
    const auto finalReport = finalRunner.run({2, {64, 64, 1}, arp::rhi::TextureFormat::Rgba8Unorm});
    if (!finalReport.success) {
        std::fprintf(stderr,
            "Vulkan second post-resize render failed: %s\n",
            finalReport.diagnostics.c_str());
        return 1;
    }
    drainRequest.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto resizedDrain = renderSystem.drainGpuForShutdown(drainRequest);
    if (!resizedDrain.completed()) {
        std::fprintf(
            stderr, "Vulkan post-resize drain failed: %s\n", resizedDrain.diagnosticId.c_str());
        return 1;
    }
    return 0;
}
