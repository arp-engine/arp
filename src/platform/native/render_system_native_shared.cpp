// Shared native RenderSystem machinery for the WebGPU (Emscripten) and
// Metal (Apple) backends. Device creation, surface naming and native
// telemetry live in the per-backend translation units; everything else
// — RenderSystem::Impl and the L2 frame-graph path — lives here.
#include "platform/native/render_system_native_shared.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>

namespace arp::platform {

namespace {

renderer::RendererResult rendererResult(bool success, std::pmr::string diagnostics)
{
    return renderer::RendererResult{success, std::move(diagnostics)};
}

class AcquiredFrameDiscardGuard {
public:
    AcquiredFrameDiscardGuard(rhi::Swapchain& swapchain,
        rhi::AcquiredFrame& currentFrame,
        const rhi::AcquiredFrame& frame)
        : swapchain_(swapchain), currentFrame_(currentFrame), frame_(frame)
    {}

    ~AcquiredFrameDiscardGuard() noexcept
    {
        if (committed_) {
            return;
        }
        const auto discarded = swapchain_.discardFrame(frame_);
        currentFrame_ = {};
        if (!discarded.success) {
            std::fprintf(stderr,
                "[render-system] failed to discard WebGPU frame after render failure: %s\n",
                discarded.diagnostics.c_str());
        }
    }

    void commit() noexcept { committed_ = true; }

private:
    rhi::Swapchain& swapchain_;
    rhi::AcquiredFrame& currentFrame_;
    const rhi::AcquiredFrame& frame_;
    bool committed_ = false;
};

void captureFrameGraphTrace(
    const render_graph::FrameGraph& graph, RenderSystemSceneRuntimeCustomGraphFrameReport& report)
{
    report.frameGraphExecutionPassCount = static_cast<std::uint32_t>(graph.executionTrace().size());
    report.frameGraphAttachmentTraceCount =
        static_cast<std::uint32_t>(graph.attachmentTrace().size());
    report.frameGraphBarrierTraceCount = static_cast<std::uint32_t>(graph.barrierTrace().size());

    std::size_t mainSceneIndex = static_cast<std::size_t>(-1);
    std::size_t particlesIndex = static_cast<std::size_t>(-1);
    std::size_t guiIndex = static_cast<std::size_t>(-1);
    for (std::size_t index = 0; index < graph.executionTrace().size(); ++index) {
        const auto& passName = graph.executionTrace()[index];
        if (!report.frameGraphExecutionTrace.empty()) {
            report.frameGraphExecutionTrace += " -> ";
        }
        report.frameGraphExecutionTrace += passName;
        if (passName == "MainScene") {
            report.frameGraphHasMainScene = true;
            mainSceneIndex = index;
        } else if (passName == "Particles") {
            report.frameGraphHasParticles = true;
            particlesIndex = index;
        } else if (passName == "Gui") {
            report.frameGraphHasGui = true;
            guiIndex = index;
        }
    }
    report.frameGraphParticlesAfterMainScene = mainSceneIndex != static_cast<std::size_t>(-1)
        && particlesIndex != static_cast<std::size_t>(-1) && particlesIndex > mainSceneIndex;
    report.frameGraphGuiAfterParticles = guiIndex != static_cast<std::size_t>(-1)
        && particlesIndex != static_cast<std::size_t>(-1) && guiIndex > particlesIndex;

    for (const auto& attachment : graph.attachmentTrace()) {
        if (attachment.passName != "Particles") {
            continue;
        }
        if (!report.frameGraphParticleAttachmentSummary.empty()) {
            report.frameGraphParticleAttachmentSummary += " | ";
        }
        report.frameGraphParticleAttachmentSummary += attachment.resourceName;
        report.frameGraphParticleAttachmentSummary += " load=";
        switch (attachment.loadOp) {
        case rhi::LoadOp::Load:
            report.frameGraphParticleAttachmentSummary += "Load";
            break;
        case rhi::LoadOp::Clear:
            report.frameGraphParticleAttachmentSummary += "Clear";
            break;
        case rhi::LoadOp::DontCare:
            report.frameGraphParticleAttachmentSummary += "DontCare";
            break;
        }
        report.frameGraphParticleAttachmentSummary += " imported=";
        report.frameGraphParticleAttachmentSummary += attachment.imported ? "true" : "false";
    }
}

RenderSystemResult renderSystemResult(bool success, std::pmr::string diagnostics)
{
    return RenderSystemResult{success, std::move(diagnostics)};
}

bool sameFrame(const rhi::AcquiredFrame& lhs, const rhi::AcquiredFrame& rhs)
{
    return lhs.success && rhs.success && lhs.frameSerial == rhs.frameSerial
        && lhs.frameIndex == rhs.frameIndex;
}

rhi::RenderTargetDesc makeBackbufferTarget(
    rhi::GpuTexture& texture, rhi::Extent2D extent, rhi::TextureFormat format)
{
    return {
        "RenderSystemBackbuffer",
        texture.id(),
        extent,
        format,
        rhi::ResourceState::ColorAttachment,
    };
}

renderer::RenderFrameContext makeFrameContext(const rhi::AcquiredFrame& frame,
    rhi::GpuTexture& backbuffer,
    rhi::Extent2D extent,
    rhi::TextureFormat format)
{
    return {
        frame,
        makeBackbufferTarget(backbuffer, extent, format),
        nullptr,
        &backbuffer,
        nullptr,
    };
}

void throwIfFailed(const renderer::RendererResult& result, const char* prefix)
{
    if (result.success) {
        return;
    }
    std::pmr::string diagnostic(prefix);
    diagnostic += result.diagnostics;
    throw std::runtime_error(arp::core::toStdString(diagnostic));
}

RenderSystemPreparedRendererFramePrepareReport preparedRendererFrameReportFrom(
    const renderer::RendererRhiProductionFramePrepareReport& source, std::uint32_t prepareCount)
{
    RenderSystemPreparedRendererFramePrepareReport report;
    report.success = source.success;
    report.diagnostics = source.diagnostics;
    report.usedRhiGpuDevice = source.usedRhiGpuDevice;
    report.rendererFramePlanAccepted = source.rendererFramePlanAccepted;
    report.rhiResourceCreated = source.rhiResourceCreated;
    report.rhiPipelineCreated = source.rhiPipelineCreated;
    report.rhiDescriptorCreated = source.rhiDescriptorCreated;
    report.rhiDescriptorWritten = source.rhiDescriptorWritten;
    report.shaderPackageValidated = source.shaderPackageValidated;
    report.fullSurfaceViewportConfigured = source.fullSurfaceViewportConfigured;
    report.fullSurfaceScissorConfigured = source.fullSurfaceScissorConfigured;
    report.backendFallbacksHiddenFromUpperLayer = source.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = source.nativeHandleExposedToUpperLayer;
    report.prepareCount = prepareCount;
    return report;
}

RenderSystemSceneRuntimeCustomGraphPrepareReport sceneRuntimePrepareReportFrom(
    const RenderSystemPreparedRendererFramePrepareReport& source)
{
    RenderSystemSceneRuntimeCustomGraphPrepareReport report;
    report.success = source.success;
    report.diagnostics = source.diagnostics;
    report.usedRhiGpuDevice = source.usedRhiGpuDevice;
    report.rendererFramePlanAccepted = source.rendererFramePlanAccepted;
    report.rhiResourceCreated = source.rhiResourceCreated;
    report.rhiPipelineCreated = source.rhiPipelineCreated;
    report.rhiDescriptorCreated = source.rhiDescriptorCreated;
    report.rhiDescriptorWritten = source.rhiDescriptorWritten;
    report.shaderPackageValidated = source.shaderPackageValidated;
    report.fullSurfaceViewportConfigured = source.fullSurfaceViewportConfigured;
    report.fullSurfaceScissorConfigured = source.fullSurfaceScissorConfigured;
    report.backendFallbacksHiddenFromUpperLayer = source.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = source.nativeHandleExposedToUpperLayer;
    report.prepareCount = source.prepareCount;
    return report;
}

std::uint32_t preparedSceneRuntimeDrawCommandCount(
    const renderer::RendererRhiFramePlanDesc& framePlan)
{
    std::uint32_t count = 0u;
    for (const auto& drawPlan : framePlan.drawPlans) {
        if (drawPlan.workKind == renderer::RendererRhiWorkKind::Scene) {
            count += drawPlan.drawCommandCount;
        }
    }
    return count;
}

bool preparedRendererFrameMatchesDecision(
    const renderer::RenderFramePreparedSubmitRequirement& requirement,
    const renderer::RendererRhiFramePlanDesc& preparedFramePlan)
{
    return requirement.required
        && requirement.expectedDrawItemCount
        == preparedSceneRuntimeDrawCommandCount(preparedFramePlan)
        && preparedSceneRuntimeDrawCommandCount(requirement.framePlan)
        == preparedSceneRuntimeDrawCommandCount(preparedFramePlan);
}

void applySceneRuntimeSubmitReport(RenderSystemSceneRuntimeCustomGraphFrameReport& target,
    const renderer::RendererRhiProductionFrameSubmitReport& submitted,
    const RenderSystemSceneRuntimeCustomGraphPrepareReport& prepared)
{
    target.success = submitted.success;
    target.diagnostics = submitted.diagnostics;
    target.renderGraphFrameUsed = true;
    target.sceneRuntimePathSubmitted = target.sceneRuntimePathSubmitted || submitted.success;
    target.surfaceBackedVulkan = false;
    target.usedRhiGpuDevice = target.usedRhiGpuDevice || submitted.usedRhiGpuDevice;
    target.preparedBeforeSubmit = target.preparedBeforeSubmit || prepared.success;
    target.resourcesPreparedBeforeSubmit =
        target.resourcesPreparedBeforeSubmit || submitted.resourcesPreparedBeforeSubmit;
    target.pipelinePreparedBeforeSubmit =
        target.pipelinePreparedBeforeSubmit || submitted.pipelinePreparedBeforeSubmit;
    target.descriptorsPreparedBeforeSubmit =
        target.descriptorsPreparedBeforeSubmit || submitted.descriptorsPreparedBeforeSubmit;
    target.rhiCommandEncoded = target.rhiCommandEncoded || submitted.rhiCommandEncoded;
    target.rhiQueueSubmitted = target.rhiQueueSubmitted || submitted.rhiQueueSubmitted;
    target.swapchainRenderTargetUsed = true;
    target.proceduralSceneRuntime3DObjectDrawn =
        target.proceduralSceneRuntime3DObjectDrawn || submitted.proceduralSceneRuntime3DObjectDrawn;
    target.proceduralSceneRuntime2DSpriteDrawn =
        target.proceduralSceneRuntime2DSpriteDrawn || submitted.proceduralSceneRuntime2DSpriteDrawn;
    target.proceduralSceneRuntimeUiDrawn =
        target.proceduralSceneRuntimeUiDrawn || submitted.proceduralSceneRuntimeUiDrawn;
    target.resourceAllocatedDuringSubmit =
        target.resourceAllocatedDuringSubmit || submitted.resourceAllocatedDuringSubmit;
    target.pipelineCreatedDuringSubmit =
        target.pipelineCreatedDuringSubmit || submitted.pipelineCreatedDuringSubmit;
    target.backendFallbacksHiddenFromUpperLayer = target.backendFallbacksHiddenFromUpperLayer
        && submitted.backendFallbacksHiddenFromUpperLayer
        && prepared.backendFallbacksHiddenFromUpperLayer;
    target.nativeHandleExposedToUpperLayer = target.nativeHandleExposedToUpperLayer
        || submitted.nativeHandleExposedToUpperLayer || prepared.nativeHandleExposedToUpperLayer;
    target.prepareCount = prepared.prepareCount;
    if (submitted.success) {
        ++target.framesSubmitted;
    }
    target.renderPassCount += submitted.renderPassCount;
    target.drawCommandCount += submitted.drawCommandCount;
}

} // namespace

RenderSystem::RenderSystem() = default;

RenderSystem::RenderSystem(core::UniquePtr<Impl> impl) : impl_(std::move(impl))
{}

RenderSystem::RenderSystem(RenderSystem&&) noexcept = default;

RenderSystem& RenderSystem::operator=(RenderSystem&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    // WebGPU has no blocking CPU wait primitive in the L2 contract.  Keep the
    // explicit teardown boundary nevertheless: the backend-owned device
    // hook is a safe no-op today, while resource payload destructors still
    // release every native object before the old implementation is replaced.
    waitGpuIdle();
    impl_ = std::move(other.impl_);
    return *this;
}

RenderSystem::~RenderSystem()
{
    // See waitGpuIdle(): WebGPU cannot synchronously drain browser work, but
    // teardown must still pass through the named lifecycle boundary so the
    // contract is not accidentally weakened when the backend gains one.
    waitGpuIdle();
}

void RenderSystem::waitGpuIdle() const
{
    if (impl_ && impl_->device) {
        // This dispatch remains backend-owned. Browser queue completion is
        // asynchronous; waitIdle does not establish a synchronous drain proof.
        // The shutdown contract below reports the actual drain availability.
        impl_->device->waitIdle();
    }
}

rhi::semantics::ShutdownDrainReport RenderSystem::drainGpuForShutdown(
    const rhi::semantics::ShutdownDrainRequest& request) const
{
    if (impl_ && impl_->device)
        return impl_->device->drainForShutdown(request);
    const auto now = std::chrono::steady_clock::now();
    rhi::semantics::ShutdownDrainReport report;
    report.backend = impl_ ? impl_->backend : rhi::Backend::Null;
    report.diagnosticId = "ARP-PLATFORM-SHUTDOWN-DRAIN-RENDER-SYSTEM-INVALID";
    rhi::semantics::finalizeShutdownDrainTiming(report, now, request, now);
    return report;
}

bool RenderSystem::valid() const
{
    return impl_ && impl_->valid;
}

const std::pmr::string& RenderSystem::diagnostics() const
{
    static const std::pmr::string empty;
    return impl_ ? impl_->diagnostics : empty;
}

RuntimeCapabilities RenderSystem::capabilities() const
{
    RuntimeCapabilities capabilities;
    capabilities.backend = backend();
    capabilities.surfaceBacked = valid() && impl_ && impl_->surfaceBackedPresentation;
    capabilities.canRender = valid();
    if (impl_) {
        capabilities.rhiFrameLoopDryRun = summarizeRenderSystemRhiFrameLoopDryRunCapability(
            impl_->rhiDryRunDesc, impl_->lastRhiDryRunReport);
        capabilities.rhiFrameLoopDryRunSmoke =
            summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(impl_->lastRhiDryRunSmokeReport);
        capabilities.rhiStartupProbe =
            summarizeRenderSystemRhiStartupProbeCapability(impl_->lastRhiStartupProbeReport);
        capabilities.rhiStartupProbeSchedule = impl_->lastRhiStartupProbeScheduleReport;
        capabilities.rhiStartupProbeBatch = impl_->lastRhiStartupProbeBatchReport;
        capabilities.rhiStartupProbeRun = impl_->lastRhiStartupProbeRunReport;
        capabilities.rhiStartupTelemetry = impl_->lastRhiStartupTelemetryReport;
        capabilities.rhiStartupReadiness = impl_->lastRhiStartupReadinessReport;
        capabilities.rhiFormalRefactorStartGate = impl_->lastRhiFormalRefactorStartGateReport;
    }
    capabilities.rhiStartupGate = evaluateRenderSystemRhiStartupGate(capabilities.rhiStartupProbe);
    capabilities.diagnostics = diagnostics();
    return capabilities;
}

rhi::Backend RenderSystem::backend() const
{
    return valid() ? impl_->backend : rhi::Backend::Null;
}

rhi::GpuDevice* RenderSystem::gpuDevice() noexcept
{
    return impl_ ? impl_->device.get() : nullptr;
}

const rhi::GpuDevice* RenderSystem::gpuDevice() const noexcept
{
    return impl_ ? impl_->device.get() : nullptr;
}

rhi::Swapchain* RenderSystem::swapchain() noexcept
{
    return impl_ ? impl_->swapchain.get() : nullptr;
}

rhi::Extent2D RenderSystem::extent() const
{
    return valid() ? impl_->extent : rhi::Extent2D{};
}

rhi::TextureFormat RenderSystem::colorFormat() const
{
    return valid() ? impl_->colorFormat : rhi::TextureFormat::Unknown;
}

rhi::MemoryStats RenderSystem::memoryStats() const
{
    return valid() && impl_->device ? impl_->device->memoryStats() : rhi::MemoryStats{};
}

rhi::GpuTimingSample RenderSystem::latestGpuTiming() const
{
    return valid() && impl_->device ? impl_->device->latestGpuTiming() : rhi::GpuTimingSample{};
}

rhi::semantics::UploadStagingReadiness RenderSystem::uploadStagingReadiness() const
{
    return valid() && impl_->device ? impl_->device->uploadStagingReadiness()
                                    : rhi::semantics::uploadStagingReadinessUnavailable();
}

rhi::SwapchainPresentationStatus RenderSystem::presentationStatus() const
{
    return valid() && impl_->swapchain ? impl_->swapchain->presentationStatus()
                                       : rhi::SwapchainPresentationStatus{};
}

RenderSystemResult RenderSystem::resize(rhi::Extent2D extent)
{
    if (!valid() || !impl_->swapchain) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (impl_->currentFrame.success) {
        return renderSystemResult(false, "cannot resize RHI swapchain while a frame is active");
    }
    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);
    const auto resized = impl_->swapchain->resize(extent);
    if (!resized.success) {
        return renderSystemResult(false, "WebGPU RHI resize failed: " + resized.diagnostics);
    }
    impl_->extent = resized.extent;
    impl_->swapchainDesc.extent = resized.extent;
    // A resize changes every extent-sized transient descriptor. Drop only
    // resources that are already safe to destroy; resources from the last
    // submitted frame remain fence-gated in the pool and are reclaimed after
    // the WebGPU completion callback.
    if (impl_->frameGraphResourcePool) {
        impl_->frameGraphResourcePool->trimCachedResources();
    }
    if (impl_->activePipeline) {
        const auto pipelineResized = impl_->activePipeline->resize(resized.extent);
        if (!pipelineResized.success) {
            return renderSystemResult(
                false, "WebGPU render pipeline resize failed: " + pipelineResized.diagnostics);
        }
    }
    if (impl_->guiRenderer) {
        const auto guiResized = impl_->guiRenderer->onResize(resized.extent);
        if (!guiResized.success) {
            return renderSystemResult(
                false, "GUI renderer resize failed: " + guiResized.diagnostics);
        }
    }
    if (impl_->particleRenderer && impl_->particleRendererConfig.valid()) {
        auto particleRenderer = core::makeUnique<renderer::ParticleRenderer>(*impl_->device);
        const auto particleResized = particleRenderer->create(renderer::ParticleRendererDesc{
            impl_->particleRendererConfig.shaderPath,
            impl_->colorFormat,
            impl_->extent,
            impl_->particleRendererConfig.maxParticles,
            impl_->particleRendererConfig.trailBeamShaderPath,
            impl_->particleRendererConfig.meshParticleShaderPath,
        });
        if (!particleResized.success) {
            return renderSystemResult(
                false, "particle renderer resize failed: " + particleResized.diagnostics);
        }
        impl_->particleRenderer = std::move(particleRenderer);
    }
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::setPresentationMode(const rhi::PresentationMode presentationMode)
{
    if (!valid() || !impl_->swapchain) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (impl_->currentFrame.success) {
        return renderSystemResult(false, "cannot change presentation mode while a frame is active");
    }
    if (impl_->swapchainDesc.presentationMode == presentationMode) {
        return renderSystemResult(true, "");
    }
    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);
    auto requested = impl_->swapchainDesc;
    requested.presentationMode = presentationMode;
    const auto result = impl_->swapchain->reconfigure({std::move(requested)});
    if (!result.success) {
        return renderSystemResult(
            false, "RHI swapchain presentation reconfigure failed: " + result.diagnostics);
    }
    impl_->swapchainDesc.presentationMode = presentationMode;
    impl_->extent = result.extent;
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::uploadMesh(
    const renderer::MeshAsset& mesh, renderer::MeshGpuBuffer& output)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    auto uploaded = renderer::uploadOrUpdateMeshToRhi(*impl_->device, mesh, output);
    if (!uploaded.success) {
        return renderSystemResult(false, std::move(uploaded.diagnostics));
    }
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::setRenderPipeline(
    std::unique_ptr<renderer::RenderPipeline>&& pipeline)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (!pipeline) {
        return renderSystemResult(false, "render pipeline is null");
    }

    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);

    const auto created = pipeline->create(renderer::RenderPipelineCreateContext{
        *impl_->device,
        impl_->colorFormat,
        rhi::TextureFormat::D32Float,
        impl_->extent,
    });
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->pipeline = std::move(pipeline);
    impl_->corePipeline.reset();
    impl_->activePipeline = impl_->pipeline.get();
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::setRenderPipeline(
    arp::core::UniquePtr<renderer::RenderPipeline>&& pipeline)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    if (!pipeline) {
        return renderSystemResult(false, "render pipeline is null");
    }

    const auto cancelled = impl_->inputAdmission.cancel();
    if (!cancelled.success)
        return renderSystemResult(false, cancelled.diagnostics);

    const auto created = pipeline->create(renderer::RenderPipelineCreateContext{
        *impl_->device,
        impl_->colorFormat,
        rhi::TextureFormat::D32Float,
        impl_->extent,
    });
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->corePipeline = std::move(pipeline);
    impl_->pipeline.reset();
    impl_->activePipeline = impl_->corePipeline.get();
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::createParticleRenderer(const std::pmr::string& shaderPath,
    std::uint32_t maxParticles,
    const std::pmr::string& trailBeamShaderPath,
    const std::pmr::string& meshParticleShaderPath)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    auto particleRenderer = core::makeUnique<renderer::ParticleRenderer>(*impl_->device);
    const auto created = particleRenderer->create(renderer::ParticleRendererDesc{
        shaderPath,
        impl_->colorFormat,
        impl_->extent,
        maxParticles,
        trailBeamShaderPath,
        meshParticleShaderPath,
    });
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->particleRendererConfig.shaderPath = shaderPath;
    impl_->particleRendererConfig.trailBeamShaderPath = trailBeamShaderPath;
    impl_->particleRendererConfig.meshParticleShaderPath = meshParticleShaderPath;
    impl_->particleRendererConfig.maxParticles = maxParticles;
    impl_->particleRenderer = std::move(particleRenderer);
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::createGuiRenderer(const std::pmr::string& shaderPath)
{
    return createGuiRenderer(renderer::GuiRendererDesc{
        shaderPath,
        impl_ ? impl_->colorFormat : rhi::TextureFormat::Unknown,
        impl_ ? impl_->extent : rhi::Extent2D{},
    });
}

RenderSystemResult RenderSystem::createGuiRenderer(const renderer::GuiRendererDesc& desc)
{
    if (!valid() || !impl_->device) {
        return renderSystemResult(false, "render system is not valid");
    }
    auto guiRenderer = core::makeUnique<renderer::GuiRenderer>(*impl_->device);
    auto rendererDesc = desc;
    rendererDesc.colorFormat = impl_->colorFormat;
    rendererDesc.extent = impl_->extent;
    const auto created = guiRenderer->create(rendererDesc);
    if (!created.success) {
        return renderSystemResult(false, created.diagnostics);
    }
    impl_->guiRenderer = std::move(guiRenderer);
    return renderSystemResult(true, "");
}

RenderSystemResult RenderSystem::updateGuiAtlasTexture(const renderer::GuiAtlasTextureDesc& atlas,
    const arp::core::pmr::Vector<arp::renderer::ui::GlyphAtlasUploadSchedule>& uploads)
{
    if (!valid() || !impl_->guiRenderer) {
        return renderSystemResult(false, "render system has no GUI renderer");
    }
    const auto updated = impl_->guiRenderer->updateAtlasTexture(atlas, uploads);
    return renderSystemResult(updated.success, updated.diagnostics.c_str());
}

renderer::UiPassTelemetry RenderSystem::guiPassTelemetry() const
{
    return impl_ && impl_->guiRenderer ? impl_->guiRenderer->passTelemetry()
                                       : renderer::UiPassTelemetry{};
}

rhi::AcquiredFrame RenderSystem::acquireFrame()
{
    if (!valid() || !impl_->swapchain) {
        return rhi::makeAcquireFailureResult("render system is not valid", 0u, 0u);
    }
    if (impl_->currentFrame.success)
        return rhi::makeAcquireFailureResult("PLATFORM-FRAME-ALREADY-ACQUIRED",
            impl_->currentFrame.frameSerial,
            impl_->currentFrame.frameIndex);
    auto frame = impl_->inputAdmission.acquire(
        *impl_->swapchain,
        [this](std::uint32_t slot) {
            return !impl_->activePipeline || impl_->activePipeline->frameInputsReady(slot);
        },
        [this] {
            return !impl_->activePipeline || impl_->activePipeline->sharedFrameInputsReady();
        });
    if (frame.success) {
        impl_->currentFrame = frame;
        impl_->currentFramePresentationCompletion.reset();
    }
    return frame;
}

renderer::RendererResult RenderSystem::renderGraphFrame(
    const rhi::AcquiredFrame& frame, const renderer::CustomRenderFrameWorkload& workload)
{
    if (!valid()) {
        return rendererResult(false, "render system is not valid");
    }
    if (!sameFrame(frame, impl_->currentFrame)) {
        return rendererResult(false, "frame is not drawable");
    }
    impl_->currentFramePresentationCompletion.reset();
    AcquiredFrameDiscardGuard frameDiscardGuard(*impl_->swapchain, impl_->currentFrame, frame);
    if (frame.frameSerial != workload.frameSerial()) {
        return rendererResult(false, "frame serial does not match CustomRenderFrameWorkload");
    }

    const auto validation = workload.validate();
    if (!validation.success) {
        return rendererResult(
            false, "invalid CustomRenderFrameWorkload: " + validation.diagnostics);
    }

    if (impl_->activePipeline == nullptr) {
        return rendererResult(false, "render pipeline is not ready");
    }

    if (impl_->guiRenderer) {
        impl_->guiRenderer->beginFrameTelemetry();
    }

    impl_->lastSceneRuntimeCustomGraphFrameReport = {};
    impl_->sceneRuntimeCustomGraphPendingPresent = false;
    impl_->sceneRuntimeCustomGraphPendingPresentSerial = 0u;

    render_graph::FrameGraph graph(*impl_->device, impl_->frameGraphResourcePool);

    // Acquire the canvas texture from the swapchain — this is the actual
    // WebGPU surface texture the browser will present.  Mirror the Vulkan
    // path: the swapchain image IS the backbuffer, not an independent
    // GpuOnly texture that never reaches the canvas.
    auto acquiredImage = impl_->swapchain->acquiredImage(frame);
    if (!acquiredImage.success || !acquiredImage.texture) {
        return rendererResult(
            false, "WebGPU acquired swapchain image failed: " + acquiredImage.diagnostics);
    }
    const rhi::RhiFrameContractValidator frameContract({impl_->swapchain->maxFramesInFlight()});
    const auto imageContract = frameContract.validateSwapchainImage(acquiredImage.ref);
    if (!imageContract.success) {
        return rendererResult(
            false, "WebGPU acquired swapchain image contract failed: " + imageContract.diagnostics);
    }

    const auto backbuffer =
        graph.importSwapchainTexture(std::move(acquiredImage.texture), acquiredImage.ref);
    rhi::GpuTexture* backbufferTexture = nullptr;

    renderer::RenderPipelineFrameFeatures frameFeatures;
    renderer::RenderFrameRequest pipelineRequest;
    pipelineRequest.view.extent = impl_->extent;
    pipelineRequest.view.colorFormat = impl_->colorFormat;
    pipelineRequest.view.frameIndex = frame.frameIndex;
    pipelineRequest.pipelinePayload =
        workload.scene().enabled() ? workload.scene().payload().object : nullptr;
    pipelineRequest.pipelinePayloadContractName = workload.scenePayloadContractName();
    pipelineRequest.pipelinePayloadContractVersion = workload.scenePayloadContractVersion();

    if (workload.particles().enabled()) {
        const auto* particlePayload =
            renderer::customParticleGraphPayloadFrom(workload.particles());
        if (!particlePayload) {
            return rendererResult(false, "Custom graph particle payload contract is invalid");
        }
        if (!impl_->particleRenderer) {
            return rendererResult(false, "particle renderer is not ready");
        }
        frameFeatures.particlesWithColor =
            [&](render_graph::RenderContext& graphContext,
                const renderer::RenderGraphColorTargetContext& particleTarget) {
                if (!particleTarget.valid()) {
                    throw std::runtime_error("Custom graph particle color target is invalid");
                }
                const renderer::RenderFrameContext frameContext{
                    frame,
                    particleTarget.colorTarget,
                    nullptr,
                    particleTarget.colorTexture,
                    nullptr,
                };
                renderer::ParticleRenderer::ParticleTextureBinding textureBinding;
                const renderer::ParticleRenderer::ParticleTextureBinding* textureBindingPtr =
                    nullptr;
                if (particlePayload->textureBinding.valid()) {
                    textureBinding.texture = particlePayload->textureBinding.texture;
                    textureBinding.textureResource =
                        particlePayload->textureBinding.textureResource;
                    textureBinding.resourceId = particlePayload->textureBinding.resourceId;
                    textureBinding.explicitUpload = particlePayload->textureBinding.explicitUpload;
                    textureBindingPtr = &textureBinding;
                }
                throwIfFailed(impl_->particleRenderer->recordPass(graphContext,
                                  frameContext,
                                  particlePayload->particleInstances(),
                                  *particlePayload->scene,
                                  true,
                                  textureBindingPtr),
                    "Custom graph particle pass failed: ");
            };
    }

    if (workload.gui().enabled()) {
        const auto* guiPayload = renderer::customGuiGraphPayloadFrom(workload.gui());
        const auto* uiPayload = renderer::customUiGraphPayloadFrom(workload.gui());
        if (!guiPayload && !uiPayload) {
            return rendererResult(false, "Custom graph GUI payload contract is invalid");
        }
        if (!impl_->guiRenderer) {
            return rendererResult(false, "UI renderer is not ready");
        }
        // Keep the callback's payload references valid through graph
        // execution; the workload wrapper itself is stack-local here.
        frameFeatures.gui = [&, guiPayload, uiPayload](render_graph::RenderContext& graphContext) {
            const auto frameContext =
                makeFrameContext(frame, *backbufferTexture, impl_->extent, impl_->colorFormat);
            if (uiPayload != nullptr && uiPayload->loweredPlan != nullptr) {
                throwIfFailed(impl_->guiRenderer->recordPass(
                                  graphContext, frameContext, uiPayload->loweredPlan->commands()),
                    "Custom graph UI pass failed: ");
            } else if (guiPayload != nullptr) {
                throwIfFailed(impl_->guiRenderer->recordPass(
                                  graphContext, frameContext, *guiPayload->DebugDrawData),
                    "Custom graph GUI pass failed: ");
            }
            if (uiPayload != nullptr && uiPayload->debugDrawData != nullptr) {
                throwIfFailed(impl_->guiRenderer->recordPass(
                                  graphContext, frameContext, *uiPayload->debugDrawData),
                    "Custom graph debug overlay pass failed: ");
            }
        };
    }

    if (workload.globalIllumination().enabled()) {
        frameFeatures.globalIllumination =
            renderer::globalIlluminationDescForProfile(renderer::QualityProfile::Medium);
    } else {
        frameFeatures.globalIllumination =
            renderer::globalIlluminationDescForProfile(renderer::QualityProfile::Low);
    }

    try {
        backbufferTexture = &graph.texture(backbuffer);
        renderer::RenderFramePreparedRendererFrameConfig preparedRendererFrame;
        renderer::RenderFrameDirectorRequest directorRequest{
            workload,
            graph,
            backbuffer,
            renderer::RenderPipelineGraphCompositionContext{
                frame,
                pipelineRequest,
                workload.scene().enabled(),
            },
            impl_->activePipeline,
            std::move(frameFeatures),
            preparedRendererFrame,
        };
        const auto pipelineComposition = impl_->frameDirector.compose(directorRequest);
        if (!pipelineComposition.success) {
            std::pmr::string diagnostics = "Custom graph pipeline composition failed: ";
            diagnostics += pipelineComposition.diagnostics.empty()
                ? "RenderFrameDirector failed without diagnostics"
                : pipelineComposition.diagnostics.front().message;
            return rendererResult(false, std::move(diagnostics));
        }
        if (pipelineComposition.decision.mode
            == renderer::RenderFrameExecutionMode::PreparedSubmit) {
            if (pipelineComposition.decision.preparedSubmitRequirements.empty()) {
                return rendererResult(false,
                    "Prepared renderer-frame WebGPU path requires an explicit prepared-submit requirement");
            }
            const auto& preparedSubmit =
                pipelineComposition.decision.preparedSubmitRequirements.front();
            if (!impl_->preparedRendererFramePath
                || !impl_->preparedRendererFramePrepareReport.success) {
                return rendererResult(false,
                    "Prepared renderer-frame WebGPU path requires explicit prepareRendererFrame before hot submit");
            }
            if (!preparedRendererFrameMatchesDecision(
                    preparedSubmit, impl_->preparedRendererFramePlan)) {
                return rendererResult(false,
                    "Prepared renderer-frame WebGPU path rejected a payload whose draw contract differs from the prepared frame plan");
            }

            auto submitted =
                impl_->preparedRendererFramePath->submit(*impl_->device, *backbufferTexture);
            applySceneRuntimeSubmitReport(impl_->lastSceneRuntimeCustomGraphFrameReport,
                submitted,
                sceneRuntimePrepareReportFrom(impl_->preparedRendererFramePrepareReport));
            if (!submitted.success) {
                return rendererResult(false,
                    "Prepared renderer-frame WebGPU submit failed: " + submitted.diagnostics);
            }
            impl_->sceneRuntimeCustomGraphPendingPresent = true;
            impl_->sceneRuntimeCustomGraphPendingPresentSerial = frame.frameSerial;
            frameDiscardGuard.commit();
            return rendererResult(true, "");
        }

        class FramePresentationCompletionObserver final
            : public render_graph::PassSubmissionObserver {
        public:
            void onSubmitted(render_graph::PassSubmissionToken,
                const std::shared_ptr<const rhi::GpuFence>& completion) noexcept override
            {
                completion_ = completion;
            }

            std::shared_ptr<const rhi::GpuFence> completion_;
        } framePresentationCompletionObserver;

        graph.addPass("PlatformFramePresentationCompletion")
            .externalSideEffects()
            .onSubmitted(framePresentationCompletionObserver)
            .execute([](render_graph::RenderContext&) {});
        graph.execute();
        if (!framePresentationCompletionObserver.completion_) {
            return rendererResult(false, "PLATFORM-FRAME-PRESENT-COMPLETION-MISSING");
        }
        impl_->currentFramePresentationCompletion =
            std::move(framePresentationCompletionObserver.completion_);
        frameDiscardGuard.commit();
        captureFrameGraphTrace(graph, impl_->lastSceneRuntimeCustomGraphFrameReport);
        impl_->lastSceneRuntimeCustomGraphFrameReport.success = true;
        impl_->lastSceneRuntimeCustomGraphFrameReport.renderGraphFrameUsed = true;
        impl_->lastSceneRuntimeCustomGraphFrameReport.legacyPipelineFallbackUsed =
            pipelineComposition.decision.mode
            == renderer::RenderFrameExecutionMode::LegacyPipelineFallback;
        impl_->lastSceneRuntimeCustomGraphFrameReport.sceneRuntimePathSubmitted =
            !impl_->lastSceneRuntimeCustomGraphFrameReport.legacyPipelineFallbackUsed;
        impl_->lastSceneRuntimeCustomGraphFrameReport.usedRhiGpuDevice = impl_->device != nullptr;
        impl_->lastSceneRuntimeCustomGraphFrameReport.swapchainRenderTargetUsed = true;
        impl_->lastSceneRuntimeCustomGraphFrameReport.backendFallbacksHiddenFromUpperLayer =
            impl_->device != nullptr
            && pipelineComposition.decision.mode
                != renderer::RenderFrameExecutionMode::LegacyPipelineFallback;
        impl_->sceneRuntimeCustomGraphPendingPresent = true;
        impl_->sceneRuntimeCustomGraphPendingPresentSerial = frame.frameSerial;
    } catch (const std::exception& error) {
        std::pmr::string diagnostics = "Custom graph RHI composition/execution failed: ";
        diagnostics += error.what();
        return rendererResult(false, std::move(diagnostics));
    }

    return rendererResult(true, "");
}

rhi::PresentResult RenderSystem::presentFrame(const rhi::AcquiredFrame& frame)
{
    if (!valid() || !impl_->swapchain) {
        return rhi::makePresentFailureResult(
            "present failure: render system is not valid", frame.frameSerial, frame.frameIndex);
    }
    if (!sameFrame(frame, impl_->currentFrame)) {
        return rhi::makePresentFailureResult(
            "present failure: frame is not presentable", frame.frameSerial, frame.frameIndex);
    }
    auto presented = impl_->swapchain->present(frame, {impl_->currentFramePresentationCompletion});
    if (!presented.success) {
        if (!(presented.recoverable && presented.swapchainRebuildRequired)) {
            const auto discarded = impl_->swapchain->discardFrame(frame);
            if (!discarded.success && !discarded.diagnostics.empty()) {
                presented.diagnostics += "; frame discard after present failure failed: ";
                presented.diagnostics += discarded.diagnostics;
            }
        }
        impl_->currentFrame = {};
        impl_->currentFramePresentationCompletion.reset();
        presented.diagnostics = "present failure: " + presented.diagnostics;
        return presented;
    }
    if (impl_->sceneRuntimeCustomGraphPendingPresent
        && impl_->sceneRuntimeCustomGraphPendingPresentSerial == frame.frameSerial
        && impl_->device) {
        applyNativeSceneRuntimeFrameStats(impl_->lastSceneRuntimeCustomGraphFrameReport,
            *impl_->device,
            impl_->extent,
            impl_->colorFormat);
        impl_->sceneRuntimeCustomGraphPendingPresent = false;
        impl_->sceneRuntimeCustomGraphPendingPresentSerial = 0u;
    }
    impl_->currentFrame = {};
    impl_->currentFramePresentationCompletion.reset();
    return presented;
}

RenderSystemPreparedRendererFramePrepareReport RenderSystem::prepareRendererFrame(
    const RenderSystemPreparedRendererFramePrepareDesc& desc)
{
    RenderSystemPreparedRendererFramePrepareReport report;
    if (!valid() || !impl_->device || !impl_->swapchain) {
        report.diagnostics =
            "Prepared renderer-frame WebGPU prepare requires a valid render system";
        return report;
    }
    if ((impl_->backend != rhi::Backend::WebGPU && impl_->backend != rhi::Backend::Metal)
        || !impl_->surfaceBackedPresentation) {
        report.diagnostics =
            "Prepared renderer-frame WebGPU prepare requires a surface-backed WebGPU backend";
        return report;
    }
    if (desc.framePlan.drawPlans.empty()) {
        report.diagnostics =
            "Prepared renderer-frame WebGPU prepare requires an explicit renderer frame plan";
        return report;
    }

    renderer::RendererRhiProductionFramePathDesc pathDesc;
    pathDesc.label = desc.label.empty() ? "PreparedRendererFrame" : desc.label;
    pathDesc.colorExtent = {impl_->extent.width, impl_->extent.height, 1u};
    pathDesc.colorFormat = impl_->colorFormat;
    pathDesc.framePlanBridge = desc.framePlanBridge;
    pathDesc.maxDrawCommandsPerPass = desc.maxDrawCommandsPerPass;
    pathDesc.useExternalColorTarget = true;
    pathDesc.shaderArtifactRoot = desc.shaderArtifactRoot;

    auto path = arp::core::makeUnique<renderer::RendererRhiProductionFramePath>(pathDesc);
    const auto prepared = path->prepare(desc.framePlan, *impl_->device);
    const auto prepareCount = impl_->preparedRendererFramePrepareCount + 1u;
    report = preparedRendererFrameReportFrom(prepared, prepareCount);
    if (!report.success) {
        return report;
    }

    impl_->preparedRendererFramePath = std::move(path);
    impl_->preparedRendererFramePlan = desc.framePlan;
    impl_->preparedRendererFramePlanBridge = desc.framePlanBridge;
    impl_->preparedRendererFrameMaxDrawCommandsPerPass = desc.maxDrawCommandsPerPass;
    impl_->preparedRendererFrameShaderArtifactRoot = desc.shaderArtifactRoot;
    impl_->preparedRendererFramePrepareCount = prepareCount;
    impl_->preparedRendererFramePrepareReport = report;
    return report;
}

RenderSystemSceneRuntimeCustomGraphPrepareReport RenderSystem::prepareSceneRuntimeCustomGraphFrame(
    const RenderSystemSceneRuntimeCustomGraphPrepareDesc& desc)
{
    RenderSystemSceneRuntimeCustomGraphPrepareReport report;
    if (!valid() || !impl_->device || !impl_->swapchain) {
        report.diagnostics =
            "SceneRuntime custom graph WebGPU prepare requires a valid render system";
        return report;
    }
    if ((impl_->backend != rhi::Backend::WebGPU && impl_->backend != rhi::Backend::Metal)
        || !impl_->surfaceBackedPresentation) {
        report.diagnostics =
            "SceneRuntime custom graph WebGPU prepare requires a surface-backed WebGPU backend";
        return report;
    }
    if (desc.framePlan.drawPlans.empty()) {
        report.diagnostics =
            "SceneRuntime custom graph WebGPU prepare requires an explicit renderer frame plan";
        return report;
    }

    RenderSystemPreparedRendererFramePrepareDesc preparedDesc;
    preparedDesc.label = desc.label.empty() ? "SceneRuntimeCustomGraph" : desc.label;
    preparedDesc.framePlan = desc.framePlan;
    preparedDesc.framePlanBridge = desc.framePlanBridge;
    preparedDesc.maxDrawCommandsPerPass = desc.maxDrawCommandsPerPass;
    preparedDesc.shaderArtifactRoot = desc.shaderArtifactRoot;

    const auto prepared = prepareRendererFrame(preparedDesc);
    report = sceneRuntimePrepareReportFrom(prepared);
    if (!report.success) {
        return report;
    }

    impl_->sceneRuntimeCustomGraphPrepareCount = prepared.prepareCount;
    impl_->sceneRuntimeCustomGraphPrepareReport = report;
    impl_->lastSceneRuntimeCustomGraphFrameReport = {};
    impl_->lastSceneRuntimeCustomGraphFrameReport.prepareCount = prepared.prepareCount;
    impl_->lastSceneRuntimeCustomGraphFrameReport.surfaceBackedVulkan = false;
    impl_->lastSceneRuntimeCustomGraphFrameReport.backendFallbacksHiddenFromUpperLayer =
        report.backendFallbacksHiddenFromUpperLayer;
    impl_->lastSceneRuntimeCustomGraphFrameReport.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer;
    impl_->sceneRuntimeCustomGraphPendingPresent = false;
    impl_->sceneRuntimeCustomGraphPendingPresentSerial = 0u;
    return report;
}

const RenderSystemSceneRuntimeCustomGraphFrameReport&
RenderSystem::lastSceneRuntimeCustomGraphFrameReport() const
{
    static const RenderSystemSceneRuntimeCustomGraphFrameReport emptyReport;
    return impl_ ? impl_->lastSceneRuntimeCustomGraphFrameReport : emptyReport;
}

RenderSystemRhiProductionFrameSubmitReport RenderSystem::submitRhiProductionFrame(
    const RenderSystemRhiProductionFrameSubmitDesc& desc)
{
    RenderSystemRhiProductionFrameSubmitReport report;
    if (!valid() || !impl_->device || !impl_->swapchain) {
        report.diagnostics =
            "WebGPU RenderSystem RHI production frame submit requires a valid render system";
        return report;
    }
    if ((impl_->backend != rhi::Backend::WebGPU && impl_->backend != rhi::Backend::Metal)
        || !impl_->surfaceBackedPresentation) {
        report.diagnostics =
            "WebGPU RenderSystem RHI production frame submit requires a surface-backed WebGPU backend";
        return report;
    }

    renderer::RendererRhiProductionFramePath path({
        desc.label.empty() ? "RenderSystemRhiProductionFrame" : desc.label,
        {impl_->extent.width, impl_->extent.height, 1u},
        impl_->colorFormat,
        desc.framePlanBridge,
        desc.maxDrawCommandsPerPass,
        desc.renderToSwapchain,
        desc.shaderArtifactRoot,
    });

    const auto prepared = path.prepare(desc.framePlan, *impl_->device);
    report.usedRhiGpuDevice = prepared.usedRhiGpuDevice;
    report.rendererFramePlanAccepted = prepared.rendererFramePlanAccepted;
    report.rhiResourceCreated = prepared.rhiResourceCreated;
    report.rhiPipelineCreated = prepared.rhiPipelineCreated;
    report.rhiDescriptorCreated = prepared.rhiDescriptorCreated;
    report.shaderPackageValidated = prepared.shaderPackageValidated;
    report.fullSurfaceViewportConfigured = prepared.fullSurfaceViewportConfigured;
    report.fullSurfaceScissorConfigured = prepared.fullSurfaceScissorConfigured;
    report.backendFallbacksHiddenFromUpperLayer = prepared.backendFallbacksHiddenFromUpperLayer;
    report.nativeHandleExposedToUpperLayer = prepared.nativeHandleExposedToUpperLayer;
    arp::core::appendDiagnostic(report.diagnostics, prepared.diagnostics);
    if (!prepared.success) {
        return report;
    }
    report.productionFramePreparedBeforeSubmit = true;

    const auto frameCount = std::max(1u, desc.frameCount);
    for (std::uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex) {
        rhi::AcquiredFrame acquired;
        rhi::AcquiredSwapchainImage acquiredImage;
        if (desc.renderToSwapchain) {
            acquired = acquireFrame();
            arp::core::appendDiagnostic(report.diagnostics, acquired.diagnostics);
            report.acquireFrame = report.acquireFrame || acquired.success;
            if (!acquired.success) {
                return report;
            }
            acquiredImage = impl_->swapchain->acquiredImage(acquired);
            arp::core::appendDiagnostic(report.diagnostics, acquiredImage.diagnostics);
            if (!acquiredImage.success || !acquiredImage.texture) {
                return report;
            }
            report.swapchainRenderTargetUsed = true;
        }

        const auto submitted = desc.renderToSwapchain
            ? path.submit(*impl_->device, *acquiredImage.texture)
            : path.submit(*impl_->device);
        arp::core::appendDiagnostic(report.diagnostics, submitted.diagnostics);
        report.rhiCommandEncoded = report.rhiCommandEncoded || submitted.rhiCommandEncoded;
        report.rhiQueueSubmitted = report.rhiQueueSubmitted || submitted.rhiQueueSubmitted;
        report.proceduralSceneRuntime3DObjectDrawn = report.proceduralSceneRuntime3DObjectDrawn
            || submitted.proceduralSceneRuntime3DObjectDrawn;
        report.proceduralSceneRuntime2DSpriteDrawn = report.proceduralSceneRuntime2DSpriteDrawn
            || submitted.proceduralSceneRuntime2DSpriteDrawn;
        report.proceduralSceneRuntimeUiDrawn =
            report.proceduralSceneRuntimeUiDrawn || submitted.proceduralSceneRuntimeUiDrawn;
        report.resourceAllocatedDuringSubmit =
            report.resourceAllocatedDuringSubmit || submitted.resourceAllocatedDuringSubmit;
        report.pipelineCreatedDuringSubmit =
            report.pipelineCreatedDuringSubmit || submitted.pipelineCreatedDuringSubmit;
        report.backendFallbacksHiddenFromUpperLayer = report.backendFallbacksHiddenFromUpperLayer
            && submitted.backendFallbacksHiddenFromUpperLayer;
        report.nativeHandleExposedToUpperLayer =
            report.nativeHandleExposedToUpperLayer || submitted.nativeHandleExposedToUpperLayer;
        report.renderPassCount += submitted.renderPassCount;
        report.drawCommandCount += submitted.drawCommandCount;
        if (!submitted.success) {
            return report;
        }
        if (desc.expectedDrawCommandCount != 0u
            && submitted.drawCommandCount != desc.expectedDrawCommandCount) {
            arp::core::appendDiagnostic(report.diagnostics,
                "WebGPU RenderSystem production submit draw count did not match the prepared frame plan");
            return report;
        }

        if (!desc.renderToSwapchain) {
            acquired = acquireFrame();
            arp::core::appendDiagnostic(report.diagnostics, acquired.diagnostics);
            report.acquireFrame = report.acquireFrame || acquired.success;
            if (!acquired.success) {
                return report;
            }
        }
        const auto presented = presentFrame(acquired);
        arp::core::appendDiagnostic(report.diagnostics, presented.diagnostics);
        report.presentFrame = report.presentFrame || presented.success;
        if (!presented.success) {
            return report;
        }
        ++report.framesSubmitted;
    }

    applyNativeProductionFrameSubmitTelemetry(
        report, *impl_->device, impl_->extent, impl_->colorFormat);
    report.success = report.productionRendererPath && report.usedRhiGpuDevice
        && report.productionFramePreparedBeforeSubmit && report.rendererFramePlanAccepted
        && report.rhiResourceCreated && report.rhiPipelineCreated && report.rhiDescriptorCreated
        && report.shaderPackageValidated && report.fullSurfaceViewportConfigured
        && report.fullSurfaceScissorConfigured && report.rhiCommandEncoded
        && report.rhiQueueSubmitted && report.acquireFrame && report.presentFrame
        && report.framesSubmitted == frameCount && report.hiddenWindowRenderOutputHashCaptured
        && report.deterministicRenderOutputHash != 0u && !report.resourceAllocatedDuringSubmit
        && !report.pipelineCreatedDuringSubmit && !report.nativeHandleExposedToUpperLayer
        && report.backendFallbacksHiddenFromUpperLayer;
    if (!report.success && report.diagnostics.empty()) {
        report.diagnostics = !report.hiddenWindowRenderOutputHashCaptured
            ? rhi::semantics::RenderOutputObservationContract::unavailableDiagnostic
            : "Native RenderSystem RHI production frame submit failed validation";
    }
    return report;
}

RenderSystemResult RenderSystem::configureRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunDesc& desc)
{
    if (!impl_) {
        impl_ = core::makeUnique<Impl>();
    }
    impl_->rhiDryRunDesc = desc;
    impl_->lastRhiDryRunReport = {};
    impl_->lastRhiDryRunSmokeReport = {};
    impl_->lastRhiStartupProbeReport = {};
    impl_->lastRhiStartupGateReport = {};
    impl_->lastRhiStartupProbeScheduleReport = {};
    impl_->lastRhiStartupProbeBatchReport = {};
    impl_->lastRhiStartupProbeRunReport = {};
    impl_->lastRhiStartupTelemetryReport = {};
    impl_->lastRhiStartupReadinessReport = {};
    impl_->lastRhiFormalRefactorStartGateReport = {};
    return renderSystemResult(true, "");
}

RenderSystemRhiFrameLoopDryRunReport RenderSystem::evaluateRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunInput& input)
{
    if (!impl_) {
        impl_ = core::makeUnique<Impl>();
    }
    impl_->lastRhiDryRunReport =
        evaluateRenderSystemRhiFrameLoopDryRun(impl_->rhiDryRunDesc, input);
    return impl_->lastRhiDryRunReport;
}

RenderSystemRhiFrameLoopDryRunReport RenderSystem::evaluateRhiFrameLoopDryRunForCurrentState(
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    platform::RenderSystemFrameLoopState state;
    state.renderPipelineReady = valid();
    state.frameInProgress = impl_ && impl_->currentFrame.success;
    state.nextFrameSerial =
        impl_ && impl_->currentFrame.success ? impl_->currentFrame.frameSerial + 1u : 1u;
    return evaluateRhiFrameLoopDryRun({rendererFramePlan, lifecycleState, state});
}

RenderSystemRhiFrameLoopDryRunSmokeReport
RenderSystem::evaluateRhiFrameLoopDryRunSmokeForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto dryRun =
        evaluateRhiFrameLoopDryRunForCurrentState(rendererFramePlan, lifecycleState);
    impl_->lastRhiDryRunSmokeReport =
        evaluateRenderSystemRhiFrameLoopDryRunSmoke(smokeDesc, dryRun);
    return impl_->lastRhiDryRunSmokeReport;
}

RenderSystemRhiStartupProbeReport RenderSystem::evaluateRhiStartupProbeForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto smoke = evaluateRhiFrameLoopDryRunSmokeForCurrentState(
        smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeReport = evaluateRenderSystemRhiStartupProbe(
        summarizeRenderSystemRhiFrameLoopDryRunSmokeCapability(smoke));
    return impl_->lastRhiStartupProbeReport;
}

RenderSystemRhiStartupGateReport RenderSystem::evaluateRhiStartupGateForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto startupProbe =
        evaluateRhiStartupProbeForCurrentState(smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupGateReport = evaluateRenderSystemRhiStartupGate(
        summarizeRenderSystemRhiStartupProbeCapability(startupProbe));
    return impl_->lastRhiStartupGateReport;
}

RenderSystemRhiStartupProbeScheduleReport RenderSystem::scheduleRhiStartupProbeForCurrentState(
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto startupGate =
        evaluateRhiStartupGateForCurrentState(smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeScheduleReport =
        scheduleRenderSystemRhiStartupProbe(scheduleDesc, startupGate);
    return impl_->lastRhiStartupProbeScheduleReport;
}

RenderSystemRhiStartupProbeBatchReport RenderSystem::planRhiStartupProbeBatchForCurrentState(
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto schedule = scheduleRhiStartupProbeForCurrentState(
        scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeBatchReport = planRenderSystemRhiStartupProbeBatch(schedule);
    return impl_->lastRhiStartupProbeBatchReport;
}

RenderSystemRhiStartupProbeRunReport RenderSystem::runRhiStartupProbeForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto batch = planRhiStartupProbeBatchForCurrentState(
        scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupProbeRunReport = runRenderSystemRhiStartupProbeBatch(runnerDesc, batch);
    return impl_->lastRhiStartupProbeRunReport;
}

RenderSystemRhiStartupTelemetryReport RenderSystem::evaluateRhiStartupTelemetryForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto run = runRhiStartupProbeForCurrentState(
        runnerDesc, scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupTelemetryReport = collectRenderSystemRhiStartupTelemetry(run);
    return impl_->lastRhiStartupTelemetryReport;
}

RenderSystemRhiStartupReadinessReport RenderSystem::evaluateRhiStartupReadinessForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto telemetry = evaluateRhiStartupTelemetryForCurrentState(
        runnerDesc, scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiStartupReadinessReport = evaluateRenderSystemRhiStartupReadiness(telemetry);
    return impl_->lastRhiStartupReadinessReport;
}

RenderSystemRhiFormalRefactorStartGateReport
RenderSystem::evaluateRhiFormalRefactorStartGateForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
    const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
    const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
    const platform::PlatformLifecycleState& lifecycleState)
{
    const auto readiness = evaluateRhiStartupReadinessForCurrentState(
        runnerDesc, scheduleDesc, smokeDesc, rendererFramePlan, lifecycleState);
    impl_->lastRhiFormalRefactorStartGateReport =
        evaluateRenderSystemRhiFormalRefactorStartGate(readiness);
    return impl_->lastRhiFormalRefactorStartGateReport;
}

const RenderSystemRhiFrameLoopDryRunReport& RenderSystem::lastRhiFrameLoopDryRunReport() const
{
    static const RenderSystemRhiFrameLoopDryRunReport emptyReport;
    return impl_ ? impl_->lastRhiDryRunReport : emptyReport;
}

const RenderSystemRhiFrameLoopDryRunSmokeReport&
RenderSystem::lastRhiFrameLoopDryRunSmokeReport() const
{
    static const RenderSystemRhiFrameLoopDryRunSmokeReport emptyReport;
    return impl_ ? impl_->lastRhiDryRunSmokeReport : emptyReport;
}

const RenderSystemRhiStartupProbeReport& RenderSystem::lastRhiStartupProbeReport() const
{
    static const RenderSystemRhiStartupProbeReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeReport : emptyReport;
}

const RenderSystemRhiStartupGateReport& RenderSystem::lastRhiStartupGateReport() const
{
    static const RenderSystemRhiStartupGateReport emptyReport;
    return impl_ ? impl_->lastRhiStartupGateReport : emptyReport;
}

const RenderSystemRhiStartupProbeScheduleReport&
RenderSystem::lastRhiStartupProbeScheduleReport() const
{
    static const RenderSystemRhiStartupProbeScheduleReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeScheduleReport : emptyReport;
}

const RenderSystemRhiStartupProbeBatchReport& RenderSystem::lastRhiStartupProbeBatchReport() const
{
    static const RenderSystemRhiStartupProbeBatchReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeBatchReport : emptyReport;
}

const RenderSystemRhiStartupProbeRunReport& RenderSystem::lastRhiStartupProbeRunReport() const
{
    static const RenderSystemRhiStartupProbeRunReport emptyReport;
    return impl_ ? impl_->lastRhiStartupProbeRunReport : emptyReport;
}

const RenderSystemRhiStartupTelemetryReport& RenderSystem::lastRhiStartupTelemetryReport() const
{
    static const RenderSystemRhiStartupTelemetryReport emptyReport;
    return impl_ ? impl_->lastRhiStartupTelemetryReport : emptyReport;
}

const RenderSystemRhiStartupReadinessReport& RenderSystem::lastRhiStartupReadinessReport() const
{
    static const RenderSystemRhiStartupReadinessReport emptyReport;
    return impl_ ? impl_->lastRhiStartupReadinessReport : emptyReport;
}

const RenderSystemRhiFormalRefactorStartGateReport&
RenderSystem::lastRhiFormalRefactorStartGateReport() const
{
    static const RenderSystemRhiFormalRefactorStartGateReport emptyReport;
    return impl_ ? impl_->lastRhiFormalRefactorStartGateReport : emptyReport;
}

} // namespace arp::platform
