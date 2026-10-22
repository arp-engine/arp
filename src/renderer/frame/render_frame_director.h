#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/frame/executable_render_feature.h"
#include "renderer/frame/render_frame_payload_adapter.h"
#include "renderer/frame/render_pass_provider.h"
#include "renderer/frame/scene_runtime_payload_adapter.h"
#include "renderer/frame/scene_runtime_world_ui_pass_provider.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "renderer/pipeline/render_pipeline.h"

namespace arp::renderer {

struct RenderFrameDirectorDesc {
    bool installDefaultAdaptersAndProviders = true;
};

enum class RenderFrameDirectorDiagnosticCode {
    None,
    WorkloadValidationFailed,
    PayloadAdapterFailed,
    PassProviderFailed,
    PreparedSubmitRejected,
    LegacyPipelineFallbackUsed,
    MissingLegacyPipeline,
    LegacyPipelineFailed,
};

struct RenderFrameDirectorDiagnostic {
    RenderFrameDirectorDiagnosticCode code = RenderFrameDirectorDiagnosticCode::None;
    arp::core::pmr::String message;
};

struct RenderFramePreparedRendererFrameConfig {
    const RendererRhiFramePlanDesc* preparedFramePlan = nullptr;
    RendererRhiDrawPlanBridgeDesc framePlanBridge{2u, 8u, 4u, 2u};
    std::uint32_t maxDrawCommandsPerPass = 1024u;
    std::uint32_t maxMaterialResourceCount = 1024u;
    std::uint32_t pushConstantSizeBytes = 128u;
    std::uint32_t pipelinePushConstantCapacityBytes = 256u;
    arp::core::pmr::String shaderArtifactRootPolicy = "DefaultRendererRhiShaderArtifactRoot";
    bool backendSpecificBranchingRequired = false;
    bool nativeHandleExposedToUpperLayer = false;
};

struct RenderFrameDirectorRequest {
    const CustomRenderFrameWorkload& workload;
    render_graph::FrameGraph& graph;
    render_graph::TextureHandle backbuffer;
    RenderPipelineGraphCompositionContext compositionContext;
    RenderPipeline* legacyPipeline = nullptr;
    RenderPipelineFrameFeatures frameFeatures;
    RenderFramePreparedRendererFrameConfig preparedRendererFrame;
};

struct RenderFrameDirectorResult {
    bool success = false;
    RenderFrameExecutionDecision decision;
    arp::core::pmr::Vector<RenderFrameDirectorDiagnostic> diagnostics;
};

class ARP_API RenderFrameDirector final {
public:
    explicit RenderFrameDirector(RenderFrameDirectorDesc desc = {});

    RenderFramePayloadAdapterRegistrationResult registerPayloadAdapter(
        const IRenderFramePayloadAdapter& adapter);
    RenderPassProviderRegistrationResult registerPassProvider(const IRenderPassProvider& provider);

    RenderFeatureResult registerFeature(IExecutableRenderFeature& feature);
    RenderFeatureResult createFeatures(const RenderFeatureCreateContext& context);
    RenderFeatureResult resizeFeatures(rhi::Extent2D extent);
    RenderFeatureResult contributeFeatures(const RenderFeatureGraphContext& context);
    void releaseFeatures() noexcept;

    RenderFrameDirectorResult compose(const RenderFrameDirectorRequest& request) const;

private:
    RenderFrameDirectorResult legacyFallback(
        const RenderFrameDirectorRequest& request, const arp::core::pmr::String& reason) const;
    RenderFrameDirectorResult failure(
        RenderFrameDirectorDiagnosticCode code, const arp::core::pmr::String& message) const;

    RenderFrameDirectorDesc desc_;
    RenderFramePayloadAdapterRegistry payloadAdapters_;
    RenderPassProviderRegistry passProviders_;
    SceneRuntimePayloadAdapter sceneRuntimePayloadAdapter_;
    SceneRuntimeWorldUiPassProvider sceneRuntimeWorldUiPassProvider_;
    ExecutableRenderFeatureSet features_;
};

} // namespace arp::renderer
