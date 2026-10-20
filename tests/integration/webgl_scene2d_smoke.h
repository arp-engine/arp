#pragma once
#include "platform/render_system.h"
#include "platform/render_system_ordered_graphics.h"
#include "renderer/scene_2d_renderer.h"
#include "tests/fixtures/scene_2d_fixture.h"
#include "webgl_product_performance.h"

namespace arp::rhi::webgl::test {
EM_JS(int, scene2dMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('scene2dOnly') === '1';
    // clang-format on
});
EM_JS(void, reportScene2D, (int success, const char* diagnostic, int frames), {
    // clang-format off
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'Scene2DOrderedProductSmoke',contractVersion:1,success:!!success,
        diagnostic:UTF8ToString(diagnostic),frames,webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        performance:Module.webGlProductPerformance || null,
        gpuCompletionClaimed:false,productionReady:false})});
    Module.arpWorkloadFinished?.();
    // clang-format on
});

class Scene2DWorkload final : public platform::OrderedGraphicsFrameWorkload {
public:
    bool validationChecks = true;

    Scene2DWorkload(renderer::Scene2DRenderer& renderer,
        const scene2d::SpriteDrawPlan& plan,
        resource::ResourceManager& resources,
        std::shared_ptr<RenderPipeline> pipeline)
        : renderer_(&renderer), plan_(plan), resources_(resources), pipelines_{std::move(pipeline)}
    {}

    void destroyAfterBuild(core::UniquePtr<renderer::Scene2DRenderer>& owner) { destroy_ = &owner; }

    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines() const noexcept override
    {
        return pipelines_;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle target) override
    {
        const auto extent = graph.texture(target).desc().extent;
        renderer::Scene2DOrderedFrame frame{
            {1, 1, 0, 0}, {4, 4, extent.width - 8, extent.height - 8}, 0};
        core::requireSuccessfulResult(
            renderer_->addOrderedPass(graph, target, plan_, frame, LoadOp::Clear, {0, 0, 0, 1}),
            "SCENE2D-GRAPH");
        if (validationChecks
            && (renderer_->readyForFrame(0)
                || renderer_->addOrderedPass(graph, target, plan_, frame, LoadOp::Load).success
                || resources_.unload(arp::tests::atlasA).success))
            throw core::DiagnosticError("SCENE2D-UNRECORDED-OWNERSHIP-GATE");
        if (destroy_) {
            destroy_->reset();
            renderer_ = nullptr;
        }
    }

private:
    renderer::Scene2DRenderer* renderer_;
    core::UniquePtr<renderer::Scene2DRenderer>* destroy_ = nullptr;
    const scene2d::SpriteDrawPlan& plan_;
    resource::ResourceManager& resources_;
    std::array<std::shared_ptr<RenderPipeline>, 1> pipelines_;
};

class Scene2DSmoke final {
public:
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    int frames = 0;

    bool run(core::UniquePtr<WebGlDevice> device, double (*nextFrame)())
    {
        try {
            ProductPerformance performance(*device);
            shader::WebGlShaderLoadBudget load{
                {4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32}, 256, 32, 1024};
            auto artifactQuota =
                core::makeShared<core::CpuMemoryQuota>(load.original.maxCpuMetadataBytes);
            auto artifact = shader::WebGlShaderProgramArtifact::load(
                "/artifacts/scene_2d_vertices.webgl-program.yaml", load, artifactQuota, diagnostic);
            if (!artifact)
                throw core::DiagnosticError(diagnostic);
            ShaderPackageDesc package;
            core::requireSuccessfulResult(
                populateSelectedGraphicsShaderInterface(
                    *artifact->vertex().original, artifact->fragment().original.get(), package),
                "SCENE2D-REFLECTION");
            PipelineLayoutDesc definition;
            definition.descriptorLayouts.push_back({"SpriteAtlasSet",
                {{0, DescriptorType::SampledTexture, ShaderStage::Fragment},
                    {1, DescriptorType::Sampler, ShaderStage::Fragment}}});
            definition.descriptorSets.push_back({0, "SpriteAtlasSet"});
            definition.pushConstantRanges.push_back({ShaderStage::Vertex, 0, 16});
            auto layout = core::shareUnique(device->createPipelineLayout(definition));
            const char* id = nullptr;
            if (!layout
                || (id = device->selectPipelineInputs(layout, artifact, {1, {1, 1024, 4096}, {}})))
                throw core::DiagnosticError(id ? id : device->diagnostic());
            RenderPipelineDesc state;
            state.layout = layout.get();
            state.vertexShader = package.entrypoints[0];
            state.fragmentShader = package.entrypoints[1];
            state.colorFormat = TextureFormat::Rgba8Unorm;
            state.dynamicViewportScissor = true;
            ColorTargetState color;
            color.format = state.colorFormat;
            color.blendEnabled = true;
            color.colorBlend =
                color.alphaBlend = {BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add};
            state.colorTargets.push_back(color);
            VertexBufferLayoutDesc stream;
            stream.strideBytes = 32;
            stream.attributes = {{0, 0, VertexFormat::Float32x2},
                {1, 8, VertexFormat::Float32x2},
                {2, 16, VertexFormat::Float32x4}};
            state.vertexBuffers.push_back(std::move(stream));
            auto pipeline = core::shareUnique(device->createRenderPipeline(state));
            if (!pipeline)
                throw core::DiagnosticError(device->diagnostic());
            OrderedGraphicsExecutionRequest request;
            request.contractVersion = 1;
            request.prototypeOptIn = true;
            request.maxSubmissions = performance.submissionBudget(16);
            request.maxCpuMetadataBytes = 256 * 1024;
            request.requirements = {1, 1, 64, 64, 3, 8, 8, 0, 1};
            auto quota = core::makeShared<core::CpuMemoryQuota>(request.maxCpuMetadataBytes);
            auto execution =
                core::shareUnique(device->createOrderedGraphicsExecution(request, quota, id));
            if (!execution || (id = device->enableSurfaceLifecycle({1, true, 8})))
                throw core::DiagnosticError(id);
            resource::ResourceManager resources;
            if (!arp::tests::loadSpriteFixture(resources))
                throw core::DiagnosticError("SCENE2D-RESOURCE-LOAD");
            platform::RenderSystemOrderedGraphicsDesc desc;
            desc.contractVersion = 1;
            desc.prototypeOptIn = true;
            desc.surface = {"#canvas", {64, 64}, TextureFormat::Rgba8Unorm, 0};
            desc.commands = {
                3, true, 3, 128, 18, {1, true, 8, 64, 48, 256 * 1024, 64}, 1, 1, 1, 16};
            desc.maxFrameCpuMetadataBytes = 4 * 1024 * 1024;
            desc.frameQuota = core::makeShared<core::CpuMemoryQuota>(desc.maxFrameCpuMetadataBytes);
            desc.commandQuota =
                core::makeShared<core::CpuMemoryQuota>(desc.commands.issue.maxCpuMetadataBytes);
            auto path = platform::RenderSystem::createOrderedGraphicsPath(
                std::move(device), execution, desc, id);
            if (!path)
                throw core::DiagnosticError(id);
            auto renderer = core::makeUnique<renderer::Scene2DRenderer>(path->device(), resources);
            renderer::Scene2DOrderedRendererDesc creation{
                1, 3, 2, 1, 576, execution, pipeline, package.reflection.sourceSha256};
            auto invalid = creation;
            invalid.shaderSourceSha256[0] = invalid.shaderSourceSha256[0] == 'a' ? 'b' : 'a';
            const auto wrongSource = renderer->createOrdered(invalid);
            invalid = creation;
            --invalid.maxVertexBytesPerSlot;
            const auto shortBudget = renderer->createOrdered(invalid);
            if (wrongSource.success
                || wrongSource.diagnostics != "ARP-2D-ORDERED-SHADER-SOURCE-IDENTITY"
                || shortBudget.success || shortBudget.diagnostics != "ARP-2D-VERTEX-PLAN-BUDGET")
                throw core::DiagnosticError("SCENE2D-CREATION-NEGATIVES");
            core::requireSuccessfulResult(renderer->createOrdered(creation), "SCENE2D-CREATE");
            if (renderer->prepareAtlas({}).success)
                throw core::DiagnosticError("SCENE2D-MISSING-ATLAS-ACCEPTED");
            for (const auto* atlas : {arp::tests::atlasA, arp::tests::atlasB})
                core::requireSuccessfulResult(
                    renderer->prepareAtlas(resources.find<scene2d::SpriteAtlasResource>(atlas)),
                    "SCENE2D-ATLAS");
            core::pmr::Vector<scene2d::SpriteDrawItem> items{&core::defaultCpuMemoryResource()};
            for (std::uint32_t i = 0; i < 3; ++i) {
                scene2d::SpriteDrawItem item;
                item.sprite = resources.find<scene2d::SpriteResource>(
                    i == 1 ? arp::tests::spriteB : arp::tests::spriteA);
                item.sorting.objectId = i + 1;
                item.world.translation.x = (float(i) - 1) * .5f;
                // The right slice is authored with a (0,0) pivot. Place its
                // bottom-left explicitly so all three test rectangles share y.
                if (i == 1)
                    item.world.translation = {-.5f, -.5f, 0};
                if (i == 2)
                    item.color.w = .5f;
                items.push_back(item);
            }
            scene2d::SpriteDrawPlan plan(3);
            Scene2DWorkload workload(*renderer, plan, resources, pipeline);
            if (!plan.build(resources, items))
                throw core::DiagnosticError("SCENE2D-CANCELLATION-PLAN");
            {
                auto target = core::shareUnique(path->device().createTexture({"CancelledScene2D",
                    {64, 64, 1},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::RenderAttachment,
                    MemoryUsage::GpuOnly,
                    ResourceState::Undefined}));
                if (!target)
                    throw core::DiagnosticError("SCENE2D-CANCEL-TARGET");
                render_graph::FrameGraph cancelled(path->device());
                workload.build(cancelled, cancelled.importTextureRef(target));
            }
            if (!renderer->readyForFrame(0))
                throw core::DiagnosticError("SCENE2D-CANCELLED-SLOT-RESERVED");
            performance.setupFinished();
            auto render = [&](unsigned w, unsigned h, bool verify = true) {
                core::CpuWorkMeasurementScope inputs(performance.inputs());
                items[2].flipY = frames % 2;
                const bool planned = plan.build(resources, items);
                inputs.finish();
                if (!planned || !nextFrame())
                    throw core::DiagnosticError("SCENE2D-PLAN-OR-RAF");
                performance.begin();
                const auto report = path->renderFrame(workload, performance.measurements());
                performance.finish(
                    w, h, desc.frameQuota, desc.commandQuota, workload.validationChecks);
                if (!report.success)
                    throw core::DiagnosticError(report.diagnostics);
                if (!report.workAccepted || !report.presented || !report.recycled
                    || report.gpuCompletionClaimed)
                    throw core::DiagnosticError("SCENE2D-RESULT");
                if (renderer && !renderer->readyForFrame(0))
                    throw core::DiagnosticError("SCENE2D-SLOT-NOT-RETIRED");
                if (renderer && renderer->lastDrawCount() != 3)
                    throw core::DiagnosticError("SCENE2D-PREPARED-DRAW-COUNT");
                if (!verify) {
                    ++frames;
                    return;
                }
                std::array<std::uint8_t, 64 * 64 * 4> pixels{};
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                if (glGetError() != GL_NO_ERROR)
                    throw core::DiagnosticError("SCENE2D-ORACLE-READBACK");
                const auto& image =
                    resources.find<scene2d::SpriteAtlasResource>(arp::tests::atlasA).get()->image();
                for (unsigned y = 0; y < h; ++y)
                    for (unsigned x = 0; x < w; ++x) {
                        std::array<float, 4> expected{0, 0, 0, 1};
                        if (x >= 4 && x < w - 4 && y >= h / 4 && y < h * 3 / 4) {
                            for (unsigned sprite = 0; sprite < 3; ++sprite) {
                                const auto left = sprite * w / 4;
                                if (x < left || x >= left + w / 2)
                                    continue;
                                const float u = float(x - left + .5f) / (w / 2);
                                // Native glReadPixels is bottom-up; the published
                                // WebGlShaderCoordinates/1 flips clip Y once.
                                const auto logicalY = h - 1 - y;
                                float v = 1 - float(logicalY - h / 4 + .5f) / (h / 2);
                                if (sprite == 2 && frames % 2)
                                    v = 1 - v;
                                const auto tx = (sprite == 1 ? 8u : 0u) + unsigned(u * 8);
                                const auto ty = unsigned(v * 8);
                                const auto offset = (ty * image.width + tx) * 4;
                                const float alpha =
                                    image.rgba8[offset + 3] / 255.f * (sprite == 2 ? .5f : 1.f);
                                for (unsigned c = 0; c < 3; ++c)
                                    expected[c] =
                                        std::round((image.rgba8[offset + c] / 255.f * alpha
                                                       + expected[c] * (1 - alpha))
                                            * 255)
                                        / 255;
                            }
                        }
                        for (unsigned c = 0; c < 4; ++c)
                            if (std::abs(int(pixels[(y * w + x) * 4 + c])
                                    - int(std::lround(expected[c] * 255)))
                                > 1) {
                                std::array<char, 160> message{};
                                std::snprintf(message.data(),
                                    message.size(),
                                    "SCENE2D-ORACLE-PIXEL-MISMATCH frame=%d x=%u y=%u c=%u expected=%d actual=%u",
                                    frames,
                                    x,
                                    y,
                                    c,
                                    int(std::lround(expected[c] * 255)),
                                    unsigned(pixels[(y * w + x) * 4 + c]));
                                throw core::DiagnosticError(message.data());
                            }
                    }
                ++frames;
            };
            for (unsigned i = 0; i < 4; ++i)
                render(64, 64);
            performance.sample(render, 64, 64, workload.validationChecks);
            core::requireSuccessfulResult(path->resize({48, 32}), "SCENE2D-RESIZE");
            render(48, 32);
            performance.sample(render, 48, 32, workload.validationChecks);
            core::requireSuccessfulResult(path->setSurfaceAvailable(false), "SCENE2D-PAUSE");
            if (path->renderFrame(workload).success)
                throw core::DiagnosticError("SCENE2D-PAUSED-DRAW");
            core::requireSuccessfulResult(path->setSurfaceAvailable(true), "SCENE2D-RESUME");
            workload.destroyAfterBuild(renderer);
            render(48, 32);
            if (renderer || !resources.unload(arp::tests::atlasA).success
                || desc.commandQuota->liveBytes())
                throw core::DiagnosticError("SCENE2D-FINAL-RETIREMENT");
            core::requireSuccessfulResult(path->resize({64, 64}), "SCENE2D-RESTORE-INITIAL-EXTENT");
            return true;
        } catch (const std::exception& error) {
            diagnostic = error.what();
            return false;
        }
    }
};
} // namespace arp::rhi::webgl::test
