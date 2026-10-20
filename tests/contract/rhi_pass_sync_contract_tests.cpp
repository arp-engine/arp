#include "core/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/native_synchronization_lowering.h"

#include <cassert>
#include <string>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    auto device = createNullDevice();
    auto gbuffer0 = device->createTexture({
        "GBufferAlbedo",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    auto gbuffer1 = device->createTexture({
        "GBufferNormal",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    auto resolveTarget = device->createTexture({
        "GBufferResolve",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::CopySrc,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    auto depthStencil = device->createTexture({
        "DepthStencil",
        {64, 64, 1},
        TextureFormat::Depth24Stencil8,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(gbuffer0 && gbuffer1 && resolveTarget && depthStencil);

    RenderPassDesc passDesc;
    passDesc.label = "MrtDepthResolve";
    passDesc.colorAttachments.push_back({
        "Albedo",
        gbuffer0->id(),
        TextureFormat::Bgra8Unorm,
        LoadOp::Clear,
        StoreOp::Store,
        true,
        resolveTarget->id(),
    });
    passDesc.colorAttachments.push_back({
        "Normal",
        gbuffer1->id(),
        TextureFormat::Bgra8Unorm,
        LoadOp::Load,
        StoreOp::Store,
        false,
        {},
    });
    passDesc.hasDepthStencilAttachment = true;
    passDesc.depthStencilAttachment = {
        "DepthStencil",
        depthStencil->id(),
        TextureFormat::Depth24Stencil8,
        LoadOp::Clear,
        StoreOp::Store,
        LoadOp::Clear,
        StoreOp::Store,
    };
    assert(passDesc.colorAttachments.size() == 2u);
    assert(passDesc.colorAttachments.front().hasResolveTarget);
    assert(passDesc.depthStencilAttachment.depthLoadOp == LoadOp::Clear);
    passDesc.depthStencilAttachment.depthClearValue = 0.375f;
    passDesc.depthStencilAttachment.stencilClearValue = 73u;
    passDesc.colorAttachments.front().clearColor = Color{0.31f, 0.31f, 0.31f, 1.0f};
    assert(passDesc.colorAttachments.front().clearColor.r == 0.31f);
    assert(passDesc.colorAttachments.front().clearColor.g == 0.31f);
    assert(passDesc.colorAttachments.front().clearColor.b == 0.31f);
    assert(passDesc.colorAttachments.front().clearColor.a == 1.0f);

    auto passEncoder = device->createCommandEncoder({"PassContract"});
    auto pass = passEncoder->beginRenderPass(passDesc);
    assert(pass);
    assert(pass->end().success);
    auto passCommands = passEncoder->finish();
    assert(passCommands);
    assert(passCommands->commandCount() == 2u);

    auto upload = device->createBuffer({
        "UploadBuffer",
        4096,
        ResourceUsage::CopySrc,
        MemoryUsage::CpuToGpu,
        ResourceState::CopySrc,
    });
    auto readback = device->createBuffer({
        "ReadbackBuffer",
        4096,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu,
        ResourceState::CopyDst,
    });
    assert(upload && readback);

    auto transferEncoder = device->createCommandEncoder({"TransferContract"});
    auto transfer = transferEncoder->beginTransferPass({"CopiesUploadReadback"});
    assert(transfer
            ->copyBufferToBuffer({
                upload->id(),
                readback->id(),
                0,
                256,
                512,
            })
            .success);
    assert(transfer
            ->copyBufferToTexture({
                upload->id(),
                gbuffer0->id(),
                0,
                256,
                64,
                {0, 1, 0, 1, TextureAspect::Color},
                {32, 32, 1},
            })
            .success);
    assert(!transfer
            ->copyBufferToTexture({
                upload->id(),
                gbuffer0->id(),
                0,
                128,
                64,
                {0, 1, 0, 1, TextureAspect::Color},
                {32, 32, 1},
            })
            .success);
    assert(transfer
            ->copyTextureToBuffer({
                gbuffer0->id(),
                readback->id(),
                {0, 1, 0, 1, TextureAspect::Color},
                128,
                256,
                64,
                {32, 32, 1},
            })
            .success);
    assert(transfer
            ->copyTextureToTexture({
                gbuffer0->id(),
                gbuffer1->id(),
                {0, 1, 0, 1, TextureAspect::Color},
                {0, 1, 0, 1, TextureAspect::Color},
                {16, 16, 1},
            })
            .success);
    assert(transfer
            ->uploadBuffer({
                "UploadIntent",
                upload->id(),
                gbuffer0->id(),
                1024,
                StagingIntent::Upload,
                true,
            })
            .success);
    assert(transfer
            ->readbackTexture({
                "ReadbackIntent",
                gbuffer1->id(),
                readback->id(),
                1024,
                StagingIntent::Readback,
                true,
            })
            .success);
    assert(transfer->end().success);
    auto transferCommands = transferEncoder->finish();
    assert(transferCommands);
    assert(transferCommands->commandCount() == 8u);

    auto barrierEncoder = device->createCommandEncoder({"BarrierContract"});
    ResourceBarrierDesc barrier;
    barrier.resource = gbuffer0->id();
    barrier.textureSubresources = {0, 1, 0, 1, TextureAspect::Color};
    barrier.before = ResourceState::Undefined;
    barrier.after = ResourceState::CopyDst;
    barrier.sourceStages = PipelineStage::TopOfPipe;
    barrier.destinationStages = PipelineStage::Transfer;
    barrier.sourceAccess = ResourceAccess::None;
    barrier.destinationAccess = ResourceAccess::TransferWrite;
    auto barrierResult = barrierEncoder->transition(*gbuffer0, barrier);
    assert(barrierResult.success);
    assert(barrierResult.resource == gbuffer0->id());
    assert(barrierResult.subresourceAware);
    assert(barrierResult.stageAccessAware);
    assert(barrierResult.sourceStages == PipelineStage::TopOfPipe);
    assert(barrierResult.destinationStages == PipelineStage::Transfer);
    assert(barrierResult.destinationAccess == ResourceAccess::TransferWrite);

    ResourceBarrierDesc invalidBarrier = barrier;
    invalidBarrier.before = ResourceState::Undefined;
    invalidBarrier.after = ResourceState::ShaderRead;
    invalidBarrier.destinationStages = PipelineStage::None;
    auto invalidBarrierResult = barrierEncoder->transition(*gbuffer0, invalidBarrier);
    assert(!invalidBarrierResult.success);
    assert(invalidBarrierResult.diagnostics.find("stage") != std::pmr::string::npos);

    // FrameInFlightSyncDesc / validateFrameInFlightSync removed during RHI
    // collapse — sync now backend-owned, validated at device creation.

    CommandWorkloadDesc workload;
    workload.label = "Phase11CommandWorkload";
    workload.commandCount = 8;
    workload.usesRenderPass = true;
    workload.colorAttachmentCount = 2;
    workload.usesDepthStencilAttachment = true;
    workload.usesResolveTargets = true;
    workload.usesTransferCopies = true;
    workload.usesUploadStaging = true;
    workload.usesReadbackStaging = true;
    workload.usesBarriers = true;
    workload.usesSubresourceBarriers = true;
    workload.usesStageAccessBarriers = true;

    const auto vulkanCommands =
        lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Vulkan),
            workload,
            CommandRecordingIntent::PrimaryCommandBuffer);
    assert(vulkanCommands.success);
    assert(hasNativeCommandSemantic(
        vulkanCommands, NativeCommandSemantic::VulkanMrtDepthStencilRenderPass));
    assert(
        hasNativeCommandSemantic(vulkanCommands, NativeCommandSemantic::VulkanResolveAttachment));
    assert(hasNativeCommandSemantic(vulkanCommands, NativeCommandSemantic::VulkanCopyCommands));
    assert(hasNativeCommandSemantic(
        vulkanCommands, NativeCommandSemantic::VulkanBackendOwnedStagingBuffer));
    assert(hasNativeCommandSemantic(
        vulkanCommands, NativeCommandSemantic::VulkanStageAccessPipelineBarrier));

    const auto metalCommands = lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Metal),
        workload,
        CommandRecordingIntent::PrimaryCommandBuffer);
    assert(metalCommands.success);
    assert(hasNativeCommandSemantic(
        metalCommands, NativeCommandSemantic::MetalMrtDepthStencilRenderEncoder));
    assert(hasNativeCommandSemantic(metalCommands, NativeCommandSemantic::MetalResolveAttachment));
    assert(hasNativeCommandSemantic(metalCommands, NativeCommandSemantic::MetalBlitCopyCommands));
    assert(hasNativeCommandSemantic(
        metalCommands, NativeCommandSemantic::MetalBackendOwnedStagingBuffer));
    assert(hasNativeCommandSemantic(metalCommands, NativeCommandSemantic::MetalResourceBarrier));

    const auto webgpuCommands =
        lowerCommandWorkload(nativeBackendCapabilityContract(Backend::WebGPU),
            workload,
            CommandRecordingIntent::PrimaryCommandBuffer);
    assert(webgpuCommands.success);
    assert(hasNativeCommandSemantic(
        webgpuCommands, NativeCommandSemantic::WebGpuMrtDepthStencilRenderPass));
    assert(
        hasNativeCommandSemantic(webgpuCommands, NativeCommandSemantic::WebGpuResolveAttachment));
    assert(hasNativeCommandSemantic(webgpuCommands, NativeCommandSemantic::WebGpuCopyCommands));
    assert(hasNativeCommandSemantic(
        webgpuCommands, NativeCommandSemantic::WebGpuBackendOwnedStagingBuffer));
    assert(hasNativeCommandSemantic(webgpuCommands, NativeCommandSemantic::WebGpuUsageTransition));

    const auto nullCommands = lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Null),
        workload,
        CommandRecordingIntent::PrimaryCommandBuffer);
    assert(nullCommands.success);
    assert(hasNativeCommandSemantic(
        nullCommands, NativeCommandSemantic::NullPassDescriptorValidation));
    assert(hasNativeCommandSemantic(
        nullCommands, NativeCommandSemantic::NullTransferCommandValidation));
    assert(hasNativeCommandSemantic(nullCommands, NativeCommandSemantic::NullBackendOwnedStaging));
    assert(hasNativeCommandSemantic(
        nullCommands, NativeCommandSemantic::NullStageAccessBarrierTracking));

    SynchronizationWorkloadDesc syncWorkload;
    syncWorkload.label = "FrameSyncAbstraction";
    syncWorkload.logicalFrameSlots = 2;
    syncWorkload.usesAcquire = true;
    syncWorkload.usesQueueSubmit = true;
    syncWorkload.usesPresent = true;
    syncWorkload.requiresTimelineProgress = true;
    syncWorkload.usesFrameInFlightSync = true;

    const auto vulkanSync = lowerSynchronizationWorkload(
        nativeBackendCapabilityContract(Backend::Vulkan), syncWorkload);
    assert(vulkanSync.success);
    assert(!vulkanSync.nativeHandleExposedToUpperLayer);
    assert(!vulkanSync.upperLayerBlockingWaitRequired);
    assert(hasNativeSynchronizationSemantic(
        vulkanSync, NativeSynchronizationSemantic::FrameInFlightSyncAbstraction));
    assert(hasNativeSynchronizationSemantic(
        vulkanSync, NativeSynchronizationSemantic::VulkanFrameInFlightTimeline));

    const auto metalSync =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::Metal), syncWorkload);
    assert(metalSync.success);
    assert(!metalSync.nativeHandleExposedToUpperLayer);
    assert(hasNativeSynchronizationSemantic(
        metalSync, NativeSynchronizationSemantic::MetalFrameInFlightCompletion));

    const auto webgpuSync = lowerSynchronizationWorkload(
        nativeBackendCapabilityContract(Backend::WebGPU), syncWorkload);
    assert(webgpuSync.success);
    assert(!webgpuSync.nativeHandleExposedToUpperLayer);
    assert(hasNativeSynchronizationSemantic(
        webgpuSync, NativeSynchronizationSemantic::WebGpuFrameInFlightCompletion));

    const auto nullSync =
        lowerSynchronizationWorkload(nativeBackendCapabilityContract(Backend::Null), syncWorkload);
    assert(nullSync.success);
    assert(hasNativeSynchronizationSemantic(
        nullSync, NativeSynchronizationSemantic::NullFrameInFlightSerial));

    return 0;
}
