#pragma once

#include "core/api.h"
#include "core/container/array_view.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/rhi_draw_plan_bridge.h"
#include "renderer/scene_runtime_frame.h"

#include <cstdint>

namespace arp::renderer {

enum class RendererSceneRuntimeRhiDiagnosticCode {
    None,
    MissingFrame,
    InvalidFrameStatus,
    IncompatibleOnePassIntent,
    UnsupportedMultiPassRequirement,
    DescriptorBudgetExceeded,
    MaterialResourceBudgetExceeded,
    DrawItemBudgetExceeded,
    PushConstantCapacityExceeded,
    BackendHandleExposure,
    StaticGeometryPayloadCopy,
    DrawMetadataVersion,
    DrawMetadataMissing,
    DrawMetadataStorage,
    DrawMetadataCount,
};

struct RendererSceneRuntimeRhiDiagnostic {
    RendererSceneRuntimeRhiDiagnosticCode code = RendererSceneRuntimeRhiDiagnosticCode::None;
    arp::core::pmr::String message;
};

enum class RendererSceneRuntimeRhiStatusLabel {
    DiagnosticFailure,
    ContractComplete,
};

struct RendererSceneRuntimeFrameRhiPlannerDesc {
    std::uint32_t frameSlot = 0;
    std::uint32_t descriptorSetCount = 2;
    std::uint32_t descriptorLayoutNameCount = 2;
    std::uint32_t maxMaterialResourceCount = 1024;
    std::uint32_t pushConstantSizeBytes = 128;
    std::uint32_t pipelinePushConstantCapacityBytes = 256;
    std::uint32_t maxDrawCommandCount = 1024;
};

struct RendererSceneRuntimeFrameRhiPlanRequest {
    const RendererSceneRuntimeFrame* frame = nullptr;
    bool requiresMultiplePhysicalPasses = false;
    bool hasExplicitAttachmentContract = false;
    bool hasExplicitPostprocessContract = false;
    bool hasExplicitOffscreenContract = false;
};

// Borrowed for the cold planning call only. The returned plan owns its copy.
struct SceneRuntimeIndexedDrawMetadata {
    static constexpr std::uint32_t currentVersion = 1u;
    std::uint32_t contractVersion = currentVersion;
    core::ArrayView<render_graph::RhiDrawIndexedCommand> commands;
};

class ARP_API RendererSceneRuntimeFrameRhiPlanResult final {
public:
    static RendererSceneRuntimeFrameRhiPlanResult success(RendererRhiFramePlanDesc framePlan);
    static RendererSceneRuntimeFrameRhiPlanResult failure(
        RendererSceneRuntimeRhiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == RendererSceneRuntimeRhiDiagnosticCode::None; }

    const RendererSceneRuntimeRhiDiagnostic& diagnostic() const { return diagnostic_; }

    RendererSceneRuntimeRhiStatusLabel statusLabel() const { return statusLabel_; }

    const RendererRhiFramePlanDesc& framePlan() const { return framePlan_; }

    bool backendSpecificBranchingRequired() const { return backendSpecificBranchingRequired_; }

    bool nativeHandleExposedToUpperLayer() const { return nativeHandleExposedToUpperLayer_; }

private:
    RendererSceneRuntimeFrameRhiPlanResult(
        RendererSceneRuntimeRhiDiagnostic diagnostic, RendererRhiFramePlanDesc framePlan);

    RendererSceneRuntimeRhiDiagnostic diagnostic_;
    RendererSceneRuntimeRhiStatusLabel statusLabel_ =
        RendererSceneRuntimeRhiStatusLabel::DiagnosticFailure;
    RendererRhiFramePlanDesc framePlan_;
    bool backendSpecificBranchingRequired_ = false;
    bool nativeHandleExposedToUpperLayer_ = false;
};

class ARP_API RendererSceneRuntimeFrameRhiPlanner final {
public:
    explicit RendererSceneRuntimeFrameRhiPlanner(RendererSceneRuntimeFrameRhiPlannerDesc desc);

    RendererSceneRuntimeFrameRhiPlanResult plan(const RendererSceneRuntimeFrame& frame) const;
    RendererSceneRuntimeFrameRhiPlanResult plan(
        const RendererSceneRuntimeFrameRhiPlanRequest& request) const;
    RendererSceneRuntimeFrameRhiPlanResult plan(
        const RendererSceneRuntimeFrameRhiPlanRequest& request,
        const SceneRuntimeIndexedDrawMetadata& metadata) const;

private:
    RendererSceneRuntimeFrameRhiPlannerDesc desc_;
};

} // namespace arp::renderer
