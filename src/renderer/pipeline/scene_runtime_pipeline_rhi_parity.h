#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/pipeline/scene_runtime_pipeline_payload.h"
#include "renderer/pipeline/scene_runtime_render_pipeline.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "renderer/scene_runtime_frame_rhi_planner.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/semantic_report_backend_admission.h"

#include <array>
#include <cstddef>

namespace arp::renderer {

inline constexpr std::size_t kSceneRuntimePipelineBackendReportCount = 4u;
inline constexpr std::uint32_t kSceneRuntimePipelineLegacyReportVersion = 1u;
inline constexpr std::uint32_t kSceneRuntimePipelineReportVersion = 2u;
inline constexpr std::size_t kSceneRuntimePipelineBackendReportCountV2 =
    rhi::semantics::SemanticReportBackendAdmission::backends.size();

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

// Version 2 is the authoritative five-backend CPU structural report. Neither
// success nor Supported proves native execution or visual/backend readiness.
struct SceneRuntimePipelineRhiParityResultV2 {
    bool success = false;
    arp::core::pmr::String diagnostics;
    RendererSceneRuntimeFrameRhiPlanRequest planRequest;
    RendererRhiFramePlanDesc framePlan;
    RendererRhiFramePlanBuildResult bridgedFramePlan;
    std::array<SceneRuntimePipelineBackendSemanticReport, kSceneRuntimePipelineBackendReportCountV2>
        backendReports{};
    std::size_t backendReportCount = 0;
};

ARP_API RendererSceneRuntimeFrameRhiPlanRequest sceneRuntimeRhiPlanRequestFromPayload(
    const SceneRuntimePipelinePayload& payload);

ARP_API SceneRuntimePipelineRhiParityResultV2 verifySceneRuntimePipelineRhiParityV2(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc);

ARP_API SceneRuntimePipelineRhiParityResultV2 verifySceneRuntimePipelineRhiParityV2(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc,
    const SceneRuntimeIndexedDrawMetadata& metadata);

// Explicit Legacy/1 projection of V2. Preserve the old return-object layout
// and four-item order; this entry cannot certify five-backend coverage.
ARP_API SceneRuntimePipelineRhiParityResult verifySceneRuntimePipelineRhiParity(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc);

ARP_API SceneRuntimePipelineRhiParityResult verifySceneRuntimePipelineRhiParity(
    const SceneRuntimePipelinePayload& payload,
    const SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const RendererSceneRuntimeFrameRhiPlannerDesc& plannerDesc,
    const RendererRhiDrawPlanBridgeDesc& bridgeDesc,
    const SceneRuntimeIndexedDrawMetadata& metadata);

} // namespace arp::renderer
