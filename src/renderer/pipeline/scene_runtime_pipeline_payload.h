#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/pipeline/custom_render_frame_workload.h"
#include "renderer/scene_runtime_frame.h"

#include <cstdint>

namespace arp::scene {
struct ResolvedCamera;
class SceneTraversalSnapshot;
} // namespace arp::scene

namespace arp::scene2d {
class Scene2DRenderQueue;
}

namespace arp::scene3d {
class Scene3DRenderQueue;
}

namespace arp::ui {
class RenderQueue;
}

namespace arp::renderer {

inline constexpr const char* kSceneRuntimePipelinePayloadContract =
    "renderer/SceneRuntimePipelinePayload";
inline constexpr std::uint32_t kSceneRuntimePipelinePayloadVersion = 1u;

struct SceneRuntimePipelinePayload {
    const arp::scene::SceneTraversalSnapshot* traversalSnapshot = nullptr;
    const arp::core::pmr::Vector<arp::scene::ResolvedCamera>* resolvedCameras = nullptr;
    const arp::scene3d::Scene3DRenderQueue* scene3DQueue = nullptr;
    const arp::scene2d::Scene2DRenderQueue* scene2DQueue = nullptr;
    const arp::ui::RenderQueue* uiQueue = nullptr;
    const RendererSceneRuntimeFrame* rendererFrame = nullptr;
    bool requiresMultiplePhysicalPasses = false;
    bool hasExplicitAttachmentContract = false;
    bool hasExplicitPostprocessContract = false;
    bool hasExplicitOffscreenContract = false;
};

struct SceneRuntimePipelinePayloadValidationResult {
    bool success = false;
    arp::core::pmr::String diagnostics;
};

ARP_API SceneRuntimePipelinePayloadValidationResult validateSceneRuntimePipelinePayload(
    const CustomWorkloadPayloadRef& payload);

ARP_API SceneRuntimePipelinePayloadValidationResult validateSceneRuntimePipelinePayload(
    const SceneRuntimePipelinePayload& payload);

ARP_API const SceneRuntimePipelinePayload* sceneRuntimePipelinePayloadFrom(
    const CustomWorkloadSection& section);

} // namespace arp::renderer
