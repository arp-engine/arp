#include "core/cpu_allocator.h"
#include "renderer/deployment/webgpu_framegraph_runtime_bridge.h"
#include "renderer/render_graph/rhi_frame_plan.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;
    using namespace arp::render_graph;
    using namespace arp::renderer::deployment;

    WebGpuRuntimeProbe browserProbe;
    browserProbe.browserRuntime = true;
    browserProbe.canvasSurface = true;
    browserProbe.maxTexture2DSize = 4096;
    browserProbe.maxColorAttachments = 1;
    browserProbe.maxDescriptorBindings = 32;
    browserProbe.maxSampledTexturesPerShaderStage = 16;
    browserProbe.uniformBufferOffsetAlignment = 256;
    browserProbe.storageBufferOffsetAlignment = 256;
    browserProbe.maxUniformBufferBindingSize = 65536;
    browserProbe.maxStorageBufferBindingSize = 134217728;
    const auto browserProfile = buildWebGpuRuntimeProfile(browserProbe);

    RhiFramePlanBuilder computeFrameBuilder({2, 6, 2, 2});
    auto computeFrame = computeFrameBuilder.setFrameSlot(1)
                            .requireFeature(EngineFeature::PushConstants,
                                FeatureCorrectnessImpact::PreservesCorrectness,
                                "material constants")
                            .requireFeature(EngineFeature::SecondaryCommandBuffer,
                                FeatureCorrectnessImpact::PreservesCorrectness,
                                "renderer draw bundles")
                            .requireFeature(EngineFeature::ComputeDispatch,
                                FeatureCorrectnessImpact::PreservesCorrectness,
                                "browser-side compute preview")
                            .requireFeature(EngineFeature::StorageTextureBinding,
                                FeatureCorrectnessImpact::PreservesCorrectness,
                                "compute output texture")
                            .requireFeature(EngineFeature::DescriptorTable,
                                FeatureCorrectnessImpact::PerformanceOnly,
                                "bounded material descriptor table")
                            .addPass({"WebComputePreview"})
                            .build();
    assert(computeFrame.success);

    auto browserPlan = buildWebGpuFrameGraphRuntimeFallbackPlan(browserProfile, computeFrame.plan);
    assert(!browserPlan.success);
    assert(browserPlan.backend == Backend::WebGPU);
    assert(browserPlan.emulatedFeatureCount == 1u);
    assert(browserPlan.cpuReplayFeatureCount == 0u);
    assert(browserPlan.degradedFeatureCount == 0u);
    assert(browserPlan.diagnosticFailureCount == 4u);
    assert(browserPlan.diagnostics.find("secondary command") != std::pmr::string::npos);
    assert(browserPlan.diagnostics.find("compute dispatch") != std::pmr::string::npos);
    assert(browserPlan.diagnostics.find("storage texture") != std::pmr::string::npos);
    assert(!browserPlan.upperLayerVisibleFallbacks);

    WebGpuRuntimeProbe promotedProbe = browserProbe;
    promotedProbe.nativeDevice = true;
    promotedProbe.computeDispatch = true;
    promotedProbe.storageTextureBinding = true;
    promotedProbe.bgra8UnormStorage = true;
    promotedProbe.maxTexture2DSize = 8192;
    promotedProbe.maxColorAttachments = 4;
    promotedProbe.maxDescriptorBindings = 64;
    promotedProbe.maxSampledTexturesPerShaderStage = 16;
    promotedProbe.uniformBufferOffsetAlignment = 256;
    promotedProbe.storageBufferOffsetAlignment = 256;
    promotedProbe.maxUniformBufferBindingSize = 65536;
    promotedProbe.maxStorageBufferBindingSize = 134217728;
    const auto promotedProfile = buildWebGpuRuntimeProfile(promotedProbe);

    WebGpuFrameGraphRuntimeBridge promotedBridge(promotedProfile);
    auto promotedPlan = promotedBridge.buildPlan(computeFrame.plan);
    assert(!promotedPlan.success);
    assert(promotedPlan.backend == Backend::WebGPU);
    assert(promotedPlan.nativeFeatureCount == 2u);
    assert(promotedPlan.emulatedFeatureCount == 1u);
    assert(promotedPlan.cpuReplayFeatureCount == 0u);
    assert(promotedPlan.degradedFeatureCount == 0u);
    assert(promotedPlan.diagnosticFailureCount == 2u);
    assert(promotedPlan.diagnostics.find("secondary command") != std::pmr::string::npos);
    assert(!promotedPlan.upperLayerVisibleFallbacks);

    auto correctnessSensitiveFrame = computeFrame.plan;
    correctnessSensitiveFrame.featureRequirements.features[4].correctnessImpact =
        FeatureCorrectnessImpact::ChangesCorrectness;
    auto correctnessPlan = promotedBridge.buildPlan(correctnessSensitiveFrame);
    assert(!correctnessPlan.success);
    assert(correctnessPlan.diagnosticFailureCount == 2u);
    assert(correctnessPlan.diagnostics.find("correctness") != std::pmr::string::npos);

    WebGpuRuntimeProbe invalidProbe = browserProbe;
    invalidProbe.canvasSurface = false;
    const auto invalidProfile = buildWebGpuRuntimeProfile(invalidProbe);
    auto invalidProfilePlan =
        buildWebGpuFrameGraphRuntimeFallbackPlan(invalidProfile, computeFrame.plan);
    assert(!invalidProfilePlan.success);
    assert(invalidProfilePlan.diagnostics.find("canvas") != std::pmr::string::npos);

    auto invalidSlotsFrame = computeFrame.plan;
    invalidSlotsFrame.featureRequirements.logicalFrameSlots = 3;
    auto invalidSlotsPlan = promotedBridge.buildPlan(invalidSlotsFrame);
    assert(!invalidSlotsPlan.success);
    assert(invalidSlotsPlan.diagnostics.find("frame slot") != std::pmr::string::npos);

    return 0;
}
