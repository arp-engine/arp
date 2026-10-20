// Dynamic viewport/scissor contract test (Phase 1.5a).
//
// Pins the four observable facets of the dynamic setViewport/setScissor L2
// command (AGENTS.md "RHI 最高优先级契约" section 7: L2 semantics, lowering
// decision, capability/execution gate, Null validation, negative assertions):
//
//   Facet 1 - Null L2 command validation (success + negative paths):
//       setPipeline -> setViewport -> setScissor -> drawIndexed all succeed;
//       setViewport before setPipeline fails with [VS-E002]; invalid values
//       fail with [VS-E003]/[VS-E005]/[VS-E006]; setViewport after end() fails
//       with [VS-E001]; recordedCommandCount does not advance on rejection.
//
//   Facet 2 - Pipeline-state lowering parity:
//       desc.dynamicViewportScissor=true lowers successfully on Null/Vulkan/
//       Metal/WebGPU, viewportScissorLowered=true on all four, and the
//       L2DynamicViewportScissorOptIn semantic is recorded. Mixing
//       dynamicViewportScissor with hasViewport/hasScissor is rejected with
//       [VS-E017]. The pipeline cache key distinguishes the dynamic variant
//       from the static variant (so Vulkan's VkPipelineDynamicStateCreateInfo
//       does not collide in the pipeline cache).
//
//   Facet 3 - Runtime command lowering parity:
//       CommandWorkloadDesc{usesDynamicViewportScissor=true} lowers
//       successfully on all four backends and each records the corresponding
//       Null/Vulkan/Metal/WebGpu SetViewport/SetScissor command semantic.
//
//   Facet 4 - L1 value validator unit tests:
//       validateViewportCommand / validateScissorCommand produce the stable
//       [VS-E003]/[VS-E004]/[VS-E005]/[VS-E006] codes for invalid values and
//       accept valid values.
//
// Status: Contract Complete. Vulkan Backend Validated via the WoCC shadow
// pipeline in Phase 1.5b. Metal source lowering is complete and capability
// gated; Apple runtime validation remains unavailable here. WebGPU native path
// is core but not exercised on this Windows MinGW build.

#include "core/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "rhi/semantics/render_target_coordinates.h"
#include "rhi/semantics/viewport_scissor_validation.h"
#include "tests/rhi_shader_package_test_helpers.h"

#include <cassert>
#include <cstdint>

namespace {

// Reuse the shader-package shape proven by rhi_command_contract_tests +
// rhi_depth_bias_clamp_contract_tests: a minimal vsMain/fsMain pair with a
// 128-byte push-constant range that satisfies AtomicFullBlockPush/1.
arp::rhi::ShaderPackageDesc makeDynamicViewportScissorShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "DynamicViewportScissorContractShaderPackage";
    shaderPackage.slangModulePath = "rhi_push_contract.slang";
    shaderPackage.entrypoints.push_back(
        {"rhi_push_contract.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {"rhi_push_contract.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.entrypoints.push_back({"rhi_push_contract.slang",
        "csMain",
        arp::rhi::ShaderStage::Compute,
        arp::rhi::ShaderSourceLanguage::Slang,
        1,
        1,
        1});
    shaderPackage.artifacts.spirvPath = "rhi_push_contract.spv";
    shaderPackage.artifacts.mslPath = "rhi_push_contract.metal";
    shaderPackage.artifacts.wgslPath = "rhi_push_contract.wgsl";
    shaderPackage.artifacts.reflectionPath = "rhi_push_contract.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = 128;
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "CS|FS|VS";
    shaderPackage.reflection.pushConstantWebGpuBindingGroup = 0;
    shaderPackage.reflection.pushConstantWebGpuBindingIndex = 0;
    shaderPackage.reflection.pushConstantMetalBufferIndex = 0;
    shaderPackage.reflection.computeWorkgroupSizeX = 1;
    shaderPackage.reflection.computeWorkgroupSizeY = 1;
    shaderPackage.reflection.computeWorkgroupSizeZ = 1;
    shaderPackage.reflection.metalGpuCountedIndirectStatus = "Unavailable";
    shaderPackage.reflection.metalTessellationStatus = "Unavailable";
    arp::rhi::test::addFloat4PushConstantMembers(shaderPackage.reflection, 8u);
    return shaderPackage;
}

arp::rhi::RenderPipelineDesc makeBasePipelineDesc(
    const arp::rhi::PipelineLayout& layout, const arp::rhi::ShaderPackageDesc& shaderPackage)
{
    using namespace arp::rhi;
    RenderPipelineDesc desc;
    desc.label = "DynamicViewportScissorContractPipeline";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader = {"rhi_push_contract.slang", "vsMain", ShaderStage::Vertex};
    desc.fragmentShader = {"rhi_push_contract.slang", "fsMain", ShaderStage::Fragment};
    desc.colorFormat = TextureFormat::Bgra8Unorm;
    desc.topology = PrimitiveTopology::TriangleList;
    return desc;
}

bool hasPipelineStateSemantic(
    const arp::rhi::semantics::NativePipelineStateLoweringDecision& decision,
    arp::rhi::semantics::NativePipelineStateSemantic semantic)
{
    return arp::rhi::semantics::hasNativePipelineStateSemantic(decision, semantic);
}

bool hasCommandSemantic(const arp::rhi::semantics::NativeCommandLoweringDecision& decision,
    arp::rhi::semantics::NativeCommandSemantic semantic)
{
    return arp::rhi::semantics::hasNativeCommandSemantic(decision, semantic);
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    static const std::uint8_t kDummyPushData[256] = {};

    // CPU-authored UI atlases use a top-left first row. Their sampled-texture
    // V transform is independent of the render-target origin: Metal and
    // WebGPU require inversion, while Vulkan retains the canonical interval.
    {
        assert(uiCoordinatesRequireYFlip(Backend::Metal));
        assert(uiCoordinatesRequireYFlip(Backend::WebGPU));
        assert(!uiCoordinatesRequireYFlip(Backend::Vulkan));
        assert(!uiCoordinatesRequireYFlip(Backend::Null));

        float metalOriginY = 0.3f;
        float metalSizeY = 0.1f;
        applyCpuRasterTextureUvYConvention(&metalOriginY, &metalSizeY, Backend::Metal);
        assert(metalOriginY == 0.4f);
        assert(metalSizeY == -0.1f);

        float vulkanOriginY = 0.3f;
        float vulkanSizeY = 0.1f;
        applyCpuRasterTextureUvYConvention(&vulkanOriginY, &vulkanSizeY, Backend::Vulkan);
        assert(vulkanOriginY == 0.3f);
        assert(vulkanSizeY == 0.1f);

        // RenderTargetCoordinates/2 covers every backend. Metal native
        // asymmetric readbacks prove both raster and sampling directions.
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const bool flip = backend == Backend::Metal || backend == Backend::WebGPU;
            assert(projectionRequiresYFlip(backend) == flip);
            assert(textureUvYTransformForBackend(backend)
                == (flip ? TextureUvYTransform::Inverted : TextureUvYTransform::Identity));
            float scale = 0.0f, bias = 0.0f;
            writeTextureUvYScaleBias(textureUvYTransformForBackend(backend), &scale, &bias);
            assert(scale == (flip ? -1.0f : 1.0f) && bias == (flip ? 1.0f : 0.0f));
            // A subviewport stays in its own atlas strip when clip Y is
            // converted to texture V (including the nonzero-origin case).
            float atlasOrigin = 0.5f, atlasSize = 0.25f;
            applyTextureUvYConvention(&atlasOrigin, &atlasSize, backend);
            assert(atlasOrigin == (flip ? 0.75f : 0.5f));
            assert(atlasSize == (flip ? -0.25f : 0.25f));
            assert(atlasOrigin + atlasSize == (flip ? 0.5f : 0.75f));
            float vp[16];
            for (unsigned i = 0; i < 16u; ++i)
                vp[i] = static_cast<float>(i + 1u);
            applyViewProjectionYConvention(vp, backend);
            for (unsigned i = 0; i < 16u; ++i)
                assert(vp[i]
                    == static_cast<float>(i + 1u) * (flip && i >= 4u && i < 8u ? -1.0f : 1.0f));
            applyRenderTargetSamplingYConvention(vp, backend);
            for (unsigned i = 0; i < 16u; ++i)
                assert(vp[i] == static_cast<float>(i + 1u));
        }
    }

    // ----------------------------------------------------------------------
    // Facet 4: L1 value validator unit tests (run first; no device needed).
    // ----------------------------------------------------------------------
    {
        // Valid viewport passes.
        const auto validVp = validateViewportCommand({0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f});
        assert(validVp.success);
        assert(validVp.diagnostics.empty());

        // width <= 0 -> [VS-E003]
        const auto zeroWidth = validateViewportCommand({0.0f, 0.0f, 0.0f, 2048.0f, 0.0f, 1.0f});
        assert(!zeroWidth.success);
        assert(zeroWidth.diagnostics.find("[VS-E003]") != std::pmr::string::npos);

        const auto negWidth = validateViewportCommand({0.0f, 0.0f, -1.0f, 2048.0f, 0.0f, 1.0f});
        assert(!negWidth.success);
        assert(negWidth.diagnostics.find("[VS-E003]") != std::pmr::string::npos);

        // height <= 0 -> [VS-E004]
        const auto zeroHeight = validateViewportCommand({0.0f, 0.0f, 2048.0f, 0.0f, 0.0f, 1.0f});
        assert(!zeroHeight.success);
        assert(zeroHeight.diagnostics.find("[VS-E004]") != std::pmr::string::npos);

        // minDepth > maxDepth -> [VS-E005]
        const auto reversedDepth =
            validateViewportCommand({0.0f, 0.0f, 2048.0f, 2048.0f, 0.8f, 0.2f});
        assert(!reversedDepth.success);
        assert(reversedDepth.diagnostics.find("[VS-E005]") != std::pmr::string::npos);

        // minDepth == maxDepth is allowed (zero-thickness depth slice).
        const auto flatDepth = validateViewportCommand({0.0f, 0.0f, 2048.0f, 2048.0f, 0.5f, 0.5f});
        assert(flatDepth.success);

        // Valid scissor passes.
        const auto validSc = validateScissorCommand({0, 0, 2048, 2048});
        assert(validSc.success);
        assert(validSc.diagnostics.empty());

        // width == 0 -> [VS-E006]
        const auto zeroScissorW = validateScissorCommand({0, 0, 0, 2048});
        assert(!zeroScissorW.success);
        assert(zeroScissorW.diagnostics.find("[VS-E006]") != std::pmr::string::npos);

        // height == 0 -> [VS-E006]
        const auto zeroScissorH = validateScissorCommand({0, 0, 2048, 0});
        assert(!zeroScissorH.success);
        assert(zeroScissorH.diagnostics.find("[VS-E006]") != std::pmr::string::npos);
    }

    // ----------------------------------------------------------------------
    // Facet 2: Pipeline-state lowering parity (no live device needed).
    // ----------------------------------------------------------------------
    auto device = createNullDevice();
    assert(device);
    PipelineLayoutDesc pipelineLayoutDesc;
    pipelineLayoutDesc.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute, 0, 128});
    auto pipelineLayout = device->createPipelineLayout(pipelineLayoutDesc);
    assert(pipelineLayout);
    auto shaderPackage = makeDynamicViewportScissorShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);

    {
        auto desc = makeBasePipelineDesc(*pipelineLayout, shaderPackage);
        desc.dynamicViewportScissor = true;

        // All four backends must lower successfully and record the L2 opt-in.
        const auto nullDecision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), desc);
        assert(nullDecision.success);
        assert(nullDecision.viewportScissorLowered);
        assert(hasPipelineStateSemantic(
            nullDecision, NativePipelineStateSemantic::L2DynamicViewportScissorOptIn));

        const auto vulkanDecision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Vulkan), desc);
        assert(vulkanDecision.success);
        assert(vulkanDecision.viewportScissorLowered);
        assert(hasPipelineStateSemantic(
            vulkanDecision, NativePipelineStateSemantic::L2DynamicViewportScissorOptIn));
        // Vulkan is the one backend that changes pipeline-creation behavior in
        // response to the opt-in (emits VkPipelineDynamicStateCreateInfo).
        assert(hasPipelineStateSemantic(
            vulkanDecision, NativePipelineStateSemantic::VulkanDynamicViewportScissor));

        const auto metalDecision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Metal), desc);
        assert(metalDecision.success);
        assert(metalDecision.viewportScissorLowered);
        assert(hasPipelineStateSemantic(
            metalDecision, NativePipelineStateSemantic::L2DynamicViewportScissorOptIn));

        const auto webGpuDecision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::WebGPU), desc);
        assert(webGpuDecision.success);
        assert(webGpuDecision.viewportScissorLowered);
        assert(hasPipelineStateSemantic(
            webGpuDecision, NativePipelineStateSemantic::L2DynamicViewportScissorOptIn));
    }

    // Mixing dynamicViewportScissor with hasViewport -> [VS-E017] rejection.
    {
        auto desc = makeBasePipelineDesc(*pipelineLayout, shaderPackage);
        desc.dynamicViewportScissor = true;
        desc.hasViewport = true;
        desc.viewport = {0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f};

        const auto decision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), desc);
        assert(!decision.success);
        assert(decision.diagnostics.find("[VS-E017]") != std::pmr::string::npos);
    }

    // Mixing dynamicViewportScissor with hasScissor -> [VS-E017] rejection.
    {
        auto desc = makeBasePipelineDesc(*pipelineLayout, shaderPackage);
        desc.dynamicViewportScissor = true;
        desc.hasScissor = true;
        desc.scissor = {0, 0, 2048, 2048};

        const auto decision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), desc);
        assert(!decision.success);
        assert(decision.diagnostics.find("[VS-E017]") != std::pmr::string::npos);
    }

    // Pipeline cache key must distinguish dynamic vs static variants. The
    // Vulkan backend emits VkPipelineDynamicStateCreateInfo only when
    // dynamicViewportScissor=true, so a cache collision between the two would
    // hand back a pipeline with the wrong dynamic-state configuration.
    {
        auto staticDesc = makeBasePipelineDesc(*pipelineLayout, shaderPackage);
        staticDesc.hasViewport = true;
        staticDesc.viewport = {0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f};
        staticDesc.hasScissor = true;
        staticDesc.scissor = {0, 0, 2048, 2048};

        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision =
                lowerRenderPipelineState(nativeBackendCapabilityContract(backend), staticDesc);
            assert(decision.success && decision.viewportScissorLowered);
            assert(!hasPipelineStateSemantic(
                decision, NativePipelineStateSemantic::L2DynamicViewportScissorOptIn));
            auto invalid = staticDesc;
            invalid.dynamicViewportScissor = true;
            const auto rejected =
                lowerRenderPipelineState(nativeBackendCapabilityContract(backend), invalid);
            assert(!rejected.success);
            assert(rejected.diagnostics.find("[VS-E017]") != std::pmr::string::npos);
        }

        auto dynamicDesc = makeBasePipelineDesc(*pipelineLayout, shaderPackage);
        dynamicDesc.dynamicViewportScissor = true;

        const auto staticKey = stableRenderPipelineStateKey(staticDesc);
        const auto dynamicKey = stableRenderPipelineStateKey(dynamicDesc);
        assert(staticKey != dynamicKey);
    }

    // ----------------------------------------------------------------------
    // Facet 3: Runtime command lowering parity.
    // ----------------------------------------------------------------------
    {
        CommandWorkloadDesc workload;
        workload.label = "DynamicViewportScissorWorkload";
        workload.commandCount = 8;
        workload.usesRenderPass = true;
        workload.usesDynamicViewportScissor = true;

        const auto nullDecision =
            lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Null),
                workload,
                CommandRecordingIntent::PrimaryCommandBuffer);
        assert(nullDecision.success);
        assert(hasCommandSemantic(nullDecision, NativeCommandSemantic::NullSetViewportCommand));
        assert(hasCommandSemantic(nullDecision, NativeCommandSemantic::NullSetScissorCommand));

        const auto vulkanDecision =
            lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Vulkan),
                workload,
                CommandRecordingIntent::PrimaryCommandBuffer);
        assert(vulkanDecision.success);
        assert(hasCommandSemantic(
            vulkanDecision, NativeCommandSemantic::VulkanDynamicViewportCommand));
        assert(
            hasCommandSemantic(vulkanDecision, NativeCommandSemantic::VulkanDynamicScissorCommand));

        const auto metalDecision =
            lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Metal),
                workload,
                CommandRecordingIntent::PrimaryCommandBuffer);
        assert(metalDecision.success);
        // Metal records the semantics for parity; its live encoder path is
        // capability-gated and owns the native viewport/scissor calls.
        assert(hasCommandSemantic(metalDecision, NativeCommandSemantic::MetalSetViewportCommand));
        assert(hasCommandSemantic(metalDecision, NativeCommandSemantic::MetalSetScissorCommand));

        const auto webGpuDecision =
            lowerCommandWorkload(nativeBackendCapabilityContract(Backend::WebGPU),
                workload,
                CommandRecordingIntent::PrimaryCommandBuffer);
        assert(webGpuDecision.success);
        assert(hasCommandSemantic(webGpuDecision, NativeCommandSemantic::WebGpuSetViewportCommand));
        assert(hasCommandSemantic(webGpuDecision, NativeCommandSemantic::WebGpuSetScissorCommand));
    }

    // Negative: a workload that does NOT use dynamic viewport/scissor must not
    // record any of the new command semantics (prevents accidental over-recording).
    {
        CommandWorkloadDesc workload;
        workload.label = "StaticViewportScissorWorkload";
        workload.commandCount = 4;
        workload.usesRenderPass = true;

        const auto nullDecision =
            lowerCommandWorkload(nativeBackendCapabilityContract(Backend::Null),
                workload,
                CommandRecordingIntent::PrimaryCommandBuffer);
        assert(nullDecision.success);
        assert(!hasCommandSemantic(nullDecision, NativeCommandSemantic::NullSetViewportCommand));
        assert(!hasCommandSemantic(nullDecision, NativeCommandSemantic::NullSetScissorCommand));
    }

    // ----------------------------------------------------------------------
    // Facet 1: Null L2 command validation (success + negative paths).
    // ----------------------------------------------------------------------
    // Build a real Null pipeline so setPipeline sets pipelineBound_=true.
    auto renderPipelineDesc = makeBasePipelineDesc(*pipelineLayout, shaderPackage);
    auto renderPipeline = device->createRenderPipeline(renderPipelineDesc);
    assert(renderPipeline);

    // Happy path: setPipeline -> setViewport -> setScissor -> drawIndexed.
    {
        auto encoder = device->createCommandEncoder({"DynamicViewportHappy"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        const auto setPipelineResult = pass->setPipeline(*renderPipeline);
        assert(setPipelineResult.success);

        const auto setViewportResult =
            pass->setViewport({0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f});
        assert(setViewportResult.success);

        const auto setScissorResult = pass->setScissor({0, 0, 2048, 2048});
        assert(setScissorResult.success);

        const auto pushResult = pass->pushConstants(ShaderStage::Vertex, 0, 128, kDummyPushData);
        assert(pushResult.success);

        const auto drawResult = pass->drawIndexed(36, 1, 0, 0, 0);
        assert(drawResult.success);

        const auto endResult = pass->end();
        assert(endResult.success);

        auto commandBuffer = encoder->finish();
        // Command count: beginRenderPass(1) + setPipeline(1) + setViewport(1)
        // + setScissor(1) + pushConstants(1) + drawIndexed(1) + end(1) = 7.
        // beginRenderPass and end() each call state_->record() (verified by
        // rhi_command_contract_tests.cpp:140-148, which counts beginRenderPass
        // as one of its 6 commands). finish() calls succeed() and does NOT
        // advance the count.
        assert(commandBuffer->commandCount() == 7u);
    }

    // Negative: setViewport before setPipeline -> [VS-E002].
    {
        auto encoder = device->createCommandEncoder({"ViewportNoPipeline"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        const auto noPipeline = pass->setViewport({0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f});
        assert(!noPipeline.success);
        assert(noPipeline.diagnostics.find("[VS-E002]") != std::pmr::string::npos);
        // recordedCommandCount must NOT advance on rejection. beginRenderPass
        // already recorded 1 command; the rejected setViewport does not advance.
        assert(noPipeline.recordedCommandCount == 1u);
        pass->end();
        encoder->finish();
    }

    // Negative: setScissor before setPipeline -> [VS-E002].
    {
        auto encoder = device->createCommandEncoder({"ScissorNoPipeline"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        const auto noPipeline = pass->setScissor({0, 0, 2048, 2048});
        assert(!noPipeline.success);
        assert(noPipeline.diagnostics.find("[VS-E002]") != std::pmr::string::npos);
        assert(noPipeline.recordedCommandCount == 1u);
        pass->end();
        encoder->finish();
    }

    // Negative: invalid viewport width=0 -> [VS-E003] (pipeline already bound).
    {
        auto encoder = device->createCommandEncoder({"ViewportZeroWidth"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        pass->setPipeline(*renderPipeline);
        const auto zeroWidth = pass->setViewport({0.0f, 0.0f, 0.0f, 2048.0f, 0.0f, 1.0f});
        assert(!zeroWidth.success);
        assert(zeroWidth.diagnostics.find("[VS-E003]") != std::pmr::string::npos);
        // beginRenderPass(1) + setPipeline(1) = 2; rejected setViewport does not advance.
        assert(zeroWidth.recordedCommandCount == 2u);
        pass->end();
        encoder->finish();
    }

    // Negative: invalid viewport minDepth>maxDepth -> [VS-E005].
    {
        auto encoder = device->createCommandEncoder({"ViewportReversedDepth"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        pass->setPipeline(*renderPipeline);
        const auto reversedDepth = pass->setViewport({0.0f, 0.0f, 2048.0f, 2048.0f, 0.8f, 0.2f});
        assert(!reversedDepth.success);
        assert(reversedDepth.diagnostics.find("[VS-E005]") != std::pmr::string::npos);
        assert(reversedDepth.recordedCommandCount == 2u);
        pass->end();
        encoder->finish();
    }

    // Negative: zero-size scissor -> [VS-E006].
    {
        auto encoder = device->createCommandEncoder({"ScissorZero"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        pass->setPipeline(*renderPipeline);
        const auto zeroScissor = pass->setScissor({0, 0, 0, 2048});
        assert(!zeroScissor.success);
        assert(zeroScissor.diagnostics.find("[VS-E006]") != std::pmr::string::npos);
        assert(zeroScissor.recordedCommandCount == 2u);
        pass->end();
        encoder->finish();
    }

    // Negative: setViewport after end() -> [VS-E001] (pass not open).
    {
        auto encoder = device->createCommandEncoder({"ViewportAfterEnd"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        pass->setPipeline(*renderPipeline);
        pass->end();
        const auto afterEnd = pass->setViewport({0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f});
        assert(!afterEnd.success);
        assert(afterEnd.diagnostics.find("[VS-E001]") != std::pmr::string::npos);
        encoder->finish();
    }

    // Override semantics: a second setViewport overrides the first and does
    // not error. The L2 contract explicitly allows this ("a second call
    // overrides the previous one and only affects subsequent draws").
    {
        auto encoder = device->createCommandEncoder({"ViewportOverride"});
        auto pass = encoder->beginRenderPass({"MainColor"});
        pass->setPipeline(*renderPipeline);
        const auto first = pass->setViewport({0.0f, 0.0f, 1024.0f, 1024.0f, 0.0f, 1.0f});
        assert(first.success);
        const auto second = pass->setViewport({1024.0f, 0.0f, 1024.0f, 1024.0f, 0.0f, 1.0f});
        assert(second.success);
        pass->end();
        encoder->finish();
    }

    return 0;
}
