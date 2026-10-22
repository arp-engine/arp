#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/metal/backend_validation_contract.h"

#include <cassert>
#include <string>

namespace {

arp::rhi::semantics::NativeBackendCapabilityContract makeQueriedMetalContract()
{
    auto contract = arp::rhi::semantics::nativeBackendCapabilityContract(arp::rhi::Backend::Metal);
    contract.queriedFromNativeDevice = true;
    contract.queriedExtendedFeatures = true;
    return contract;
}

arp::rhi::DescriptorLayoutDesc makeSceneDescriptorLayout()
{
    arp::rhi::DescriptorLayoutDesc layout;
    layout.name = "MetalSceneArguments";
    layout.bindings = {
        {0, arp::rhi::DescriptorType::UniformBuffer, arp::rhi::ShaderStage::Vertex},
        {1, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
        {2, arp::rhi::DescriptorType::Sampler, arp::rhi::ShaderStage::Fragment},
        {3, arp::rhi::DescriptorType::StorageBuffer, arp::rhi::ShaderStage::Compute},
        {4, arp::rhi::DescriptorType::StorageTexture, arp::rhi::ShaderStage::Compute},
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
            96,
        },
    };
    return layout;
}

arp::rhi::semantics::metal::MetalBackendValidationWorkload makeWorkload()
{
    const auto sceneLayout = makeSceneDescriptorLayout();
    arp::rhi::semantics::metal::MetalBackendValidationWorkload workload;
    workload.contract = makeQueriedMetalContract();
    workload.privateVertexBuffer = {
        "MetalPrivateVertexBuffer",
        4096,
        arp::rhi::ResourceUsage::VertexBuffer,
        arp::rhi::MemoryUsage::GpuOnly,
        arp::rhi::ResourceState::ShaderRead,
    };
    workload.sharedUploadBuffer = {
        "MetalSharedUploadBuffer",
        1024,
        arp::rhi::ResourceUsage::UniformBuffer | arp::rhi::ResourceUsage::CopyDst,
        arp::rhi::MemoryUsage::CpuToGpu,
        arp::rhi::ResourceState::CopyDst,
    };
    workload.managedReadbackBuffer = {
        "MetalManagedReadbackBuffer",
        512,
        arp::rhi::ResourceUsage::StorageBuffer | arp::rhi::ResourceUsage::CopySrc,
        arp::rhi::MemoryUsage::GpuToCpu,
        arp::rhi::ResourceState::CopySrc,
    };
    workload.memorylessColorTarget = {
        "MetalMemorylessColor",
        {1280, 720, 1},
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::Sampled | arp::rhi::TextureUsage::Storage
            | arp::rhi::TextureUsage::RenderAttachment,
        arp::rhi::MemoryUsage::Transient,
        arp::rhi::ResourceState::ColorAttachment,
    };
    workload.descriptorLayout = sceneLayout;
    workload.descriptorArena = {"MetalArgumentArena", 4, 24};
    workload.requestedDescriptorCount = 5;
    workload.pipelineLayout = makeScenePipelineLayout(sceneLayout);
    workload.commandWorkload = {
        "MetalSceneCommands",
        9,
        true,
        true,
        false,
        true,
    };
    workload.commandIntent =
        arp::rhi::semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer;
    workload.synchronizationWorkload = {
        "MetalDrawableFrameSync",
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
    workload.drawableLifecycleState = {
        arp::platform::PlatformSurfaceKind::IosLayer,
        2,
        true,
        false,
        false,
        true,
        true,
        true,
        false,
        false,
        false,
        false,
        false,
    };
    return workload;
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;
    using namespace arp::rhi::semantics::metal;

    const auto report = validateMetalBackendContracts(makeWorkload());
    assert(report.success);
    assert(report.contractComplete);
    assert(report.backend == Backend::Metal);
    assert(report.argumentBindingsValidated);
    assert(report.pushConstantEmulationValidated);
    assert(report.resourceStorageModesValidated);
    assert(report.synchronizationValidated);
    assert(report.drawableLifecycleValidated);
    assert(report.backgroundStopsDrawableAcquire);
    assert(report.foregroundRecreatesDrawableResources);
    assert(report.layerResizeRebuildsDrawableResources);
    assert(report.logicalFrameSlotsPreserved);
    assert(report.logicalFrameSlots == 2u);
    assert(report.sceneRuntimeSupportLabel == SceneRuntimeBackendSupportLabel::Supported);
    assert(report.sceneRuntimeWorkloadValidated);
    assert(report.sceneRuntimePushConstantEmulationValidated);
    assert(report.sceneRuntimeOnePassValidated);
    assert(!report.nativeHandleExposedToUpperLayer);
    assert(report.diagnostics.find("native handle") == std::pmr::string::npos);

    assert(hasNativeDescriptorSemantic(
        report.descriptorLayoutLowering, NativeDescriptorSemantic::MetalArgumentBufferLayout));
    assert(hasNativeDescriptorSemantic(
        report.descriptorLayoutLowering, NativeDescriptorSemantic::MetalBufferArgument));
    assert(hasNativeDescriptorSemantic(
        report.descriptorLayoutLowering, NativeDescriptorSemantic::MetalTextureArgument));
    assert(hasNativeDescriptorSemantic(
        report.descriptorLayoutLowering, NativeDescriptorSemantic::MetalSamplerArgument));
    assert(hasNativePipelineLayoutSemantic(report.pipelineLayoutLowering,
        NativePipelineLayoutSemantic::MetalPushConstantUploadBuffer));
    assert(hasNativeCommandSemantic(
        report.commandLowering, NativeCommandSemantic::MetalPushConstantUpload));
    assert(hasNativeCommandSemantic(
        report.commandLowering, NativeCommandSemantic::MetalIndirectCommandBuffer));
    assert(hasNativeResourceSemantic(
        report.privateBufferLowering, NativeResourceSemantic::MetalPrivateStorage));
    assert(hasNativeResourceSemantic(
        report.sharedBufferLowering, NativeResourceSemantic::MetalSharedStorage));
    assert(hasNativeResourceSemantic(
        report.managedBufferLowering, NativeResourceSemantic::MetalManagedStorage));
    assert(hasNativeResourceSemantic(
        report.memorylessColorLowering, NativeResourceSemantic::MetalMemorylessOrTransientStorage));
    assert(hasNativeSynchronizationSemantic(report.synchronizationLowering,
        NativeSynchronizationSemantic::MetalCommandBufferCompletion));
    assert(hasNativeSynchronizationSemantic(
        report.synchronizationLowering, NativeSynchronizationSemantic::MetalDrawablePresent));
    assert(hasNativePlatformLifecycleSemantic(report.backgroundLifecycle,
        NativePlatformLifecycleSemantic::IosBackgroundStopsDrawableAcquire));
    assert(hasNativePlatformLifecycleSemantic(report.foregroundLifecycle,
        NativePlatformLifecycleSemantic::IosLayerInvalidatesDrawableResources));
    assert(hasNativePlatformLifecycleSemantic(report.resizeLifecycle,
        NativePlatformLifecycleSemantic::IosLayerResizeRebuildsDrawableResources));

    auto wrongBackend = makeWorkload();
    wrongBackend.contract = nativeBackendCapabilityContract(Backend::Vulkan);
    const auto rejected = validateMetalBackendContracts(wrongBackend);
    assert(!rejected.success);
    assert(!rejected.contractComplete);
    assert(rejected.diagnostics.find("Metal") != std::pmr::string::npos);

    auto missingArgumentBuffers = makeWorkload();
    missingArgumentBuffers.contract.features = {
        NativeBackendFeature::TileMemoryHints,
        NativeBackendFeature::IndirectCommandBuffers,
    };
    const auto argumentRejected = validateMetalBackendContracts(missingArgumentBuffers);
    assert(!argumentRejected.success);
    assert(!argumentRejected.argumentBindingsValidated);
    assert(argumentRejected.resourceStorageModesValidated);
    assert(argumentRejected.diagnostics.find("argument") != std::pmr::string::npos);

    auto missingTileMemory = makeWorkload();
    missingTileMemory.contract.features = {
        NativeBackendFeature::ArgumentBuffers,
        NativeBackendFeature::IndirectCommandBuffers,
    };
    const auto storageRejected = validateMetalBackendContracts(missingTileMemory);
    assert(!storageRejected.success);
    assert(!storageRejected.resourceStorageModesValidated);
    assert(storageRejected.argumentBindingsValidated);
    assert(storageRejected.diagnostics.find("storage") != std::pmr::string::npos);

    auto invalidSceneRuntime = makeWorkload();
    invalidSceneRuntime.sceneRuntimeWorkload.backendHandleExposedToUpperLayer = true;
    const auto sceneRuntimeRejected = validateMetalBackendContracts(invalidSceneRuntime);
    assert(!sceneRuntimeRejected.success);
    assert(sceneRuntimeRejected.sceneRuntimeSupportLabel
        == SceneRuntimeBackendSupportLabel::DiagnosticFailure);
    assert(sceneRuntimeRejected.diagnostics.find("scene-runtime") != std::pmr::string::npos);

    return 0;
}
