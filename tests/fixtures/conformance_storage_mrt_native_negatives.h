#pragma once

#include "conformance_native_frame_lease.h"
#include "core/diagnostic_error.h"
#include "renderer/render_graph/conformance_storage_mrt_recorder.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::tests {
class ConformanceStorageMrtNativeNegatives final {
public:
    static void verify(rhi::GpuDevice& device,
        rhi::Swapchain& surface,
        core::StringView yaml,
        const rhi::RenderPipelineDesc& graphicsState,
        const rhi::ComputePipelineDesc& computeState,
        const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& graphics,
        const std::shared_ptr<const shader::SingleEntryShaderArtifact>& computeArtifact,
        rhi::ConformanceSampledArtifactBudget artifacts = {},
        void (*advance)() = nullptr,
        void (*beforeDeadline)() = nullptr,
        void (*acceptedTimeout)() = nullptr,
        bool timeoutOnly = false)
    {
        using namespace rhi;
        const auto parsed =
            ConformanceStorageMrtWorkload::parse(yaml, {16384, 4096, 3}, diagnostic_);
        assert(parsed && !diagnostic_);
        const auto frame = parsed->frames()[0];
        auto sharedAcquired = advance ? AcquiredFrame{} : surface.acquireNextFrame();
        assert(advance || sharedAcquired.success);
        auto sharedTarget =
            advance ? AcquiredSwapchainImage{} : surface.acquiredImage(sharedAcquired);
        assert(advance || (sharedTarget.success && sharedTarget.texture));
        for (int fault = 0; fault < 30; ++fault) {
            if (timeoutOnly && fault != 29)
                continue;
            ConformanceNativeFrameLease lease;
            lease.advance = advance;
            if (!advance && fault == 29) {
                sharedTarget.texture.reset();
                sharedAcquired = surface.acquireNextFrame();
                assert(sharedAcquired.success);
                sharedTarget = surface.acquiredImage(sharedAcquired);
                assert(sharedTarget.success && sharedTarget.texture);
            }
            const auto& acquired = advance ? lease.acquired : sharedAcquired;
            auto& target = advance ? lease.target : sharedTarget;
            const char* id = nullptr;
            core::pmr::String authored(yaml.data(), yaml.size(), &core::defaultCpuMemoryResource());
            if (fault == 28) {
                const auto at = authored.find("expectedColor1: [143");
                assert(at != core::pmr::String::npos);
                authored.replace(at, std::strlen("expectedColor1: [143"), "expectedColor1: [142");
            }
            auto workload = ConformanceStorageMrtWorkload::parse(authored, {16384, 4096, 3}, id);
            constexpr std::uint64_t metadata = 4 * 1024 * 1024;
            auto quota = core::makeShared<core::CpuMemoryQuota>(fault == 3 ? 1 : metadata);
            ConformanceStorageMrtExecutionRequest request{
                {{1, 3, true, 196608, quota->limitBytes()}, 1048576, 5000000000}, 1048576, 1048576};
            request.artifacts = artifacts;
            lease.quota = quota;
            if (fault == 0)
                request.maxNativeInputBufferBytes = 0;
            if (fault == 1)
                request.maxNativeTextureBytes = 0;
            if (fault == 2)
                request.execution.maxNativeReadbackBytes = 1;
            if (fault == 8)
                request.maxNativeTextureBytes = 1;
            if (fault == 9)
                request.maxNativeInputBufferBytes = 1;
            if (fault == 29)
                request.execution.completionTimeoutNanoseconds = 1;
            const auto before = device.queue().submitCount();
            auto execution = device.createConformanceStorageMrtExecution(
                std::move(workload), request, quota, id);
            if (fault < 4) {
                assert(!execution && id && device.queue().submitCount() == before);
                std::fprintf(
                    stderr, "[storage-negative-%d] request/allocation rejected: %s\n", fault, id);
                continue;
            }
            assert(execution && !id);
            auto definition = RenderPipelineStateCopy::copy(graphicsState);
            if (fault == 6)
                definition.colorTargets[1].blendEnabled = true;
            auto pipeline =
                execution->preparePipeline(definition, fault == 4 ? nullptr : graphics, id);
            if (fault == 4 || fault == 6) {
                assert(!pipeline && id && device.queue().submitCount() == before);
                std::fprintf(
                    stderr, "[storage-negative-%d] original graphics rejected: %s\n", fault, id);
                continue;
            }
            assert(pipeline && !id);
            auto computeDefinition = ComputePipelineStateCopy::copy(computeState);
            if (fault == 7)
                computeDefinition.label = "foreign definition";
            auto compute = execution->prepareComputePipeline(
                computeDefinition, fault == 5 ? nullptr : computeArtifact, id);
            if (fault == 5 || fault == 7) {
                assert(!compute && id && device.queue().submitCount() == before);
                std::fprintf(
                    stderr, "[storage-negative-%d] original compute rejected: %s\n", fault, id);
                continue;
            }
            assert(compute && !id);
            auto input = execution->prepareStorageInput(0, id);
            if (fault == 9) {
                assert(!input && id && device.queue().submitCount() == before);
                continue;
            }
            assert(input && !id);
            auto color1 = execution->prepareStorageTexture(ConformanceOutputRole::Color1, 0, id);
            if (fault == 8) {
                assert(!color1 && id && device.queue().submitCount() == before);
                continue;
            }
            auto storage = execution->prepareStorageTexture(ConformanceOutputRole::Storage, 0, id);
            assert(color1 && storage && !id);
            if (fault == 10) {
                input = core::shareUnique(device.createBuffer(input->desc()));
                assert(input
                    && device.writeBuffer(*input, frame.values.data(), sizeof(frame.values))
                        .success);
                input->sealWrites();
            }
            core::UniquePtr<OriginalConformanceStorageMrtExecution> foreign;
            if (advance && (fault == 11 || fault == 12)) {
                auto foreignSource =
                    ConformanceStorageMrtWorkload::parse(yaml, {16384, 4096, 3}, id);
                foreign = device.createConformanceStorageMrtExecution(
                    std::move(foreignSource), request, quota, id);
                assert(foreign && !id);
                if (fault == 11)
                    compute = foreign->prepareComputePipeline(computeState, computeArtifact, id);
                else
                    pipeline = foreign->preparePipeline(graphicsState, graphics, id);
            } else {
                if (fault == 11)
                    compute = core::shareUnique(device.createComputePipeline(computeState));
                if (fault == 12)
                    pipeline = core::shareUnique(device.createRenderPipeline(graphicsState));
            }
            if (fault == 13)
                storage = core::shareUnique(device.createTexture(storage->desc()));
            if (fault == 19)
                color1 = core::shareUnique(device.createTexture(color1->desc()));
            assert(input && compute && pipeline && color1 && storage);
            TextureViewDesc view;
            view.format = TextureFormat::Rgba16Float;
            view.aspect = TextureAspect::Color;
            view.usage = TextureUsage::Storage;
            auto writeView = device.createTextureView(*storage, view);
            view.usage = TextureUsage::Sampled;
            auto readView = device.createTextureView(*storage, view);
            auto arena = device.createDescriptorArena({"Storage negative", 2, 4});
            assert(writeView && readView && arena);
            DescriptorSetDesc set;
            set.layout = compute->layout()->desc().descriptorLayouts[0];
            auto fillSet = arena->allocateDescriptorSet(set);
            set.layout = pipeline->layout()->desc().descriptorLayouts[0];
            auto drawSet = arena->allocateDescriptorSet(set);
            assert(fillSet && drawSet);
            for (auto* descriptor : {fillSet.get(), drawSet.get()})
                assert(descriptor
                        ->write({0,
                            DescriptorType::ReadOnlyStorageBuffer,
                            input->id(),
                            DescriptorResourceKind::Buffer})
                        .success);
            assert(fillSet
                       ->write({1,
                           DescriptorType::StorageTexture,
                           (fault == 17 ? readView : writeView)->id(),
                           DescriptorResourceKind::TextureView})
                       .success
                == (fault != 17));
            if (fault == 17) {
                assert(device.queue().submitCount() == before);
                std::fprintf(stderr,
                    "[storage-negative-17] wrong storage view usage rejected at descriptor write\n");
                continue;
            }
            const auto sampledWrite = drawSet->write({1,
                DescriptorType::SampledTexture,
                (fault == 18 ? writeView : readView)->id(),
                DescriptorResourceKind::TextureView});
            if (fault == 18) {
                assert(!sampledWrite.success && device.queue().submitCount() == before);
                std::fprintf(stderr,
                    "[storage-negative-18] wrong sampled view usage rejected at descriptor write\n");
                continue;
            }
            assert(sampledWrite.success);
            // Canvas textures expire at the next browser rendering update.
            // Finish every cold asynchronous creation before acquiring it.
            if (advance)
                lease.acquire(surface);
            auto encoder = execution->prepare({}, *target.texture, id);
            assert(encoder && !id);
            if (fault == 24)
                execution.reset();
            bool rejected = false;
            try {
                auto fillBarrier = makeInferredResourceBarrier(storage->id(),
                    defaultTextureSubresourceRange(storage->desc()),
                    fault == 26 ? ResourceState::ShaderRead : ResourceState::Undefined,
                    ResourceState::ShaderWrite);
                core::requireSuccessfulResult(
                    encoder->transition(*storage, fillBarrier), "STORAGE-NEGATIVE-FILL-BARRIER");
                auto fill = encoder->beginComputePass({"Storage negative fill"});
                if (!fill)
                    throw core::DiagnosticError("STORAGE-NEGATIVE-COMPUTE-BEGIN");
                core::requireSuccessfulResult(
                    fill->setPipeline(*compute), "STORAGE-NEGATIVE-COMPUTE-PIPELINE");
                core::requireSuccessfulResult(
                    fill->bindDescriptorSet(0, *fillSet), "STORAGE-NEGATIVE-COMPUTE-DESCRIPTOR");
                if (fault == 25) {
                    const std::uint32_t push = 1;
                    core::requireSuccessfulResult(
                        fill->pushConstants(ShaderStage::Compute, 0, sizeof(push), &push),
                        "STORAGE-NEGATIVE-PUSH");
                }
                core::requireSuccessfulResult(
                    fill->dispatch(
                        frame.extent.width - (fault == 14 ? 1 : 0), frame.extent.height, 1),
                    "STORAGE-NEGATIVE-DISPATCH");
                if (fault == 20)
                    core::requireSuccessfulResult(
                        fill->dispatch(frame.extent.width, frame.extent.height, 1),
                        "STORAGE-NEGATIVE-DUPLICATE-DISPATCH");
                if (fault != 15)
                    core::requireSuccessfulResult(fill->end(), "STORAGE-NEGATIVE-COMPUTE-END");
                if (fault == 22) {
                    auto extra = encoder->beginComputePass({});
                    assert(!extra);
                    rejected = true;
                }
                auto publish = makeInferredResourceBarrier(storage->id(),
                    defaultTextureSubresourceRange(storage->desc()),
                    ResourceState::ShaderWrite,
                    ResourceState::ShaderRead);
                publish.destinationStages =
                    fault == 16 ? PipelineStage::VertexShader : PipelineStage::FragmentShader;
                core::requireSuccessfulResult(
                    encoder->transition(*storage, publish), "STORAGE-NEGATIVE-PUBLISH");
                core::requireSuccessfulResult(
                    encoder->transition(
                        *color1, ResourceState::Undefined, ResourceState::ColorAttachment),
                    "STORAGE-NEGATIVE-COLOR1");
                core::requireSuccessfulResult(
                    encoder->transition(
                        *target.texture, target.texture->state(), ResourceState::ColorAttachment),
                    "STORAGE-NEGATIVE-CANVAS");
                RenderPassDesc pass;
                for (const auto* texture : {target.texture.get(), color1.get()}) {
                    RenderPassColorAttachmentDesc color;
                    color.target = texture->id();
                    color.format = texture->desc().format;
                    color.loadOp = LoadOp::Clear;
                    color.storeOp = StoreOp::Store;
                    color.clearColor = {0, 0, 0, 1};
                    pass.colorAttachments.push_back(color);
                }
                auto draw = encoder->beginRenderPass(pass);
                if (!draw)
                    throw core::DiagnosticError("STORAGE-NEGATIVE-DRAW-BEGIN");
                core::requireSuccessfulResult(
                    draw->setPipeline(*pipeline), "STORAGE-NEGATIVE-GRAPHICS-PIPELINE");
                core::requireSuccessfulResult(
                    draw->setViewport(
                        {0, 0, float(frame.extent.width), float(frame.extent.height), 0, 1}),
                    "STORAGE-NEGATIVE-VIEWPORT");
                core::requireSuccessfulResult(
                    draw->setScissor({0, 0, frame.extent.width, frame.extent.height}),
                    "STORAGE-NEGATIVE-SCISSOR");
                core::requireSuccessfulResult(
                    draw->bindDescriptorSet(0, *drawSet), "STORAGE-NEGATIVE-GRAPHICS-DESCRIPTORS");
                core::requireSuccessfulResult(
                    draw->draw(fault == 21 ? 2 : 3, 1, 0, 0), "STORAGE-NEGATIVE-DRAW");
                core::requireSuccessfulResult(draw->end(), "STORAGE-NEGATIVE-DRAW-END");
                if (fault != 27)
                    core::requireSuccessfulResult(encoder->transition(*target.texture,
                                                      ResourceState::ColorAttachment,
                                                      ResourceState::Present),
                        "STORAGE-NEGATIVE-PRESENT");
            } catch (const core::DiagnosticError& error) {
                rejected = true;
                std::fprintf(stderr, "[storage-negative-%d] %s\n", fault, error.what());
            }
            auto command = encoder->finish();
            if (command && execution) {
                if (fault == 29 && beforeDeadline)
                    beforeDeadline();
                const auto result = fault == 23 ? device.queue().submit(*command)
                                                : execution->submit(*command, nullptr);
                if (fault >= 28) {
                    assert(!result.success && result.hasAcceptedWork()
                        && (advance || device.queue().submitCount() == before + 1));
                    if (advance) {
                        assert(result.diagnostics
                            == (fault == 28 ? "RHI-CONFORMANCE-STORAGE-MRT-OUTPUT-MISMATCH"
                                            : "RHI-CONFORMANCE-CLEAR-NATIVE-DEADLINE"));
                        std::printf("storageAcceptedFault%d success=%d accepted=%d diagnostic=%s\n",
                            fault,
                            result.success,
                            result.hasAcceptedWork(),
                            result.diagnostics.c_str());
                        if (fault == 29)
                            assert(!surface.discardFrame(acquired).success);
                        command.reset();
                        encoder.reset();
                        execution.reset();
                        assert(quota->liveBytes());
                        if (fault == 29 && acceptedTimeout)
                            acceptedTimeout();
                        continue;
                    }
                    device.waitIdle();
                    assert(surface.present(acquired).success);
                    assert(execution->finalizePresented());
                    assert(!OriginalConformanceAudit::inspect(execution->run())
                            .originalOutputValidated);
                    std::fprintf(stderr,
                        "[storage-negative-%d] actual accepted GPU work retained; audit rejected: %s\n",
                        fault,
                        result.diagnostics.c_str());
                    continue;
                }
                assert(!result.success && !result.hasAcceptedWork());
                rejected = true;
            }
            assert(rejected || fault == 27);
            assert(device.queue().submitCount() == before);
            if (execution)
                assert(
                    !OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
            std::fprintf(stderr, "[storage-negative-%d] rejected before queue acceptance\n", fault);
        }
    }

private:
    static inline const char* diagnostic_ = nullptr;
};
} // namespace arp::tests
