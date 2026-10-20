#include "scene/main_scene_registry.h"

#include "scene/scene.h"

#include <algorithm>

namespace arp::scene {

bool MainSceneRegistry::contains(const Scene& scene) const noexcept
{
    return std::find(scenes_, scenes_ + sceneCount_, &scene) != scenes_ + sceneCount_;
}

bool MainSceneRegistry::registerScene(Scene& scene) noexcept
{
    if (contains(scene))
        return true;
    if (sceneCount_ >= kMaxScenes)
        return false;
    scenes_[sceneCount_++] = &scene;
    if (active_ == nullptr) {
        active_ = &scene;
        ++generation_;
    }
    return true;
}

bool MainSceneRegistry::unregisterScene(Scene& scene) noexcept
{
    const auto it = std::find(scenes_, scenes_ + sceneCount_, &scene);
    if (it == scenes_ + sceneCount_)
        return false;
    *it = scenes_[sceneCount_ - 1u];
    scenes_[--sceneCount_] = nullptr;
    if (pending_ == &scene)
        pending_ = nullptr;
    if (active_ == &scene) {
        active_ = nullptr;
        // Removing the active root must not silently promote an arbitrary
        // candidate. A new main scene becomes active only after an explicit
        // request/commit at a frame boundary.
        pending_ = nullptr;
        ++generation_;
    }
    return true;
}

bool MainSceneRegistry::request(Scene& scene) noexcept
{
    if (!contains(scene))
        return false;
    pending_ = &scene;
    return true;
}

Scene* MainSceneRegistry::createScene(const arp::core::pmr::String& name)
{
    if (!factory_ || !destroyer_)
        return nullptr;
    auto* scene = factory_(name);
    if (scene == nullptr)
        return nullptr;
    // Every factory-created canonical Scene must receive the same host-only
    // registry binding before it becomes script-visible.
    scene->bindMainSceneRegistry(this);
    if (!registerScene(*scene)) {
        (void)destroyer_(*scene);
        return nullptr;
    }
    return scene;
}

bool MainSceneRegistry::destroyScene(Scene& scene) noexcept
{
    if (!contains(scene) || !destroyer_)
        return false;
    if (!unregisterScene(scene))
        return false;
    return destroyer_(scene);
}

bool MainSceneRegistry::commit() noexcept
{
    if (pending_ == nullptr)
        return true;
    if (!contains(*pending_)) {
        pending_ = nullptr;
        return false;
    }
    if (active_ != pending_) {
        active_ = pending_;
        ++generation_;
    }
    pending_ = nullptr;
    return true;
}

} // namespace arp::scene
