#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "rhi/gpu_resources.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::renderer {

enum class RenderFrameStage {
    Invalid,
    ShadowDepth,
    WorldOpaque,
    WorldTransparent,
    StylizedAux,
    Postprocess,
    UI,
    Debug,
};

inline constexpr std::size_t kRenderFrameStageCount = 7u;

ARP_API const std::array<RenderFrameStage, kRenderFrameStageCount>& renderFrameStageOrder();
ARP_API bool isRenderFrameStageSupported(RenderFrameStage stage);
ARP_API std::size_t renderFrameStageSortKey(RenderFrameStage stage);
ARP_API const char* renderFrameStageName(RenderFrameStage stage);

enum class RenderFrameAttachmentRole {
    BackbufferColor,
    SceneColor,
    SceneDepth,
    PostprocessColor,
    UiColor,
    DebugColor,
    OffscreenColor,
};

enum class RenderFrameAttachmentAccess {
    Read,
    Write,
};

struct RenderFrameAttachmentUse {
    arp::core::pmr::String attachmentId;
    rhi::ResourceState state = rhi::ResourceState::Undefined;
    RenderFrameAttachmentAccess access = RenderFrameAttachmentAccess::Read;
};

struct RenderFrameAttachmentRequirement {
    arp::core::pmr::String id;
    RenderFrameAttachmentRole role = RenderFrameAttachmentRole::BackbufferColor;
    rhi::Extent2D extent;
    rhi::TextureFormat format = rhi::TextureFormat::Unknown;
    rhi::ResourceState initialState = rhi::ResourceState::Undefined;
    rhi::ResourceState finalState = rhi::ResourceState::Undefined;
    bool imported = false;
    bool explicitContract = false;
};

struct RenderFrameSceneRequirement {
    arp::core::pmr::String id;
    std::size_t resolvedCameraCount = 0;
    std::size_t drawItemCount = 0;
    std::size_t materialResourceCount = 0;
    std::size_t textureResourceCount = 0;
    const void* traversalSnapshot = nullptr;
    const void* rendererFrame = nullptr;
    bool compatibleOnePassIntent = false;
};

enum class RenderFrameQueueKind {
    Scene3D,
    Scene2D,
    UI,
};

struct RenderFrameQueueRequirement {
    RenderFrameQueueKind kind = RenderFrameQueueKind::Scene3D;
    std::size_t order = 0;
    std::size_t drawItemCount = 0;
    std::size_t materialResourceCount = 0;
    std::size_t textureResourceCount = 0;
    bool compatibleWithOnePass = true;
};

struct RenderFramePostprocessRequirement {
    arp::core::pmr::String id;
    bool enabled = false;
};

struct RenderFrameUiRequirement {
    arp::core::pmr::String id;
    std::size_t drawItemCount = 0;
};

struct RenderFramePreparedSubmitRequirement {
    arp::core::pmr::String id;
    bool required = false;
    arp::core::pmr::String plannerContractName;
    std::size_t expectedDrawItemCount = 0;
    RendererRhiFramePlanDesc framePlan;
    RendererRhiDrawPlanBridgeDesc framePlanBridge;
    std::uint32_t maxDrawCommandsPerPass = 0;
    arp::core::pmr::String shaderArtifactRootPolicy;
    bool backendSpecificBranchingRequired = false;
    bool nativeHandleExposedToUpperLayer = false;
};

struct RenderFramePassIntent {
    arp::core::pmr::String id;
    RenderFrameStage stage = RenderFrameStage::Invalid;
    arp::core::pmr::Vector<RenderFrameAttachmentUse> attachmentUses;
    bool requiresPreparedSubmit = false;
};

struct RenderFrameIntentBudget {
    std::size_t maxPasses = 32u;
    std::size_t maxSceneRequirements = 4u;
    std::size_t maxQueueRequirements = 8u;
    std::size_t maxPostprocessRequirements = 8u;
    std::size_t maxUiRequirements = 4u;
    std::size_t maxAttachmentRequirements = 16u;
    std::size_t maxPreparedSubmitRequirements = 4u;
};

struct RenderFrameIntent {
    arp::core::pmr::String contractName;
    std::uint32_t contractVersion = 0;
    RenderFrameIntentBudget budget;
    arp::core::pmr::Vector<RenderFrameSceneRequirement> sceneRequirements;
    arp::core::pmr::Vector<RenderFrameQueueRequirement> queueRequirements;
    arp::core::pmr::Vector<RenderFramePostprocessRequirement> postprocessRequirements;
    arp::core::pmr::Vector<RenderFrameUiRequirement> uiRequirements;
    arp::core::pmr::Vector<RenderFrameAttachmentRequirement> attachmentRequirements;
    arp::core::pmr::Vector<RenderFramePreparedSubmitRequirement> preparedSubmitRequirements;
    arp::core::pmr::Vector<RenderFramePassIntent> passes;
    bool requiresMultiplePhysicalPasses = false;
    bool hasExplicitAttachmentContract = false;
    bool hasExplicitPostprocessContract = false;
    bool hasExplicitOffscreenContract = false;
};

enum class RenderFrameIntentDiagnosticCode {
    None,
    MissingContract,
    CapacityExceeded,
    InvalidStage,
    MissingPassId,
    DuplicatePassId,
    MissingAttachmentContract,
    UnsupportedMultiPassIntent,
};

struct RenderFrameIntentDiagnostic {
    RenderFrameIntentDiagnosticCode code = RenderFrameIntentDiagnosticCode::None;
    arp::core::pmr::String message;
};

struct RenderFrameIntentValidationResult {
    bool success = false;
    arp::core::pmr::Vector<RenderFrameIntentDiagnostic> diagnostics;
};

class ARP_API RenderFrameIntentValidator final {
public:
    RenderFrameIntentValidationResult validate(const RenderFrameIntent& intent) const;
};

enum class RenderFrameExecutionMode {
    None,
    ComposeGraph,
    PreparedSubmit,
    LegacyPipelineFallback,
    DiagnosticFailure,
};

struct RenderFrameExecutionDecision {
    RenderFrameExecutionMode mode = RenderFrameExecutionMode::None;
    arp::core::pmr::String statusLabel;
    arp::core::pmr::Vector<RenderFramePassIntent> graphPasses;
    arp::core::pmr::Vector<RenderFramePreparedSubmitRequirement> preparedSubmitRequirements;
    arp::core::pmr::Vector<RenderFrameIntentDiagnostic> diagnostics;
};

} // namespace arp::renderer
