#pragma once

// Shared native RenderSystem machinery for the WebGPU (Emscripten) and
// Metal (Apple) backend translation units. The L2 frame-graph path is
// identical for both backends: RenderSystem::Impl and every backend-neutral
// RenderSystem method live in render_system_native_shared.cpp.
//
// Each backend TU (render_system_webgpu.cpp / render_system_metal.cpp)
// supplies only the platform-specific pieces:
//   - createDefaultRenderSystem / createRenderSystem for its platform
//   - the two native telemetry hooks declared below
// The hooks keep native command/swapchain telemetry out of the shared path
// so the shared TU never names a backend header.

#include "core/diagnostics/diagnostic_builder.h"
#include "core/memory/cpu_allocator.h"
#include "platform/frame_input_admission.h"
#include "platform/render_system.h"
#include "renderer/frame/render_frame_director.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "renderer/particles/particle_renderer.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "renderer/pipeline/render_pipeline.h"
#include "renderer/rhi_production_frame.h"
#include "renderer/ui/gui_renderer.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_swapchain.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arp::platform {

struct RenderSystem::Impl {
    struct ParticleRendererConfig {
        core::pmr::String shaderPath{&core::defaultCpuMemoryResource()};
        core::pmr::String trailBeamShaderPath{&core::defaultCpuMemoryResource()};
        core::pmr::String meshParticleShaderPath{&core::defaultCpuMemoryResource()};
        std::uint32_t maxParticles = 0u;

        bool valid() const noexcept { return !shaderPath.empty() && maxParticles > 0u; }
    };

    arp::core::UniquePtr<rhi::GpuDevice> device;
    // RenderSystem owns the transient pool so short-lived per-frame graphs
    // reuse L2 resources without extending device lifetime globally.
    std::shared_ptr<render_graph::FrameGraphResourcePool> frameGraphResourcePool;
    arp::core::UniquePtr<rhi::Swapchain> swapchain;
    rhi::SwapchainDesc swapchainDesc;
    renderer::RenderFrameDirector frameDirector;
    std::unique_ptr<renderer::RenderPipeline> pipeline;
    arp::core::UniquePtr<renderer::RenderPipeline> corePipeline;
    renderer::RenderPipeline* activePipeline = nullptr;
    arp::core::UniquePtr<renderer::RendererRhiProductionFramePath> preparedRendererFramePath;
    renderer::RendererRhiFramePlanDesc preparedRendererFramePlan;
    renderer::RendererRhiDrawPlanBridgeDesc preparedRendererFramePlanBridge{2u, 8u, 4u, 2u};
    std::uint32_t preparedRendererFrameMaxDrawCommandsPerPass = 1024u;
    std::pmr::string preparedRendererFrameShaderArtifactRoot;
    RenderSystemPreparedRendererFramePrepareReport preparedRendererFramePrepareReport;
    std::uint32_t preparedRendererFramePrepareCount = 0u;
    RenderSystemSceneRuntimeCustomGraphPrepareReport sceneRuntimeCustomGraphPrepareReport;
    std::uint32_t sceneRuntimeCustomGraphPrepareCount = 0u;
    core::UniquePtr<renderer::GuiRenderer> guiRenderer;
    core::UniquePtr<renderer::ParticleRenderer> particleRenderer;
    ParticleRendererConfig particleRendererConfig;
    rhi::AcquiredFrame currentFrame;
    FrameInputAdmission inputAdmission;
    rhi::Extent2D extent{1280u, 720u};
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Bgra8Unorm;
    rhi::Backend backend = rhi::Backend::Null;
    bool surfaceBackedPresentation = false;
    RenderSystemSceneRuntimeCustomGraphFrameReport lastSceneRuntimeCustomGraphFrameReport;
    std::uint64_t sceneRuntimeCustomGraphPendingPresentSerial = 0;
    bool sceneRuntimeCustomGraphPendingPresent = false;
    std::shared_ptr<const rhi::GpuFence> currentFramePresentationCompletion;
    RenderSystemRhiFrameLoopDryRunDesc rhiDryRunDesc;
    RenderSystemRhiFrameLoopDryRunReport lastRhiDryRunReport;
    RenderSystemRhiFrameLoopDryRunSmokeReport lastRhiDryRunSmokeReport;
    RenderSystemRhiStartupProbeReport lastRhiStartupProbeReport;
    RenderSystemRhiStartupGateReport lastRhiStartupGateReport;
    RenderSystemRhiStartupProbeScheduleReport lastRhiStartupProbeScheduleReport;
    RenderSystemRhiStartupProbeBatchReport lastRhiStartupProbeBatchReport;
    RenderSystemRhiStartupProbeRunReport lastRhiStartupProbeRunReport;
    RenderSystemRhiStartupTelemetryReport lastRhiStartupTelemetryReport;
    RenderSystemRhiStartupReadinessReport lastRhiStartupReadinessReport;
    RenderSystemRhiFormalRefactorStartGateReport lastRhiFormalRefactorStartGateReport;
    std::pmr::string diagnostics;
    bool valid = false;
};

/// Backend hook: fold native command/swapchain telemetry into the
/// scene-runtime custom graph frame report after a successful present.
void applyNativeSceneRuntimeFrameStats(RenderSystemSceneRuntimeCustomGraphFrameReport& target,
    const rhi::GpuDevice& device,
    rhi::Extent2D extent,
    rhi::TextureFormat format);

/// Backend hook: fold native telemetry into the RHI production frame submit
/// report once submission and presentation have succeeded.
void applyNativeProductionFrameSubmitTelemetry(RenderSystemRhiProductionFrameSubmitReport& report,
    const rhi::GpuDevice& device,
    rhi::Extent2D extent,
    rhi::TextureFormat format);

} // namespace arp::platform
