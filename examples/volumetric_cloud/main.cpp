#include "core/diagnostics/frame_time_statistics.h"
#include "core/io/file_io.h"
#include "platform/render_system.h"
#include "platform/renderdoc_capture_scope.h"
#include "renderer/raster_viewport.h"
#include "renderer/renderer_common.h"
#include "renderer/volumetric_cloud/volumetric_cloud_feature.h"
#include "resource/cloud_asset_cooked.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace arp;
namespace cloud = renderer::cloud;

namespace {
template <class T> void require(const T& result)
{
    if (!result.success)
        throw std::runtime_error(core::toStdString(result.diagnostics));
}

void requireCloud(const cloud::CloudResult& result)
{
    if (!result.success)
        throw std::runtime_error(result.diagnostic);
}

struct Options {
    core::pmr::String asset, capture, backend = "native", camera = "below";
    unsigned frames = 40u, captureFrame = 20u, divisor = 1u;
    unsigned width = 640u, height = 360u, warmup = 8u;
    float coverage = 1.0f, extinction = .004f;
    bool hidden = false, temporal = false, timings = false;
};

Options parse(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view key = argv[i];
        const auto value = [&]() -> std::string_view {
            if (++i >= argc)
                throw std::runtime_error("missing option value");
            return argv[i];
        };
        const auto integer = [&]() {
            const auto text = value();
            unsigned result = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                throw std::runtime_error("invalid integer");
            return result;
        };
        const auto scalar = [&]() {
            const auto text = value();
            float result = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                throw std::runtime_error("invalid scalar");
            return result;
        };
        if (key == "--asset")
            o.asset = value();
        else if (key == "--capture")
            o.capture = value();
        else if (key == "--backend")
            o.backend = value();
        else if (key == "--camera")
            o.camera = value();
        else if (key == "--frames")
            o.frames = integer();
        else if (key == "--capture-frame")
            o.captureFrame = integer();
        else if (key == "--divisor")
            o.divisor = integer();
        else if (key == "--width")
            o.width = integer();
        else if (key == "--height")
            o.height = integer();
        else if (key == "--warmup")
            o.warmup = integer();
        else if (key == "--coverage")
            o.coverage = scalar();
        else if (key == "--extinction")
            o.extinction = scalar();
        else if (key == "--hidden")
            o.hidden = true;
        else if (key == "--temporal")
            o.temporal = true;
        else if (key == "--timings")
            o.timings = true;
        else
            throw std::runtime_error("unknown cloud example option");
    }
    if (o.asset.empty() || !o.frames || !o.divisor || !o.width || !o.height
        || (o.camera != "below" && o.camera != "inside" && o.camera != "above"))
        throw std::runtime_error(
            "requires --asset cooked.yaml; camera below|inside|above; positive frames/divisor");
    return o;
}

struct Flight final : render_graph::PassSubmissionObserver {
    core::UniquePtr<render_graph::FrameGraph> graph;
    std::shared_ptr<const rhi::GpuFence> completion;
    core::UniquePtr<rhi::GpuTextureView> view;
    core::UniquePtr<rhi::DescriptorSet> set;

    void onSubmitted(render_graph::PassSubmissionToken,
        const std::shared_ptr<const rhi::GpuFence>& value) noexcept override
    {
        completion = value;
    }

    bool ready() const { return !completion || completion->isSignaled(); }
};

// Declared after graphs, so exception unwinding drains before graph destruction.
// This is teardown only, never a frame recording or admission operation.
class DeviceDrain final {
public:
    explicit DeviceDrain(rhi::GpuDevice& device) : device_(&device) {}

    ~DeviceDrain() { complete(); }

    void complete()
    {
        if (device_) {
            device_->waitIdle();
            device_ = nullptr;
        }
    }

private:
    rhi::GpuDevice* device_;
};

int run(const Options& o)
{
    constexpr std::uint64_t maxDocument = 4u * 1024u * 1024u;
    constexpr std::uint64_t maxPayload = 1u * 1024u * 1024u;
    const auto file = core::readTextFile(o.asset.c_str(), maxDocument);
    if (!file.success)
        throw std::runtime_error(core::toStdString(file.diagnostics));
    assets::cloud::CloudAsset asset;
    const auto decoded =
        resource::CloudAssetCookedCodec::decode(file.text, maxDocument, maxPayload, asset);
    if (!decoded.success)
        throw std::runtime_error(decoded.diagnostic);
    platform::WindowSystem windows;
    platform::PlatformWindow window;
    core::UniquePtr<platform::RenderSystem> system;
    core::UniquePtr<rhi::GpuDevice> nullDevice;
    rhi::GpuDevice* device = nullptr;
    rhi::Swapchain* swapchain = nullptr;
    rhi::Extent2D extent{o.width, o.height};
    constexpr std::uint32_t frameSlots = 3u;
    // Three cloud layout/pipeline pairs plus one present pair. Every frame
    // slot has twelve cloud descriptors and one present sampled input.
    const rhi::semantics::DirectObjectBudget directBudget{
        rhi::semantics::kDirectObjectBudgetContractVersion,
        "Cloud.DemoDirectObjects/1",
        cloud::kCloudDirectPipelineEntries + 2u,
        frameSlots * (cloud::kCloudDescriptorElementsPerSlot + 1u)};
    // Explicit demo allowance: at most one maximum-size cooked payload per
    // frame slot. This workload currently uses mapped uploads, so actual
    // staging usage may be zero; the ceiling remains authored and observable.
    const rhi::semantics::UploadStagingBudgetV1 stagingBudget{
        rhi::semantics::kUploadStagingBudgetVersion,
        maxPayload,
        maxPayload,
        frameSlots,
        maxPayload * frameSlots,
        rhi::kTextureUploadRowPitchAlignment};
    if (o.backend == "null") {
        nullDevice = rhi::createNullDevice();
        device = nullDevice.get();
    } else {
        require(windows.initialize());
        platform::WindowDesc d;
        d.title = "ARP Volumetric Cloud";
        d.width = int(extent.width);
        d.height = int(extent.height);
        d.hidden = o.hidden;
        window = windows.createWindow(d);
        if (!window.valid())
            throw std::runtime_error("CLOUD-DEMO-WINDOW");
        system =
            core::makeUnique<platform::RenderSystem>(platform::createDefaultRenderSystem(windows,
                window,
                "ARP Volumetric Cloud",
                rhi::PresentationMode::Auto,
                &stagingBudget,
                &directBudget));
        if (!system->valid())
            throw std::runtime_error(core::toStdString(system->diagnostics()));
        device = system->gpuDevice();
        swapchain = system->swapchain();
        extent = system->extent();
        const auto backend = device->backend();
        if ((o.backend == "vulkan" && backend != rhi::Backend::Vulkan)
            || (o.backend == "metal" && backend != rhi::Backend::Metal)
            || (o.backend == "webgpu" && backend != rhi::Backend::WebGPU)
            || (o.backend != "native" && o.backend != "vulkan" && o.backend != "metal"
                && o.backend != "webgpu"))
            throw std::runtime_error("CLOUD-DEMO-REQUESTED-BACKEND-UNAVAILABLE");
    }
    cloud::CloudRenderConfig config;
    if (o.timings && !device->capabilities().features.timestampQueries)
        throw std::runtime_error("CLOUD-DEMO-TIMESTAMP-QUERIES-UNAVAILABLE");
    config.resolutionDivisor = o.divisor;
    config.viewSteps = 64u;
    config.lightSteps = 8u;
    config.framesInFlight = frameSlots;
    config.gpuBudgetBytes = 128u * 1024u * 1024u;
    config.maxDensityEvaluations = 2000000000u;
    config.maxDistance = 60000;
    config.historyWeight = .9f;
    config.historyDepthTolerance = 50;
    config.temporal = o.temporal;
    cloud::VolumetricCloudFeature feature;
    requireCloud(feature.create({*device, extent, config, asset, ARP_CLOUD_SHADER_DIR}));
    scene3d::CloudLayerDesc layer;
    layer.planetCenter = {0, -6371000, 0};
    layer.planetRadius = 6371000;
    layer.baseAltitude = 1500;
    layer.thickness = 2000;
    layer.extinction = o.extinction;
    layer.scatteringAlbedo = {1, .97f, .93f};
    layer.anisotropy = .55f;
    layer.windVelocity = {15, 0, 4};
    layer.weatherPeriod = 28000;
    layer.shapePeriod = 7000;
    layer.detailPeriod = 900;
    layer.coverage = o.coverage;
    layer.erosion = .12f;
    rhi::DescriptorLayoutDesc bindings;
    bindings.name = "Cloud.DemoPresent";
    bindings.bindings = {{0u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment}};
    auto package = renderer::detail::makeGraphicsShaderPackage("Cloud.DemoPresent",
        core::pmr::String(ARP_CLOUD_SHADER_DIR) + "/cloud_demo_present.slang",
        0);
    if (!renderer::hydrateShaderPackageReflectionFromArtifact(package))
        throw std::runtime_error("CLOUD-DEMO-REFLECTION");
    package.reflection.descriptorBindings.push_back(
        rhi::makeShaderReflectionBinding(0u, bindings.bindings[0]));
    rhi::PipelineLayoutDesc ld;
    ld.descriptorLayouts.push_back(bindings);
    ld.descriptorSets.push_back({0u, bindings.name});
    auto layout = device->createPipelineLayout(ld);
    if (!layout)
        throw std::runtime_error("CLOUD-DEMO-PIPELINE-LAYOUT");
    auto pd = renderer::detail::makeRenderPipelineDesc("Cloud.DemoPresent",
        *layout,
        package,
        swapchain ? system->colorFormat() : rhi::TextureFormat::Bgra8Unorm,
        extent);
    renderer::useDynamicRasterViewport(pd);
    auto present = device->createRenderPipeline(pd);
    auto arena = device->createDescriptorArena({"Cloud.DemoDescriptors", frameSlots, frameSlots});
    if (!present || !arena)
        throw std::runtime_error("CLOUD-DEMO-PIPELINE");
    std::array<Flight, frameSlots> flights;
    DeviceDrain drain(*device);
    for (auto& f : flights) {
        f.set = arena->allocateDescriptorSet({bindings.name, bindings});
        if (!f.set)
            throw std::runtime_error("CLOUD-DEMO-DESCRIPTOR");
    }
    platform::RenderDocCaptureScope capture;
    core::FrameTimeStatistics gpuTimes(o.timings ? o.frames : 0);
    std::uint64_t lastTimingSerial = 0;
    const auto collectTiming = [&](unsigned frame) {
        if (!o.timings || frame < o.warmup)
            return;
        const auto sample = device->latestGpuTiming();
        if (sample.availability != rhi::GpuTimingAvailability::Available
            || sample.completedFrameSerial == lastTimingSerial)
            return;
        lastTimingSerial = sample.completedFrameSerial;
        double total = 0;
        unsigned mask = 0;
        for (unsigned i = 0; i < sample.scopeCount; ++i)
            if (sample.scopes[i].scopeId >= 1 && sample.scopes[i].scopeId <= 3) {
                mask |= 1u << (sample.scopes[i].scopeId - 1u);
                total += sample.scopes[i].elapsedMs;
            }
        if (mask == 7u && !gpuTimes.add(total))
            throw std::runtime_error("CLOUD-DEMO-TIMING-SAMPLE-BUDGET");
    };
    unsigned historyFrames = 0;
    for (unsigned frame = 0; frame < o.frames;) {
        platform::WindowEventBatch events;
        if (swapchain)
            windows.pumpEvents(events);
        bool closing = false;
        for (const auto& event : events.windowEvents)
            if (event.type == platform::WindowEventType::CloseRequested)
                closing = true;
        if (closing)
            break;
        auto& flight = flights[frame % flights.size()];
        if (!flight.ready() || !feature.frameReady()) {
            std::this_thread::yield();
            continue;
        }
        flight.view.reset();
        flight.graph = core::makeUnique<render_graph::FrameGraph>(*device);
        auto& graph = *flight.graph;
        if (o.timings)
            graph.setGpuTimingPassScopes({{"Cloud.Raymarch", 1u, true},
                {"Cloud.Temporal", 2u, true},
                {"Cloud.Composite", 3u}});
        const auto color = graph.createTransientTexture({"Cloud.DemoBackground",
            {extent.width, extent.height, 1},
            rhi::TextureFormat::Rgba16Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
        const auto depth = graph.createTransientTexture({"Cloud.DemoDepth",
            {extent.width, extent.height, 1},
            rhi::TextureFormat::D32Float,
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled});
        graph.addPass("Cloud.DemoClear")
            .writeAttachment(color,
                rhi::ResourceState::ColorAttachment,
                rhi::LoadOp::Clear,
                rhi::StoreOp::Store,
                {},
                {.15f, .35f, .65f, 1})
            .writeAttachment(
                depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Clear, rhi::StoreOp::Store)
            .executeRenderPass([](render_graph::RenderPassContext&) {});
        cloud::CloudGraphContext c{graph, color, depth};
        c.layer = layer;
        c.cameraPosition = {0,
            o.camera == "below"        ? 20.0f
                : o.camera == "inside" ? 2400.0f
                                       : 6000.0f,
            -100};
        c.view = core::lookAt(c.cameraPosition,
            c.cameraPosition + core::Vec3{.25f, o.camera == "above" ? -.5f : .25f, 1});
        c.projection = core::perspective(65, float(extent.width) / extent.height, 1, 100000);
        rhi::semantics::applyProjectionYConvention(c.projection.m.data(), device->backend());
        c.sunDirection = core::normalize(core::Vec3{.7f, .5f, .8f});
        c.sunRadiance = {12, 11, 10};
        c.ambientRadiance = {.3f, .4f, .55f};
        c.time = 10.0f; // Fixed camera/time for capture reproducibility.
        cloud::CloudOutputs output;
        requireCloud(feature.contributeGraph(c, output));
        historyFrames += output.historyUsed;
        rhi::AcquiredFrame acquired;
        if (swapchain) {
            acquired = swapchain->acquireNextFrame();
            require(acquired);
            auto image = swapchain->acquiredImage(acquired);
            require(image);
            const auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
            graph.addPass("Cloud.DemoTonemapPresent")
                .read(output.color, rhi::ResourceState::ShaderRead)
                .writeAttachment(target,
                    rhi::ResourceState::ColorAttachment,
                    rhi::LoadOp::DontCare,
                    rhi::StoreOp::Store)
                .onSubmitted(flight)
                .executeRenderPass([&flight, &graph, handle = output.color, &present, extent](
                                       render_graph::RenderPassContext& rc) {
                    auto& texture = graph.texture(handle);
                    flight.view = rc.device().createTextureView(texture,
                        {"Cloud.DemoPresentInput",
                            texture.desc().format,
                            rhi::TextureUsage::Sampled});
                    if (!flight.view)
                        throw std::runtime_error("CLOUD-DEMO-TEXTURE-VIEW");
                    require(flight.set->write({0u,
                        rhi::DescriptorType::SampledTexture,
                        flight.view->id(),
                        rhi::DescriptorResourceKind::TextureView}));
                    require(renderer::bindRasterPipeline(rc.renderPass(), *present, extent));
                    require(rc.renderPass().bindDescriptorSet(0u, *flight.set));
                    require(rc.renderPass().draw(3u, 1u, 0u, 0u));
                });
        } else {
            graph.addPass("Cloud.DemoNullConsume")
                .read(output.color, rhi::ResourceState::ShaderRead)
                .externalSideEffects()
                .onSubmitted(flight)
                .execute([](render_graph::RenderContext&) {});
        }
        const bool capturing = frame == o.captureFrame && !o.capture.empty();
        if (capturing && !capture.begin(o.capture))
            throw std::runtime_error("RENDERDOC-INJECTION-UNAVAILABLE");
        graph.execute();
        if (swapchain)
            require(swapchain->present(acquired, {flight.completion}));
        if (capturing && !capture.end())
            throw std::runtime_error("RENDERDOC-END-FAILED");
        collectTiming(frame);
        ++frame;
    }
    drain.complete();
    collectTiming(o.frames);
    std::cout << "Cloud example frames=" << o.frames << " history=" << historyFrames
              << " retainedBytes=" << feature.plan().retainedGpuBytes << '\n';
    if (swapchain) {
        const auto ledger = device->physicalBudgetLedger();
        const auto& pipelines =
            ledger.category(rhi::semantics::PhysicalBudgetCategory::DirectPipelineObjects);
        const auto& descriptors =
            ledger.category(rhi::semantics::PhysicalBudgetCategory::DirectDescriptorArenas);
        if (!ledger.valid() || !pipelines.observed() || !descriptors.observed()
            || pipelines.capacity != directBudget.pipelineObjectCapacity
            || descriptors.capacity != directBudget.descriptorCapacity)
            throw std::runtime_error("CLOUD-DEMO-DIRECT-BUDGET-UNOBSERVED");
        std::cout << "Cloud direct object budget pipelineEntriesPeak=" << pipelines.peak
                  << " descriptorCapacityPeak=" << descriptors.peak
                  << " liveDescriptorReservations=" << descriptors.liveCount << '\n';
        const auto& staging =
            ledger.category(rhi::semantics::PhysicalBudgetCategory::UploadStaging);
        if (!staging.observed() || staging.capacity != stagingBudget.totalCapacityBytes)
            throw std::runtime_error("CLOUD-DEMO-STAGING-BUDGET-UNOBSERVED");
        std::cout << "Cloud upload staging budget capacityBytes=" << staging.capacity
                  << " peakBytes=" << staging.peak << " liveSlices=" << staging.liveCount << '\n';
    }
    if (o.timings) {
        double median = 0, p95 = 0;
        if (!gpuTimes.percentile(.5, median) || !gpuTimes.percentile(.95, p95))
            throw std::runtime_error("CLOUD-DEMO-TIMING-NO-COMPLETED-SAMPLES");
        const auto memory = device->memoryStats();
        std::cout << "Cloud native timestamps extent=" << extent.width << 'x' << extent.height
                  << " divisor=" << o.divisor << " viewSteps=" << config.viewSteps
                  << " lightSteps=" << config.lightSteps << " observations=" << gpuTimes.count()
                  << " warmupFrames=" << o.warmup << " medianMs=" << median << " p95Ms=" << p95
                  << " devicePeakBytes=" << memory.peakAllocatedBytes << '\n';
    }
    for (auto& flight : flights)
        flight.graph.reset();
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    try {
        return run(parse(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
