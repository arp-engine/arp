#pragma once
#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "platform/render_backend_preference.h"
#include "platform/render_system_rhi_dry_run.h"
#include "platform/render_system_rhi_startup_readiness.h"
#include "platform/window.h"
#include "renderer/render_frame.h"
#include "renderer/renderer_result.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/frontend/memory_shutdown_drain.h"
#include "rhi/frontend/memory_upload_staging_budget.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/render_output_observation.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arp::assets {
struct MeshAsset;
}

namespace arp::rhi {
class GpuDevice;
class OriginalOrderedGraphicsExecution;
} // namespace arp::rhi

namespace arp::renderer::ui {
struct GlyphAtlasUploadSchedule;
}

namespace arp::renderer {
using MeshAsset = assets::MeshAsset;
class CustomRenderFrameWorkload;
class MeshGpuBuffer;
class RenderPipeline;
struct GuiAtlasTextureDesc;
struct GuiRendererDesc;
struct SceneConstants;
} // namespace arp::renderer

namespace arp::platform {

class RenderSystemOrderedGraphicsPath;
struct RenderSystemOrderedGraphicsDesc;
struct RenderSystemOrderedProfile;
struct OrderedPipelineLoadDesc;
struct OrderedRenderSystemCapabilities;
struct RenderSystemOrderedGraphicsFrameReport;
class OrderedGraphicsFrameWorkload;

struct RenderSystemResult {
    bool success = false;
    std::pmr::string diagnostics;
};

struct PlatformSurfaceDesc {
    WindowSystem* windowSystem = nullptr;
    const PlatformWindow* window = nullptr;
};

struct RenderSystemDesc {
    BackendPreference backendPreference = BackendPreference::Default;
    PlatformSurfaceDesc surface;
    std::pmr::string applicationName = "AnimeRenderPipeline";
    rhi::PresentationMode presentationMode = rhi::PresentationMode::Auto;
    const RenderSystemOrderedProfile* orderedGraphics = nullptr;
};

struct RuntimeCapabilities {
    rhi::Backend backend = rhi::Backend::Null;
    bool surfaceBacked = false;
    bool canRender = false;
    RenderSystemRhiFrameLoopDryRunCapabilityState rhiFrameLoopDryRun;
    RenderSystemRhiFrameLoopDryRunSmokeCapabilityState rhiFrameLoopDryRunSmoke;
    RenderSystemRhiStartupProbeCapabilityState rhiStartupProbe;
    RenderSystemRhiStartupGateReport rhiStartupGate;
    RenderSystemRhiStartupProbeScheduleReport rhiStartupProbeSchedule;
    RenderSystemRhiStartupProbeBatchReport rhiStartupProbeBatch;
    RenderSystemRhiStartupProbeRunReport rhiStartupProbeRun;
    RenderSystemRhiStartupTelemetryReport rhiStartupTelemetry;
    RenderSystemRhiStartupReadinessReport rhiStartupReadiness;
    RenderSystemRhiFormalRefactorStartGateReport rhiFormalRefactorStartGate;
    std::pmr::string diagnostics;
};

struct RenderSystemRhiProductionFrameSubmitDesc {
    std::pmr::string label = "RenderSystemRhiProductionFrame";
    renderer::RendererRhiFramePlanDesc framePlan;
    renderer::RendererRhiDrawPlanBridgeDesc framePlanBridge{2, 8, 4, 2};
    std::uint32_t maxDrawCommandsPerPass = 1024;
    std::uint32_t expectedDrawCommandCount = 0;
    std::uint32_t frameCount = 1;
    bool renderToSwapchain = false;
    std::pmr::string shaderArtifactRoot;
};

struct RenderSystemRhiProductionFrameSubmitReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool productionRendererPath = true;
    bool usedRhiGpuDevice = false;
    bool productionFramePreparedBeforeSubmit = false;
    bool rendererFramePlanAccepted = false;
    bool rhiResourceCreated = false;
    bool rhiPipelineCreated = false;
    bool rhiDescriptorCreated = false;
    bool shaderPackageValidated = false;
    bool fullSurfaceViewportConfigured = false;
    bool fullSurfaceScissorConfigured = false;
    bool rhiCommandEncoded = false;
    bool rhiQueueSubmitted = false;
    bool proceduralSceneRuntime3DObjectDrawn = false;
    bool proceduralSceneRuntime2DSpriteDrawn = false;
    bool proceduralSceneRuntimeUiDrawn = false;
    bool acquireFrame = false;
    bool presentFrame = false;
    bool swapchainRenderTargetUsed = false;
    bool resourceAllocatedDuringSubmit = false;
    bool pipelineCreatedDuringSubmit = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool vulkanNativeStatsCaptured = false;
    bool hiddenWindowRenderOutputHashCaptured = false;
    const char* renderOutputEvidenceDiagnostic =
        arp::rhi::semantics::RenderOutputObservationContract::unavailableDiagnostic;
    std::uint32_t framesSubmitted = 0;
    std::uint32_t renderPassCount = 0;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t vulkanNativeRenderPassBeginCount = 0;
    std::uint32_t vulkanNativeIndexedDrawCount = 0;
    std::uint32_t vulkanNativeQueueSubmitCount = 0;
    std::uint64_t deterministicRenderOutputHash = 0;
};

struct RenderSystemPreparedRendererFramePrepareDesc {
    std::pmr::string label = "PreparedRendererFrame";
    renderer::RendererRhiFramePlanDesc framePlan;
    renderer::RendererRhiDrawPlanBridgeDesc framePlanBridge{2, 8, 4, 2};
    std::uint32_t maxDrawCommandsPerPass = 1024;
    std::pmr::string shaderArtifactRoot;
};

struct RenderSystemPreparedRendererFramePrepareReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool usedRhiGpuDevice = false;
    bool rendererFramePlanAccepted = false;
    bool rhiResourceCreated = false;
    bool rhiPipelineCreated = false;
    bool rhiDescriptorCreated = false;
    bool rhiDescriptorWritten = false;
    bool shaderPackageValidated = false;
    bool fullSurfaceViewportConfigured = false;
    bool fullSurfaceScissorConfigured = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t prepareCount = 0;
};

struct RenderSystemSceneRuntimeCustomGraphPrepareDesc {
    std::pmr::string label = "SceneRuntimeCustomGraph";
    renderer::RendererRhiFramePlanDesc framePlan;
    renderer::RendererRhiDrawPlanBridgeDesc framePlanBridge{2, 8, 4, 2};
    std::uint32_t maxDrawCommandsPerPass = 1024;
    std::pmr::string shaderArtifactRoot;
};

struct RenderSystemSceneRuntimeCustomGraphPrepareReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool usedRhiGpuDevice = false;
    bool rendererFramePlanAccepted = false;
    bool rhiResourceCreated = false;
    bool rhiPipelineCreated = false;
    bool rhiDescriptorCreated = false;
    bool rhiDescriptorWritten = false;
    bool shaderPackageValidated = false;
    bool fullSurfaceViewportConfigured = false;
    bool fullSurfaceScissorConfigured = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t prepareCount = 0;
};

struct RenderSystemSceneRuntimeCustomGraphFrameReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool renderGraphFrameUsed = false;
    bool sceneRuntimePathSubmitted = false;
    bool legacyPipelineFallbackUsed = false;
    bool surfaceBackedVulkan = false;
    bool usedRhiGpuDevice = false;
    bool preparedBeforeSubmit = false;
    bool resourcesPreparedBeforeSubmit = false;
    bool pipelinePreparedBeforeSubmit = false;
    bool descriptorsPreparedBeforeSubmit = false;
    bool rhiCommandEncoded = false;
    bool rhiQueueSubmitted = false;
    bool swapchainRenderTargetUsed = false;
    bool proceduralSceneRuntime3DObjectDrawn = false;
    bool proceduralSceneRuntime2DSpriteDrawn = false;
    bool proceduralSceneRuntimeUiDrawn = false;
    bool resourceAllocatedDuringSubmit = false;
    bool pipelineCreatedDuringSubmit = false;
    bool hotPathFileIo = false;
    bool hotPathNetworkIo = false;
    bool backendFallbacksHiddenFromUpperLayer = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool vulkanNativeStatsCaptured = false;
    bool hiddenWindowRenderOutputHashCaptured = false;
    const char* renderOutputEvidenceDiagnostic =
        arp::rhi::semantics::RenderOutputObservationContract::unavailableDiagnostic;
    bool frameGraphHasMainScene = false;
    bool frameGraphHasParticles = false;
    bool frameGraphHasGui = false;
    bool frameGraphParticlesAfterMainScene = false;
    bool frameGraphGuiAfterParticles = false;
    std::uint32_t prepareCount = 0;
    std::uint32_t framesSubmitted = 0;
    std::uint32_t renderPassCount = 0;
    std::uint32_t drawCommandCount = 0;
    std::uint32_t frameGraphExecutionPassCount = 0;
    std::uint32_t frameGraphAttachmentTraceCount = 0;
    std::uint32_t frameGraphBarrierTraceCount = 0;
    std::uint32_t vulkanNativeRenderPassBeginCount = 0;
    std::uint32_t vulkanNativeIndexedDrawCount = 0;
    std::uint32_t vulkanNativeQueueSubmitCount = 0;
    std::uint64_t deterministicRenderOutputHash = 0;
    std::pmr::string frameGraphExecutionTrace;
    std::pmr::string frameGraphParticleAttachmentSummary;
};

class ARP_API RenderSystem {
public:
    static core::UniquePtr<RenderSystemOrderedGraphicsPath> createOrderedGraphicsPath(
        core::UniquePtr<rhi::GpuDevice>,
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>,
        const RenderSystemOrderedGraphicsDesc&,
        const char*& diagnostic);
    RenderSystem();
    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&) noexcept;
    /// Moving over a live system is a Vulkan teardown boundary: submitted
    /// work must finish before the old implementation releases its objects.
    RenderSystem& operator=(RenderSystem&&) noexcept;
    ~RenderSystem();

    bool valid() const;
    const std::pmr::string& diagnostics() const;
    RuntimeCapabilities capabilities() const;
    rhi::Backend backend() const;
    OrderedRenderSystemCapabilities orderedGraphicsCapabilities() const;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> orderedGraphicsExecution() const;
    std::shared_ptr<rhi::RenderPipeline> createOrderedPipeline(
        const OrderedPipelineLoadDesc&, core::pmr::String& diagnostic);
    RenderSystemOrderedGraphicsFrameReport renderOrderedFrame(OrderedGraphicsFrameWorkload&);
    RenderSystemResult setSurfaceAvailable(bool available);
    RenderSystemResult synchronizeSurfaceLifecycle();
    /// L2-only diagnostics/smoke access. Native backend handles and
    /// allocations remain private to the returned device implementation.
    rhi::GpuDevice* gpuDevice() noexcept;
    const rhi::GpuDevice* gpuDevice() const noexcept;
    /// L2-only presentation lifecycle access for contract smoke tests.
    /// Native swapchain handles remain backend-private.
    rhi::Swapchain* swapchain() noexcept;
    rhi::Extent2D extent() const;
    rhi::TextureFormat colorFormat() const;
    rhi::MemoryStats memoryStats() const;
    rhi::GpuTimingSample latestGpuTiming() const;
    /// Backend-neutral completion credential for typed upload-staging Deferred
    /// work. It contains neither native synchronization nor queue serials.
    rhi::semantics::UploadStagingReadiness uploadStagingReadiness() const;
    RenderSystemResult resize(rhi::Extent2D extent);
    /// Request a presentation-policy update through the RHI swapchain
    /// contract. Backends own lowering, fallback and native recreation.
    RenderSystemResult setPresentationMode(rhi::PresentationMode presentationMode);
    rhi::SwapchainPresentationStatus presentationStatus() const;
    RenderSystemResult uploadMesh(const renderer::MeshAsset& mesh, renderer::MeshGpuBuffer& output);
    RenderSystemResult setRenderPipeline(std::unique_ptr<renderer::RenderPipeline>&& pipeline);
    RenderSystemResult setRenderPipeline(arp::core::UniquePtr<renderer::RenderPipeline>&& pipeline);
    RenderSystemResult createParticleRenderer(const std::pmr::string& shaderPath,
        std::uint32_t maxParticles,
        const std::pmr::string& trailBeamShaderPath = {},
        const std::pmr::string& meshParticleShaderPath = {});
    RenderSystemResult createGuiRenderer(const std::pmr::string& shaderPath);
    RenderSystemResult createGuiRenderer(const renderer::GuiRendererDesc& desc);
    RenderSystemResult updateGuiAtlasTexture(const renderer::GuiAtlasTextureDesc& atlas,
        const arp::core::pmr::Vector<arp::renderer::ui::GlyphAtlasUploadSchedule>& uploads);
    renderer::UiPassTelemetry guiPassTelemetry() const;
    /// Blocks only at explicit teardown/recovery boundaries until all submitted
    /// native GPU work is complete.  It is deliberately not a per-frame API.
    void waitGpuIdle() const;
    /// Bounded teardown boundary. The caller owns the one absolute deadline
    /// shared with higher-level shutdown stages; platform code only forwards
    /// it to the backend-neutral RHI contract.
    rhi::semantics::ShutdownDrainReport drainGpuForShutdown(
        const rhi::semantics::ShutdownDrainRequest& request) const;
    rhi::AcquiredFrame acquireFrame();
    renderer::RendererResult renderGraphFrame(
        const rhi::AcquiredFrame& frame, const renderer::CustomRenderFrameWorkload& workload);
    rhi::PresentResult presentFrame(const rhi::AcquiredFrame& frame);
    RenderSystemPreparedRendererFramePrepareReport prepareRendererFrame(
        const RenderSystemPreparedRendererFramePrepareDesc& desc);
    RenderSystemSceneRuntimeCustomGraphPrepareReport prepareSceneRuntimeCustomGraphFrame(
        const RenderSystemSceneRuntimeCustomGraphPrepareDesc& desc);
    const RenderSystemSceneRuntimeCustomGraphFrameReport&
    lastSceneRuntimeCustomGraphFrameReport() const;
    RenderSystemRhiProductionFrameSubmitReport submitRhiProductionFrame(
        const RenderSystemRhiProductionFrameSubmitDesc& desc);
    RenderSystemResult configureRhiFrameLoopDryRun(const RenderSystemRhiFrameLoopDryRunDesc& desc);
    RenderSystemRhiFrameLoopDryRunReport evaluateRhiFrameLoopDryRun(
        const RenderSystemRhiFrameLoopDryRunInput& input);
    RenderSystemRhiFrameLoopDryRunReport evaluateRhiFrameLoopDryRunForCurrentState(
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiFrameLoopDryRunSmokeReport evaluateRhiFrameLoopDryRunSmokeForCurrentState(
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiStartupProbeReport evaluateRhiStartupProbeForCurrentState(
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiStartupGateReport evaluateRhiStartupGateForCurrentState(
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiStartupProbeScheduleReport scheduleRhiStartupProbeForCurrentState(
        const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiStartupProbeBatchReport planRhiStartupProbeBatchForCurrentState(
        const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiStartupProbeRunReport runRhiStartupProbeForCurrentState(
        const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
        const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiStartupTelemetryReport evaluateRhiStartupTelemetryForCurrentState(
        const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
        const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiStartupReadinessReport evaluateRhiStartupReadinessForCurrentState(
        const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
        const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    RenderSystemRhiFormalRefactorStartGateReport evaluateRhiFormalRefactorStartGateForCurrentState(
        const RenderSystemRhiStartupProbeRunnerDesc& runnerDesc,
        const RenderSystemRhiStartupProbeScheduleDesc& scheduleDesc,
        const RenderSystemRhiFrameLoopDryRunSmokeDesc& smokeDesc,
        const renderer::RendererRhiFramePlanBuildResult& rendererFramePlan,
        const platform::PlatformLifecycleState& lifecycleState);
    const RenderSystemRhiFrameLoopDryRunReport& lastRhiFrameLoopDryRunReport() const;
    const RenderSystemRhiFrameLoopDryRunSmokeReport& lastRhiFrameLoopDryRunSmokeReport() const;
    const RenderSystemRhiStartupProbeReport& lastRhiStartupProbeReport() const;
    const RenderSystemRhiStartupGateReport& lastRhiStartupGateReport() const;
    const RenderSystemRhiStartupProbeScheduleReport& lastRhiStartupProbeScheduleReport() const;
    const RenderSystemRhiStartupProbeBatchReport& lastRhiStartupProbeBatchReport() const;
    const RenderSystemRhiStartupProbeRunReport& lastRhiStartupProbeRunReport() const;
    const RenderSystemRhiStartupTelemetryReport& lastRhiStartupTelemetryReport() const;
    const RenderSystemRhiStartupReadinessReport& lastRhiStartupReadinessReport() const;
    const RenderSystemRhiFormalRefactorStartGateReport&
    lastRhiFormalRefactorStartGateReport() const;

private:
    struct Impl;
    explicit RenderSystem(core::UniquePtr<Impl> impl);

    core::UniquePtr<Impl> impl_;

    friend RenderSystem createDefaultRenderSystem(WindowSystem& platform,
        const PlatformWindow& window,
        const std::pmr::string& applicationName,
        rhi::PresentationMode presentationMode,
        const rhi::semantics::UploadStagingBudgetV1* uploadStagingBudgetOverride,
        const rhi::semantics::DirectObjectBudget* directObjectBudgetOverride);
    friend RenderSystem createRenderSystem(const RenderSystemDesc& desc);
};

RenderSystem createDefaultRenderSystem(WindowSystem& platform,
    const PlatformWindow& window,
    const std::pmr::string& applicationName,
    rhi::PresentationMode presentationMode = rhi::PresentationMode::Auto,
    const rhi::semantics::UploadStagingBudgetV1* uploadStagingBudgetOverride = nullptr,
    const rhi::semantics::DirectObjectBudget* directObjectBudgetOverride = nullptr);

RenderSystem createRenderSystem(const RenderSystemDesc& desc);

} // namespace arp::platform
