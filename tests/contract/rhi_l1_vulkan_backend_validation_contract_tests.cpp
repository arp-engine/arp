#include "core/cpu_allocator.h"
#include "rhi/semantics/vulkan/backend_validation_contract.h"

#include <cassert>
#include <string>

namespace {

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedVulkanContract()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Vulkan);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    contract.maxPushConstantSizeBytes = 256;
    contract.queueFamilyCount = 2;
    return contract;
}

arp::rhi::DescriptorLayoutDesc makeSceneDescriptorLayout()
{
    arp::rhi::DescriptorLayoutDesc layout;
    layout.name = "VulkanSceneSet";
    layout.bindings = {
        {0, arp::rhi::DescriptorType::UniformBuffer, arp::rhi::ShaderStage::Vertex},
        {1, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
        {2, arp::rhi::DescriptorType::Sampler, arp::rhi::ShaderStage::Fragment},
    };
    return layout;
}

arp::rhi::PipelineLayoutDesc makeScenePipelineLayout(
    const arp::rhi::DescriptorLayoutDesc& sceneLayout)
{
    arp::rhi::PipelineLayoutDesc layout;
    layout.descriptorLayouts = {sceneLayout};
    layout.descriptorSets = {{0, sceneLayout.name}};
    layout.pushConstantRanges = {
        {
            arp::rhi::ShaderStage::Vertex | arp::rhi::ShaderStage::Fragment,
            0,
            128,
        },
    };
    return layout;
}

arp::rhi::semantics::vulkan::VulkanBackendValidationWorkload makeWorkload()
{
    const auto sceneLayout = makeSceneDescriptorLayout();
    arp::rhi::semantics::vulkan::VulkanBackendValidationWorkload workload;
    workload.contract = makeQueriedVulkanContract();
    workload.vertexBuffer = {
        "VulkanSceneVertexBuffer",
        4096,
        arp::rhi::ResourceUsage::VertexBuffer | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::CopyDst,
    };
    workload.colorTarget = {
        "VulkanSceneColor",
        {1280, 720, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::Sampled | arp::rhi::TextureUsage::RenderAttachment
            | arp::rhi::TextureUsage::CopyDst,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::ColorAttachment,
    };
    workload.descriptorLayout = sceneLayout;
    workload.descriptorArena = {"VulkanDescriptorArena", 4, 16};
    workload.requestedDescriptorCount = 3;
    workload.pipelineLayout = makeScenePipelineLayout(sceneLayout);
    workload.commandWorkload = {
        "VulkanSceneCommands",
        8,
        true,
        true,
        false,
        true,
    };
    workload.commandIntent =
        arp::rhi::semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer;
    workload.synchronizationWorkload = {
        "VulkanSceneFrameSync",
        2,
        true,
        true,
        true,
        true,
    };
    workload.sceneRuntimeWorkload = {
        true,
        3,
        3,
        2,
        128,
        256,
        true,
        false,
    };
    workload.swapchainImageCount = {3, 0, 2};
    return workload;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;
    using namespace arp::rhi::semantics::vulkan;

    const auto report = validateVulkanBackendContracts(makeWorkload());
    assert(report.success);
    assert(report.contractComplete);
    assert(report.backend == Backend::Vulkan);
    assert(report.resourceUsageValidated);
    assert(report.descriptorLoweringValidated);
    assert(report.pipelineLayoutValidated);
    assert(report.pushConstantsValidated);
    assert(report.barrierLoweringValidated);
    assert(report.commandLoweringValidated);
    assert(report.synchronizationValidated);
    assert(report.nativeFirstBehaviorPreserved);
    assert(report.nativePushConstantsPreserved);
    assert(report.descriptorIndexingPreserved);
    assert(report.secondaryCommandBuffersPreserved);
    assert(report.timelineSynchronizationPreserved);
    assert(report.explicitMemoryControlPreserved);
    assert(report.queueFamilyFactsPreserved);
    assert(report.queueFamilyCount == 2u);
    assert(report.swapchainImagePolicyValidated);
    assert(report.logicalFrameSlotsPreserved);
    assert(report.nativeImageCount == 3u);
    assert(report.logicalFrameSlots == 2u);
    assert(report.sceneRuntimeSupportLabel == SceneRuntimeBackendSupportLabel::Supported);
    assert(report.sceneRuntimeWorkloadValidated);
    assert(report.sceneRuntimeOnePassValidated);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(report.diagnostics.find("native handle") == std::pmr::string::npos);
    assert(hasNativeResourceSemantic(
        report.vertexBufferLowering, NativeResourceSemantic::VulkanBufferVertexUsage));
    assert(hasNativeResourceSemantic(
        report.colorTargetLowering, NativeResourceSemantic::VulkanImageColorAttachmentUsage));
    assert(hasNativeResourceSemantic(
        report.colorTargetLowering, NativeResourceSemantic::VulkanTransientAliasingMemory));
    assert(hasNativeDescriptorSemantic(
        report.descriptorLayoutLowering, NativeDescriptorSemantic::VulkanDescriptorSetLayout));
    assert(hasNativeDescriptorSemantic(
        report.descriptorTableLowering, NativeDescriptorSemantic::VulkanSamplerDescriptor));
    assert(hasNativePipelineLayoutSemantic(report.pipelineLayoutLowering,
        NativePipelineLayoutSemantic::VulkanNativePushConstantRange));
    assert(hasNativeCommandSemantic(
        report.commandLowering, NativeCommandSemantic::VulkanSecondaryCommandBuffer));
    assert(hasNativeCommandSemantic(
        report.commandLowering, NativeCommandSemantic::VulkanPipelineBarrier));
    assert(hasNativeSynchronizationSemantic(
        report.synchronizationLowering, NativeSynchronizationSemantic::VulkanTimelineSemaphore));
    assert(hasNativeSynchronizationSemantic(
        report.synchronizationLowering, NativeSynchronizationSemantic::VulkanPresentQueue));

    auto wrongBackend = makeWorkload();
    wrongBackend.contract = nativeBackendCapabilityContract(Backend::Null);
    const auto rejected = validateVulkanBackendContracts(wrongBackend);
    assert(!rejected.success);
    assert(!rejected.contractComplete);
    assert(rejected.diagnostics.find("Vulkan") != std::pmr::string::npos);

    auto missingNativeFirstFeature = makeWorkload();
    missingNativeFirstFeature.contract.features = {
        NativeBackendFeature::PushConstants,
        NativeBackendFeature::SecondaryCommandBuffers,
        NativeBackendFeature::TimelineSynchronization,
        NativeBackendFeature::QueueFamilies,
    };
    const auto nativeFirstRejected = validateVulkanBackendContracts(missingNativeFirstFeature);
    assert(!nativeFirstRejected.success);
    assert(!nativeFirstRejected.contractComplete);
    assert(!nativeFirstRejected.nativeFirstBehaviorPreserved);
    assert(nativeFirstRejected.nativePushConstantsPreserved);
    assert(!nativeFirstRejected.descriptorIndexingPreserved);
    assert(!nativeFirstRejected.explicitMemoryControlPreserved);
    assert(nativeFirstRejected.queueFamilyFactsPreserved);
    assert(nativeFirstRejected.diagnostics.find("native-first") != std::pmr::string::npos);

    auto invalidSceneRuntime = makeWorkload();
    invalidSceneRuntime.sceneRuntimeWorkload.oneCompatibleWorldUiPass = false;
    const auto sceneRuntimeRejected = validateVulkanBackendContracts(invalidSceneRuntime);
    assert(!sceneRuntimeRejected.success);
    assert(sceneRuntimeRejected.sceneRuntimeSupportLabel
        == SceneRuntimeBackendSupportLabel::DiagnosticFailure);
    assert(sceneRuntimeRejected.diagnostics.find("scene-runtime") != std::pmr::string::npos);

    return 0;
}
