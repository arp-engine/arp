#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/push_constant_validation.h"
#include "tests/rhi_shader_package_test_helpers.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;

    // L2 contract requires non-null push-constant data (AtomicFullBlockPush).
    // Provide a dummy buffer large enough for all test fixtures.
    static const std::uint8_t kDummyPushData[256] = {};

    // WebGPU soft-secondary commands use an explicit CPU-replay lowering. A
    // descriptor that has not authorized replay must still fail closed.
    {
        semantics::CommandWorkloadDesc workload;
        workload.label = "WebGpuSoftSecondaryContract";
        workload.commandCount = 1;
        const auto decision = semantics::lowerCommandWorkload(
            semantics::nativeBackendCapabilityContract(Backend::WebGPU),
            workload,
            semantics::CommandRecordingIntent::SoftSecondaryCommandBuffer);
        assert(!decision.success);
        assert(decision.fallbackMode == FallbackMode::DiagnosticFailure);
        assert(decision.diagnostics.find("explicit CPU replay authorization")
            != std::pmr::string::npos);
    }

    // WebGPU push-constant lowering is executable only after the browser
    // runtime has supplied the uniform-buffer limits consumed by the native
    // payload. A backend enum alone must not promote this command.
    {
        semantics::CommandWorkloadDesc workload;
        workload.label = "WebGpuPushConstantRuntimeGate";
        workload.commandCount = 1;
        workload.usesPushConstants = true;
        auto contract = semantics::nativeBackendCapabilityContract(Backend::WebGPU);
        const auto unqueried = semantics::lowerCommandWorkload(
            contract, workload, semantics::CommandRecordingIntent::PrimaryCommandBuffer);
        assert(!unqueried.success);
        assert(unqueried.diagnostics.find("[PC-E011]") != std::pmr::string::npos);

        contract.queriedFromNativeDevice = true;
        contract.queriedExtendedFeatures = true;
        contract.uniformBufferOffsetAlignmentBytes = 256;
        contract.maxUniformBufferBindingSizeBytes = 64u * 1024u;
        contract.pushConstantUploadRingBudgetBytes = 256;
        const auto queried = semantics::lowerCommandWorkload(
            contract, workload, semantics::CommandRecordingIntent::PrimaryCommandBuffer);
        assert(queried.success);
        assert(semantics::hasNativeCommandSemantic(
            queried, semantics::NativeCommandSemantic::WebGpuPushConstantUpload));
    }

    // P0-B1: pass termination only closes the encoder lifecycle.  It must
    // never be represented as a successful draw/dispatch consumption.
    {
        semantics::PushConstantConsumptionState consumption;
        consumption.onPush();
        assert(consumption.hasUnconsumedUpdate());
        consumption.onPassEnd();
        assert(!consumption.hasUnconsumedUpdate());
        consumption.onPush();
        consumption.onConsume();
        assert(!consumption.hasUnconsumedUpdate());
    }

    auto device = createNullDevice();
    const auto stagingReadinessBeforeSubmit = device->uploadStagingReadiness();
    assert(stagingReadinessBeforeSubmit.available());
    assert(
        stagingReadinessBeforeSubmit.status == semantics::UploadStagingReadinessStatus::Equivalent);
    PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute, 0, 128});
    auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDesc);
    assert(pipelineLayout);

    auto shaderPackage = arp::rhi::test::makePushContractShaderPackage(true);
    assert(validateShaderPackage(shaderPackage).success);

    auto invalidContractReflection = shaderPackage.reflection;
    invalidContractReflection.pushConstantContractVersion = "1.0";
    const auto invalidContract = semantics::validatePushConstantBinding(pipelineLayout.get(),
        ShaderStage::Vertex | ShaderStage::Fragment,
        ShaderStage::Vertex,
        0,
        128,
        &invalidContractReflection);
    assert(!invalidContract.success);
    assert(invalidContract.diagnostics.find("[PC-E015]") != std::pmr::string::npos);

    auto invalidStageReflection = shaderPackage.reflection;
    invalidStageReflection.pushConstantStages = "CS";
    const auto invalidStages = semantics::validatePushConstantBinding(pipelineLayout.get(),
        ShaderStage::Vertex | ShaderStage::Fragment,
        ShaderStage::Vertex,
        0,
        128,
        &invalidStageReflection);
    assert(!invalidStages.success);
    assert(invalidStages.diagnostics.find("[PC-E016]") != std::pmr::string::npos);

    RenderPipelineDesc renderPipelineDesc;
    renderPipelineDesc.label = "CommandValidationRenderPipeline";
    renderPipelineDesc.layout = pipelineLayout.get();
    renderPipelineDesc.shaderPackage = &shaderPackage;
    renderPipelineDesc.vertexShader = {"rhi_push_contract.slang", "vsMain", ShaderStage::Vertex};
    renderPipelineDesc.fragmentShader = {
        "rhi_push_contract.slang", "fsMain", ShaderStage::Fragment};
    renderPipelineDesc.colorFormat = TextureFormat::Bgra8Unorm;
    auto renderPipeline = device->createRenderPipeline(renderPipelineDesc);
    assert(renderPipeline);

    ComputePipelineDesc computePipelineDesc;
    computePipelineDesc.label = "CommandValidationComputePipeline";
    computePipelineDesc.layout = pipelineLayout.get();
    computePipelineDesc.shaderPackage = &shaderPackage;
    computePipelineDesc.computeShader = {"rhi_push_contract.slang",
        "csMain",
        ShaderStage::Compute,
        ShaderSourceLanguage::Slang,
        1,
        1,
        1};
    auto computePipeline = device->createComputePipeline(computePipelineDesc);
    assert(computePipeline);

    auto encoder = device->createCommandEncoder({"Frame"});
    auto sceneColor = device->createTexture({
        "SceneColor",
        {64, 64, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(sceneColor);
    assert(sceneColor->state() == ResourceState::Undefined);
    const auto colorBarrier =
        encoder->transition(*sceneColor, ResourceState::Undefined, ResourceState::ColorAttachment);
    assert(colorBarrier.success);
    assert(colorBarrier.resource == sceneColor->id());
    assert(sceneColor->state() == ResourceState::ColorAttachment);

    TextureDesc shadowAtlasDesc;
    shadowAtlasDesc.label = "ShadowAtlasDepth";
    shadowAtlasDesc.extent = {64, 64, 1};
    shadowAtlasDesc.format = TextureFormat::D32Float;
    shadowAtlasDesc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    shadowAtlasDesc.memory = MemoryUsage::GpuOnly;
    shadowAtlasDesc.initialState = ResourceState::Undefined;
    shadowAtlasDesc.mipLevelCount = 4;
    shadowAtlasDesc.arrayLayerCount = 2;
    const auto d32DefaultRange = defaultTextureSubresourceRange(shadowAtlasDesc);
    assert(d32DefaultRange.aspect == TextureAspect::Depth);
    assert(d32DefaultRange.mipLevelCount == shadowAtlasDesc.mipLevelCount);
    assert(d32DefaultRange.arrayLayerCount == shadowAtlasDesc.arrayLayerCount);
    shadowAtlasDesc.format = TextureFormat::Depth24Stencil8;
    assert(defaultTextureSubresourceRange(shadowAtlasDesc).aspect == TextureAspect::Depth);
    shadowAtlasDesc.format = TextureFormat::Bgra8Unorm;
    assert(defaultTextureSubresourceRange(shadowAtlasDesc).aspect == TextureAspect::Color);

    const auto mismatchedBarrier =
        encoder->transition(*sceneColor, ResourceState::ShaderRead, ResourceState::Present);
    assert(!mismatchedBarrier.success);
    assert(mismatchedBarrier.diagnostics == "RHI-ORIGINAL-TEXTURE-STATE-BEFORE-MISMATCH");
    assert(sceneColor->state() == ResourceState::ColorAttachment);
    auto pass = encoder->beginRenderPass({"MainColor"});
    pass->setPipeline(*renderPipeline);
    const auto initialRenderPush = pass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData);
    assert(initialRenderPush.success);
    pass->drawIndexed(36, 1, 0, 0, 0);
    pass->end();
    auto commandBuffer = encoder->finish();
    assert(commandBuffer->commandCount() == 6u);

    auto computeEncoder = device->createCommandEncoder({"ComputeFrame"});
    auto skinningBuffer = device->createBuffer({
        "SkinningBuffer",
        1024,
        ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(skinningBuffer);
    const auto bufferBarrier = computeEncoder->transition(
        *skinningBuffer, ResourceState::Undefined, ResourceState::ShaderWrite);
    assert(bufferBarrier.success);
    assert(skinningBuffer->state() == ResourceState::ShaderWrite);
    auto computePass = computeEncoder->beginComputePass({"SkinningCompute"});
    computePass->setPipeline(*computePipeline);
    computePass->pushConstants(ShaderStage::Compute, 0, 128, kDummyPushData);
    computePass->dispatch(8, 1, 1);
    computePass->end();
    auto computeCommands = computeEncoder->finish();
    assert(computeCommands->commandCount() == 6u);

    // Access-only dependencies are distinct from state transitions: the
    // shader-write state stays in place while the next compute phase gains
    // read/write visibility. A no-op state transition remains invalid.
    auto memoryBarrierEncoder = device->createCommandEncoder({"MemoryBarrier"});
    const auto stateBeforeMemoryBarrier = skinningBuffer->state();
    const ResourceMemoryBarrierDesc memoryBarrier{
        skinningBuffer->id(),
        PipelineStage::ComputeShader,
        PipelineStage::ComputeShader,
        ResourceAccess::ShaderWrite,
        ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
    };
    const auto memoryBarrierResult =
        memoryBarrierEncoder->memoryBarrier(*skinningBuffer, memoryBarrier);
    assert(memoryBarrierResult.success);
    assert(memoryBarrierResult.stageAccessAware);
    assert(memoryBarrierResult.before == stateBeforeMemoryBarrier);
    assert(memoryBarrierResult.after == stateBeforeMemoryBarrier);
    assert(skinningBuffer->state() == stateBeforeMemoryBarrier);

    auto missingSourceAccess = memoryBarrier;
    missingSourceAccess.sourceAccess = ResourceAccess::None;
    const auto missingSourceAccessResult =
        memoryBarrierEncoder->memoryBarrier(*skinningBuffer, missingSourceAccess);
    assert(!missingSourceAccessResult.success);
    assert(missingSourceAccessResult.diagnostics
        == "RHI-RESOURCE-MEMORY-BARRIER-SOURCE-ACCESS-MISSING");

    const ResourceBarrierDesc noOpBarrier{
        skinningBuffer->id(),
        {},
        ResourceState::ShaderWrite,
        ResourceState::ShaderWrite,
        PipelineStage::ComputeShader,
        PipelineStage::ComputeShader,
        ResourceAccess::ShaderWrite,
        ResourceAccess::ShaderRead,
    };
    const auto noOpBarrierResult = memoryBarrierEncoder->transition(*skinningBuffer, noOpBarrier);
    assert(!noOpBarrierResult.success);
    assert(memoryBarrierEncoder->finish());

    auto storageImage = device->createTexture({"MemoryBarrierImage",
        {8, 8, 1},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Storage,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderWrite});
    assert(storageImage);
    auto textureBarrierEncoder = device->createCommandEncoder({"TextureMemoryBarrier"});
    auto textureDependency = memoryBarrier;
    textureDependency.resource = storageImage->id();
    textureDependency.destinationStages = PipelineStage::VertexShader;
    textureDependency.destinationAccess = ResourceAccess::ShaderRead;
    auto textureDependencyResult =
        textureBarrierEncoder->memoryBarrier(*storageImage, textureDependency);
    assert(textureDependencyResult.success);
    assert(textureDependencyResult.sourceStages == PipelineStage::ComputeShader);
    assert(textureDependencyResult.destinationStages == PipelineStage::VertexShader);
    assert(storageImage->state() == ResourceState::ShaderWrite);
    auto foreignDevice = createNullDevice();
    auto foreignTexture = foreignDevice->createTexture(storageImage->desc());
    auto foreignDependency = textureDependency;
    foreignDependency.resource = foreignTexture->id();
    const auto foreignResult =
        textureBarrierEncoder->memoryBarrier(*foreignTexture, foreignDependency);
    assert(!foreignResult.success);
    assert(foreignResult.diagnostics == "RHI-RESOURCE-MEMORY-BARRIER-FOREIGN-RESOURCE");
    auto wrongImageDependency = textureDependency;
    wrongImageDependency.resource = skinningBuffer->id();
    assert(!textureBarrierEncoder->memoryBarrier(*storageImage, wrongImageDependency).success);
    auto openPass = textureBarrierEncoder->beginComputePass({"MemoryBarrierActivePass"});
    assert(openPass);
    assert(!textureBarrierEncoder->memoryBarrier(*storageImage, textureDependency).success);
    openPass->end();
    assert(textureBarrierEncoder->finish());
    assert(!textureBarrierEncoder->memoryBarrier(*storageImage, textureDependency).success);

    // P0-B1 reference matrix: a successful consuming command permits the
    // next update, while two updates without an intervening consume fail with
    // the same stable E013 diagnostic.
    auto consumptionEncoder = device->createCommandEncoder({"PushConsumption"});
    auto consumptionPass = consumptionEncoder->beginRenderPass({"ConsumptionRender"});
    assert(consumptionPass->setPipeline(*renderPipeline).success);
    assert(consumptionPass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
    assert(consumptionPass->drawIndexed(3, 1, 0, 0, 0).success);
    assert(consumptionPass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
    const auto duplicateRenderPush =
        consumptionPass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData);
    assert(!duplicateRenderPush.success);
    assert(duplicateRenderPush.diagnostics.find("[PC-E013]") != std::pmr::string::npos);
    assert(consumptionPass->end().success);
    assert(consumptionEncoder->finish());

    auto consumptionComputeEncoder = device->createCommandEncoder({"PushConsumptionCompute"});
    auto consumptionComputePass =
        consumptionComputeEncoder->beginComputePass({"ConsumptionCompute"});
    assert(consumptionComputePass->setPipeline(*computePipeline).success);
    assert(consumptionComputePass->pushConstants(ShaderStage::Compute, 0, 128, kDummyPushData)
            .success);
    assert(consumptionComputePass->dispatch(1, 1, 1).success);
    assert(consumptionComputePass->pushConstants(ShaderStage::Compute, 0, 128, kDummyPushData)
            .success);
    const auto duplicateComputePush =
        consumptionComputePass->pushConstants(ShaderStage::Compute, 0, 128, kDummyPushData);
    assert(!duplicateComputePush.success);
    assert(duplicateComputePush.diagnostics.find("[PC-E013]") != std::pmr::string::npos);
    assert(consumptionComputePass->end().success);
    assert(consumptionComputeEncoder->finish());

    // P0-B1 edge-case matrix: every successful consuming command (draw,
    // drawIndexed, drawIndexedIndirect, dispatch) must clear the pending
    // push-constant update so the next push succeeds.  Also verifies that
    // pass end resets the per-pass consumption state: a push left
    // unconsumed at end() must NOT leak into the next pass.
    {
        // draw (non-indexed) consumes a pending push.
        auto drawConsumeEncoder = device->createCommandEncoder({"DrawConsume"});
        auto drawConsumePass = drawConsumeEncoder->beginRenderPass({"DrawConsumeRender"});
        assert(drawConsumePass->setPipeline(*renderPipeline).success);
        assert(drawConsumePass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
        assert(drawConsumePass->draw(3, 1, 0, 0).success);
        // After draw consumes, the next push must succeed (not E013).
        assert(drawConsumePass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
        assert(drawConsumePass->draw(3, 1, 0, 0).success);
        assert(drawConsumePass->end().success);
        assert(drawConsumeEncoder->finish());
    }
    {
        // drawIndexedIndirect consumes a pending push.
        auto indirectEncoder = device->createCommandEncoder({"IndirectConsume"});
        // Indirect draw args buffer (5 uint32: indexCount, instanceCount,
        // firstIndex, vertexOffset, firstInstance).  Content is irrelevant
        // for the Null backend - it only validates the consume state.
        // ResourceUsage has no IndirectBuffer flag in this RHI profile; the
        // Null backend does not gate drawIndexedIndirect on usage flags.
        auto indirectArgs = device->createBuffer({
            "IndirectArgs",
            32,
            ResourceUsage::StorageBuffer | ResourceUsage::CopyDst,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined,
        });
        assert(indirectArgs);
        auto indirectPass = indirectEncoder->beginRenderPass({"IndirectConsumeRender"});
        assert(indirectPass->setPipeline(*renderPipeline).success);
        assert(indirectPass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
        // drawIndexedIndirect has no validation gate on pipeline/descriptors
        // in the Null profile - it unconditionally consumes the pending push.
        assert(indirectPass->drawIndexedIndirect(*indirectArgs, 0, 1, 0).success);
        assert(indirectPass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
        assert(indirectPass->end().success);
        assert(indirectEncoder->finish());
    }
    {
        // Pass end resets the per-pass consumption state: a push left
        // unconsumed at end() must NOT leak into the next pass on the same
        // encoder. The second pass must be able to push without E013.
        auto leakCheckEncoder = device->createCommandEncoder({"LeakCheck"});
        auto firstPass = leakCheckEncoder->beginRenderPass({"LeakCheckFirst"});
        assert(firstPass->setPipeline(*renderPipeline).success);
        // Push without an intervening consume - end() must reset state.
        assert(firstPass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
        assert(firstPass->end().success);
        // New pass on the same encoder: push must succeed (no leaked E013).
        auto secondPass = leakCheckEncoder->beginRenderPass({"LeakCheckSecond"});
        assert(secondPass->setPipeline(*renderPipeline).success);
        assert(secondPass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData).success);
        assert(secondPass->drawIndexed(3, 1, 0, 0, 0).success);
        assert(secondPass->end().success);
        assert(leakCheckEncoder->finish());
    }

    auto transferEncoder = device->createCommandEncoder({"TransferFrame"});
    auto transferPass = transferEncoder->beginTransferPass({"UploadPass"});
    transferPass->copyBufferToBuffer(128);
    transferPass->end();
    auto transferCommands = transferEncoder->finish();
    assert(transferCommands->commandCount() == 3u);

    auto bundleEncoder = device->createRenderBundleEncoder({"StaticMeshBundle"});
    bundleEncoder->drawIndexed(12, 1, 0, 0, 0);
    auto bundle = bundleEncoder->finish();
    assert(bundle->commandCount() == 1u);
    auto secondBundle = bundleEncoder->finish();
    assert(!secondBundle);
    const auto bundleDrawAfterFinish = bundleEncoder->drawIndexed(12, 1, 0, 0, 0);
    assert(!bundleDrawAfterFinish.success);
    assert(bundleDrawAfterFinish.diagnostics.find("finished") != std::pmr::string::npos);
    assert(bundleDrawAfterFinish.recordedCommandCount == 1u);

    auto secondaryEncoder = device->createSecondaryCommandEncoder({"StaticMeshSecondary", true});
    // A-04: secondary push-constant recording is rejected in this profile.
    const auto secondaryPush =
        secondaryEncoder->pushConstants(ShaderStage::Vertex, 0, 32, kDummyPushData);
    assert(!secondaryPush.success);
    assert(secondaryPush.diagnostics.find("[PC-E014]") != std::pmr::string::npos);
    secondaryEncoder->drawIndexed(24, 1, 0, 0, 0);
    auto secondaryCommands = secondaryEncoder->finish();
    assert(secondaryCommands);
    assert(secondaryCommands->commandCount() == 1u);
    assert(secondaryCommands->softSecondary());
    auto secondSecondary = secondaryEncoder->finish();
    assert(!secondSecondary);
    const auto secondaryPushAfterFinish =
        secondaryEncoder->pushConstants(ShaderStage::Vertex, 0, 32, kDummyPushData);
    assert(!secondaryPushAfterFinish.success);
    assert(secondaryPushAfterFinish.diagnostics.find("finished") != std::pmr::string::npos);
    assert(secondaryPushAfterFinish.recordedCommandCount == 1u);

    auto swapchain = device->createSwapchain({
        "SmokeSurface",
        {320, 180},
        TextureFormat::Bgra8Unorm,
        2u,
    });
    auto frame = swapchain->acquireNextFrame();
    assert(frame.success);
    auto frameEncoder = device->createCommandEncoder({"NullSmokeFrame"});
    auto framePass = frameEncoder->beginRenderPass({"Backbuffer"});
    framePass->setPipeline(*renderPipeline);
    framePass->pushConstants(ShaderStage::Fragment, 0, 128, kDummyPushData);
    framePass->executeSecondary(*secondaryCommands);
    framePass->drawIndexed(3, 1, 0, 0, 0);
    framePass->end();
    auto frameCommands = frameEncoder->finish();
    const auto submitted = device->queue().submit(*frameCommands);
    assert(submitted.success);
    assert(submitted.submittedCommandCount == frameCommands->commandCount());
    const auto stagingReadinessAfterSubmit = device->uploadStagingReadiness();
    assert(stagingReadinessAfterSubmit.advancedSince(stagingReadinessBeforeSubmit));
    const auto presented = swapchain->present(frame);
    assert(presented.success);
    assert(device->queue().submitCount() == 1u);

    auto reacquired = swapchain->acquireNextFrame();
    assert(reacquired.success);
    // After present the slot is freed; the next acquire yields a valid free
    // slot. The exact index depends on the backend's slot-rotation policy
    // (linear-scan vs round-robin), so only assert that a free slot was found
    // and the serial advanced.
    assert(reacquired.frameIndex < 2u);
    assert(reacquired.frameSerial > frame.frameSerial);

    auto missingPipelineEncoder = device->createCommandEncoder({"MissingPipeline"});
    auto missingPipelinePass = missingPipelineEncoder->beginRenderPass({"Backbuffer"});
    const auto pushWithoutPipeline =
        missingPipelinePass->pushConstants(ShaderStage::Vertex, 0, 16, kDummyPushData);
    assert(!pushWithoutPipeline.success);
    assert(pushWithoutPipeline.diagnostics.find("[PC-E005]") != std::pmr::string::npos);
    assert(pushWithoutPipeline.recordedCommandCount == 1u);
    const auto drawWithoutPipeline = missingPipelinePass->drawIndexed(3, 1, 0, 0, 0);
    assert(!drawWithoutPipeline.success);
    assert(drawWithoutPipeline.diagnostics.find("pipeline") != std::pmr::string::npos);
    const auto setMissingPipeline = missingPipelinePass->setPipeline(*renderPipeline);
    assert(setMissingPipeline.success);
    const auto pushWithoutStages =
        missingPipelinePass->pushConstants(ShaderStage::None, 0, 16, kDummyPushData);
    assert(!pushWithoutStages.success);
    assert(pushWithoutStages.diagnostics.find("[PC-E001]") != std::pmr::string::npos);
    assert(pushWithoutStages.recordedCommandCount == 2u);
    const auto pushOutsideRange =
        missingPipelinePass->pushConstants(ShaderStage::Vertex, 96, 64, kDummyPushData);
    assert(!pushOutsideRange.success);
    assert(pushOutsideRange.diagnostics.find("[PC-E002]") != std::pmr::string::npos);
    assert(pushOutsideRange.recordedCommandCount == 2u);
    const auto overflowingPushUpdate =
        missingPipelinePass->pushConstants(ShaderStage::Vertex, 0xfffffff0u, 32, kDummyPushData);
    assert(!overflowingPushUpdate.success);
    assert(overflowingPushUpdate.diagnostics.find("[PC-E012]") != std::pmr::string::npos);
    assert(overflowingPushUpdate.recordedCommandCount == 2u);

    // Size-mismatch negative matrix: pipeline layout declares 128 bytes.
    // AtomicFullBlockPush requires exact range.sizeBytes == push.sizeBytes.
    {
        const auto mismatch64 =
            missingPipelinePass->pushConstants(ShaderStage::Vertex, 0, 64, kDummyPushData);
        assert(!mismatch64.success);
        assert(mismatch64.diagnostics.find("[PC-E009]") != std::pmr::string::npos);
        assert(mismatch64.recordedCommandCount == 2u);

        const auto mismatch32 =
            missingPipelinePass->pushConstants(ShaderStage::Vertex, 0, 32, kDummyPushData);
        assert(!mismatch32.success);
        assert(mismatch32.diagnostics.find("[PC-E009]") != std::pmr::string::npos);
        assert(mismatch32.recordedCommandCount == 2u);
    }

    const auto drawAfterPipeline = missingPipelinePass->drawIndexed(3, 1, 0, 0, 0);
    assert(drawAfterPipeline.success);
    const auto firstEnd = missingPipelinePass->end();
    assert(firstEnd.success);
    const auto secondEnd = missingPipelinePass->end();
    assert(!secondEnd.success);
    assert(secondEnd.diagnostics.find("ended") != std::pmr::string::npos);
    auto validAfterPipelineCommands = missingPipelineEncoder->finish();
    assert(validAfterPipelineCommands);
    assert(validAfterPipelineCommands->commandCount() == 4u);

    auto missingComputePipelineEncoder = device->createCommandEncoder({"MissingComputePipeline"});
    auto missingComputePipelinePass =
        missingComputePipelineEncoder->beginComputePass({"MissingComputePipelinePass"});
    const auto computePushWithoutPipeline =
        missingComputePipelinePass->pushConstants(ShaderStage::Compute, 0, 16, kDummyPushData);
    assert(!computePushWithoutPipeline.success);
    assert(computePushWithoutPipeline.diagnostics.find("[PC-E005]") != std::pmr::string::npos);
    assert(computePushWithoutPipeline.recordedCommandCount == 1u);
    const auto dispatchWithoutPipeline = missingComputePipelinePass->dispatch(1, 1, 1);
    assert(!dispatchWithoutPipeline.success);
    assert(dispatchWithoutPipeline.diagnostics.find("pipeline") != std::pmr::string::npos);
    missingComputePipelinePass->setPipeline(*computePipeline);
    const auto computePushOutsideRange =
        missingComputePipelinePass->pushConstants(ShaderStage::Compute, 120, 16, kDummyPushData);
    assert(!computePushOutsideRange.success);
    assert(computePushOutsideRange.diagnostics.find("[PC-E002]") != std::pmr::string::npos);
    assert(computePushOutsideRange.recordedCommandCount == 2u);
    const auto dispatchAfterPipeline = missingComputePipelinePass->dispatch(1, 1, 1);
    assert(dispatchAfterPipeline.success);
    missingComputePipelinePass->end();
    assert(missingComputePipelineEncoder->finish());

    auto activeScopeEncoder = device->createCommandEncoder({"ActivePassScope"});
    auto activeScopeTexture = device->createTexture({
        "ActiveScopeColor",
        {16, 16, 1},
        TextureFormat::Bgra8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined,
    });
    assert(activeScopeTexture);
    auto activeScopePass = activeScopeEncoder->beginRenderPass({"ActiveScopePass"});
    const auto barrierDuringPass = activeScopeEncoder->transition(
        *activeScopeTexture, ResourceState::Undefined, ResourceState::ColorAttachment);
    assert(!barrierDuringPass.success);
    assert(barrierDuringPass.diagnostics.find("active pass") != std::pmr::string::npos);
    auto unfinishedCommands = activeScopeEncoder->finish();
    assert(!unfinishedCommands);
    assert(!activeScopeEncoder->lastValidation().success);
    assert(activeScopeEncoder->lastValidation().diagnostics.find("active pass")
        != std::pmr::string::npos);
    activeScopePass->end();
    const auto barrierAfterPass = activeScopeEncoder->transition(
        *activeScopeTexture, ResourceState::Undefined, ResourceState::ColorAttachment);
    assert(barrierAfterPass.success);
    assert(activeScopeEncoder->finish());
    return 0;
}
