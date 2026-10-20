#include "core/cpu_allocator.h"
#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/rhi_frame_plan.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/vulkan/vulkan_device.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/semantics/vulkan_device_capability_query.h"

#include <cassert>
#include <string>
#include <string_view>

namespace {

arp::render_graph::CompiledFrameGraph makeCustomPassCompiledGraph()
{
    using namespace arp::render_graph;

    CompiledFrameGraph compiled;
    compiled.addPass({0, "MainScene", PassKind::Graphics, QueueRole::Graphics, false, false});
    compiled.addPass({1, "Particles", PassKind::Graphics, QueueRole::Graphics, false, false});
    compiled.addPass(
        {2, "CustomPostprocessFxaa", PassKind::Graphics, QueueRole::Graphics, false, false});
    compiled.addPass({3, "Gui", PassKind::Graphics, QueueRole::Graphics, false, false});
    compiled.addPass(
        {4, "GiIrradianceResolve", PassKind::Compute, QueueRole::Compute, true, false});
    compiled.addPass({5, "CustomTextureUpload", PassKind::Copy, QueueRole::Transfer, false, false});
    for (std::size_t index = 0; index < compiled.passCount(); ++index) {
        compiled.addExecutablePass(index);
    }
    compiled.addQueuePlan({"MainScene", PassKind::Graphics, QueueRole::Graphics, false});
    compiled.addQueuePlan({"Particles", PassKind::Graphics, QueueRole::Graphics, false});
    compiled.addQueuePlan(
        {"CustomPostprocessFxaa", PassKind::Graphics, QueueRole::Graphics, false});
    compiled.addQueuePlan({"Gui", PassKind::Graphics, QueueRole::Graphics, false});
    compiled.addQueuePlan({"GiIrradianceResolve", PassKind::Compute, QueueRole::Compute, true});
    compiled.addQueuePlan({"CustomTextureUpload", PassKind::Copy, QueueRole::Transfer, false});
    return compiled;
}

const arp::render_graph::FrameGraphRhiRecordedPassReport* findRecordedPass(
    const arp::render_graph::FrameGraphRhiCommandExecutionReport& report,
    const std::pmr::string& name)
{
    for (const auto& pass : report.recordedPasses) {
        if (pass.name == name) {
            return &pass;
        }
    }
    return nullptr;
}

void assertGraphicsPassRecorded(
    const arp::render_graph::FrameGraphRhiCommandExecutionReport& report,
    const std::pmr::string& name,
    bool expectsPushConstants)
{
    const auto* pass = findRecordedPass(report, name);
    assert(pass != nullptr);
    assert(pass->kind == arp::render_graph::PassKind::Graphics);
    assert(pass->drawCommandCount == 1u);
    assert(pass->recordedThroughCommandEncoder);
    assert(pass->pushConstantCommandCount == (expectsPushConstants ? 1u : 0u));
}

arp::rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot makeVulkanSnapshot()
{
    arp::rhi::semantics::vulkan::VulkanDeviceCapabilitySnapshot snapshot;
    snapshot.queriedFromNativeDevice = true;
    snapshot.queriedExtendedFeatures = true;
    snapshot.maxPushConstantSizeBytes = 256;
    snapshot.supportsDescriptorIndexing = true;
    snapshot.supportsTimelineSynchronization = true;
    snapshot.supportsSecondaryCommandBuffers = true;
    snapshot.queueFamilyCount = 2;
    snapshot.hasDeviceLocalMemory = true;
    snapshot.hasHostVisibleMemory = true;
    return snapshot;
}

arp::core::UniquePtr<arp::rhi::GpuDevice> makeVulkanDevice()
{
    const auto snapshot = makeVulkanSnapshot();
    arp::rhi_vulkan::VulkanRhiRuntimeDesc runtimeDesc;
    runtimeDesc.label = "CustomGraphRhiVulkanRuntime";
    runtimeDesc.enableSyntheticRuntime = true;
    runtimeDesc.nativeHandlesPrivate = true;
    runtimeDesc.queueFamilyCount = snapshot.queueFamilyCount;
    runtimeDesc.hasDeviceLocalMemory = snapshot.hasDeviceLocalMemory;
    runtimeDesc.hasHostVisibleMemory = snapshot.hasHostVisibleMemory;
    const auto runtime = arp::rhi_vulkan::createVulkanRhiRuntime(runtimeDesc);
    assert(runtime);

    arp::rhi_vulkan::VulkanRhiDeviceDesc desc;
    desc.label = "CustomGraphRhiVulkanDevice";
    desc.capabilityContract =
        arp::rhi::semantics::vulkan::buildVulkanNativeCapabilityContract(snapshot);
    desc.memoryBudgetBytes = 64u * 1024u * 1024u;
    desc.runtime = runtime;
    desc.enableVmaAllocator = true;
    desc.requireNativeResourceAllocation = true;
    desc.allowNativeHandleExposureToUpperLayers = false;
    desc.allowBlockingFrameWaits = false;
    desc.allowPerDrawPipelineCreation = false;
    desc.allowPerFrameShaderCompilation = false;

    const auto deviceReport = arp::rhi_vulkan::describeVulkanRhiDevice(desc);
    assert(deviceReport.success);
    assert(!deviceReport.nativeHandleExposedToUpperLayer);
    return arp::rhi_vulkan::createVulkanRhiDevice(desc);
}

void assertCustomGraphRecordedOnDevice(arp::rhi::GpuDevice& device)
{
    using namespace arp::render_graph;

    FrameGraphRhiCommandExecutor executor({
        2,
        16,
        16,
        16,
    });
    const auto report = executor.execute(makeCustomPassCompiledGraph(), 1, device);

    assert(report.success);
    assert(report.recordedThroughRhiCommandEncoder);
    assert(report.passRecordsConsumed);
    assert(report.executedPassCount == 6u);
    assert(report.renderPassCount == 4u);
    assert(report.computePassCount == 1u);
    assert(report.transferPassCount == 1u);
    assert(report.drawCommandCount == 4u);
    assert(report.dispatchCommandCount == 1u);
    assert(report.transferCommandCount == 1u);
    assert(report.pushConstantCommandCount == 2u);
    assert(report.recordedPasses.size() == 6u);
    assert(report.backendOwnedPassResources);
    assert(report.backendFallbacksHiddenFromUpperLayer);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(device.queue().submitCount() >= 1u);

    assertGraphicsPassRecorded(report, "MainScene", true);
    assertGraphicsPassRecorded(report, "Particles", true);
    assertGraphicsPassRecorded(report, "CustomPostprocessFxaa", false);
    assertGraphicsPassRecorded(report, "Gui", false);

    const auto* compute = findRecordedPass(report, "GiIrradianceResolve");
    assert(compute != nullptr);
    assert(compute->kind == PassKind::Compute);
    assert(compute->dispatchCommandCount == 1u);
    assert(compute->recordedThroughCommandEncoder);

    const auto* upload = findRecordedPass(report, "CustomTextureUpload");
    assert(upload != nullptr);
    assert(upload->kind == PassKind::Copy);
    assert(upload->transferCommandCount == 1u);
    assert(upload->recordedThroughCommandEncoder);
}

} // namespace

int main()
{
    auto device = arp::rhi::createNullDevice();
    assert(device);
    assertCustomGraphRecordedOnDevice(*device);

    auto vulkanDevice = makeVulkanDevice();
    assert(vulkanDevice);
    assertCustomGraphRecordedOnDevice(*vulkanDevice);

    auto swapchain = vulkanDevice->createSwapchain({
        "CustomGraphVulkanHiddenWindowSurface",
        {320, 180},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2,
    });
    assert(swapchain);
    const auto frame = swapchain->acquireNextFrame();
    assert(frame.success);
    const auto presented = swapchain->present(frame);
    assert(presented.success);

    const auto nativeStats = arp::rhi_vulkan::inspectVulkanRhiNativeResourceStats(*vulkanDevice);
    assert(nativeStats.runtimeConnected);
    assert(nativeStats.nativeQueueSubmitCount >= 1u);
    assert(nativeStats.nativeRenderPassBeginRecordCount >= 4u);
    assert(nativeStats.nativeIndexedDrawRecordCount >= 4u);
    assert(nativeStats.nativeComputePipelineBindRecordCount >= 1u);
    assert(nativeStats.backendDispatchRecordCount >= 1u);
    assert(nativeStats.nativeExecutableDispatchRecordCount >= 1u);
    assert(nativeStats.nativeUploadStagingSliceCount >= 1u);
    assert(nativeStats.nativeSwapchainAcquireCount == 1u);
    assert(nativeStats.nativeSwapchainPresentCount == 1u);
    assert(!nativeStats.hiddenWindowRenderOutputHashCaptured);
    assert(std::string_view(nativeStats.renderOutputEvidenceDiagnostic)
        == "RHI-OUTPUT-ORIGINAL-OBSERVATION-UNAVAILABLE");
    assert(nativeStats.deterministicRenderOutputHash == 0u);
    assert(nativeStats.nativeHandlesHiddenFromPublicApi);

    return 0;
}
