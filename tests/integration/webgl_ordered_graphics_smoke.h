#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "platform/render_system.h"
#include "platform/render_system_ordered_graphics.h"
#include "renderer/render_graph/frame_graph_uniform_ring_buffer.h"

namespace arp::rhi::webgl::test {
EM_JS(int, orderedGraphicsMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('orderedOnly') === '1';
    // clang-format on
});
EM_JS(int, orderedLossMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('orderedLoss') === '1';
    // clang-format on
});
EM_JS(int, orderedFaultMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('orderedFault') === '1';
    // clang-format on
});

EM_JS(void, reportOrderedGraphics, (int success, const char* diagnostic, int frames), {
    // clang-format off
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'RenderSystemOrderedGraphicsSmoke',contractVersion:1,success:!!success,
        diagnostic:UTF8ToString(diagnostic),frames,webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        gpuCompletionClaimed:false,productionReady:false})});
    Module.arpWorkloadFinished?.();
    // clang-format on
});

class OrderedGraphicsWorkload final : public platform::OrderedGraphicsFrameWorkload {
public:
    OrderedGraphicsWorkload(GpuDevice& device,
        std::shared_ptr<RenderPipeline> pipeline,
        std::shared_ptr<OriginalOrderedGraphicsExecution> execution)
        : device_(device), pipelines_{std::move(pipeline)}, execution_(std::move(execution))
    {
        ring_ = core::makeShared<render_graph::FrameGraphUniformRingBuffer>(
            device, 1, 256, ResourceState::ShaderRead);
        BufferDesc vertex;
        vertex.sizeBytes = 27 * sizeof(float);
        vertex.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
        vertex.memory = MemoryUsage::CpuToGpu;
        vertex.initialState = ResourceState::ShaderRead;
        vertex_ = device.createBuffer(vertex);
        const std::array<float, 27> vertices{-1,
            -1,
            .25f,
            .5f,
            .5f,
            1,
            1,
            1,
            1,
            -1,
            3,
            .25f,
            .5f,
            .5f,
            1,
            1,
            1,
            1,
            3,
            -1,
            .25f,
            .5f,
            .5f,
            1,
            1,
            1,
            1};
        if (!vertex_)
            throw core::DiagnosticError("ORDERED-SMOKE-VERTEX-CREATION");
        core::requireSuccessfulResult(
            device.writeBuffer(*vertex_, vertices.data(), sizeof(vertices), 0),
            "ORDERED-SMOKE-VERTEX-UPLOAD");
        SamplerDesc sampler;
        sampler.mipFilter = SamplerMipFilter::None;
        sampler_ = device.createSampler(sampler);
        arena_ = device.createDescriptorArena({{}, 1, 5});
        DescriptorSetDesc set;
        set.layout = pipelines_[0]->layout()->desc().descriptorLayouts[0];
        descriptors_ = arena_->allocateDescriptorSet(set);
        if (!sampler_ || !descriptors_)
            throw core::DiagnosticError("ORDERED-SMOKE-DESCRIPTORS");
    }

    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines() const noexcept override
    {
        return pipelines_;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle canvas) override
    {
        ring_->resetOrdered(0, *execution_);
        const auto uniform = ring_->allocate(0, 128);
        std::array<float, 32> constants{};
        for (auto i : {0u, 5u, 10u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u})
            constants[i] = 1;
        core::requireSuccessfulResult(
            device_.writeBuffer(
                *uniform.buffer, constants.data(), sizeof(constants), uniform.offsetBytes),
            "ORDERED-SMOKE-UNIFORM-UPLOAD");
        auto uniformOwner = std::shared_ptr<GpuBuffer>(ring_, uniform.buffer);
        const auto uniformHandle = graph.importBufferRef(uniformOwner);
        TextureDesc desc;
        desc.label = "OrderedOffscreen";
        desc.extent = {64, 64, 1};
        desc.format = TextureFormat::Rgba8Unorm;
        desc.memory = MemoryUsage::GpuOnly;
        desc.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
        const auto source = graph.createTexture(desc);
        ids[frames % ids.size()] = graph.texture(source).id();
        TextureViewDesc view;
        view.format = desc.format;
        view.usage = TextureUsage::Sampled;
        view.aspect = TextureAspect::Color;
        view_ = device_.createTextureView(graph.texture(source), view);
        if (!view_)
            throw core::DiagnosticError("ORDERED-SMOKE-SOURCE-VIEW");
        DescriptorBindingWrite write;
        write.type = DescriptorType::UniformBuffer;
        write.resourceKind = DescriptorResourceKind::Buffer;
        write.resource = uniform.buffer->id();
        if (uniform.offsetBytes)
            throw core::DiagnosticError("ORDERED-SMOKE-UNIFORM-OFFSET");
        core::requireSuccessfulResult(descriptors_->write(write), "ORDERED-SMOKE-UNIFORM-BINDING");
        for (auto binding : {1u, 3u}) {
            write = {};
            write.binding = binding;
            write.type = DescriptorType::SampledTexture;
            write.resourceKind = DescriptorResourceKind::TextureView;
            write.resource = view_->id();
            core::requireSuccessfulResult(
                descriptors_->write(write), "ORDERED-SMOKE-TEXTURE-BINDING");
            write.binding = binding + 1;
            write.type = DescriptorType::Sampler;
            write.resourceKind = DescriptorResourceKind::Sampler;
            write.resource = sampler_->id();
            core::requireSuccessfulResult(
                descriptors_->write(write), "ORDERED-SMOKE-SAMPLER-BINDING");
        }
        const Color clear = (frames % 2) ? Color{0, 1, 1, 1} : Color{1, 0, 1, 1};
        graph.addPass("OrderedClear")
            .writeAttachment(
                source, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store, {}, clear)
            .executeRenderPass([](render_graph::RenderPassContext&) {});
        const auto extent = graph.texture(canvas).desc().extent;
        graph.addPass("OrderedSample")
            .read(source, ResourceState::ShaderRead)
            .read(uniformHandle, ResourceState::ShaderRead)
            .writeAttachment(canvas, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store)
            .executeRenderPass([this, extent](render_graph::RenderPassContext& context) {
                auto& pass = context.renderPass();
                core::requireSuccessfulResult(
                    pass.setPipeline(*pipelines_[0]), "ORDERED-SMOKE-PIPELINE");
                core::requireSuccessfulResult(
                    pass.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}),
                    "ORDERED-SMOKE-VIEWPORT");
                core::requireSuccessfulResult(
                    pass.setScissor({0, 0, extent.width, extent.height}), "ORDERED-SMOKE-SCISSOR");
                core::requireSuccessfulResult(
                    pass.bindVertexBuffer(3, *vertex_), "ORDERED-SMOKE-VERTEX");
                core::requireSuccessfulResult(
                    pass.bindDescriptorSet(0, *descriptors_), "ORDERED-SMOKE-DESCRIPTOR");
                const std::array<float, 8> push{0, 0, 0, 0, 1, 1, 1, 0};
                core::requireSuccessfulResult(
                    pass.pushConstants(
                        ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data()),
                    "ORDERED-SMOKE-PUSH");
                core::requireSuccessfulResult(pass.draw(3, 1, 0, 0), "ORDERED-SMOKE-DRAW");
                bool retainedRejected = false;
                try {
                    ring_->resetOrdered(0, *execution_);
                } catch (const std::exception& error) {
                    retainedRejected =
                        std::string_view(error.what()) == "RHI-ORDERED-GRAPHICS-RESOURCE-RECORDED";
                }
                if (!retainedRejected)
                    throw core::DiagnosticError("ORDERED-SMOKE-RECORDED-STAGING-REUSED");
                if (onRecorded) {
                    onRecorded();
                    onRecorded = nullptr;
                }
            });
        ++frames;
    }

    std::array<GpuResourceId, 4> ids;
    int frames = 0;
    void (*onRecorded)() = nullptr;

private:
    GpuDevice& device_;
    std::array<std::shared_ptr<RenderPipeline>, 1> pipelines_;
    std::shared_ptr<OriginalOrderedGraphicsExecution> execution_;
    std::shared_ptr<render_graph::FrameGraphUniformRingBuffer> ring_;
    core::UniquePtr<GpuBuffer> vertex_;
    core::UniquePtr<GpuTextureView> view_;
    core::UniquePtr<GpuSampler> sampler_;
    core::UniquePtr<DescriptorArena> arena_;
    core::UniquePtr<DescriptorSet> descriptors_;
};

class OrderedGraphicsSmoke final {
public:
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    int frames = 0;

    bool run(core::UniquePtr<WebGlDevice> device,
        std::shared_ptr<RenderPipeline> depthPipeline,
        double (*nextFrame)(),
        int (*loseContext)(),
        void (*faultOnUnbind)())
    {
        try {
            auto state = RenderPipelineStateCopy::copy(
                dynamic_cast<WebGlRenderPipeline*>(depthPipeline.get())->impl->definition);
            state.depthFormat = state.depthStencil.format = TextureFormat::Unknown;
            state.shaderPackage = nullptr;
            auto pipeline = core::shareUnique(device->createRenderPipeline(state));
            if (!pipeline)
                throw core::DiagnosticError(device->diagnostic());
            if (const auto* id = device->enableSurfaceLifecycle({1, true, 8}))
                throw core::DiagnosticError(id);
            OrderedGraphicsExecutionRequest request;
            request.contractVersion = 1;
            request.prototypeOptIn = true;
            request.maxSubmissions = 12;
            request.maxCpuMetadataBytes = 256 * 1024;
            request.requirements = {1, 1, 64, 64, 4, 32, 32, 0, 2};
            const char* id = nullptr;
            auto executionQuota =
                core::makeShared<core::CpuMemoryQuota>(request.maxCpuMetadataBytes);
            const auto rejectRequest = [&](OrderedGraphicsExecutionRequest invalid) {
                const auto rejected =
                    device->createOrderedGraphicsExecution(invalid, executionQuota, id);
                if (rejected || !id)
                    throw core::DiagnosticError("ORDERED-SMOKE-INVALID-REQUEST-ADMITTED");
            };
            auto invalid = request;
            invalid.prototypeOptIn = false;
            rejectRequest(invalid);
            invalid = request;
            invalid.maxSubmissions = UINT32_MAX;
            rejectRequest(invalid);
            invalid = request;
            invalid.requirements.compute = true;
            rejectRequest(invalid);
            invalid = request;
            ++invalid.maxCpuMetadataBytes;
            rejectRequest(invalid);
            auto execution = core::shareUnique(
                device->createOrderedGraphicsExecution(request, executionQuota, id));
            if (!execution)
                throw core::DiagnosticError(id);
            OrderedGraphicsWorkload workload(*device, pipeline, execution);
            platform::RenderSystemOrderedGraphicsDesc desc;
            desc.contractVersion = 1;
            desc.prototypeOptIn = true;
            desc.surface = {"#canvas", {64, 64}, TextureFormat::Rgba8Unorm, 0};
            desc.commands = {3, true, 1, 160, 3, {1, true, 5, 32, 24, 256 * 1024, 32}, 1, 2, 2, 16};
            desc.maxFrameCpuMetadataBytes = 4 * 1024 * 1024;
            desc.frameQuota = core::makeShared<core::CpuMemoryQuota>(desc.maxFrameCpuMetadataBytes);
            desc.commandQuota =
                core::makeShared<core::CpuMemoryQuota>(desc.commands.issue.maxCpuMetadataBytes);
            const auto rejectGraph = [&](int mode, const char* expected) {
                render_graph::FrameGraph graph(*device);
                if (mode) {
                    auto pass = graph.addPass("RejectedOrderedPass");
                    if (mode == 5 || mode == 6) {
                        TextureDesc shape;
                        shape.extent = {8, 8, 1};
                        shape.format = TextureFormat::Rgba8Unorm;
                        shape.usage = TextureUsage::RenderAttachment;
                        const auto target = mode == 5 ? graph.createTransientTexture(shape)
                                                      : graph.createTexture(shape);
                        pass.writeAttachment(
                            target, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store);
                        if (mode == 6) {
                            BufferDesc buffer;
                            buffer.sizeBytes = 128;
                            buffer.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
                            graph.createTransientBuffer(buffer);
                        }
                    }
                    if (mode == 1)
                        pass.kind(render_graph::PassKind::Compute);
                    if (mode == 2)
                        pass.queue(render_graph::QueueRole::Compute);
                    if (mode == 3)
                        pass.asyncComputeEligible(true);
                    pass.executeRenderPass([](render_graph::RenderPassContext&) {});
                }
                try {
                    graph.executeOrdered(mode ? execution : nullptr,
                        workload.pipelines(),
                        desc.commands,
                        desc.commandQuota);
                } catch (const std::exception& error) {
                    if (std::string_view(error.what()) == expected)
                        return;
                    throw;
                }
                throw core::DiagnosticError("ORDERED-SMOKE-INVALID-GRAPH-ADMITTED");
            };
            rejectGraph(0, "RG-ORDERED-ISSUER-MISSING");
            for (int mode : {1, 2, 3})
                rejectGraph(mode, "RG-ORDERED-GRAPH-OPERATION-UNSUPPORTED");
            rejectGraph(4, "RG-ORDERED-GRAPH-ATTACHMENT-REQUIREMENTS");
            rejectGraph(5, "RG-ORDERED-GRAPH-RESOURCE-REQUIREMENTS");
            rejectGraph(6, "RG-ORDERED-GRAPH-RESOURCE-REQUIREMENTS");
            auto path = platform::RenderSystem::createOrderedGraphicsPath(
                std::move(device), execution, desc, id);
            if (!path)
                throw core::DiagnosticError(id);
            auto render = [&](unsigned width, unsigned height) {
                if (!nextFrame())
                    throw core::DiagnosticError("ORDERED-SMOKE-RAF");
                const auto result = path->renderFrame(workload);
                if (!result.success)
                    throw core::DiagnosticError(result.diagnostics);
                if (!result.workAccepted || !result.presented || !result.recycled
                    || result.gpuCompletionClaimed || result.executedPasses != 2)
                    throw core::DiagnosticError("ORDERED-SMOKE-RESULT");
                std::array<std::uint8_t, 64 * 64 * 4> pixels{};
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                if (glGetError() != GL_NO_ERROR)
                    throw core::DiagnosticError("ORDERED-SMOKE-READBACK-ORACLE");
                const bool cyan = frames % 2;
                for (unsigned i = 0; i < width * height; ++i)
                    if (pixels[i * 4] != (cyan ? 0 : 255) || pixels[i * 4 + 1] != (cyan ? 255 : 0)
                        || pixels[i * 4 + 2] != 255 || pixels[i * 4 + 3] != 255)
                        throw core::DiagnosticError("ORDERED-SMOKE-PIXELS");
                ++frames;
            };
            for (unsigned i = 0; i < 4; ++i)
                render(64, 64);
            if (workload.ids[0] != workload.ids[1] || workload.ids[1] != workload.ids[2]
                || workload.ids[2] != workload.ids[3] || path->poolReport().hitCount < 3
                || path->poolReport().pendingResourceCount || desc.commandQuota->liveBytes())
                throw core::DiagnosticError("ORDERED-SMOKE-POOL-REUSE");
            core::requireSuccessfulResult(path->resize({48, 32}), "ORDERED-SMOKE-RESIZE");
            render(48, 32);
            render(48, 32);
            core::requireSuccessfulResult(path->setSurfaceAvailable(false), "ORDERED-SMOKE-PAUSE");
            if (path->renderFrame(workload).success)
                throw core::DiagnosticError("ORDERED-SMOKE-PAUSED-EXECUTION");
            core::requireSuccessfulResult(path->setSurfaceAvailable(true), "ORDERED-SMOKE-RESUME");
            render(48, 32);
            render(48, 32);
            core::requireSuccessfulResult(
                path->resize({64, 64}), "ORDERED-SMOKE-RESTORE-ORIGINAL-EXTENT");
            core::requireSuccessfulResult(path->resize({0, 0}), "ORDERED-SMOKE-ZERO-EXTENT");
            const auto zero = path->renderFrame(workload);
            if (zero.success || zero.workAccepted || zero.gpuCompletionClaimed)
                throw core::DiagnosticError("ORDERED-SMOKE-ZERO-EXTENT-ADMITTED");
            core::requireSuccessfulResult(
                path->resize({64, 64}), "ORDERED-SMOKE-ZERO-EXTENT-RESTORE");
            if (orderedLossMode()) {
                if (!loseContext() || !nextFrame())
                    throw core::DiagnosticError("ORDERED-SMOKE-LOSS-INJECTION");
                const auto lost = path->renderFrame(workload);
                if (lost.success || lost.workAccepted || lost.diagnostics.empty()
                    || !execution->diagnostic(path->device()))
                    throw core::DiagnosticError("ORDERED-SMOKE-LOST-OWNER-ADMITTED");
            }
            if (orderedFaultMode()) {
                workload.onRecorded = faultOnUnbind;
                const auto failed = path->renderFrame(workload);
                if (failed.success || !failed.workAccepted || failed.presented
                    || failed.gpuCompletionClaimed || failed.diagnostics.empty())
                    throw core::DiagnosticError("ORDERED-SMOKE-PARTIAL-ISSUE-RESULT");
                const auto retry = path->renderFrame(workload);
                if (retry.success || retry.workAccepted)
                    throw core::DiagnosticError("ORDERED-SMOKE-PARTIAL-ISSUE-RETRIED");
            }
            return true;
        } catch (const std::exception& error) {
            diagnostic = error.what();
            return false;
        }
    }
};
} // namespace arp::rhi::webgl::test
