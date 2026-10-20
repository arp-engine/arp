#pragma once
#include "core/quad_geometry.h"
#include "platform/render_system.h"
#include "platform/render_system_ordered_graphics.h"
#include "renderer/gui/gui_ordered_renderer.h"
#include "renderer/pipeline/basic_forward_pipeline.h"
#include "tests/fixtures/scene_2d_fixture.h"
#include "webgl_product_performance.h"

#include <GLES2/gl2.h>
#include <emscripten.h>

namespace arp::rhi::webgl::test {
EM_JS(int, basicForwardMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('basicForwardOnly') === '1';
    // clang-format on
});
EM_JS(void, reportBasicForward, (int success, const char* diagnostic, int frames), {
    // clang-format off
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'BasicForwardProductSmoke',contractVersion:1,success:!!success,
        diagnostic:UTF8ToString(diagnostic),frames,webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        performance:Module.webGlProductPerformance || null,
        depthDiagnostic:true,gpuCompletionClaimed:false,productionReady:false})});
    Module.arpWorkloadFinished?.();
    // clang-format on
});

struct BasicDepthDiagnostic {
    std::shared_ptr<RenderPipeline> pipeline;
    std::shared_ptr<GpuBuffer> camera, vertices;
    std::shared_ptr<GpuTexture> source, output;
    core::UniquePtr<GpuSampler> sampler;
    core::UniquePtr<GpuTextureView> view;
    core::UniquePtr<DescriptorArena> arena;
    core::UniquePtr<DescriptorSet> set;
};

class BasicForwardWorkload final : public platform::OrderedGraphicsFrameWorkload {
public:
    bool validationChecks = true;
    renderer::BasicForwardRenderPipeline* scene = nullptr;
    renderer::GuiOrderedRenderer* gui = nullptr;
    std::array<std::shared_ptr<RenderPipeline>, 6> native;
    std::shared_ptr<BasicDepthDiagnostic> receiver;
    renderer::BasicForwardFrame frame;
    std::array<std::uint32_t, 1> meshIds{};
    std::shared_ptr<GpuTexture> offscreen;
    const arp::ui::UiBitmapFontResource* font = nullptr;
    core::UniquePtr<renderer::BasicForwardRenderPipeline>* destroy = nullptr;

    core::ArrayView<std::shared_ptr<RenderPipeline>> pipelines() const noexcept override
    {
        return native;
    }

    void build(render_graph::FrameGraph& graph, render_graph::TextureHandle target) override
    {
        const auto color = graph.importTextureRef(offscreen);
        frame.meshes = meshIds;
        // Every failure must leave the graph unreserved and recoverable.
        if (validationChecks) {
            auto invalid = frame;
            invalid.viewProjection.m[0] = std::numeric_limits<float>::infinity();
            if (scene->addPass(graph, color, invalid).success)
                throw core::DiagnosticError("BASIC-FORWARD-NONFINITE-ACCEPTED");
            const std::array<std::uint32_t, 1> absent{UINT32_MAX};
            invalid = frame;
            invalid.meshes = absent;
            if (scene->addPass(graph, color, invalid).success)
                throw core::DiagnosticError("BASIC-FORWARD-MISSING-MESH-ACCEPTED");
        }
        core::requireSuccessfulResult(scene->addPass(graph, color, frame), "BASIC-FORWARD-GRAPH");
        if (validationChecks
            && (scene->frameInputsReady(0) || scene->addPass(graph, color, frame).success))
            throw core::DiagnosticError("BASIC-FORWARD-GRAPH-RESERVATION");
        auto diagnostic = receiver;
        graph.addPass("BasicDepthDiagnostic")
            .read(graph.importTextureRef(diagnostic->source), ResourceState::ShaderRead)
            .access(graph.importBufferRef(diagnostic->camera),
                {ResourceState::ShaderRead,
                    PipelineStage::VertexShader,
                    ResourceAccess::ShaderRead},
                0,
                64)
            .access(graph.importBufferRef(diagnostic->vertices),
                {ResourceState::ShaderRead,
                    PipelineStage::VertexInput,
                    ResourceAccess::VertexAttributeRead},
                0,
                6 * sizeof(assets::MeshVertex))
            .writeAttachment(graph.importTextureRef(diagnostic->output),
                ResourceState::ColorAttachment,
                LoadOp::Clear,
                StoreOp::Store)
            .executeRenderPass([diagnostic](render_graph::RenderPassContext& context) {
                auto& pass = context.renderPass();
                core::requireSuccessfulResult(
                    pass.setPipeline(*diagnostic->pipeline), "BASIC-DEPTH-PIPELINE");
                core::requireSuccessfulResult(
                    pass.setViewport({0, 0, 64, 64, 0, 1}), "BASIC-DEPTH-VIEWPORT");
                core::requireSuccessfulResult(
                    pass.setScissor({0, 0, 64, 64}), "BASIC-DEPTH-SCISSOR");
                core::requireSuccessfulResult(
                    pass.bindVertexBuffer(0, *diagnostic->vertices), "BASIC-DEPTH-VERTEX");
                core::requireSuccessfulResult(
                    pass.bindDescriptorSet(0, *diagnostic->set), "BASIC-DEPTH-DESCRIPTOR");
                const core::Vec4 map{.5f, -.5f, .5f, .5f};
                core::requireSuccessfulResult(
                    pass.pushConstants(ShaderStage::Vertex, 0, 16, &map), "BASIC-DEPTH-MAPPING");
                core::requireSuccessfulResult(pass.draw(6, 1, 0, 0), "BASIC-DEPTH-DRAW");
            });
        const auto extent = graph.texture(target).desc().extent;
        renderer::ui::UiDrawCommand fullscreen;
        fullscreen.kind = renderer::ui::UiDrawCommandKind::Image;
        fullscreen.rect = fullscreen.clipRect = {0, 0, float(extent.width), float(extent.height)};
        fullscreen.textureResourceId = "basic-offscreen";
        fullscreen.textureSource = renderer::ui::UiTextureSource::External;
        fullscreen.hasTextureSourceRect = true;
        fullscreen.textureSourceRect = {0, 0, 1, 1};
        std::fill_n(fullscreen.color, 4, 1);
        core::pmr::Vector<renderer::ui::UiDrawCommand> commands{&core::defaultCpuMemoryResource()};
        commands.push_back(fullscreen);
        auto image = fullscreen;
        image.textureResourceId = "image";
        image.textureSource = renderer::ui::UiTextureSource::Atlas;
        image.rect = {float(extent.width - 16), 0, 16, 8};
        commands.push_back(image);
        auto glyph = image;
        glyph.kind = renderer::ui::UiDrawCommandKind::GlyphQuad;
        glyph.rect = {4, 4, 6, 8};
        glyph.textureResourceId = font->atlas().id();
        glyph.fontToken = font->fontToken();
        glyph.hasAtlasPageIndex = true;
        glyph.atlasPageIndex = font->page();
        const auto& atlas = *font->atlas().get();
        const auto& slice = *atlas.slice(font->glyph(65)->sliceName);
        glyph.textureSourceRect = {float(slice.x) / atlas.image().width,
            float(slice.y) / atlas.image().height,
            float(slice.width) / atlas.image().width,
            float(slice.height) / atlas.image().height};
        commands.push_back(glyph);
        auto depthImage = fullscreen;
        depthImage.rect = {float(extent.width - 16), float(extent.height - 16), 16, 16};
        depthImage.textureResourceId = "depth-diagnostic";
        commands.push_back(depthImage);
        core::requireSuccessfulResult(
            gui->addPass(graph, target, commands, "image", 0, LoadOp::Clear),
            "BASIC-FORWARD-OVERLAY");
        if (destroy)
            destroy->reset();
    }
};

class BasicForwardSmoke final {
public:
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    int frames = 0;
    bool run(core::UniquePtr<WebGlDevice> device, double (*nextFrame)())
    try {
        ProductPerformance performance(*device);
        shader::WebGlShaderLoadBudget load{
            {4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32}, 256, 32, 1024};
        auto aq = core::makeShared<core::CpuMemoryQuota>(load.original.maxCpuMetadataBytes);
        auto basic = shader::WebGlShaderProgramArtifact::load(
            "/artifacts/basic_forward.webgl-program.yaml", load, aq, diagnostic);
        auto overlay = shader::WebGlShaderProgramArtifact::load(
            "/artifacts/gui_bitmap.webgl-program.yaml", load, aq, diagnostic);
        auto depthArtifact = shader::WebGlShaderProgramArtifact::load(
            "/artifacts/basic_depth_receiver.webgl-program.yaml", load, aq, diagnostic);
        if (!basic || !overlay || !depthArtifact)
            throw core::DiagnosticError(diagnostic);
        ShaderPackageDesc package, guiPackage;
        core::requireSuccessfulResult(
            populateSelectedGraphicsShaderInterface(
                *basic->vertex().original, basic->fragment().original.get(), package),
            "BASIC-REFLECTION");
        core::requireSuccessfulResult(
            populateSelectedGraphicsShaderInterface(
                *overlay->vertex().original, overlay->fragment().original.get(), guiPackage),
            "GUI-REFLECTION");
        PipelineLayoutDesc ld;
        ld.descriptorLayouts.push_back({"BasicForwardSet",
            {{0, DescriptorType::UniformBuffer, ShaderStage::Vertex},
                {1, DescriptorType::SampledTexture, ShaderStage::Fragment},
                {2, DescriptorType::Sampler, ShaderStage::Fragment}}});
        ld.descriptorSets.push_back({0, "BasicForwardSet"});
        ld.pushConstantRanges.push_back({ShaderStage::Fragment, 0, 80});
        auto layout = core::shareUnique(device->createPipelineLayout(ld));
        const char* id = nullptr;
        if (!layout || (id = device->selectPipelineInputs(layout, basic, {1, {1, 1024, 4096}, {}})))
            throw core::DiagnosticError(id ? id : device->diagnostic());
        std::array<std::shared_ptr<RenderPipeline>, 4> variants;
        for (unsigned i = 0; i < 4; ++i) {
            RenderPipelineDesc p;
            p.layout = layout.get();
            p.vertexShader = package.entrypoints[0];
            p.fragmentShader = package.entrypoints[1];
            p.colorFormat = TextureFormat::Rgba8Unorm;
            p.depthFormat = TextureFormat::Depth24Stencil8;
            p.depthStencil.format = p.depthFormat;
            p.depthStencil.depthTestEnabled = true;
            p.depthStencil.depthWriteEnabled = i < 2;
            p.depthStencil.depthCompare = CompareOp::LessEqual;
            p.cullMode = i % 2 ? CullMode::None : CullMode::Back;
            p.frontFace = FrontFace::CounterClockwise;
            p.dynamicViewportScissor = true;
            ColorTargetState color;
            color.format = p.colorFormat;
            color.blendEnabled = i >= 2;
            color.colorBlend =
                color.alphaBlend = {BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add};
            p.colorTargets.push_back(color);
            VertexBufferLayoutDesc stream;
            stream.strideBytes = sizeof(assets::MeshVertex);
            stream.attributes = {{0, 0, VertexFormat::Float32x3},
                {1, 12, VertexFormat::Float32x3},
                {2, 24, VertexFormat::Float32x2}};
            p.vertexBuffers.push_back(std::move(stream));
            variants[i] = core::shareUnique(device->createRenderPipeline(p));
            if (!variants[i])
                throw core::DiagnosticError(device->diagnostic());
        }
        PipelineLayoutDesc gd;
        gd.descriptorLayouts.push_back({"GuiBitmapSet",
            {{0, DescriptorType::SampledTexture, ShaderStage::Fragment},
                {1, DescriptorType::Sampler, ShaderStage::Fragment}}});
        gd.descriptorSets.push_back({0, "GuiBitmapSet"});
        gd.pushConstantRanges.push_back({ShaderStage::Vertex, 0, 64});
        auto guiLayout = core::shareUnique(device->createPipelineLayout(gd));
        if (!guiLayout
            || (id = device->selectPipelineInputs(guiLayout, overlay, {1, {1, 1024, 4096}, {}})))
            throw core::DiagnosticError(id ? id : device->diagnostic());
        RenderPipelineDesc gp;
        gp.layout = guiLayout.get();
        gp.vertexShader = guiPackage.entrypoints[0];
        gp.fragmentShader = guiPackage.entrypoints[1];
        gp.colorFormat = TextureFormat::Rgba8Unorm;
        gp.dynamicViewportScissor = true;
        ColorTargetState gc;
        gc.format = gp.colorFormat;
        gc.blendEnabled = true;
        gc.colorBlend =
            gc.alphaBlend = {BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add};
        gp.colorTargets.push_back(gc);
        VertexBufferLayoutDesc gs;
        gs.strideBytes = 8;
        gs.attributes = {{0, 0, VertexFormat::Float32x2}};
        gp.vertexBuffers.push_back(std::move(gs));
        auto guiPipeline = core::shareUnique(device->createRenderPipeline(gp));
        if (!guiPipeline)
            throw core::DiagnosticError(device->diagnostic());
        ShaderPackageDesc depthPackage;
        core::requireSuccessfulResult(
            populateSelectedGraphicsShaderInterface(*depthArtifact->vertex().original,
                depthArtifact->fragment().original.get(),
                depthPackage),
            "BASIC-DEPTH-REFLECTION");
        auto depthLayoutDesc = ld;
        depthLayoutDesc.pushConstantRanges = {{ShaderStage::Vertex, 0, 16}};
        auto depthLayout = core::shareUnique(device->createPipelineLayout(depthLayoutDesc));
        if (!depthLayout
            || (id = device->selectPipelineInputs(
                    depthLayout, depthArtifact, {1, {1, 1024, 4096}, {}})))
            throw core::DiagnosticError(id ? id : device->diagnostic());
        RenderPipelineDesc dp;
        dp.layout = depthLayout.get();
        dp.vertexShader = depthPackage.entrypoints[0];
        dp.fragmentShader = depthPackage.entrypoints[1];
        dp.colorFormat = TextureFormat::Rgba8Unorm;
        dp.dynamicViewportScissor = true;
        dp.cullMode = CullMode::None;
        dp.colorTargets.push_back({TextureFormat::Rgba8Unorm});
        VertexBufferLayoutDesc ds;
        ds.strideBytes = sizeof(assets::MeshVertex);
        ds.attributes = {{0, 0, VertexFormat::Float32x3}};
        dp.vertexBuffers.push_back(std::move(ds));
        auto depthPipeline = core::shareUnique(device->createRenderPipeline(dp));
        if (!depthPipeline)
            throw core::DiagnosticError(device->diagnostic());
        OrderedGraphicsExecutionRequest req;
        req.contractVersion = 1;
        req.prototypeOptIn = true;
        req.maxSubmissions = performance.submissionBudget(16);
        req.maxCpuMetadataBytes = 512 * 1024;
        req.requirements = {1, 1, 64, 64, 3, 16, 20, 0, 1};
        auto execution = core::shareUnique(device->createOrderedGraphicsExecution(
            req, core::makeShared<core::CpuMemoryQuota>(req.maxCpuMetadataBytes), id));
        if (!execution || (id = device->enableSurfaceLifecycle({1, true, 8})))
            throw core::DiagnosticError(id);
        platform::RenderSystemOrderedGraphicsDesc pd;
        pd.contractVersion = 1;
        pd.prototypeOptIn = true;
        pd.surface = {"#canvas", {64, 64}, TextureFormat::Rgba8Unorm, 0};
        pd.commands = {
            4, true, 9, 4096, 24, {1, true, 32, 192, 160, 1024 * 1024, 512}, 6, 3, 3, 32};
        pd.maxFrameCpuMetadataBytes = 4 * 1024 * 1024;
        pd.frameQuota = core::makeShared<core::CpuMemoryQuota>(pd.maxFrameCpuMetadataBytes);
        pd.commandQuota =
            core::makeShared<core::CpuMemoryQuota>(pd.commands.issue.maxCpuMetadataBytes);
        auto path =
            platform::RenderSystem::createOrderedGraphicsPath(std::move(device), execution, pd, id);
        if (!path)
            throw core::DiagnosticError(id);
        auto& gpu = path->device();
        resource::ResourceManager resources;
        if (!arp::tests::loadSpriteFixture(resources))
            throw core::DiagnosticError("BASIC-RESOURCE");
        constexpr const char* glyphId = "00000000-0000-4000-8000-000000000006";
        constexpr const char* fontId = "00000000-0000-4000-8000-000000000007";
        core::requireSuccessfulResult(
            resources.load<scene2d::SpriteAtlasResource>({},
                core::pmr::String(glyphId),
                core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/glyph.yaml"),
                core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/glyph.png"),
                65536u),
            "BASIC-GLYPH");
        core::requireSuccessfulResult(
            resources.load<arp::ui::UiBitmapFontResource>({},
                core::pmr::String(fontId),
                core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/glyph.font.yaml"),
                core::pmr::String(glyphId),
                65536u,
                1u),
            "BASIC-FONT");
        const auto& font = *resources.find<arp::ui::UiBitmapFontResource>(fontId).get();
        const auto& atlas =
            resources.find<scene2d::SpriteAtlasResource>(arp::tests::atlasA).get()->image();
        auto image = core::shareUnique(gpu.createTexture({"BasicSource",
            {atlas.width, atlas.height, 1},
            TextureFormat::Rgba8Unorm,
            TextureUsage::Sampled | TextureUsage::CopyDst,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead}));
        if (!image)
            throw core::DiagnosticError("BASIC-TEXTURE");
        semantics::ClientTextureUpload upload;
        upload.contractVersion = 1;
        upload.extent = {atlas.width, atlas.height, 1};
        upload.bytesPerRow = atlas.width * 4;
        upload.bytes = atlas.rgba8;
        core::requireSuccessfulResult(execution->uploadTexture(*image, upload), "BASIC-UPLOAD");
        auto color = core::shareUnique(gpu.createTexture({"BasicColor",
            {64, 64, 1},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment | TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined}));
        auto depth = core::shareUnique(gpu.createTexture({"BasicDepth",
            {64, 64, 1},
            TextureFormat::Depth24Stencil8,
            TextureUsage::RenderAttachment | TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined}));
        if (!color || !depth)
            throw core::DiagnosticError("BASIC-TARGETS");
        auto quota = core::makeShared<core::CpuMemoryQuota>(512 * 1024);
        auto scene = core::makeUnique<renderer::BasicForwardRenderPipeline>(
            renderer::BasicForwardPipelineDesc{1,
                1,
                4,
                4,
                1,
                512 * 1024,
                quota,
                execution,
                variants,
                package.reflection.sourceSha256,
                "00000000-0000-4000-8000-000000000010"});
        core::requireSuccessfulResult(
            scene->create(
                {gpu, TextureFormat::Rgba8Unorm, TextureFormat::Depth24Stencil8, {64, 64}}),
            "BASIC-CREATE");
        auto guiQuota = core::makeShared<core::CpuMemoryQuota>(256 * 1024);
        renderer::GuiOrderedRenderer gui(gpu);
        core::requireSuccessfulResult(gui.create({1,
                                          4,
                                          4,
                                          1,
                                          256 * 1024,
                                          guiQuota,
                                          execution,
                                          guiPipeline,
                                          guiPackage.reflection.sourceSha256,
                                          {1, false, false}}),
            "BASIC-GUI");
        core::requireSuccessfulResult(gui.registerTexture({"image", image}), "BASIC-IMAGE");
        core::requireSuccessfulResult(gui.registerBitmapFont(font), "BASIC-FONT-BIND");
        core::requireSuccessfulResult(gui.registerTexture({"basic-offscreen",
                                          color,
                                          renderer::GuiSampleAlpha::Premultiplied,
                                          true,
                                          false,
                                          true,
                                          {}}),
            "BASIC-OFFSCREEN");
        resource::ShaderPackageResource sm;
        sm.materialInterfaceContract = renderer::kBasicForwardMaterialContract;
        sm.materialInterfaceVersion = 1;
        constexpr const char* shaderId = "00000000-0000-4000-8000-000000000010";
        std::array<resource::MaterialResource, 4> materials;
        for (unsigned i = 0; i < 4; ++i) {
            auto& m = materials[i];
            m.materialContractVersion = 1;
            m.surfaceModel = renderer::kBasicForwardMaterialContract;
            m.shaderPackageId = shaderId;
            m.metallicFactor = 0;
            m.roughnessFactor = 1;
            m.alphaMode = i < 2 ? "Opaque" : "Blend";
            m.doubleSided = i % 2;
            m.baseColorR = m.baseColorG = m.baseColorB = m.baseColorA = 1;
            if (i < 2) {
                m.textureBindingCount = 1;
                m.baseColorTextureId = arp::tests::atlasA;
            }
        }
        materials[0].baseColorR = .8f;
        materials[1].baseColorG = .8f;
        materials[2].baseColorR = materials[2].baseColorG = 0;
        materials[2].baseColorA = .5f;
        materials[3].baseColorA = .25f;
        std::array<renderer::BasicForwardMaterialBinding, 4> bindings;
        for (unsigned i = 0; i < 4; ++i)
            bindings[i] = {&materials[i], core::pmr::String(arp::tests::atlasA), image};
        core::Mat4 camera;
        camera.m = {.5f, .125f, 0, .125f, .0625f, -.5f, 0, .0625f, 0, 0, .5f, .25f, 0, 0, 0, 1};
        const auto inverse = core::inverse(camera);
        const auto product = core::multiply(camera, inverse);
        for (unsigned i = 0; i < 16; ++i)
            if (std::abs(product.m[i] - (i % 5 == 0 ? 1.f : 0.f)) > .00001f)
                throw core::DiagnosticError("BASIC-CAMERA-INVERSE");
        assets::MeshAsset mesh;
        for (unsigned section = 0; section < 4; ++section) {
            for (auto corner : core::QuadTriangleTopology::corners) {
                const float x = section == 0 ? corner.x * .5f : corner.x;
                const float y = section == 2 ? .5f + corner.y * .5f : corner.y;
                const auto world = core::transformPoint(inverse,
                    {x * 2 - 1, y * 2 - 1, std::array<float, 4>{.25f, .75f, .125f, .5f}[section]});
                assets::MeshVertex v;
                v.position[0] = world.x;
                v.position[1] = world.y;
                v.position[2] = world.z;
                v.uv[0] = section == 0 ? .125f : .75f;
                v.uv[1] = .5f;
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                mesh.vertices.push_back(v);
            }
            mesh.submeshes.push_back({section * 6, 6, section});
        }
        renderer::BasicWorldMeshProfile profile{1,
            true,
            renderer::BasicWorldMeshIndexPolicy::Uint16Exact,
            24,
            24,
            4,
            4,
            1,
            24 * sizeof(assets::MeshVertex),
            48};
        renderer::BasicForwardLighting light{
            1, 1, 1, {0, 0, -2}, .5f, {1, 1, 1}, {.25f, .25f, .25f}};
        BasicForwardWorkload workload;
        workload.scene = scene.get();
        workload.gui = &gui;
        workload.offscreen = color;
        workload.font = &font;
        std::copy(variants.begin(), variants.end(), workload.native.begin());
        workload.native[4] = guiPipeline;
        workload.native[5] = depthPipeline;
        core::requireSuccessfulResult(
            scene->registerMesh(mesh, profile, bindings, sm, shaderId, light, workload.meshIds[0]),
            "BASIC-MESH");
        workload.frame.viewProjection = camera;
        workload.frame.depth = depth;
        auto receiver = core::makeShared<BasicDepthDiagnostic>();
        receiver->pipeline = depthPipeline;
        receiver->source = depth;
        receiver->output = core::shareUnique(gpu.createTexture({"BasicDepthDiagnosticColor",
            {64, 64, 1},
            TextureFormat::Rgba8Unorm,
            TextureUsage::RenderAttachment | TextureUsage::Sampled,
            MemoryUsage::GpuOnly,
            ResourceState::Undefined}));
        receiver->camera = core::shareUnique(gpu.createBuffer({"BasicDepthCamera",
            64,
            ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead}));
        receiver->vertices = core::shareUnique(gpu.createBuffer({"BasicDepthWorldMesh",
            6 * sizeof(assets::MeshVertex),
            ResourceUsage::VertexBuffer | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead}));
        if (!receiver->output || !receiver->camera || !receiver->vertices)
            throw core::DiagnosticError("BASIC-DEPTH-RESOURCES");
        core::requireSuccessfulResult(
            gpu.writeBuffer(*receiver->camera, &camera, 64), "BASIC-DEPTH-CAMERA-BYTES");
        core::requireSuccessfulResult(
            gpu.writeBuffer(
                *receiver->vertices, mesh.vertices.data() + 6, 6 * sizeof(assets::MeshVertex)),
            "BASIC-DEPTH-WORLD-BYTES");
        SamplerDesc sampler;
        sampler.minFilter = sampler.magFilter = SamplerFilter::Nearest;
        sampler.mipFilter = SamplerMipFilter::None;
        receiver->sampler = gpu.createSampler(sampler);
        TextureViewDesc depthView;
        depthView.label = "BasicDepthSource";
        depthView.format = TextureFormat::Depth24Stencil8;
        depthView.usage = TextureUsage::Sampled;
        depthView.aspect = TextureAspect::Depth;
        receiver->view = gpu.createTextureView(*depth, depthView);
        receiver->arena = gpu.createDescriptorArena({"BasicDepthDiagnostic", 1, 3});
        if (!receiver->view || !receiver->sampler || !receiver->arena)
            throw core::DiagnosticError("BASIC-DEPTH-BINDINGS");
        receiver->set = receiver->arena->allocateDescriptorSet(
            {"BasicForwardSet", depthLayoutDesc.descriptorLayouts[0]});
        if (!receiver->set)
            throw core::DiagnosticError("BASIC-DEPTH-SET");
        for (const auto& write : {DescriptorBindingWrite{0,
                                      DescriptorType::UniformBuffer,
                                      receiver->camera->id(),
                                      DescriptorResourceKind::Buffer},
                 DescriptorBindingWrite{1,
                     DescriptorType::SampledTexture,
                     receiver->view->id(),
                     DescriptorResourceKind::TextureView},
                 DescriptorBindingWrite{2,
                     DescriptorType::Sampler,
                     receiver->sampler->id(),
                     DescriptorResourceKind::Sampler}})
            core::requireSuccessfulResult(receiver->set->write(write), "BASIC-DEPTH-WRITE");
        workload.receiver = receiver;
        core::requireSuccessfulResult(gui.registerTexture({"depth-diagnostic",
                                          receiver->output,
                                          renderer::GuiSampleAlpha::Premultiplied,
                                          true,
                                          false,
                                          true,
                                          {}}),
            "BASIC-DEPTH-GUI");
        {
            auto cancelledTarget = core::shareUnique(gpu.createTexture({"CancelledBasic",
                {64, 64, 1},
                TextureFormat::Rgba8Unorm,
                TextureUsage::RenderAttachment,
                MemoryUsage::GpuOnly,
                ResourceState::ColorAttachment}));
            if (!cancelledTarget)
                throw core::DiagnosticError("BASIC-CANCEL-TARGET");
            const std::array<OrderedGraphicsTarget, 2> targets{
                {{color.get(), depth.get()}, {cancelledTarget.get(), nullptr}}};
            auto budget = pd.commands;
            budget.contractVersion = 3;
            if (execution->prepare(workload.native, targets, budget, pd.commandQuota, id) || !id
                || std::string_view(id) != "RHI-WEBGL1-PREPARED-PIPELINE-ABI-MISMATCH")
                throw core::DiagnosticError("BASIC-LEGACY-MIXED-ABI-GATE");
            budget.contractVersion = 4;
            budget.maxUniformBytes = 2591;
            if (execution->prepare(workload.native, targets, budget, pd.commandQuota, id) || !id
                || std::string_view(id) != "SHADER-WEBGL1-SNAPSHOT-BUDGET")
                throw core::DiagnosticError("BASIC-MIXED-ABI-UNIFORM-BUDGET");
            budget.maxUniformBytes = 2592;
            {
                auto exact =
                    execution->prepare(workload.native, targets, budget, pd.commandQuota, id);
                if (!exact)
                    throw core::DiagnosticError(id ? id : "BASIC-MIXED-ABI-EXACT-BUDGET");
                RenderPassDesc pass;
                pass.colorAttachments.push_back({{},
                    cancelledTarget->id(),
                    TextureFormat::Rgba8Unorm,
                    LoadOp::Clear,
                    StoreOp::Store});
                auto active = exact->beginRenderPass(pass);
                if (!active || !active->setPipeline(*guiPipeline).success)
                    throw core::DiagnosticError("BASIC-MIXED-ABI-PASS-BEGIN");
                const auto rejected = active->setPipeline(*depthPipeline);
                if (rejected.success
                    || rejected.diagnostics != "RHI-ORDERED-GRAPHICS-ABI-PASS-BOUNDARY-REQUIRED")
                    throw core::DiagnosticError("BASIC-MIXED-ABI-IN-PASS-GATE");
            }
            if (pd.commandQuota->liveBytes())
                throw core::DiagnosticError("BASIC-MIXED-ABI-REFUND");
            render_graph::FrameGraph cancelled(gpu);
            workload.build(cancelled, cancelled.importTextureRef(cancelledTarget));
        }
        if (!scene->frameInputsReady(0))
            throw core::DiagnosticError("BASIC-CANCEL-RESERVATION");
        performance.setupFinished();
        auto render = [&](unsigned w, unsigned h, bool verify = true) {
            core::CpuWorkMeasurementScope inputs(performance.inputs());
            inputs.finish();
            if (!nextFrame())
                throw core::DiagnosticError("BASIC-RAF");
            performance.begin();
            const auto report = path->renderFrame(workload, performance.measurements());
            performance.finish(w, h, pd.frameQuota, pd.commandQuota, workload.validationChecks);
            if (!report.success)
                throw core::DiagnosticError(report.diagnostics);
            if (!report.presented || !report.recycled || report.gpuCompletionClaimed
                || (scene && !scene->frameInputsReady(0)))
                throw core::DiagnosticError("BASIC-FRAME");
            if (!verify) {
                ++frames;
                return;
            }
            std::array<std::uint8_t, 64 * 64 * 4> pixels{};
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (glGetError() != GL_NO_ERROR)
                throw core::DiagnosticError("BASIC-ORACLE-READ");
            const auto& glyph = font.atlas().get()->image();
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; ++x) {
                    const auto top = h - 1 - y;
                    std::array<int, 4> expected{0, 0, 0, 255};
                    const auto texel = (4 * 16 + (x < w / 2 ? 2 : 12)) * 4;
                    for (unsigned c = 0; c < 3; ++c)
                        expected[c] = int(std::lround(atlas.rgba8[texel + c] * .75f
                            * ((x < w / 2 ? c == 0 : c == 1) ? .8f : 1.f)));
                    if (top >= h / 2) {
                        for (unsigned c = 0; c < 3; ++c)
                            expected[c] =
                                int(std::lround((c == 2 ? .375f * 255 : 0) + expected[c] * .5f));
                    }
                    if (x >= w / 2)
                        for (unsigned c = 0; c < 3; ++c)
                            expected[c] = int(std::lround(.1875f * 255 + expected[c] * .75f));
                    if (x >= w - 16 && top < 8) {
                        const auto offset = (top * 16 + x - (w - 16)) * 4;
                        const auto alpha = atlas.rgba8[offset + 3] / 255.f;
                        for (unsigned c = 0; c < 3; ++c)
                            expected[c] =
                                int(std::lround(std::lround(atlas.rgba8[offset + c] * alpha)
                                    + expected[c] * (1 - alpha)));
                    }
                    if (x >= 4 && x < 10 && top >= 4 && top < 12
                        && glyph.rgba8[((top - 4) * 8 + x - 4) * 4 + 3])
                        expected = {255, 255, 255, 255};
                    if (x >= w - 16 && top >= h - 16) {
                        const auto value = x < w - 8 ? 64 : 191;
                        expected = {value, value, value, 255};
                    }
                    for (unsigned c = 0; c < 4; ++c)
                        if (std::abs(int(pixels[(y * w + x) * 4 + c]) - expected[c]) > 2) {
                            std::fprintf(stderr,
                                "BASIC pixel %u %u channel %u actual %u expected %d\n",
                                x,
                                top,
                                c,
                                pixels[(y * w + x) * 4 + c],
                                expected[c]);
                            char detail[192];
                            std::snprintf(detail,
                                sizeof(detail),
                                "BASIC-ORACLE-PIXEL x=%u top=%u c=%u actual=%u expected=%d",
                                x,
                                top,
                                c,
                                pixels[(y * w + x) * 4 + c],
                                expected[c]);
                            throw core::DiagnosticError(detail);
                        }
                }
            ++frames;
        };
        for (unsigned i = 0; i < 4; ++i)
            render(64, 64);
        performance.sample(render, 64, 64, workload.validationChecks);
        core::requireSuccessfulResult(path->resize({48, 32}), "BASIC-RESIZE");
        render(48, 32);
        performance.sample(render, 48, 32, workload.validationChecks);
        core::requireSuccessfulResult(path->setSurfaceAvailable(false), "BASIC-PAUSE");
        if (path->renderFrame(workload).success)
            throw core::DiagnosticError("BASIC-PAUSED-DRAW");
        core::requireSuccessfulResult(path->setSurfaceAvailable(true), "BASIC-RESUME");
        workload.destroy = &scene;
        render(48, 32);
        if (scene || quota->liveBytes())
            throw core::DiagnosticError("BASIC-GRAPH-RETIREMENT");
        if (pd.commandQuota->liveBytes())
            throw core::DiagnosticError("BASIC-COMMAND-RETIREMENT");
        core::requireSuccessfulResult(path->resize({64, 64}), "BASIC-RESTORE-EXTENT");
        return true;
    } catch (const std::exception& e) {
        diagnostic = e.what();
        return false;
    }
};
} // namespace arp::rhi::webgl::test
