#include "3d/scene_3d.h"
#include "platform/render_system.h"
#include "platform/renderdoc_capture_scope.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_common.h"
#include "renderer/shader_package_reflection.h"
#include "renderer/volumetric_fog/volumetric_fog_feature.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_extensions.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

using namespace arp;
namespace fog = renderer::fog;

namespace {
template <class T> void require(const T& result)
{
    if (!result.success)
        throw std::runtime_error(core::toStdString(result.diagnostics));
}

void requireFog(const fog::FogResult& result)
{
    if (!result.success)
        throw std::runtime_error(core::toStdString(result.diagnostic));
}

struct Options {
    bool hidden = false, enabled = true, local = true, noise = true, shadow = true, temporal = true,
         inside = false, homogeneous = false;
    bool prototypeBatch = false;
    bool slits = false;
    bool pointShadow = false;
    std::uint32_t pixels = 0, slices = 0;
    core::pmr::String camera = "toward";
    float motion = 0;
    std::uint32_t frames = 120, captureFrame = 30, seed = 123, resizeFrame = UINT32_MAX,
                  disableFrame = UINT32_MAX;
    float fixedTime = -1;
    fog::FogQuality quality = fog::FogQuality::Medium;
    core::pmr::String backend = "native", capture, report, nativeCapture;
};

Options parse(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const auto value = [&]() {
            if (++i >= argc)
                throw std::runtime_error("missing option value");
            return std::string_view(argv[i]);
        };
        const auto integer = [&]() {
            auto s = value();
            std::uint32_t v = 0;
            auto r = std::from_chars(s.data(), s.data() + s.size(), v);
            if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
                throw std::runtime_error("invalid integer");
            return v;
        };
        const auto real = [&]() {
            const auto s = value();
            float v = 0;
            const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
            if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !std::isfinite(v))
                throw std::runtime_error("invalid real");
            return v;
        };
        if (a == "--hidden")
            o.hidden = true;
        else if (a == "--no-fog")
            o.enabled = false;
        else if (a == "--no-local")
            o.local = false;
        else if (a == "--no-noise")
            o.noise = false;
        else if (a == "--no-shadow")
            o.shadow = false;
        else if (a == "--no-temporal")
            o.temporal = false;
        else if (a == "--point-shadow")
            o.pointShadow = true;
        else if (a == "--camera-inside")
            o.inside = true;
        else if (a == "--homogeneous") {
            o.homogeneous = true;
            o.local = false;
            o.shadow = false;
            o.temporal = false;
        } else if (a == "--slits") {
            o.slits = true;
            o.local = false;
        } else if (a == "--pixels")
            o.pixels = integer();
        else if (a == "--slices")
            o.slices = integer();
        else if (a == "--camera") {
            o.camera = core::pmr::String(value());
            if (o.camera != "toward" && o.camera != "side" && o.camera != "away")
                throw std::runtime_error("invalid camera preset");
        } else if (a == "--motion")
            o.motion = real();
        else if (a == "--resize-frame")
            o.resizeFrame = integer();
        else if (a == "--disable-frame")
            o.disableFrame = integer();
        else if (a == "--frames")
            o.frames = integer();
        else if (a == "--capture-frame")
            o.captureFrame = integer();
        else if (a == "--seed")
            o.seed = integer();
        else if (a == "--prototype-frozen-batch")
            o.prototypeBatch = true;
        else if (a == "--backend")
            o.backend = core::pmr::String(value());
        else if (a == "--capture")
            o.capture = core::pmr::String(value());
        else if (a == "--native-capture")
            o.nativeCapture = core::pmr::String(value());
        else if (a == "--report")
            o.report = core::pmr::String(value());
        else if (a == "--fixed-time") {
            const auto s = value();
            std::size_t consumed = 0;
            o.fixedTime = std::stof(std::string(s), &consumed);
            if (consumed != s.size() || !std::isfinite(o.fixedTime) || o.fixedTime < 0)
                throw std::runtime_error("invalid time");
        } else if (a == "--quality") {
            auto s = value();
            if (s == "low")
                o.quality = fog::FogQuality::Low;
            else if (s == "medium")
                o.quality = fog::FogQuality::Medium;
            else if (s == "high")
                o.quality = fog::FogQuality::High;
            else
                throw std::runtime_error("invalid quality");
        } else
            throw std::runtime_error("unknown fog example option");
    }
    if (o.frames == 0)
        throw std::runtime_error("frames must be positive");
    return o;
}

struct DemoConstants {
    core::Mat4 viewProjection;
    core::Vec4 lightDirection;
};

struct Pipeline {
    rhi::ShaderPackageDesc package;
    rhi::DescriptorLayoutDesc bindings;
    core::UniquePtr<rhi::PipelineLayout> layout;
    core::UniquePtr<rhi::RenderPipeline> pipeline;
    core::UniquePtr<rhi::DescriptorArena> arena;
    std::array<core::UniquePtr<rhi::DescriptorSet>, 2> sets;

    void create(rhi::GpuDevice& device,
        const char* name,
        rhi::Extent2D extent,
        rhi::TextureFormat color,
        rhi::DescriptorLayoutDesc descriptors,
        bool depth,
        bool blend = false)
    {
        bindings = std::move(descriptors);
        package = renderer::detail::makeGraphicsShaderPackage(
            name, core::pmr::String(ARP_FOG_SHADER_DIR) + "/" + name + ".slang", 0);
        if (!renderer::hydrateShaderPackageReflectionFromArtifact(package))
            throw std::runtime_error("demo reflection");
        for (const auto& b : bindings.bindings)
            package.reflection.descriptorBindings.push_back(rhi::makeShaderReflectionBinding(0, b));
        rhi::PipelineLayoutDesc ld;
        ld.descriptorLayouts.push_back(bindings);
        ld.descriptorSets.push_back({0, bindings.name});
        layout = device.createPipelineLayout(ld);
        if (!layout)
            throw std::runtime_error("demo pipeline layout");
        auto pd = renderer::detail::makeRenderPipelineDesc(name, *layout, package, color, extent);
        renderer::useDynamicRasterViewport(pd);
        if (color != rhi::TextureFormat::Unknown)
            pd.colorTargets = {{color}};
        if (depth) {
            pd.depthFormat = rhi::TextureFormat::D32Float;
            pd.depthStencil.format = pd.depthFormat;
            pd.depthStencil.depthTestEnabled = true;
            pd.depthStencil.depthWriteEnabled = !blend;
            pd.depthStencil.depthCompare = rhi::CompareOp::LessEqual;
        }
        if (blend) {
            auto& target = pd.colorTargets.front();
            target.blendEnabled = true;
            target.colorBlend = {
                rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha, rhi::BlendOp::Add};
            target.alphaBlend = {
                rhi::BlendFactor::One, rhi::BlendFactor::OneMinusSrcAlpha, rhi::BlendOp::Add};
        }
        pipeline = device.createRenderPipeline(pd);
        if (!pipeline)
            throw std::runtime_error("demo graphics pipeline");
        arena =
            device.createDescriptorArena({name, 2u, std::uint32_t(bindings.bindings.size()) * 2u});
        if (!arena)
            throw std::runtime_error("demo descriptor arena");
        for (auto& set : sets) {
            set = arena->allocateDescriptorSet({name, bindings});
            if (!set)
                throw std::runtime_error("demo descriptor");
        }
    }
};

struct Flight final : render_graph::PassSubmissionObserver {
    core::UniquePtr<render_graph::FrameGraph> graph;
    std::shared_ptr<const rhi::GpuFence> completion;
    core::UniquePtr<rhi::GpuTextureView> colorView;

    void onSubmitted(render_graph::PassSubmissionToken,
        const std::shared_ptr<const rhi::GpuFence>& fence) noexcept override
    {
        completion = fence;
    }

    bool ready() const { return !completion || completion->isSignaled(); }
};

int run(Options o)
{
    platform::WindowSystem windows;
    core::UniquePtr<rhi::GpuDevice> nullDevice;
    core::UniquePtr<platform::RenderSystem> renderSystem;
    platform::PlatformWindow window;
    rhi::GpuDevice* device = nullptr;
    rhi::Swapchain* swapchain = nullptr;
    if (o.backend == "null") {
        nullDevice = rhi::createNullDevice();
        device = nullDevice.get();
    } else {
        require(windows.initialize());
        platform::WindowDesc wd;
        wd.title = "ARP Volumetric Fog";
        wd.width = 640;
        wd.height = 360;
        wd.hidden = o.hidden;
        window = windows.createWindow(wd);
        if (!window.valid())
            throw std::runtime_error("demo window");
        renderSystem = core::makeUnique<platform::RenderSystem>(
            platform::createDefaultRenderSystem(windows, window, "ARP Volumetric Fog"));
        if (!renderSystem->valid())
            throw std::runtime_error(core::toStdString(renderSystem->diagnostics()));
        device = renderSystem->gpuDevice();
        swapchain = renderSystem->swapchain();
        const auto actual = device->capabilities().backend;
        if ((o.backend == "vulkan" && actual != rhi::Backend::Vulkan)
            || (o.backend == "metal" && actual != rhi::Backend::Metal)
            || (o.backend == "webgpu" && actual != rhi::Backend::WebGPU))
            throw std::runtime_error("requested backend unavailable");
        if (o.backend != "native" && o.backend != "vulkan" && o.backend != "metal"
            && o.backend != "webgpu")
            throw std::runtime_error("unknown backend");
    }
    rhi::Extent2D extent{640, 360};
    if (swapchain)
        extent = renderSystem->extent();
    fog::FogConfig config;
    config.distance = 100;
    config.quality = o.quality;
    config.temporal = o.temporal;
    config.stratifiedShadows = !o.pointShadow;
    config.pixelsPerCell = o.pixels;
    config.depthSlices = o.slices;
    fog::VolumetricFogFeature feature;
    if (o.enabled)
        requireFog(feature.create({*device, extent, config, ARP_FOG_SHADER_DIR}));
    scene::Scene authored;
    auto& environment = authored.createObject("Fog environment");
    scene3d::VolumetricFogDesc fd;
    fd.density = 0.018f;
    fd.heightFalloff = 0.2f;
    fd.baseHeight = 1;
    fd.anisotropy = 0.45f;
    if (o.homogeneous) {
        fd.density = 0.04f;
        fd.heightFalloff = 0;
        fd.scatteringColor = {0.6f, 0.7f, 0.8f};
        fd.anisotropy = 0;
    }
    if (o.slits) {
        fd.density = 0.025f;
        fd.heightFalloff = 0;
        fd.scatteringColor = {1, 1, 1};
        fd.anisotropy = 0.18f;
    }
    environment.addComponent<scene3d::VolumetricFogComponent>(fd);
    for (std::uint32_t i = 0; i < 2 && o.local; ++i) {
        auto& region = authored.createObject(i == 0 ? "Valley box" : "Ellipsoid fog");
        scene3d::LocalFogVolumeDesc ld;
        ld.shape = i == 0 ? scene3d::FogVolumeShape::Box : scene3d::FogVolumeShape::Sphere;
        ld.density = 0.08f;
        ld.noiseSeed = o.seed + i;
        ld.noiseStrength = o.noise ? 0.8f : 0;
        region.addComponent<scene3d::LocalFogVolumeComponent>(ld);
        scene::Transform t;
        t.translation = {i == 0 ? -3.0f : 9.0f, 3, 25};
        t.scale = {16, 4, 20};
        region.set_local_transform(t);
    }
    auto& sunlight = authored.createObject("Sun");
    scene3d::LightComponentDesc light;
    light.type = scene3d::LightType::Directional;
    light.direction = core::normalize(core::Vec3{-25, -45, -20});
    if (o.slits)
        light.direction = core::normalize(core::Vec3{0, -0.35f, -1});
    light.intensity = 12;
    light.castShadow = o.shadow;
    sunlight.addComponent<scene3d::LightComponent>(light);
    const auto extracted =
        scene3d::Scene3DRenderExtractor{}.extract(authored, authored.extractTraversalSnapshot({}));
    if (!extracted.ok())
        throw std::runtime_error(core::toStdString(extracted.diagnostic().message));
    const auto& scene = extracted.queue();
    auto eye = o.inside ? core::Vec3{0, 3, 20} : core::Vec3{0, 5, -18};
    auto target = core::Vec3{0, 4, 40};
    if (o.slits) {
        eye = o.camera == "side" ? core::Vec3{24, 4, 0}
            : o.camera == "away" ? core::Vec3{0, 4, 18}
                                 : core::Vec3{0, 4, -18};
        target = o.camera == "side" ? core::Vec3{0, 3, 4}
            : o.camera == "away"    ? core::Vec3{0, 3, -10}
                                    : core::Vec3{0, 3, 12};
    }
    const auto initialEye = eye;
    auto view = core::lookAt(eye, target);
    auto projection = core::perspective(60, float(extent.width) / extent.height, 0.1f, 180);
    rhi::semantics::applyProjectionYConvention(projection.m.data(), device->backend());
    auto lightVP =
        core::multiply(core::orthographic(48, 48, 1, 140), core::lookAt({25, 45, 45}, {0, 0, 25}));
    if (o.slits)
        lightVP = core::multiply(
            core::orthographic(100, 100, 0.1f, 200), core::lookAt({0, 35, 100}, {0, 0, 0}));
    rhi::semantics::applyViewProjectionYConvention(lightVP.m.data(), device->backend());
    const std::uint32_t shadowSize = o.slits ? 2048u : 1024u;
    const std::uint32_t vertexCount = o.slits ? 5u * 36u : 6144u + 32u * 36u;
    DemoConstants sceneConstants{core::multiply(projection, view),
        {light.direction.x, light.direction.y, light.direction.z, o.slits ? 1.0f : 0.0f}};
    DemoConstants shadowConstants{lightVP, sceneConstants.lightDirection};
    auto sceneUbo = device->createBuffer({"Demo.Camera",
        sizeof(DemoConstants),
        rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu});
    auto shadowUbo = device->createBuffer({"Demo.Sun",
        sizeof(DemoConstants),
        rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu});
    if (!sceneUbo || !shadowUbo)
        throw std::runtime_error("demo uniforms");
    require(device->writeBuffer(*sceneUbo, &sceneConstants, sizeof(sceneConstants)));
    require(device->writeBuffer(*shadowUbo, &shadowConstants, sizeof(shadowConstants)));
    using D = rhi::DescriptorType;
    using S = rhi::ShaderStage;
    Pipeline opaque, shadow, glass, plainGlass, present;
    opaque.create(*device,
        "fog_demo_scene",
        extent,
        rhi::TextureFormat::Rgba16Float,
        {"Scene", {{0, D::UniformBuffer, S::Vertex | S::Fragment}}},
        true);
    shadow.create(*device,
        "fog_demo_shadow",
        {shadowSize, shadowSize},
        rhi::TextureFormat::Unknown,
        {"Shadow", {{0, D::UniformBuffer, S::Vertex}}},
        true);
    glass.create(*device,
        "fog_demo_glass",
        extent,
        rhi::TextureFormat::Rgba16Float,
        {"Glass",
            {{0, D::UniformBuffer, S::Fragment},
                {1, D::ReadOnlyStorageBuffer, S::Fragment},
                {2, D::UniformBuffer, S::Vertex}}},
        true,
        true);
    plainGlass.create(*device,
        "fog_demo_plain_glass",
        extent,
        rhi::TextureFormat::Rgba16Float,
        {"PlainGlass", {{2, D::UniformBuffer, S::Vertex}}},
        true,
        true);
    if (swapchain)
        present.create(*device,
            "fog_demo_present",
            extent,
            renderSystem->colorFormat(),
            {"Present", {{0, D::SampledTexture, S::Fragment}}},
            false);
    for (std::uint32_t i = 0; i < 2; ++i) {
        require(opaque.sets[i]->write(
            {0, D::UniformBuffer, sceneUbo->id(), rhi::DescriptorResourceKind::Buffer}));
        require(shadow.sets[i]->write(
            {0, D::UniformBuffer, shadowUbo->id(), rhi::DescriptorResourceKind::Buffer}));
        require(glass.sets[i]->write(
            {2, D::UniformBuffer, sceneUbo->id(), rhi::DescriptorResourceKind::Buffer}));
        require(plainGlass.sets[i]->write(
            {2, D::UniformBuffer, sceneUbo->id(), rhi::DescriptorResourceKind::Buffer}));
    }
    std::array<Flight, 2> flights;
    platform::RenderDocCaptureScope renderdoc;
    bool capturing = false;
    std::uint32_t historyFrames = 0;
    for (std::uint32_t frame = 0; frame < o.frames;) {
        platform::WindowEventBatch events;
        if (swapchain)
            windows.pumpEvents(events);
        for (const auto& event : events.windowEvents)
            if (event.type == platform::WindowEventType::CloseRequested)
                frame = o.frames;
        if (frame >= o.frames)
            break;
        if (frame == o.resizeFrame || frame == o.disableFrame) {
            bool drained = true;
            for (const auto& f : flights)
                drained = drained && f.ready();
            if (!drained) {
                std::this_thread::yield();
                continue;
            }
            for (auto& f : flights) {
                f.graph.reset();
                f.colorView.reset();
            }
            if (frame == o.disableFrame) {
                feature.release();
                o.enabled = false;
                o.disableFrame = UINT32_MAX;
            }
            if (frame == o.resizeFrame) {
                extent = {800, 450};
                if (swapchain) {
                    window.resize(800, 450);
                    require(window.sync());
                    require(renderSystem->resize(extent));
                }
                if (o.enabled)
                    requireFog(feature.resize(extent));
                projection = core::perspective(60, float(extent.width) / extent.height, 0.1f, 180);
                rhi::semantics::applyProjectionYConvention(projection.m.data(), device->backend());
                sceneConstants.viewProjection = core::multiply(projection, view);
                require(device->writeBuffer(*sceneUbo, &sceneConstants, sizeof(sceneConstants)));
                o.resizeFrame = UINT32_MAX;
            }
        }
        auto& flight = flights[frame % 2];
        if (!flight.ready()) {
#if defined(__EMSCRIPTEN__)
            emscripten_sleep(1);
#else
            std::this_thread::yield();
#endif
            continue;
        }
        flight.graph.reset();
        flight.colorView.reset();
        if (o.motion != 0) {
            // The camera UBO is shared by both flights; retire both before writing it.
            if (!flights[0].ready() || !flights[1].ready()) {
                std::this_thread::yield();
                continue;
            }
            eye = initialEye + core::Vec3{o.motion * float(frame), 0, 0};
            view = core::lookAt(eye, target);
            sceneConstants.viewProjection = core::multiply(projection, view);
            require(device->writeBuffer(*sceneUbo, &sceneConstants, sizeof(sceneConstants)));
        }
        flight.graph = core::makeUnique<render_graph::FrameGraph>(*device);
        auto& graph = *flight.graph;
        if (o.prototypeBatch) {
            rhi::CommandEncoderDesc recording{"FogPrototypeFrozenBatch"};
            // Standalone validation workload bounds, independently authored.
            recording.prototypeBatch =
                rhi::PrototypeCommandBatchBudget{rhi::PrototypeCommandBatchContract::version,
                    true,
                    512,
                    8u * 1024u * 1024u,
                    256u * 1024u * 1024u,
                    10000000000ull};
            graph.setCommandEncoderDesc(std::move(recording));
        }
        const auto color = graph.createTransientTexture({"Demo.Hdr",
            {extent.width, extent.height, 1},
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled
                | rhi::TextureUsage::CopySrc,
            rhi::MemoryUsage::GpuOnly});
        const auto depth = graph.createTransientTexture({"Demo.Depth",
            {extent.width, extent.height, 1},
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::GpuOnly});
        auto shadowDepth = graph.createTransientTexture({"Demo.Shadow",
            {shadowSize, shadowSize, 1},
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::GpuOnly});
        const auto slot = frame % 2;
        if (o.shadow)
            graph.addPass("Demo.Shadow")
                .writeAttachment(shadowDepth,
                    rhi::ResourceState::DepthAttachment,
                    rhi::LoadOp::Clear,
                    rhi::StoreOp::Store)
                .executeRenderPass(
                    [&shadow, slot, shadowSize, vertexCount](render_graph::RenderPassContext& c) {
                        require(renderer::bindRasterPipeline(
                            c.renderPass(), *shadow.pipeline, {shadowSize, shadowSize}));
                        require(c.renderPass().bindDescriptorSet(0, *shadow.sets[slot]));
                        require(c.renderPass().draw(vertexCount, 1, 0, 0));
                    });
        graph.addPass("Demo.Opaque")
            .writeAttachment(color,
                rhi::ResourceState::ColorAttachment,
                rhi::LoadOp::Clear,
                rhi::StoreOp::Store,
                {},
                o.slits ? rhi::Color{0.015f, 0.02f, 0.025f, 1} : rhi::Color{0.3f, 0.5f, 0.8f, 1})
            .writeAttachment(
                depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
            .executeRenderPass(
                [&opaque, slot, extent, vertexCount](render_graph::RenderPassContext& c) {
                    require(renderer::bindRasterPipeline(c.renderPass(), *opaque.pipeline, extent));
                    require(c.renderPass().bindDescriptorSet(0, *opaque.sets[slot]));
                    require(c.renderPass().draw(vertexCount, 1, 0, 0));
                });
        fog::FogOutputs fogOutput;
        auto finalColor = color;
        if (o.enabled) {
            fog::FogGraphContext fc{graph, scene, color, depth};
            fc.view = view;
            fc.projection = projection;
            fc.cameraPosition = eye;
            fc.time = o.fixedTime >= 0 ? o.fixedTime : float(frame) / 60;
            fc.primaryLight = scene.lights().front();
            if (o.shadow) {
                fc.shadow.depth = shadowDepth;
                fc.shadow.cascadeCount = 1;
                fc.shadow.matrices[0] = lightVP;
                fc.shadow.uvRects[0] = {0, 0, 1, 1};
                fc.shadow.cascadeEnds[0] = 100;
            }
            const auto result = feature.contributeGraph(fc, fogOutput);
            if (!result.success && result.diagnostic == "FOG-FRAMES-IN-FLIGHT")
                continue;
            requireFog(result);
            historyFrames += fogOutput.historyUsed ? 1u : 0u;
            finalColor = fogOutput.color;
            if (!o.slits)
                graph.addPass("Demo.Transparent")
                    .read(fogOutput.constants, rhi::ResourceState::ShaderRead)
                    .read(fogOutput.integrated, rhi::ResourceState::ShaderRead)
                    .writeAttachment(finalColor,
                        rhi::ResourceState::ColorAttachment,
                        rhi::LoadOp::Load,
                        rhi::StoreOp::Store)
                    .writeAttachment(depth,
                        rhi::ResourceState::DepthAttachment,
                        rhi::LoadOp::Load,
                        rhi::StoreOp::Store)
                    .executeRenderPass([&glass, &graph, fogOutput, slot, extent](
                                           render_graph::RenderPassContext& c) {
                        require(glass.sets[slot]->write({0,
                            D::UniformBuffer,
                            graph.buffer(fogOutput.constants).id(),
                            rhi::DescriptorResourceKind::Buffer}));
                        require(glass.sets[slot]->write({1,
                            D::ReadOnlyStorageBuffer,
                            graph.buffer(fogOutput.integrated).id(),
                            rhi::DescriptorResourceKind::Buffer}));
                        require(
                            renderer::bindRasterPipeline(c.renderPass(), *glass.pipeline, extent));
                        require(c.renderPass().bindDescriptorSet(0, *glass.sets[slot]));
                        require(c.renderPass().draw(6, 1, 0, 0));
                    });
        } else if (!o.slits) {
            graph.addPass("Demo.TransparentWithoutFog")
                .writeAttachment(finalColor,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::Load,
                    rhi::StoreOp::Store)
                .writeAttachment(depth,
                    rhi::ResourceState::DepthAttachment,
                    rhi::LoadOp::Load,
                    rhi::StoreOp::Store)
                .executeRenderPass([&plainGlass, slot, extent](render_graph::RenderPassContext& c) {
                    require(
                        renderer::bindRasterPipeline(c.renderPass(), *plainGlass.pipeline, extent));
                    require(c.renderPass().bindDescriptorSet(0, *plainGlass.sets[slot]));
                    require(c.renderPass().draw(6, 1, 0, 0));
                });
        }
        rhi::AcquiredFrame acquired;
        if (swapchain) {
            acquired = swapchain->acquireNextFrame();
            require(acquired);
            auto image = swapchain->acquiredImage(acquired);
            require(image);
            const auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
            graph.addPass("Demo.TonemapPresent")
                .read(finalColor, rhi::ResourceState::ShaderRead)
                .writeAttachment(target,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::DontCare,
                    rhi::StoreOp::Store)
                .onSubmitted(flight)
                .executeRenderPass([&present, &flight, &graph, finalColor, slot, extent](
                                       render_graph::RenderPassContext& c) {
                    auto& texture = graph.texture(finalColor);
                    flight.colorView = c.device().createTextureView(texture,
                        {"Demo.PresentInput", texture.desc().format, rhi::TextureUsage::Sampled});
                    if (!flight.colorView)
                        throw std::runtime_error("present view");
                    require(present.sets[slot]->write({0,
                        D::SampledTexture,
                        flight.colorView->id(),
                        rhi::DescriptorResourceKind::TextureView}));
                    require(
                        renderer::bindRasterPipeline(c.renderPass(), *present.pipeline, extent));
                    require(c.renderPass().bindDescriptorSet(0, *present.sets[slot]));
                    require(c.renderPass().draw(3, 1, 0, 0));
                });
        } else
            graph.addPass("Demo.NullConsume")
                .read(finalColor, rhi::ResourceState::ShaderRead)
                .externalSideEffects()
                .onSubmitted(flight)
                .execute([](render_graph::RenderContext&) {});
        if (frame == o.captureFrame) {
            if (!o.capture.empty()) {
                capturing = renderdoc.begin(o.capture);
                if (!capturing)
                    throw std::runtime_error("RENDERDOC-INJECTION-UNAVAILABLE");
            }
            if (!o.nativeCapture.empty()) {
                const auto cap = device->beginFrameCapture(
                    {rhi::FrameCaptureTool::NativeGpuTrace, o.nativeCapture});
                if (!cap.success())
                    throw std::runtime_error(core::toStdString(cap.diagnostics));
            }
        }
        graph.execute();
        if (swapchain)
            require(swapchain->present(acquired, {flight.completion}));
        if (frame == o.captureFrame) {
            if (capturing && !renderdoc.end())
                throw std::runtime_error("RENDERDOC-END-FAILED");
            if (!o.nativeCapture.empty() && !device->endFrameCapture().success())
                throw std::runtime_error("NATIVE-CAPTURE-END-FAILED");
        }
        ++frame;
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(1);
#endif
    }
    device->waitIdle();
    if (!o.report.empty()) {
        std::ofstream out(o.report.c_str());
        out << "type: VolumetricFogEvidence\ncontractVersion: 1\nbackend: "
            << int(device->capabilities().backend) << "\nframes: " << o.frames
            << "\nhistoryFrames: " << historyFrames
            << "\ngpuBudgetBytes: " << feature.grid().budgetBytes
            << "\nvalidatorOnly: " << (o.backend == "null" ? "true" : "false") << "\n";
        if (!out)
            throw std::runtime_error("report write");
    }
    for (auto& f : flights)
        f.graph.reset();
    std::cout << "VolumetricFog/1 frames=" << o.frames << " history=" << historyFrames
              << " bytes=" << feature.grid().budgetBytes << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    try {
        return run(parse(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "volumetric_fog: " << error.what() << '\n';
        return 1;
    }
}
