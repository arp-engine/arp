#pragma once

#include "rhi/backends/webgl/webgl_device.h"
#include "rhi/backends/webgl/webgl_pipeline_internal.h"

namespace arp::rhi::tests {
class WebGlCpuRetirementSmoke final {
public:
    const char* diagnostic = "";

    static bool rejectedDrain(webgl::WebGlDevice& device)
    {
        using namespace semantics;
        const auto invalid = device.drainForShutdown({});
        const auto expired = device.drainForShutdown(
            {1, std::chrono::steady_clock::now() - std::chrono::seconds(1)});
        const auto unavailable = device.drainForShutdown(
            {1, std::chrono::steady_clock::now() + std::chrono::seconds(1)});
        return !invalid.completed() && !expired.completed() && !unavailable.completed()
            && invalid.diagnosticId == kShutdownDrainInvalidDeadlineDiagnostic
            && expired.state == ShutdownDrainState::DeadlineExpired
            && expired.diagnosticId == kShutdownDrainDeadlineExpiredDiagnostic
            && unavailable.state == ShutdownDrainState::DiagnosticFailure
            && unavailable.diagnosticId == kShutdownDrainUnavailableDiagnostic;
    }

    bool run(webgl::WebGlDevice& device,
        const std::shared_ptr<RenderPipeline>& pipeline,
        bool pause,
        int (*loseContext)() = nullptr)
    {
        const auto require = [&](bool result, const char* id) {
            if (!result)
                diagnostic = id;
            return result;
        };
#define RETIRE_REQUIRE(value, id)                                                                  \
    if (!require(bool(value), id))                                                                 \
    return false
        RETIRE_REQUIRE(!device.retireCpu({}).success && !device.retireCpu({1, false}).success,
            "CPU-RETIREMENT-REQUEST-REJECTS-WITHOUT-CLOSING");
        RETIRE_REQUIRE(!device.enableSurfaceLifecycle({1, true, 2}), device.diagnostic());
        auto definition = RenderPipelineStateCopy::copy(
            dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get())->impl->definition);
        definition.shaderPackage = nullptr;
        definition.depthFormat = definition.depthStencil.format = TextureFormat::Unknown;
        auto clearPipeline = core::shareUnique(device.createRenderPipeline(definition));
        auto& pipelines = device.pipelineCache();
        auto& descriptors = device.descriptorCache();
        RETIRE_REQUIRE(clearPipeline && pipelines.acquireRenderPipeline(definition)
                && pipelines.acquirePipelineLayout(pipeline->layout()->desc())
                && pipelines.entryCount() == 2,
            "CPU-RETIREMENT-ORIGINAL-PIPELINE-CACHE");
        DescriptorArenaDesc arenaDesc{"retirement", 1, 1};
        auto* arena = descriptors.acquireDescriptorArena(arenaDesc);
        DescriptorSetDesc setDesc;
        setDesc.layout.name = "retirement";
        setDesc.layout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
        auto* set = descriptors.acquireDescriptorSet(setDesc, arena);
        BufferDesc bufferDesc;
        bufferDesc.sizeBytes = 64;
        bufferDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
        auto buffer = device.createBuffer(bufferDesc);
        DescriptorBindingWrite write;
        write.type = DescriptorType::UniformBuffer;
        write.resourceKind = DescriptorResourceKind::Buffer;
        if (buffer)
            write.resource = buffer->id();
        RETIRE_REQUIRE(set && buffer && set->write(write).success && descriptors.entryCount() == 2,
            "CPU-RETIREMENT-ORIGINAL-DESCRIPTOR-CACHE");
        SwapchainDesc desc;
        desc.surfaceName = "#canvas";
        desc.extent = {64, 64};
        desc.format = TextureFormat::Rgba8Unorm;
        desc.imageCount = 0;
        auto canvas = device.createSwapchain(desc);
        RETIRE_REQUIRE(canvas, device.diagnostic());
        const auto frame = canvas->acquireNextFrame();
        auto image = canvas->acquiredImage(frame);
        auto target = device.canvasFramebuffer(*canvas, frame);
        semantics::WebGlPreparedCommandsRequest request;
        request.contractVersion = 1;
        request.prototypeOptIn = true;
        request.maxDraws = 1;
        request.maxUniformBytes = 160;
        request.maxAnalysisIndices = 3;
        request.issue = {1, true, 3, 8, 12, 256 * 1024, 16};
        auto quota = core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
        RETIRE_REQUIRE(target && !device.prepareCommand(clearPipeline, target, request, quota),
            "CPU-RETIREMENT-PREPARE-DISCARD");
        auto encoder = device.createCommandEncoder({});
        RETIRE_REQUIRE(encoder && quota->liveBytes(), "CPU-RETIREMENT-ENCODER-OWNS-QUOTA");
        encoder.reset();
        RETIRE_REQUIRE(!quota->liveBytes(), "CPU-RETIREMENT-DISCARDED-ENCODER-REFUNDS");
        auto retainedQuota =
            core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
        RETIRE_REQUIRE(!device.prepareCommand(clearPipeline, target, request, retainedQuota),
            "CPU-RETIREMENT-PREPARE-EXTERNAL-COMMAND");
        encoder = device.createCommandEncoder({});
        RenderPassDesc passDesc;
        passDesc.colorAttachments.push_back({{},
            image.ref.image,
            TextureFormat::Rgba8Unorm,
            LoadOp::Clear,
            StoreOp::Store,
            false,
            {},
            {0, 0, 1, 1}});
        auto pass =
            encoder ? encoder->beginRenderPass(passDesc) : core::UniquePtr<RenderPassEncoder>{};
        RETIRE_REQUIRE(pass && pass->end().success, "CPU-RETIREMENT-RETAINED-RECORD");
        auto commands = encoder->finish();
        RETIRE_REQUIRE(commands && retainedQuota->liveBytes(),
            "CPU-RETIREMENT-EXTERNAL-COMMAND-RETAINS-EXACT-QUOTA");
        RETIRE_REQUIRE(!device.prepareCommand(clearPipeline, target, request, quota),
            "CPU-RETIREMENT-PREPARE-CANCEL");
        auto* original = dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get());
        auto domain = original->impl->object->domain;
        const auto liveBefore = domain->live.objectCount;
        const auto submissions = device.queue().submitCount();
        RETIRE_REQUIRE(quota->liveBytes() && rejectedDrain(device),
            "CPU-RETIREMENT-PREPARED-AND-NO-FAKE-DRAIN");
        if (pause)
            RETIRE_REQUIRE(
                canvas->setSurfaceAvailable(false).success, "CPU-RETIREMENT-PAUSED-OWNER");
        if (loseContext)
            RETIRE_REQUIRE(loseContext(), "CPU-RETIREMENT-NATIVE-LOSS");
        const auto result = device.retireCpu({1, true});
        RETIRE_REQUIRE(result.success && result.admissionClosed && !result.gpuCompletionKnown
                && result.retainedResourceCount == domain->live.objectCount
                && result.retainedResourceCount > 0 && result.retainedResourceCount < liveBefore
                && !quota->liveBytes() && !pipelines.entryCount() && !descriptors.entryCount()
                && !descriptors.budgetStats().current && !device.createCommandEncoder({})
                && !device.createBuffer(bufferDesc) && !canvas->acquireNextFrame().success
                && retainedQuota->liveBytes() && !device.queue().submit(*commands).hasAcceptedWork()
                && webgl::WebGlRenderPipeline::current(*original->impl)
                && device.queue().submitCount() == submissions && rejectedDrain(device),
            "CPU-RETIREMENT-CLOSES-CANCELS-CLEARS-WITHOUT-GPU-COMPLETION");
        const auto retained = domain->live.objectCount;
        commands.reset();
        pass.reset();
        encoder.reset();
        RETIRE_REQUIRE(
            !retainedQuota->liveBytes(), "CPU-RETIREMENT-EXTERNAL-COMMAND-LAST-OWNER-REFUNDS");
        target.reset();
        image.texture.reset();
        canvas.reset();
        clearPipeline.reset();
        buffer.reset();
        const auto repeated = device.retireCpu({1, true});
        RETIRE_REQUIRE(repeated.success && repeated.admissionClosed && !repeated.gpuCompletionKnown
                && repeated.retainedResourceCount == domain->live.objectCount
                && repeated.retainedResourceCount < retained && !quota->liveBytes(),
            "CPU-RETIREMENT-EXACT-EXTERNAL-OWNERS-AND-IDEMPOTENCE");
#undef RETIRE_REQUIRE
        return true;
    }
};
} // namespace arp::rhi::tests
