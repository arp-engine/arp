#include "3d/scene_3d_render_queue.h"

#include <utility>

namespace arp::scene3d {

void Scene3DRenderQueue::addItem(Scene3DRenderItem item)
{
    if (item.kind == Scene3DRenderItemKind::Mesh) {
        ++meshItemCount_;
    } else if (item.kind == Scene3DRenderItemKind::Terrain) {
        ++terrainItemCount_;
    }
    items_.push_back(std::move(item));
}

void Scene3DRenderQueue::addCamera(Scene3DCameraBinding camera)
{
    cameras_.push_back(std::move(camera));
}

void Scene3DRenderQueue::addAnimation(Scene3DAnimationBinding animation)
{
    animations_.push_back(animation);
}

void Scene3DRenderQueue::addLight(Scene3DLightBinding light)
{
    lights_.push_back(std::move(light));
}

Scene3DExtractionResult::Scene3DExtractionResult(
    Scene3DDiagnostic diagnostic, Scene3DRenderQueue queue)
    : diagnostic_(std::move(diagnostic)), queue_(std::move(queue))
{}

Scene3DExtractionResult Scene3DExtractionResult::success(Scene3DRenderQueue queue)
{
    return Scene3DExtractionResult(Scene3DDiagnostic{}, std::move(queue));
}

Scene3DExtractionResult Scene3DExtractionResult::failure(
    Scene3DDiagnosticCode code, arp::core::pmr::String message)
{
    return Scene3DExtractionResult(
        Scene3DDiagnostic{code, std::move(message)}, Scene3DRenderQueue{});
}

} // namespace arp::scene3d
