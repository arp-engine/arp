#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/pipeline/scene_runtime_render_pipeline.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <array>
#include <cstddef>

namespace arp::renderer {

inline constexpr std::size_t kSceneRuntimePipelineBackendReportCount = 4u;

struct SceneRuntimePipelineBackendSemanticReport {
    rhi::Backend backend = rhi::Backend::Null;
    rhi::semantics::SceneRuntimeBackendSupportLabel supportLabel =
        rhi::semantics::SceneRuntimeBackendSupportLabel::NotRequested;
    arp::core::pmr::String diagnostics;
};

struct SceneRuntimePipelineRhiParityResult {
    bool success = false;
    arp::core::pmr::String diagnostics;
    RendererSceneRuntimeFrameRhiPlanRequest planRequest;
    RendererRhiFramePlanDesc framePlan;
    RendererRhiFramePlanBuildResult bridgedFramePlan;
    std::array<SceneRuntimePipelineBackendSemanticReport, kSceneRuntimePipelineBackendReportCount>
        backendReports{};
    std::size_t backendReportCount = 0;
};

ARP_API RendererSceneRuntimeFrameRhiPlanRequest sceneRuntimeRhiPlanRequestFromPayload(
    const SceneRuntimePipelinePayload& payload);

ARP_API SceneRuntimePipelineRhiParityResult verifySceneRuntimePipelineRhiParity(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc);

} // namespace arp::renderer
