// Compile against the frozen pre-V2 public declaration, not the current header.
// This verifies return-object ABI interoperation with the preserved Legacy/1 entry.
#include "tests/fixtures/scene_runtime_pipeline_rhi_parity_legacy_abi.h"

// New overload, deliberately declared with the frozen return-object type.
namespace arp::renderer {
ARP_API SceneRuntimePipelineRhiParityResult verifySceneRuntimePipelineRhiParity(
    const SceneRuntimePipelinePayload&,
    const SceneRuntimeRenderPipelineSemanticRecord&,
    const RendererSceneRuntimeFrameRhiPlannerDesc&,
    const RendererRhiDrawPlanBridgeDesc&,
    const SceneRuntimeIndexedDrawMetadata&);
}

bool legacySceneRuntimeReportAbiMatches(const arp::renderer::SceneRuntimePipelinePayload& payload,
    const arp::renderer::SceneRuntimeRenderPipelineSemanticRecord& semantic,
    const arp::renderer::RendererSceneRuntimeFrameRhiPlannerDesc& planner,
    const arp::renderer::RendererRhiDrawPlanBridgeDesc& bridge,
    bool expectedSuccess,
    const arp::renderer::SceneRuntimeIndexedDrawMetadata* metadata)
{
    const auto result = metadata
        ? arp::renderer::verifySceneRuntimePipelineRhiParity(
              payload, semantic, planner, bridge, *metadata)
        : arp::renderer::verifySceneRuntimePipelineRhiParity(payload, semantic, planner, bridge);
    if (result.success != expectedSuccess)
        return false;
    if (!expectedSuccess)
        return result.backendReportCount == 0u && !result.diagnostics.empty();
    return result.backendReportCount == 4u && result.diagnostics.empty()
        && result.planRequest.frame == payload.rendererFrame
        && result.framePlan.drawPlans.size() == 1u && result.bridgedFramePlan.success
        && result.backendReports[0].backend == arp::rhi::Backend::Null
        && result.backendReports[3].backend == arp::rhi::Backend::WebGPU;
}
