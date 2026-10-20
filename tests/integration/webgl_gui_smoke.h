#pragma once
#include "platform/render_system.h"
#include "platform/render_system_ordered_graphics.h"
#include "renderer/gui/gui_ordered_renderer.h"
#include "tests/fixtures/scene_2d_fixture.h"
#include "webgl_product_performance.h"

namespace arp::rhi::webgl::test {
EM_JS(int, guiMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('guiOnly') === '1';
    // clang-format on
});
EM_JS(void, reportGui, (int success, const char* diagnostic, int frames), {
    // clang-format off
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'GuiOrderedProductSmoke',contractVersion:1,success:!!success,
        diagnostic:UTF8ToString(diagnostic),frames,webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        performance:Module.webGlProductPerformance || null,
        gpuCompletionClaimed:false,productionReady:false})});
    Module.arpWorkloadFinished?.();
    // clang-format on
});

class GuiWorkload final : public platform::OrderedGraphicsFrameWorkload {
public:
    bool validationChecks = true;

    GuiWorkload(renderer::GuiOrderedRenderer& renderer,
        std::shared_ptr<RenderPipeline> pipeline,
        std::shared_ptr<GpuTexture> offscreen,
        const arp::ui::UiBitmapFontResource& font)
        : renderer_(renderer), pipelines_{pipeline}, offscreen_(std::move(offscreen)), font_(font)
    {}

    void destroyAfterBuild(core::UniquePtr<renderer::GuiOrderedRenderer>& owner)
    {
        destroy_ = &owner;
    }

    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines() const noexcept override
    {
        return pipelines_;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle target) override
    {
        const auto& extent = graph.texture(target).desc().extent;
        const auto source = graph.importTextureRef(offscreen_);
        graph.addPass("GuiOffscreenColor")
            .writeAttachment(source,
                ResourceState::ColorAttachment,
                LoadOp::Clear,
                StoreOp::Store,
                {},
                {.1f, .2f, .3f, .5f})
            .executeRenderPass([](render_graph::RenderPassContext&) {});
        core::pmr::Vector<renderer::ui::UiDrawCommand> commands{&core::defaultCpuMemoryResource()};
        const arp::ui::LayoutRect full{0, 0, float(extent.width), float(extent.height)};
        renderer::ui::UiDrawCommand box;
        box.rect = full;
        box.clipRect = full;
        std::copy_n(std::array<float, 4>{.1f, .1f, .1f, 1}.data(), 4, box.color);
        commands.push_back(box);
        auto image = box;
        image.kind = renderer::ui::UiDrawCommandKind::Image;
        image.rect = {0, 0, float(extent.width / 2), float(extent.height / 2)};
        image.clipRect = {4, 4, float(extent.width / 2 - 4), float(extent.height / 2 - 4)};
        image.textureResourceId = "image";
        image.hasTextureSourceRect = true;
        image.textureSourceRect = {0, 0, .5f, 1};
        std::fill_n(image.color, 4, 1);
        commands.push_back(image);
        auto glyph = image;
        glyph.kind = renderer::ui::UiDrawCommandKind::GlyphQuad;
        glyph.rect = {float(extent.width / 4),
            float(extent.height / 4),
            float(extent.width / 2),
            float(extent.height / 2)};
        glyph.clipRect = full;
        const auto* selected = font_.glyph(65);
        if (!selected)
            throw core::DiagnosticError("GUI-COOKED-GLYPH-MISSING");
        const auto& atlas = *font_.atlas().get();
        const auto& slice = *atlas.slice(selected->sliceName);
        glyph.textureResourceId = font_.atlas().id();
        glyph.textureSourceRect = {float(slice.x) / atlas.image().width,
            float(slice.y) / atlas.image().height,
            float(slice.width) / atlas.image().width,
            float(slice.height) / atlas.image().height};
        glyph.fontToken = font_.fontToken();
        glyph.hasAtlasPageIndex = true;
        glyph.atlasPageIndex = font_.page();
        commands.push_back(glyph);
        auto external = image;
        external.rect = {float(extent.width / 2),
            float(extent.height / 2),
            float(extent.width / 2),
            float(extent.height / 2)};
        external.clipRect = full;
        external.textureResourceId = "offscreen";
        external.textureSourceRect = {0, 0, 1, 1};
        external.textureSource = renderer::ui::UiTextureSource::External;
        commands.push_back(external);
        // Reject every unsupported/ambiguous input before modifying the graph.
        const auto reject = [&](const char* diagnostic) {
            const auto result =
                renderer_.addPass(graph, target, commands, "image", 0, LoadOp::Clear);
            if (result.success || result.diagnostics != diagnostic)
                throw core::DiagnosticError("GUI-NEGATIVE-DIAGNOSTIC");
        };
        if (validationChecks) {
            commands[2].sdfMode = true;
            reject("GUI-ORDERED-SDF-PROFILE-REQUIRED");
            commands[2].sdfMode = false;
            commands[2].fontToken = "missing";
            reject("GUI-ORDERED-GLYPH-PAGE-FONT-MISMATCH");
            commands[2].fontToken = font_.fontToken();
            commands[2].atlasPageIndex = 1;
            reject("GUI-ORDERED-GLYPH-PAGE-FONT-MISMATCH");
            commands[2].atlasPageIndex = 0;
            commands[1].clipRect.x = 4.5f;
            reject("GUI-ORDERED-EXPLICIT-INTEGER-CLIP-REQUIRED");
            commands[1].clipRect.x = 4;
            commands[1].textureResourceId = "missing";
            reject("GUI-ORDERED-TEXTURE-MISSING");
            commands[1].textureResourceId = "image";
            commands[1].clipRect.width = 0;
            reject("GUI-ORDERED-EXPLICIT-INTEGER-CLIP-REQUIRED");
            commands[1].clipRect.width = float(extent.width / 2 - 4);
            commands[1].kind = static_cast<renderer::ui::UiDrawCommandKind>(255);
            reject("GUI-ORDERED-COMMAND-KIND");
            commands[1].kind = renderer::ui::UiDrawCommandKind::Image;
            commands[1].textureSource = static_cast<renderer::ui::UiTextureSource>(255);
            reject("GUI-ORDERED-TEXTURE-SOURCE");
            commands[1].textureSource = renderer::ui::UiTextureSource::Atlas;
            commands.push_back(external);
            reject("GUI-ORDERED-COMMAND-CAPACITY");
            commands.pop_back();
        }
        core::requireSuccessfulResult(
            renderer_.addPass(graph, target, commands, "image", 0, LoadOp::Clear), "GUI-GRAPH");
        if (validationChecks)
            reject("GUI-ORDERED-FRAME-RESERVED");
        if (validationChecks
            && (renderer_.readyForFrame(0)
                || renderer_.unregisterTexture(font_.atlas().id()).success))
            throw core::DiagnosticError("GUI-GRAPH-RESERVATION");
        if (destroy_)
            destroy_->reset();
    }

private:
    renderer::GuiOrderedRenderer& renderer_;
    std::array<std::shared_ptr<RenderPipeline>, 1> pipelines_;
    std::shared_ptr<GpuTexture> offscreen_;
    const arp::ui::UiBitmapFontResource& font_;
    core::UniquePtr<renderer::GuiOrderedRenderer>* destroy_ = nullptr;
};

class GuiSmoke final {
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
                "/artifacts/gui_bitmap.webgl-program.yaml", load, artifactQuota, diagnostic);
            if (!artifact)
                throw core::DiagnosticError(diagnostic);
            ShaderPackageDesc package;
            core::requireSuccessfulResult(
                populateSelectedGraphicsShaderInterface(
                    *artifact->vertex().original, artifact->fragment().original.get(), package),
                "GUI-REFLECTION");
            PipelineLayoutDesc definition;
            definition.descriptorLayouts.push_back({"GuiBitmapSet",
                {{0, DescriptorType::SampledTexture, ShaderStage::Fragment},
                    {1, DescriptorType::Sampler, ShaderStage::Fragment}}});
            definition.descriptorSets.push_back({0, "GuiBitmapSet"});
            definition.pushConstantRanges.push_back({ShaderStage::Vertex, 0, 64});
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
            stream.strideBytes = 8;
            stream.attributes = {{0, 0, VertexFormat::Float32x2}};
            state.vertexBuffers.push_back(std::move(stream));
            auto pipeline = core::shareUnique(device->createRenderPipeline(state));
            if (!pipeline)
                throw core::DiagnosticError(device->diagnostic());
            OrderedGraphicsExecutionRequest request;
            request.contractVersion = 1;
            request.prototypeOptIn = true;
            request.maxSubmissions = performance.submissionBudget(16);
            request.maxCpuMetadataBytes = 256 * 1024;
            request.requirements = {1, 1, 64, 64, 1, 16, 8, 0, 1};
            auto quota = core::makeShared<core::CpuMemoryQuota>(request.maxCpuMetadataBytes);
            auto execution =
                core::shareUnique(device->createOrderedGraphicsExecution(request, quota, id));
            if (!execution || (id = device->enableSurfaceLifecycle({1, true, 8})))
                throw core::DiagnosticError(id);
            resource::ResourceManager resources;
            if (!arp::tests::loadSpriteFixture(resources))
                throw core::DiagnosticError("GUI-RESOURCE-LOAD");
            constexpr const char* glyphId = "00000000-0000-4000-8000-000000000006";
            auto loaded = resources.load<scene2d::SpriteAtlasResource>({},
                core::pmr::String(glyphId),
                core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/glyph.yaml"),
                core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/glyph.png"),
                65536u);
            if (!loaded.success)
                throw core::DiagnosticError(loaded.diagnostics);
            constexpr const char* fontId = "00000000-0000-4000-8000-000000000007";
            auto fontLoaded = resources.load<arp::ui::UiBitmapFontResource>({},
                core::pmr::String(fontId),
                core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/glyph.font.yaml"),
                core::pmr::String(glyphId),
                65536u,
                1u);
            if (!fontLoaded.success)
                throw core::DiagnosticError(fontLoaded.diagnostics);
            const auto& font = *resources.find<arp::ui::UiBitmapFontResource>(fontId).get();
            const auto upload = [&](const char* resourceId) {
                const auto& image =
                    resources.find<scene2d::SpriteAtlasResource>(resourceId).get()->image();
                auto texture = core::shareUnique(device->createTexture({resourceId,
                    {image.width, image.height, 1},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::Sampled | TextureUsage::CopyDst,
                    MemoryUsage::GpuOnly,
                    ResourceState::ShaderRead}));
                if (!texture)
                    throw core::DiagnosticError(device->diagnostic());
                semantics::ClientTextureUpload data;
                data.contractVersion = 1;
                data.extent = {image.width, image.height, 1};
                data.bytesPerRow = image.width * 4;
                data.bytes = image.rgba8;
                core::requireSuccessfulResult(
                    execution->uploadTexture(*texture, data), "GUI-RESOURCE-UPLOAD");
                return texture;
            };
            auto image = upload(arp::tests::atlasA);
            auto offscreen = core::shareUnique(device->createTexture({"GuiOffscreen",
                {64, 64, 1},
                TextureFormat::Rgba8Unorm,
                TextureUsage::Sampled | TextureUsage::RenderAttachment,
                MemoryUsage::GpuOnly,
                ResourceState::Undefined}));
            if (!offscreen)
                throw core::DiagnosticError(device->diagnostic());
            platform::RenderSystemOrderedGraphicsDesc desc;
            desc.contractVersion = 1;
            desc.prototypeOptIn = true;
            desc.surface = {"#canvas", {64, 64}, TextureFormat::Rgba8Unorm, 0};
            desc.commands = {
                3, true, 4, 256, 24, {1, true, 16, 96, 64, 512 * 1024, 256}, 1, 2, 2, 16};
            desc.maxFrameCpuMetadataBytes = 4 * 1024 * 1024;
            desc.frameQuota = core::makeShared<core::CpuMemoryQuota>(desc.maxFrameCpuMetadataBytes);
            desc.commandQuota =
                core::makeShared<core::CpuMemoryQuota>(desc.commands.issue.maxCpuMetadataBytes);
            auto path = platform::RenderSystem::createOrderedGraphicsPath(
                std::move(device), execution, desc, id);
            if (!path)
                throw core::DiagnosticError(id);
            auto rendererOwner = core::makeUnique<renderer::GuiOrderedRenderer>(path->device());
            auto& renderer = *rendererOwner;
            auto guiQuota = core::makeShared<core::CpuMemoryQuota>(256 * 1024);
            core::requireSuccessfulResult(renderer.create({1,
                                              4,
                                              3,
                                              1,
                                              256 * 1024,
                                              guiQuota,
                                              execution,
                                              pipeline,
                                              package.reflection.sourceSha256,
                                              {1, false, false}}),
                "GUI-CREATE");
            core::requireSuccessfulResult(renderer.registerTexture({"image", image}), "GUI-IMAGE");
            core::requireSuccessfulResult(renderer.registerBitmapFont(font), "GUI-GLYPH");
            core::requireSuccessfulResult(renderer.registerTexture({"offscreen",
                                              offscreen,
                                              renderer::GuiSampleAlpha::Premultiplied,
                                              true,
                                              false,
                                              true,
                                              {}}),
                "GUI-EXTERNAL");
            GuiWorkload workload(renderer, pipeline, offscreen, font);
            {
                auto target = core::shareUnique(path->device().createTexture({"CancelledGui",
                    {64, 64, 1},
                    TextureFormat::Rgba8Unorm,
                    TextureUsage::RenderAttachment,
                    MemoryUsage::GpuOnly,
                    ResourceState::Undefined}));
                if (!target)
                    throw core::DiagnosticError("GUI-CANCEL-TARGET");
                render_graph::FrameGraph cancelled(path->device());
                workload.build(cancelled, cancelled.importTextureRef(target));
            }
            if (!renderer.readyForFrame(0))
                throw core::DiagnosticError("GUI-CANCELLED-SLOT-RESERVED");
            performance.setupFinished();
            auto render = [&](unsigned w, unsigned h, bool verify = true) {
                core::CpuWorkMeasurementScope inputs(performance.inputs());
                inputs.finish();
                if (!nextFrame())
                    throw core::DiagnosticError("GUI-RAF");
                performance.begin();
                const auto report = path->renderFrame(workload, performance.measurements());
                performance.finish(
                    w, h, desc.frameQuota, desc.commandQuota, workload.validationChecks);
                if (!report.success)
                    throw core::DiagnosticError(report.diagnostics);
                if (!report.presented || !report.recycled || report.gpuCompletionClaimed
                    || (rendererOwner && !rendererOwner->readyForFrame(0)))
                    throw core::DiagnosticError("GUI-FRAME-RESULT");
                if (!verify) {
                    ++frames;
                    return;
                }
                std::array<std::uint8_t, 64 * 64 * 4> pixels{};
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                if (glGetError() != GL_NO_ERROR)
                    throw core::DiagnosticError("GUI-ORACLE-READBACK");
                const auto& atlas =
                    resources.find<scene2d::SpriteAtlasResource>(arp::tests::atlasA).get()->image();
                const auto& font =
                    resources.find<scene2d::SpriteAtlasResource>(glyphId).get()->image();
                for (unsigned y = 0; y < h; ++y)
                    for (unsigned x = 0; x < w; ++x) {
                        const auto top = h - 1 - y;
                        std::array<int, 4> expected{26, 26, 26, 255};
                        if (x >= 4 && x < w / 2 && top >= 4 && top < h / 2) {
                            const auto tx = unsigned((x + .5f) / (w / 2) * 8),
                                       ty = unsigned((top + .5f) / (h / 2) * 8);
                            for (unsigned c = 0; c < 3; ++c)
                                expected[c] = atlas.rgba8[(ty * 16 + tx) * 4 + c];
                        }
                        if (x >= w / 4 && x < w * 3 / 4 && top >= h / 4 && top < h * 3 / 4) {
                            const auto tx = unsigned((x - w / 4 + .5f) / (w / 2) * 6),
                                       ty = unsigned((top - h / 4 + .5f) / (h / 2) * 8);
                            if (font.rgba8[(ty * 8 + tx) * 4 + 3])
                                expected = {255, 255, 255, 255};
                        }
                        if (x >= w / 2 && top >= h / 2)
                            for (unsigned c = 0; c < 3; ++c)
                                expected[c] =
                                    int(std::lround(std::array<float, 3>{.1f, .2f, .3f}[c] * 255))
                                    + int(std::lround(expected[c] * (1 - 128 / 255.f)));
                        for (unsigned c = 0; c < 4; ++c)
                            if (std::abs(int(pixels[(y * w + x) * 4 + c]) - expected[c]) > 1)
                                throw core::DiagnosticError("GUI-ORACLE-PIXEL-MISMATCH");
                    }
                ++frames;
            };
            for (unsigned i = 0; i < 4; ++i)
                render(64, 64);
            performance.sample(render, 64, 64, workload.validationChecks);
            core::requireSuccessfulResult(path->resize({48, 32}), "GUI-RESIZE");
            render(48, 32);
            performance.sample(render, 48, 32, workload.validationChecks);
            core::requireSuccessfulResult(path->setSurfaceAvailable(false), "GUI-PAUSE");
            if (path->renderFrame(workload).success)
                throw core::DiagnosticError("GUI-PAUSED-DRAW");
            core::requireSuccessfulResult(path->setSurfaceAvailable(true), "GUI-RESUME");
            core::requireSuccessfulResult(
                renderer.unregisterTexture(font.atlas().id()), "GUI-UNREGISTER");
            core::requireSuccessfulResult(renderer.registerBitmapFont(font), "GUI-RELOAD-GLYPH");
            workload.destroyAfterBuild(rendererOwner);
            render(48, 32);
            if (rendererOwner || guiQuota->liveBytes())
                throw core::DiagnosticError("GUI-RENDERER-GRAPH-RETIREMENT");
            if (desc.commandQuota->liveBytes())
                throw core::DiagnosticError("GUI-COMMAND-RETIREMENT");
            core::requireSuccessfulResult(path->resize({64, 64}), "GUI-RESTORE-EXTENT");
            return true;
        } catch (const std::exception& e) {
            diagnostic = e.what();
            return false;
        }
    }
};
} // namespace arp::rhi::webgl::test
