#include "platform/render_system.h"
#include "platform/render_system_ordered_profile.h"
#include "rhi/backends/webgl/webgl_device.h"
#include "rhi/semantics/webgl_shader_coordinates.h"
#include "shader/webgl_shader_program_artifact.h"

#include <chrono>
#include <emscripten/html5.h>

namespace arp::platform {
namespace {
constexpr const char* kNativeProfileRequired = "PLATFORM-ORDERED-NATIVE-FRAME-PROFILE-REQUIRED";
void* visibilityOwner = nullptr;

template <class Report> Report unsupported()
{
    Report report;
    report.diagnostics = kNativeProfileRequired;
    return report;
}

template <class Report> const Report& unsupportedObservation()
{
    static const auto report = unsupported<Report>();
    return report;
}
} // namespace

struct RenderSystem::Impl {
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    rhi::Backend backend = rhi::Backend::WebGL;
    rhi::Extent2D extent{};
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution;
    core::UniquePtr<RenderSystemOrderedGraphicsPath> path;
    bool suspended = false;
    bool visibilityDirty = false;
    bool requestedAvailable = true;

    ~Impl()
    {
        if (visibilityOwner == this) {
            emscripten_set_visibilitychange_callback(nullptr, false, nullptr);
            visibilityOwner = nullptr;
        }
    }
};

RenderSystem::RenderSystem() = default;

RenderSystem::RenderSystem(core::UniquePtr<Impl> impl) : impl_(std::move(impl))
{}

RenderSystem::RenderSystem(RenderSystem&&) noexcept = default;
RenderSystem& RenderSystem::operator=(RenderSystem&&) noexcept = default;
RenderSystem::~RenderSystem() = default;

RenderSystem createRenderSystem(const RenderSystemDesc& desc)
{
    auto impl = core::makeUnique<RenderSystem::Impl>();
    const auto fail = [&](const char* id) {
        impl->diagnostics = id;
    };
    const auto route = RenderSystemBackendSelection::forBuild(desc.backendPreference,
        rhi::Backend::WebGL,
        desc.orderedGraphics && desc.orderedGraphics->prototypeOptIn);
    impl->backend = route.requestedBackend;
    if (!route.routeToFactory) {
        fail(route.diagnostic);
        return RenderSystem(std::move(impl));
    }
    if (!desc.orderedGraphics) {
        fail("PLATFORM-ORDERED-PROFILE-MISSING");
        return RenderSystem(std::move(impl));
    }
    const auto& p = *desc.orderedGraphics;
    if (const auto* id = RenderSystemOrderedProfileContract::validate(rhi::Backend::WebGL, p)) {
        fail(id);
        return RenderSystem(std::move(impl));
    }
    if (desc.surface.window || desc.surface.windowSystem
        || desc.presentationMode != rhi::PresentationMode::Auto) {
        fail("PLATFORM-ORDERED-PROFILE-SURFACE");
        return RenderSystem(std::move(impl));
    }
    if (visibilityOwner) {
        fail("PLATFORM-ORDERED-SURFACE-OWNER-ACTIVE");
        return RenderSystem(std::move(impl));
    }
    try {
        const auto& b = p.budgets;
        const char* id = nullptr;
        rhi::semantics::WebGlContextOwnershipRequest context;
        context.contractVersion = context.context.contractVersion = 1;
        context.canvasSelector = p.frames.surface.surfaceName;
        context.colorSpace = rhi::semantics::WebGlCanvasColorSpace::SRgb;
        context.maxCpuMetadataBytes = b.contextMetadataBytes;
        context.maxIdentityFieldBytes = b.identityFieldBytes;
        context.extensionOptIn.contractVersion = 1;
        context.extensionOptIn.extensions.depthTextures = p.sampledDepthOptIn;
        auto runtime = rhi::webgl::WebGlRuntime::create(
            context, core::makeShared<core::CpuMemoryQuota>(b.contextMetadataBytes), id);
        if (!runtime) {
            fail(id);
            return RenderSystem(std::move(impl));
        }
        rhi::semantics::WebGlResourceOwnershipRequest ownership;
        ownership.contractVersion = ownership.budget.contractVersion = 1;
        ownership.budget.capacity.cpuMetadataBytes = b.resourceMetadataBytes;
        ownership.budget.capacity.cpuUploadBytes = b.uploadBytes;
        ownership.budget.capacity.objectCount = b.resourceObjects;
        ownership.budget.capacity.logicalBufferBytes = b.logicalBufferBytes;
        ownership.budget.capacity.logicalTextureBytes = b.logicalTextureBytes;
        if (context.extensionOptIn.extensions.any())
            ownership.extensions = {1, true, context.extensionOptIn.extensions};
        auto resources = rhi::webgl::WebGlResources::create(std::move(runtime),
            ownership,
            core::makeShared<core::CpuMemoryQuota>(b.resourceMetadataBytes),
            core::makeShared<core::CpuMemoryQuota>(b.uploadBytes),
            id);
        if (!resources) {
            fail(id);
            return RenderSystem(std::move(impl));
        }
        rhi::semantics::WebGlDeviceCompositionRequest composition{
            1, true, b.deviceMetadataBytes, b.pipelines, b.descriptors};
        auto device = rhi::webgl::WebGlDevice::create(std::move(resources),
            composition,
            core::makeShared<core::CpuMemoryQuota>(b.deviceMetadataBytes),
            id);
        if (!device) {
            fail(id);
            return RenderSystem(std::move(impl));
        }
        impl->execution = core::shareUnique(device->createOrderedGraphicsExecution(p.execution,
            core::makeShared<core::CpuMemoryQuota>(p.execution.maxCpuMetadataBytes),
            id));
        if (!impl->execution
            || (id = device->enableSurfaceLifecycle({1, true, b.surfaceMutations}))) {
            fail(id);
            return RenderSystem(std::move(impl));
        }
        impl->path = RenderSystem::createOrderedGraphicsPath(
            std::move(device), impl->execution, p.frames, id);
        if (!impl->path)
            fail(id);
        else {
            impl->extent = p.frames.surface.extent;
            const auto registered = emscripten_set_visibilitychange_callback(impl.get(),
                false,
                [](int, const EmscriptenVisibilityChangeEvent*, void* user) -> EM_BOOL {
                    static_cast<RenderSystem::Impl*>(user)->visibilityDirty = true;
                    return EM_FALSE;
                });
            if (registered != EMSCRIPTEN_RESULT_SUCCESS) {
                impl->path.reset();
                impl->execution.reset();
                fail("PLATFORM-ORDERED-VISIBILITY-SUBSCRIPTION");
            } else
                visibilityOwner = impl.get();
        }
    } catch (const std::exception& error) {
        impl->path.reset();
        impl->execution.reset();
        impl->diagnostics = error.what();
    }
    return RenderSystem(std::move(impl));
}

RenderSystem createDefaultRenderSystem(WindowSystem&,
    const PlatformWindow&,
    const std::pmr::string& name,
    rhi::PresentationMode mode,
    const rhi::semantics::UploadStagingBudgetV1*,
    const rhi::semantics::DirectObjectBudget*)
{
    RenderSystemDesc desc;
    desc.applicationName = name;
    desc.presentationMode = mode;
    return createRenderSystem(desc);
}

bool RenderSystem::valid() const
{
    return impl_ && impl_->path && impl_->diagnostics.empty();
}

const std::pmr::string& RenderSystem::diagnostics() const
{
    static const core::pmr::String empty{&core::defaultCpuMemoryResource()};
    return impl_ ? impl_->diagnostics : empty;
}

rhi::Backend RenderSystem::backend() const
{
    return impl_ ? impl_->backend : rhi::Backend::Null;
}

RuntimeCapabilities RenderSystem::capabilities() const
{
    RuntimeCapabilities caps;
    caps.backend = backend();
    caps.surfaceBacked = valid();
    caps.canRender = valid() && !impl_->suspended && impl_->extent.width && impl_->extent.height;
    caps.diagnostics = diagnostics();
    return caps;
}

OrderedRenderSystemCapabilities RenderSystem::orderedGraphicsCapabilities() const
{
    if (!valid())
        return {};
    return {true,
        true,
        true,
        false,
        rhi::semantics::WebGlShaderCoordinates::attachmentSampleFlipY,
        rhi::semantics::WebGlShaderCoordinates::readbackReverseRows};
}

std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>
RenderSystem::orderedGraphicsExecution() const
{
    return valid() ? impl_->execution : nullptr;
}

rhi::GpuDevice* RenderSystem::gpuDevice() noexcept
{
    return valid() ? &impl_->path->device() : nullptr;
}

const rhi::GpuDevice* RenderSystem::gpuDevice() const noexcept
{
    return valid() ? &impl_->path->device() : nullptr;
}

rhi::Extent2D RenderSystem::extent() const
{
    return valid() ? impl_->extent : rhi::Extent2D{};
}

rhi::TextureFormat RenderSystem::colorFormat() const
{
    return valid() ? rhi::TextureFormat::Rgba8Unorm : rhi::TextureFormat::Unknown;
}

rhi::MemoryStats RenderSystem::memoryStats() const
{
    return valid() ? gpuDevice()->memoryStats() : rhi::MemoryStats{};
}

rhi::GpuTimingSample RenderSystem::latestGpuTiming() const
{
    return valid() ? gpuDevice()->latestGpuTiming() : rhi::GpuTimingSample{};
}

rhi::semantics::UploadStagingReadiness RenderSystem::uploadStagingReadiness() const
{
    return valid() ? gpuDevice()->uploadStagingReadiness()
                   : rhi::semantics::uploadStagingReadinessUnavailable();
}

std::shared_ptr<rhi::RenderPipeline> RenderSystem::createOrderedPipeline(
    const OrderedPipelineLoadDesc& request, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    if (!valid()) {
        diagnostic = "PLATFORM-ORDERED-SYSTEM-INVALID";
        return {};
    }
    if (!request.maxInterfaceNameBytes || !request.maxDiagnosticBytes || request.pipeline.layout
        || request.pipeline.shaderPackage || !request.pipeline.vertexShader.entryPoint.empty()
        || !request.pipeline.fragmentShader.entryPoint.empty()) {
        diagnostic = "PLATFORM-ORDERED-PIPELINE-REQUEST";
        return {};
    }
    try {
        shader::WebGlShaderLoadBudget budget{request.artifactBudget,
            request.maxUniformLeaves,
            request.maxInterfaces,
            request.maxTypeNodes};
        auto artifact = shader::WebGlShaderProgramArtifact::load(request.artifactPath,
            budget,
            core::makeShared<core::CpuMemoryQuota>(budget.original.maxCpuMetadataBytes),
            diagnostic);
        if (!artifact)
            return {};
        rhi::ShaderPackageDesc package;
        const auto reflected = rhi::populateSelectedGraphicsShaderInterface(
            *artifact->vertex().original, artifact->fragment().original.get(), package);
        if (!reflected.success) {
            diagnostic = reflected.diagnostics;
            return {};
        }
        auto& device = static_cast<rhi::webgl::WebGlDevice&>(impl_->path->device());
        auto definition = request.layout;
        if (request.useReflectedLayout) {
            if (!definition.pushConstantRanges.empty()) {
                diagnostic = "PLATFORM-ORDERED-PIPELINE-REFLECTED-LAYOUT";
                return {};
            }
            for (const auto& set : definition.descriptorLayouts) {
                if (!set.bindings.empty()) {
                    diagnostic = "PLATFORM-ORDERED-PIPELINE-REFLECTED-LAYOUT";
                    return {};
                }
            }
            auto reflectedLayout =
                rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection);
            if (!reflectedLayout.pushConstantRanges.empty()) {
                const auto stages = rhi::parseShaderReflectionPushConstantStages(
                    package.reflection.pushConstantStages);
                if (!stages) {
                    diagnostic = "PLATFORM-ORDERED-PIPELINE-PUSH-STAGES";
                    return {};
                }
                reflectedLayout.pushConstantRanges[0].stages = *stages;
            }
            for (const auto& layout : definition.descriptorLayouts)
                reflectedLayout.descriptorLayouts.push_back(layout);
            for (const auto& set : definition.descriptorSets)
                reflectedLayout.descriptorSets.push_back(set);
            definition = std::move(reflectedLayout);
        }
        auto layout = core::shareUnique(device.createPipelineLayout(definition));
        if (!layout) {
            diagnostic = device.diagnostic();
            return {};
        }
        const rhi::semantics::WebGlPipelineCreationRequest selection{1,
            {1, request.maxInterfaceNameBytes, request.maxDiagnosticBytes},
            request.stencilReference};
        if (const auto* id = device.selectPipelineInputs(layout, artifact, selection)) {
            diagnostic = id;
            return {};
        }
        auto desc = request.pipeline;
        desc.layout = layout.get();
        desc.vertexShader = package.entrypoints[0];
        desc.fragmentShader = package.entrypoints[1];
        auto result = core::shareUnique(device.createRenderPipeline(desc));
        if (!result)
            diagnostic = device.diagnostic();
        return result;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return {};
    }
}

RenderSystemOrderedGraphicsFrameReport RenderSystem::renderOrderedFrame(
    OrderedGraphicsFrameWorkload& workload)
{
    if (!valid()) {
        RenderSystemOrderedGraphicsFrameReport report;
        report.diagnostics = "PLATFORM-ORDERED-SYSTEM-INVALID";
        return report;
    }
    const auto surface = synchronizeSurfaceLifecycle();
    if (!surface.success || impl_->suspended || !impl_->extent.width || !impl_->extent.height) {
        RenderSystemOrderedGraphicsFrameReport report;
        report.diagnostics = !surface.success ? surface.diagnostics
            : impl_->suspended                ? "PLATFORM-ORDERED-SURFACE-SUSPENDED"
                                              : "PLATFORM-ORDERED-SURFACE-ZERO-EXTENT";
        return report;
    }
    auto report = impl_->path->renderFrame(workload);
    if (!report.success && !impl_->suspended)
        impl_->diagnostics = report.diagnostics;
    return report;
}

RenderSystemResult RenderSystem::setSurfaceAvailable(bool available)
{
    if (!valid())
        return {false, "PLATFORM-ORDERED-SYSTEM-INVALID"};
    impl_->requestedAvailable = available;
    return synchronizeSurfaceLifecycle();
}

RenderSystemResult RenderSystem::synchronizeSurfaceLifecycle()
{
    if (!valid())
        return {false, "PLATFORM-ORDERED-SYSTEM-INVALID"};
    EmscriptenVisibilityChangeEvent visibility{};
    if (emscripten_get_visibility_status(&visibility) != EMSCRIPTEN_RESULT_SUCCESS)
        return {false, "PLATFORM-ORDERED-VISIBILITY-QUERY"};
    // RAF runs after capture and bubble listeners have completed.
    const bool available = impl_->requestedAvailable && !visibility.hidden;
    if (impl_->visibilityDirty || available == impl_->suspended) {
        const auto result = impl_->path->setSurfaceAvailable(available);
        if (result.success) {
            impl_->suspended = !available;
            impl_->visibilityDirty = false;
        }
        return {result.success, result.diagnostics};
    }
    return {true, {}};
}

RenderSystemResult RenderSystem::resize(rhi::Extent2D extent)
{
    if (!valid())
        return {false, "PLATFORM-ORDERED-SYSTEM-INVALID"};
    const auto result = impl_->path->resize(extent);
    if (result.success)
        impl_->extent = extent;
    return {result.success, result.diagnostics};
}

void RenderSystem::waitGpuIdle() const
{
    if (auto* device = gpuDevice())
        const_cast<rhi::GpuDevice*>(device)->waitIdle();
}

rhi::semantics::ShutdownDrainReport RenderSystem::drainGpuForShutdown(
    const rhi::semantics::ShutdownDrainRequest& request) const
{
    if (auto* device = gpuDevice())
        return const_cast<rhi::GpuDevice*>(device)->drainForShutdown(request);
    const auto now = std::chrono::steady_clock::now();
    rhi::semantics::ShutdownDrainReport report;
    report.backend = backend();
    report.diagnosticId = "ARP-PLATFORM-SHUTDOWN-DRAIN-RENDER-SYSTEM-INVALID";
    rhi::semantics::finalizeShutdownDrainTiming(report, now, request, now);
    return report;
}

// Native frame APIs cannot imply completion or bypass the ordered issuer.
rhi::Swapchain* RenderSystem::swapchain() noexcept
{
    return nullptr;
}

rhi::SwapchainPresentationStatus RenderSystem::presentationStatus() const
{
    return {};
}

RenderSystemResult RenderSystem::setPresentationMode(rhi::PresentationMode)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemResult RenderSystem::uploadMesh(const renderer::MeshAsset&, renderer::MeshGpuBuffer&)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemResult RenderSystem::setRenderPipeline(std::unique_ptr<renderer::RenderPipeline>&&)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemResult RenderSystem::setRenderPipeline(core::UniquePtr<renderer::RenderPipeline>&&)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemResult RenderSystem::createParticleRenderer(
    const std::pmr::string&, std::uint32_t, const std::pmr::string&, const std::pmr::string&)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemResult RenderSystem::createGuiRenderer(const std::pmr::string&)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemResult RenderSystem::createGuiRenderer(const renderer::GuiRendererDesc&)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemResult RenderSystem::updateGuiAtlasTexture(const renderer::GuiAtlasTextureDesc&,
    const core::pmr::Vector<renderer::ui::GlyphAtlasUploadSchedule>&)
{
    return unsupported<RenderSystemResult>();
}

renderer::UiPassTelemetry RenderSystem::guiPassTelemetry() const
{
    return {};
}

rhi::AcquiredFrame RenderSystem::acquireFrame()
{
    return unsupported<rhi::AcquiredFrame>();
}

renderer::RendererResult RenderSystem::renderGraphFrame(
    const rhi::AcquiredFrame&, const renderer::CustomRenderFrameWorkload&)
{
    return unsupported<renderer::RendererResult>();
}

rhi::PresentResult RenderSystem::presentFrame(const rhi::AcquiredFrame&)
{
    return unsupported<rhi::PresentResult>();
}

RenderSystemPreparedRendererFramePrepareReport RenderSystem::prepareRendererFrame(
    const RenderSystemPreparedRendererFramePrepareDesc&)
{
    return unsupported<RenderSystemPreparedRendererFramePrepareReport>();
}

RenderSystemSceneRuntimeCustomGraphPrepareReport RenderSystem::prepareSceneRuntimeCustomGraphFrame(
    const RenderSystemSceneRuntimeCustomGraphPrepareDesc&)
{
    return unsupported<RenderSystemSceneRuntimeCustomGraphPrepareReport>();
}

const RenderSystemSceneRuntimeCustomGraphFrameReport&
RenderSystem::lastSceneRuntimeCustomGraphFrameReport() const
{
    return unsupportedObservation<RenderSystemSceneRuntimeCustomGraphFrameReport>();
}

RenderSystemRhiProductionFrameSubmitReport RenderSystem::submitRhiProductionFrame(
    const RenderSystemRhiProductionFrameSubmitDesc&)
{
    return unsupported<RenderSystemRhiProductionFrameSubmitReport>();
}

RenderSystemResult RenderSystem::configureRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunDesc&)
{
    return unsupported<RenderSystemResult>();
}

RenderSystemRhiFrameLoopDryRunReport RenderSystem::evaluateRhiFrameLoopDryRun(
    const RenderSystemRhiFrameLoopDryRunInput&)
{
    return unsupported<RenderSystemRhiFrameLoopDryRunReport>();
}

RenderSystemRhiFrameLoopDryRunReport RenderSystem::evaluateRhiFrameLoopDryRunForCurrentState(
    const renderer::RendererRhiFramePlanBuildResult&, const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiFrameLoopDryRunReport>();
}

RenderSystemRhiFrameLoopDryRunSmokeReport
RenderSystem::evaluateRhiFrameLoopDryRunSmokeForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiFrameLoopDryRunSmokeReport>();
}

RenderSystemRhiStartupProbeReport RenderSystem::evaluateRhiStartupProbeForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiStartupProbeReport>();
}

RenderSystemRhiStartupGateReport RenderSystem::evaluateRhiStartupGateForCurrentState(
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiStartupGateReport>();
}

RenderSystemRhiStartupProbeScheduleReport RenderSystem::scheduleRhiStartupProbeForCurrentState(
    const RenderSystemRhiStartupProbeScheduleDesc&,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiStartupProbeScheduleReport>();
}

RenderSystemRhiStartupProbeBatchReport RenderSystem::planRhiStartupProbeBatchForCurrentState(
    const RenderSystemRhiStartupProbeScheduleDesc&,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiStartupProbeBatchReport>();
}

RenderSystemRhiStartupProbeRunReport RenderSystem::runRhiStartupProbeForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc&,
    const RenderSystemRhiStartupProbeScheduleDesc&,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiStartupProbeRunReport>();
}

RenderSystemRhiStartupTelemetryReport RenderSystem::evaluateRhiStartupTelemetryForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc&,
    const RenderSystemRhiStartupProbeScheduleDesc&,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiStartupTelemetryReport>();
}

RenderSystemRhiStartupReadinessReport RenderSystem::evaluateRhiStartupReadinessForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc&,
    const RenderSystemRhiStartupProbeScheduleDesc&,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiStartupReadinessReport>();
}

RenderSystemRhiFormalRefactorStartGateReport
RenderSystem::evaluateRhiFormalRefactorStartGateForCurrentState(
    const RenderSystemRhiStartupProbeRunnerDesc&,
    const RenderSystemRhiStartupProbeScheduleDesc&,
    const RenderSystemRhiFrameLoopDryRunSmokeDesc&,
    const renderer::RendererRhiFramePlanBuildResult&,
    const PlatformLifecycleState&)
{
    return unsupported<RenderSystemRhiFormalRefactorStartGateReport>();
}

const RenderSystemRhiFrameLoopDryRunReport& RenderSystem::lastRhiFrameLoopDryRunReport() const
{
    return unsupportedObservation<RenderSystemRhiFrameLoopDryRunReport>();
}

const RenderSystemRhiFrameLoopDryRunSmokeReport&
RenderSystem::lastRhiFrameLoopDryRunSmokeReport() const
{
    return unsupportedObservation<RenderSystemRhiFrameLoopDryRunSmokeReport>();
}

const RenderSystemRhiStartupProbeReport& RenderSystem::lastRhiStartupProbeReport() const
{
    return unsupportedObservation<RenderSystemRhiStartupProbeReport>();
}

const RenderSystemRhiStartupGateReport& RenderSystem::lastRhiStartupGateReport() const
{
    return unsupportedObservation<RenderSystemRhiStartupGateReport>();
}

const RenderSystemRhiStartupProbeScheduleReport&
RenderSystem::lastRhiStartupProbeScheduleReport() const
{
    return unsupportedObservation<RenderSystemRhiStartupProbeScheduleReport>();
}

const RenderSystemRhiStartupProbeBatchReport& RenderSystem::lastRhiStartupProbeBatchReport() const
{
    return unsupportedObservation<RenderSystemRhiStartupProbeBatchReport>();
}

const RenderSystemRhiStartupProbeRunReport& RenderSystem::lastRhiStartupProbeRunReport() const
{
    return unsupportedObservation<RenderSystemRhiStartupProbeRunReport>();
}

const RenderSystemRhiStartupTelemetryReport& RenderSystem::lastRhiStartupTelemetryReport() const
{
    return unsupportedObservation<RenderSystemRhiStartupTelemetryReport>();
}

const RenderSystemRhiStartupReadinessReport& RenderSystem::lastRhiStartupReadinessReport() const
{
    return unsupportedObservation<RenderSystemRhiStartupReadinessReport>();
}

const RenderSystemRhiFormalRefactorStartGateReport&
RenderSystem::lastRhiFormalRefactorStartGateReport() const
{
    return unsupportedObservation<RenderSystemRhiFormalRefactorStartGateReport>();
}
} // namespace arp::platform
