#pragma once

#include "conformance_native_frame_lease.h"
#include "core/diagnostic_error.h"
#include "renderer/render_graph/conformance_sampled_recorder.h"
#include "rhi/frontend/command_encoder_base.h"
#include "rhi/gpu_device.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::tests {
class ConformanceSampledNativeNegatives final {
    using FrameLease = ConformanceNativeFrameLease;

public:
    static void verify(rhi::GpuDevice& device,
        rhi::Swapchain& surface,
        core::StringView yaml,
        const rhi::RenderPipelineDesc& state,
        const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& artifact,
        rhi::ConformanceSampledArtifactBudget artifacts = {},
        void (*advance)() = nullptr,
        void (*acceptedTimeout)() = nullptr,
        bool timeoutOnly = false)
    {
        using namespace rhi;
        core::pmr::String authored(yaml.data(), yaml.size(), &core::defaultCpuMemoryResource());
        // Exercise genuine explicit Uint16 geometry in the first acquired frame.
        const auto indexed = authored.find("indexed: false");
        assert(indexed != core::pmr::String::npos);
        authored.replace(indexed, std::strlen("indexed: false"), "indexed: true");
        auto sharedAcquired = advance ? AcquiredFrame{} : surface.acquireNextFrame();
        assert(advance || sharedAcquired.success);
        auto sharedTarget =
            advance ? AcquiredSwapchainImage{} : surface.acquiredImage(sharedAcquired);
        assert(advance || (sharedTarget.success && sharedTarget.texture));
        // The accepted deadline case presents this one acquisition; run it last.
        for (const int fault : {0,
                 1,
                 2,
                 3,
                 4,
                 5,
                 6,
                 7,
                 8,
                 9,
                 10,
                 11,
                 12,
                 13,
                 14,
                 15,
                 16,
                 17,
                 18,
                 19,
                 21,
                 22,
                 23,
                 20}) {
            if (timeoutOnly && fault != 20)
                continue;
            FrameLease lease;
            lease.advance = advance;
            const auto& acquired = advance ? lease.acquired : sharedAcquired;
            auto& target = advance ? lease.target : sharedTarget;
            constexpr std::uint64_t metadata = 4 * 1024 * 1024;
            const char* id = nullptr;
            auto workload = ConformanceSampledWorkload::parse(authored, {16384, 4096, 3, 4}, id);
            assert(workload && !id);
            const auto& frame = workload->frames()[0];
            const auto& input = frame;
            auto quota = core::makeShared<core::CpuMemoryQuota>(metadata);
            lease.quota = quota;
            ConformanceSampledExecutionRequest request{
                {{1, 3, true, 49152, metadata}, 1048576, 5000000000}, 1048576};
            request.artifacts = artifacts;
            if (fault == 0)
                request.maxNativeInputBufferBytes = 0;
            if (fault == 1)
                request.maxNativeInputBufferBytes = 1;
            if (fault == 20)
                request.execution.completionTimeoutNanoseconds = 1;
            const auto before = device.queue().submitCount();
            auto execution =
                device.createConformanceSampledExecution(std::move(workload), request, quota, id);
            if (fault == 0) {
                assert(!execution && id);
                std::fprintf(stderr, "[sampled-negative-%d] input budget rejected\n", fault);
                continue;
            }
            assert(execution && !id);
            auto definition = RenderPipelineStateCopy::copy(state);
            if (fault == 3)
                definition.cullMode = CullMode::Back;
            auto pipeline =
                execution->preparePipeline(definition, fault == 2 ? nullptr : artifact, id);
            if (fault == 2 || fault == 3) {
                assert(!pipeline && id && device.queue().submitCount() == before);
                std::fprintf(
                    stderr, "[sampled-negative-%d] artifact/pipeline rejected: %s\n", fault, id);
                continue;
            }
            if (!pipeline || id)
                std::printf(
                    "sampled-negative-%d pipeline diagnostic=%s\n", fault, id ? id : "missing");
            assert(pipeline && !id);
            auto vertices = execution->prepareInput(ConformanceSampledInput::Vertices, 0, id);
            if (fault == 1) {
                assert(!vertices && id && device.queue().submitCount() == before);
                std::fprintf(
                    stderr, "[sampled-negative-%d] native allocation budget rejected\n", fault);
                continue;
            }
            auto indices = execution->prepareInput(ConformanceSampledInput::Indices, 0, id);
            auto uniform = execution->prepareInput(ConformanceSampledInput::Uniforms, 0, id);
            auto sampler = execution->prepareSampler(id);
            assert(vertices && indices && uniform && sampler && !id);
            const auto clone = [&](const std::shared_ptr<GpuBuffer>& original,
                                   const void* bytes,
                                   std::size_t size) {
                auto copy = core::shareUnique(device.createBuffer(original->desc()));
                core::ByteBuffer padded(
                    copy->desc().sizeBytes, 0, &core::defaultCpuMemoryResource());
                assert(size <= padded.size());
                std::memcpy(padded.data(), bytes, size);
                assert(copy && device.writeBuffer(*copy, padded.data(), padded.size()).success);
                copy->sealWrites();
                return copy;
            };
            const auto parsed =
                ConformanceSampledWorkload::parse(authored, {16384, 4096, 3, 4}, id);
            if (fault == 4)
                vertices = clone(vertices, parsed->vertices().data(), sizeof(parsed->vertices()));
            if (fault == 5)
                uniform = clone(uniform, input.uniforms.data(), sizeof(input.uniforms));
            if (fault == 6)
                indices = clone(indices, parsed->indices().data(), sizeof(parsed->indices()));
            if (fault == 17) {
                auto incompatible = RenderPipelineStateCopy::copy(state);
                incompatible.colorFormat = state.colorFormat == TextureFormat::Rgba8Unorm
                    ? TextureFormat::Bgra8Unorm
                    : TextureFormat::Rgba8Unorm;
                incompatible.colorTargets[0].format = incompatible.colorFormat;
                pipeline = execution->preparePipeline(incompatible, artifact, id);
                assert(pipeline && !id);
            }
            core::UniquePtr<OriginalConformanceSampledExecution> foreign;
            if (fault == 7) {
                if (advance) {
                    auto foreignQuota = core::makeShared<core::CpuMemoryQuota>(metadata);
                    foreign = device.createConformanceSampledExecution(
                        ConformanceSampledWorkload::parse(authored, {16384, 4096, 3, 4}, id),
                        request,
                        foreignQuota,
                        id);
                    assert(foreign);
                    pipeline = foreign->preparePipeline(state, artifact, id);
                } else
                    pipeline = core::shareUnique(device.createRenderPipeline(state));
                assert(pipeline);
            }
            if (fault == 8)
                sampler = core::shareUnique(device.createSampler(sampler->desc()));
            std::array<core::UniquePtr<GpuTexture>, 2> sources;
            std::array<core::UniquePtr<GpuTextureView>, 2> views;
            for (std::size_t t = 0; t < 2; ++t) {
                sources[t] = device.createTexture({"Conformance.Negative.Source",
                    {input.extent.width, input.extent.height, 1},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::RenderAttachment | TextureUsage::Sampled,
                    MemoryUsage::GpuOnly,
                    ResourceState::Undefined});
                TextureViewDesc view;
                view.format = TextureFormat::Rgba8Unorm;
                view.usage = TextureUsage::Sampled;
                views[t] = device.createTextureView(*sources[t], view);
                assert(sources[t] && views[t]);
            }
            auto arena = device.createDescriptorArena({"Conformance.Negative", 1, 5});
            DescriptorSetDesc set;
            set.layout = pipeline->layout()->desc().descriptorLayouts[0];
            auto descriptors = arena->allocateDescriptorSet(set);
            assert(descriptors
                && descriptors
                    ->write({0,
                        DescriptorType::UniformBuffer,
                        uniform->id(),
                        DescriptorResourceKind::Buffer})
                    .success);
            for (std::uint32_t t = 0; t < 2; ++t) {
                assert(descriptors
                        ->write({1 + t * 2,
                            DescriptorType::SampledTexture,
                            views[fault == 12 ? 1 - t : t]->id(),
                            DescriptorResourceKind::TextureView})
                        .success);
                assert(descriptors
                        ->write({2 + t * 2,
                            DescriptorType::Sampler,
                            sampler->id(),
                            DescriptorResourceKind::Sampler})
                        .success);
            }
            // Canvas textures expire at the next browser rendering update.
            // Finish every cold asynchronous creation before acquiring it.
            if (advance)
                lease.acquire(surface);
            auto encoder = execution->prepare({}, *target.texture, id);
            assert(encoder && !id);
            if (fault == 19) {
                execution.reset();
                const auto stopped = encoder->transition(
                    *sources[0], ResourceState::Undefined, ResourceState::ColorAttachment);
                assert(!stopped.success && device.queue().submitCount() == before);
                std::fprintf(stderr,
                    "[sampled-negative-19] issuer retirement rejected under retained encoder\n");
                continue;
            }
            bool rejected = false;
            try {
                if (fault == 14) {
                    assert(!encoder->beginComputePass({}));
                    rejected = true;
                } else {
                    for (std::size_t t = 0; t < 2; ++t) {
                        core::requireSuccessfulResult(encoder->transition(*sources[t],
                                                          ResourceState::Undefined,
                                                          ResourceState::ColorAttachment),
                            "NEGATIVE-SOURCE-BARRIER");
                        RenderPassDesc pass;
                        RenderPassColorAttachmentDesc color;
                        color.target = sources[t]->id();
                        color.format = TextureFormat::Rgba8Unorm;
                        color.loadOp = LoadOp::Clear;
                        color.storeOp = StoreOp::Store;
                        color.clearColor =
                            ConformanceClearFrame{input.extent, t ? input.sourceB : input.sourceA}
                                .clearColor();
                        pass.colorAttachments.push_back(color);
                        auto render = encoder->beginRenderPass(pass);
                        if (!render)
                            throw core::DiagnosticError("NEGATIVE-SOURCE-PASS");
                        core::requireSuccessfulResult(render->end(), "NEGATIVE-SOURCE-END");
                    }
                    for (const auto& texture : sources) {
                        auto barrier = makeInferredResourceBarrier(texture->id(),
                            defaultTextureSubresourceRange(texture->desc()),
                            ResourceState::ColorAttachment,
                            ResourceState::ShaderRead);
                        if (fault == 21)
                            barrier.destinationStages = PipelineStage::ComputeShader;
                        core::requireSuccessfulResult(
                            encoder->transition(*texture, barrier), "NEGATIVE-SAMPLE-BARRIER");
                    }
                    core::requireSuccessfulResult(encoder->transition(*target.texture,
                                                      target.texture->state(),
                                                      ResourceState::ColorAttachment),
                        "NEGATIVE-TARGET-BARRIER");
                    RenderPassDesc pass;
                    RenderPassColorAttachmentDesc color;
                    color.target = target.texture->id();
                    color.format = target.texture->desc().format;
                    color.loadOp = LoadOp::Clear;
                    color.storeOp = StoreOp::Store;
                    color.clearColor = {0, 0, 0, 1};
                    pass.colorAttachments.push_back(color);
                    auto render = encoder->beginRenderPass(pass);
                    if (!render)
                        throw core::DiagnosticError("NEGATIVE-OUTPUT-PASS");
                    core::requireSuccessfulResult(
                        render->setPipeline(*pipeline), "NEGATIVE-PIPELINE");
                    if (fault == 22)
                        core::requireSuccessfulResult(
                            render->setPipeline(*pipeline), "NEGATIVE-DUPLICATE-PIPELINE");
                    core::requireSuccessfulResult(
                        render->setViewport({0,
                            0,
                            float(input.extent.width - (fault == 10 ? 1 : 0)),
                            float(input.extent.height),
                            0,
                            1}),
                        "NEGATIVE-VIEWPORT");
                    core::requireSuccessfulResult(render->setScissor({0,
                                                      0,
                                                      input.extent.width - (fault == 18 ? 1 : 0),
                                                      input.extent.height}),
                        "NEGATIVE-SCISSOR");
                    core::requireSuccessfulResult(
                        render->bindVertexBuffer(3, *vertices), "NEGATIVE-VERTEX");
                    core::requireSuccessfulResult(
                        render->bindIndexBuffer(
                            *indices, fault == 16 ? IndexFormat::Uint32 : IndexFormat::Uint16),
                        "NEGATIVE-INDEX");
                    core::requireSuccessfulResult(
                        render->bindDescriptorSet(0, *descriptors), "NEGATIVE-DESCRIPTOR");
                    auto push = input.draws[0].push;
                    if (fault == 9)
                        push[0] += 1;
                    core::requireSuccessfulResult(
                        render->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment,
                            0,
                            sizeof(push),
                            push.data()),
                        "NEGATIVE-PUSH");
                    core::requireSuccessfulResult(
                        render->drawIndexed(fault == 11 ? 2 : 3, 1, 0, 0, 0), "NEGATIVE-DRAW");
                    if (fault == 23)
                        core::requireSuccessfulResult(
                            render->drawIndexed(3, 1, 0, 0, 0), "NEGATIVE-EXTRA-DRAW");
                    core::requireSuccessfulResult(render->end(), "NEGATIVE-END");
                    if (fault != 15)
                        core::requireSuccessfulResult(encoder->transition(*target.texture,
                                                          ResourceState::ColorAttachment,
                                                          ResourceState::Present),
                            "NEGATIVE-PRESENT-BARRIER");
                }
            } catch (const core::DiagnosticError& error) {
                rejected = true;
                std::fprintf(stderr, "[sampled-negative-%d] %s\n", fault, error.what());
            }
            auto command = encoder->finish();
            if (command) {
                const auto result = fault == 13 ? device.queue().submit(*command)
                                                : execution->submit(*command, nullptr);
                if (fault == 20) {
                    std::printf("sampledAcceptedTimeout success=%d accepted=%d diagnostic=%s\n",
                        result.success,
                        result.hasAcceptedWork(),
                        result.diagnostics.c_str());
                    assert(!result.success && result.hasAcceptedWork()
                        && (advance || device.queue().submitCount() == before + 1));
                    if (advance) {
                        assert(result.diagnostics == "RHI-CONFORMANCE-CLEAR-NATIVE-DEADLINE");
                        assert(!surface.discardFrame(acquired).success);
                        command.reset();
                        encoder.reset();
                        execution.reset();
                        assert(quota->liveBytes());
                        if (acceptedTimeout)
                            acceptedTimeout();
                        std::fprintf(stderr,
                            "[sampled-negative-%d] accepted GPU work retained, real callback drain required\n",
                            fault);
                        continue;
                    }
                    device.waitIdle();
                    assert(surface.present(acquired).success);
                    assert(execution->finalizePresented());
                    assert(!OriginalConformanceAudit::inspect(execution->run())
                            .originalOutputValidated);
                    std::fprintf(stderr,
                        "[sampled-negative-%d] accepted GPU work preserved after deadline\n",
                        fault);
                    continue;
                }
                assert(!result.success && !result.hasAcceptedWork());
                rejected = true;
            }
            assert(rejected || fault == 15 || fault == 16);
            assert(device.queue().submitCount() == before);
            assert(!OriginalConformanceAudit::inspect(execution->run()).originalOutputValidated);
            std::fprintf(stderr, "[sampled-negative-%d] rejected before queue acceptance\n", fault);
        }
    }
};
} // namespace arp::tests
