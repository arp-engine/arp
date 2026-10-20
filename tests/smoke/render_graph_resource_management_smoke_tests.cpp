#include "renderer/render_graph/render_graph_resource_management_smoke.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <cstdio>

#if defined(__EMSCRIPTEN__)
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"

#include <emscripten.h>
#endif

namespace {

#if defined(__EMSCRIPTEN__)
arp::core::UniquePtr<arp::rhi::GpuDevice> createSmokeDevice()
{
    arp::rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.canvasSurface = true;
    probe.nativeDevice = true;
    probe.computeDispatch = true;
    probe.storageTextureBinding = true;
    probe.storageBuffer = true;
    probe.bgra8UnormStorage = true;
    probe.rgba16FloatStorage = true;
    probe.rgba16FloatBlendable = true;
    probe.maxTexture2DSize = 8192;
    probe.maxColorAttachments = 4;
    probe.maxDescriptorBindings = 64;
    probe.maxSampledTexturesPerShaderStage = 16;
    probe.uniformBufferOffsetAlignment = 256;
    probe.storageBufferOffsetAlignment = 256;
    probe.maxUniformBufferBindingSize = 65536;
    probe.maxStorageBufferBindingSize = 134217728;
    const auto profile = arp::rhi::buildWebGpuRuntimeProfile(probe);
    const auto bridge = arp::rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);
    assert(bridge.success);

    arp::rhi_webgpu::WebGpuRhiDeviceDesc desc;
    desc.label = "RenderGraphResourceManagementWebGpuSmoke";
    desc.runtimeProfile = profile;
    desc.capabilityContract = bridge.contract;
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.enableBackendDescriptorGroups = true;
    desc.enablePushConstantUploadRing = true;
    desc.pushConstantUploadRingByteCapacity = 4096;
    desc.uploadStagingByteCapacity = 4u * 1024u * 1024u;
    desc.enableCanvasLifecycleDiagnostics = true;
    desc.enableResizeDiagnostics = true;
    desc.enableSurfaceLostDiagnostics = true;
    desc.enableHiddenTabFrameSkipping = true;
    return arp::rhi_webgpu::createWebGpuRhiDevice(desc);
}
#else
arp::core::UniquePtr<arp::rhi::GpuDevice> createSmokeDevice()
{
    return arp::rhi::createNullDevice();
}
#endif

} // namespace

int main()
{
    auto device = createSmokeDevice();
    assert(device);
    arp::core::UniquePtr<arp::rhi::Swapchain> swapchain;
#if defined(__EMSCRIPTEN__)
    swapchain = device->createSwapchain({
        "#RenderGraphResourceManagementSmoke",
        {64, 64},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2,
    });
    assert(swapchain);
#endif
    arp::render_graph::RenderGraphResourceManagementSmokeRunner runner(*device, swapchain.get());
    const auto report = runner.run({2,
        {64, 64, 1},
        swapchain ? arp::rhi::TextureFormat::Bgra8Unorm : arp::rhi::TextureFormat::Rgba8Unorm,
        5000});
    if (!report.success) {
        std::fprintf(stderr,
            "%s frames=%u handoffs=%llu premature=%llu barriers=%llu "
            "fallbacks=%llu final=%d budget=%d hits=%llu evictions=%llu "
            "pending=%u\n",
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
            report.pool.pendingResourceCount);
#if defined(__EMSCRIPTEN__)
        EM_ASM({
            // clang-format off
                var canvas = document.getElementById("RenderGraphResourceManagementSmoke");
                canvas.dataset.smokeStatus = "failed";
            // clang-format on
        });
#endif
        return 1;
    }
    assert(report.requestedFrameCount == 4);
    assert(report.completedFrameCount == 4);
    assert(!report.transientAliasingAdvertised);
    assert(report.aliasHandoffCount == 0);
    assert(report.prematureReuseCount == 0);
    assert(report.barrierFailureCount == 0);
    assert(report.fallbackCount == 0);
    assert(report.externalFinalStateCorrect);
    assert(report.budgetWithinBounds);
    assert(report.graphRebuildExercised);
    assert(report.pool.hitCount > 0);
    assert(report.pool.evictionCount > 0);
#if defined(__EMSCRIPTEN__)
    assert(report.backend == arp::rhi::Backend::WebGPU);
    EM_ASM(
        {
            // clang-format off
                var smoke = {};
                smoke.success = true;
                smoke.completedFrames = $0;
                smoke.aliasHandoffs = $1;
                smoke.prematureReuse = $2;
                smoke.barrierFailures = $3;
                smoke.fallbackCount = $4;
                smoke.externalFinalStateCorrect = $5 !== 0;
                smoke.budgetWithinBounds = $6 !== 0;
                smoke.poolHits = $7;
                smoke.poolEvictions = $8;
                globalThis.arpRenderGraphResourceSmoke = smoke;
                var canvas = document.getElementById("RenderGraphResourceManagementSmoke");
                canvas.dataset.smokeStatus = "passed";
                canvas.dataset.smokeReport = JSON.stringify(smoke);
            // clang-format on
        },
        report.completedFrameCount,
        static_cast<std::uint32_t>(report.aliasHandoffCount),
        static_cast<std::uint32_t>(report.prematureReuseCount),
        static_cast<std::uint32_t>(report.barrierFailureCount),
        static_cast<std::uint32_t>(report.fallbackCount),
        report.externalFinalStateCorrect,
        report.budgetWithinBounds,
        static_cast<std::uint32_t>(report.pool.hitCount),
        static_cast<std::uint32_t>(report.pool.evictionCount));
#else
    assert(report.backend == arp::rhi::Backend::Null);
#endif
    return 0;
}
