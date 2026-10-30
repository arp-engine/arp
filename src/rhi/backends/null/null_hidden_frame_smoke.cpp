#include "null_hidden_frame_smoke.h"

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"

#include <limits>
#include <utility>

namespace arp::rhi {

namespace {

constexpr std::uint32_t kSmokeFrameCount = 2;
constexpr std::uint64_t kSurfaceFrameResourceBaseId = 100000;
constexpr std::uint64_t kSurfaceFrameResourceBytes = 64;

NullHiddenFrameSmokeReport fail(NullHiddenFrameSmokeReport report,
    const std::pmr::string& diagnostics,
    const FrameResourceRecycler& recycler)
{
    report.success = false;
    arp::core::appendDiagnostic(report.diagnostics, diagnostics);
    report.resourceStats = recycler.stats();
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && report.resourceStats.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
        || report.resourceStats.nativeHandleExposedToUpperLayer;
    return report;
}

DescriptorLayoutDesc makeFrameDescriptorLayout()
{
    DescriptorLayoutDesc layout;
    layout.name = "HiddenFrameDescriptors";
    layout.bindings.push_back(
        {0, DescriptorType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment});
    return layout;
}

PipelineLayoutDesc makePipelineLayoutDesc(const DescriptorLayoutDesc& frameLayout)
{
    PipelineLayoutDesc desc;
    desc.descriptorLayouts.push_back(frameLayout);
    desc.descriptorSets.push_back({0, frameLayout.name});
    desc.pushConstantRanges.push_back({ShaderStage::Vertex | ShaderStage::Fragment, 0, 64});
    return desc;
}

RenderPipelineDesc makeRenderPipelineDesc(
    const PipelineLayout& layout, const ShaderPackageDesc& shaderPackage, TextureFormat format)
{
    RenderPipelineDesc desc;
    desc.label = "NullHiddenFrameSmokePipeline";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader = {shaderPackage.slangModulePath, "vsMain", ShaderStage::Vertex};
    desc.fragmentShader = {shaderPackage.slangModulePath, "fsMain", ShaderStage::Fragment};
    desc.colorFormat = format;
    return desc;
}

bool commandSucceeded(const CommandValidationResult& result, std::pmr::string& diagnostics)
{
    if (result.success) {
        return true;
    }
    arp::core::appendDiagnostic(diagnostics, result.diagnostics);
    return false;
}

bool frameResourceIdWouldOverflow(std::uint64_t frameSerial)
{
    return frameSerial > std::numeric_limits<std::uint64_t>::max() - kSurfaceFrameResourceBaseId;
}

} // namespace

NullHiddenFrameSmokeRunner::NullHiddenFrameSmokeRunner(GpuDevice& device) : device_(device)
{}

NullHiddenFrameSmokeReport NullHiddenFrameSmokeRunner::run(const NullHiddenFrameSmokeConfig& config)
{
    FrameResourceRecycler recycler({2, kSmokeFrameCount, kSmokeFrameCount});

    NullHiddenFrameSmokeReport report;
    report.backend = device_.backend();
    report.frames.reserve(kSmokeFrameCount);

    if (device_.backend() != Backend::Null) {
        return fail(report, "hidden-frame smoke requires the Null backend", recycler);
    }
    if (config.frameCount != kSmokeFrameCount) {
        return fail(report, "hidden-frame smoke requires exactly two frames", recycler);
    }
    if (config.extent.width == 0 || config.extent.height == 0) {
        return fail(report, "hidden-frame smoke requires non-zero extent", recycler);
    }
    if (config.format == TextureFormat::Unknown) {
        return fail(report, "hidden-frame smoke requires a color format", recycler);
    }

    const auto capabilities = device_.capabilities();
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && capabilities.memory.backendFallbacksHiddenFromUpperLayer;
    if (!capabilities.core.graphics) {
        return fail(report, "hidden-frame smoke requires graphics capability", recycler);
    }

    const auto frameLayout = makeFrameDescriptorLayout();
    auto pipelineLayout = device_.createPipelineLayout(makePipelineLayoutDesc(frameLayout));
    if (!pipelineLayout) {
        return fail(report, "hidden-frame smoke pipeline layout creation failed", recycler);
    }
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && pipelineLayout->backendFallbacksHiddenFromUpperLayer();
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || pipelineLayout->nativeHandleExposedToUpperLayer();

    if (!config.shaderPackage)
        return fail(report, "RHI-GENERATED-PACKAGE-INPUT-MISSING", recycler);
    const auto& shaderPackage = *config.shaderPackage;
    const auto renderPipelineDesc =
        makeRenderPipelineDesc(*pipelineLayout, shaderPackage, config.format);
    const auto renderPipelineValidation = device_.validateRenderPipeline(renderPipelineDesc);
    if (!renderPipelineValidation.success) {
        return fail(report,
            "hidden-frame smoke render pipeline validation failed: "
                + renderPipelineValidation.diagnostics,
            recycler);
    }
    auto renderPipeline = device_.createRenderPipeline(renderPipelineDesc);
    if (!renderPipeline) {
        return fail(report, "hidden-frame smoke render pipeline creation failed", recycler);
    }
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && renderPipeline->backendFallbacksHiddenFromUpperLayer();
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || renderPipeline->nativeHandleExposedToUpperLayer();

    auto descriptorArena = device_.createDescriptorArena({
        "NullHiddenFrameSmokeDescriptorArena",
        1,
        1,
    });
    if (!descriptorArena) {
        return fail(report, "hidden-frame smoke descriptor arena creation failed", recycler);
    }
    auto frameDescriptorSet = descriptorArena->allocateDescriptorSet({
        "NullHiddenFrameSmokeFrameSet",
        frameLayout,
    });
    if (!frameDescriptorSet) {
        return fail(report, "hidden-frame smoke descriptor set allocation failed", recycler);
    }

    auto frameConstants = device_.createBuffer({
        "NullHiddenFrameSmokeFrameConstants",
        256,
        ResourceUsage::UniformBuffer | ResourceUsage::CopyDst,
        MemoryUsage::CpuToGpu,
        ResourceState::Undefined,
    });
    if (!frameConstants) {
        return fail(report, "hidden-frame smoke frame constants creation failed", recycler);
    }
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || frameConstants->allocation().nativeHandleExposed;

    const auto descriptorWrite = frameDescriptorSet->write({
        0,
        DescriptorType::UniformBuffer,
        frameConstants->id(),
        DescriptorResourceKind::Buffer,
    });
    if (!descriptorWrite.success) {
        return fail(report, descriptorWrite.diagnostics, recycler);
    }
    const auto descriptorStats = frameDescriptorSet->stats();
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && descriptorStats.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || descriptorStats.nativeHandleExposedToUpperLayer;

    auto swapchain = device_.createSwapchain({
        config.surfaceName,
        config.extent,
        config.format,
        2u,
    });
    if (!swapchain) {
        return fail(report, "hidden-frame smoke swapchain creation failed", recycler);
    }
    report.swapchainImageCount = swapchain->imageCount();
    report.maxFramesInFlight = swapchain->maxFramesInFlight();
    if (report.swapchainImageCount != 2u || report.maxFramesInFlight != 2u) {
        return fail(report, "hidden-frame smoke requires a double-buffer swapchain", recycler);
    }

    for (std::uint32_t frameIndex = 0; frameIndex < kSmokeFrameCount; ++frameIndex) {
        NullHiddenFrameSmokeFrameReport frameReport;
        std::pmr::string frameDiagnostics;

        const auto acquired = swapchain->acquireNextFrame();
        if (!acquired.success) {
            arp::core::appendDiagnostic(frameDiagnostics, acquired.diagnostics);
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }
        frameReport.frameSerial = acquired.frameSerial;

        if (frameResourceIdWouldOverflow(acquired.frameSerial)) {
            arp::core::appendDiagnostic(
                frameDiagnostics, "hidden-frame smoke resource id overflow");
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }

        const auto tracked = recycler.trackTransient(
            {
                GpuResourceId{kSurfaceFrameResourceBaseId + acquired.frameSerial},
                kSurfaceFrameResourceBytes,
                FrameResourceKind::SwapchainImage,
                FrameResourceScope::Surface,
                false,
            },
            acquired.frameSerial);
        if (!tracked.success) {
            frameReport.diagnostics = tracked.diagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, tracked.diagnostics, recycler);
        }
        frameReport.frameSlot = tracked.frameSlot;

        auto encoder = device_.createCommandEncoder({"NullHiddenFrameSmokeFrame"});
        if (!encoder) {
            arp::core::appendDiagnostic(
                frameDiagnostics, "hidden-frame smoke command encoder creation failed");
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }

        auto pass = encoder->beginRenderPass({"HiddenBackbuffer"});
        if (!pass) {
            arp::core::appendDiagnostic(frameDiagnostics, encoder->lastValidation().diagnostics);
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }

        commandSucceeded(pass->setPipeline(*renderPipeline), frameDiagnostics);
        commandSucceeded(pass->bindDescriptorSet(0, *frameDescriptorSet), frameDiagnostics);
        {
            static const std::uint8_t kPushData[256] = {};
            commandSucceeded(
                pass->pushConstants(ShaderStage::Vertex | ShaderStage::Fragment, 0, 64, kPushData),
                frameDiagnostics);
        }
        commandSucceeded(pass->drawIndexed(3, 1, 0, 0, 0), frameDiagnostics);
        commandSucceeded(pass->end(), frameDiagnostics);
        if (!frameDiagnostics.empty()) {
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }
        report.descriptorCompletenessValidated = true;

        auto commandBuffer = encoder->finish();
        if (!commandBuffer) {
            arp::core::appendDiagnostic(frameDiagnostics, encoder->lastValidation().diagnostics);
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }

        const auto submitted = device_.queue().submit(*commandBuffer);
        if (!submitted.success) {
            arp::core::appendDiagnostic(frameDiagnostics, submitted.diagnostics);
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }
        frameReport.submittedCommandCount = submitted.submittedCommandCount;

        const auto presented = swapchain->present(acquired);
        frameReport.presentSucceeded = presented.success;
        if (!presented.success) {
            arp::core::appendDiagnostic(frameDiagnostics, presented.diagnostics);
            frameReport.diagnostics = frameDiagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameDiagnostics, recycler);
        }

        frameReport.releaseStats = recycler.releaseTransientForFrameSlot(tracked.frameSlot);
        if (!frameReport.releaseStats.success) {
            frameReport.diagnostics = frameReport.releaseStats.diagnostics;
            report.frames.push_back(std::move(frameReport));
            return fail(report, frameReport.releaseStats.diagnostics, recycler);
        }

        report.totalSubmittedCommandCount += submitted.submittedCommandCount;
        ++report.submittedFrameCount;
        report.frames.push_back(std::move(frameReport));
    }

    report.resourceStats = recycler.stats();
    const auto memoryStats = device_.memoryStats();
    report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
        && memoryStats.backendFallbacksHiddenFromUpperLayer
        && report.resourceStats.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
        || report.resourceStats.nativeHandleExposedToUpperLayer;
    report.success = report.submittedFrameCount == kSmokeFrameCount
        && report.frames.size() == kSmokeFrameCount && report.backendFallbacksHiddenFromUpperLayer
        && !report.nativeHandleExposedToUpperLayer && report.resourceStats.pendingDestroyCount == 0u
        && report.resourceStats.transientResourceCount == 0u;
    if (!report.success) {
        arp::core::appendDiagnostic(
            report.diagnostics, "hidden-frame smoke report validation failed");
    }
    return report;
}

} // namespace arp::rhi
