#pragma once

#include "core/memory/cpu_allocator.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace arp::scene {

class Scene;

/// Non-owning host registry for the process' current main Scene.
///
/// The registry never owns Scene objects. A world/session owns the objects and
/// registers them for the duration of that world. Script calls only enqueue a
/// switch; the host commits it at a frame boundary so update/render cannot see
/// two roots in one frame.
class ARP_API MainSceneRegistry final {
public:
    using SceneFactory = std::function<Scene*(const arp::core::pmr::String& name)>;
    using SceneDestroyer = std::function<bool(Scene& scene)>;

    bool registerScene(Scene& scene) noexcept;
    bool unregisterScene(Scene& scene) noexcept;
    bool request(Scene& scene) noexcept;
    bool commit() noexcept;

    // Host-only ownership hooks. The registry never owns the returned Scene;
    // the host factory remains responsible for storage and destruction.
    void bindFactory(SceneFactory factory, SceneDestroyer destroyer)
    {
        factory_ = std::move(factory);
        destroyer_ = std::move(destroyer);
    }

    void clearFactory() noexcept
    {
        factory_ = {};
        destroyer_ = {};
    }

    Scene* createScene(const arp::core::pmr::String& name);
    bool destroyScene(Scene& scene) noexcept;

    Scene* active() const noexcept { return active_; }

    Scene* pending() const noexcept { return pending_; }

    std::uint64_t generation() const noexcept { return generation_; }

    bool contains(const Scene& scene) const noexcept;

private:
    static constexpr std::size_t kMaxScenes = 64u;
    Scene* scenes_[kMaxScenes] = {};
    std::size_t sceneCount_ = 0u;
    Scene* active_ = nullptr;
    Scene* pending_ = nullptr;
    std::uint64_t generation_ = 0u;
    SceneFactory factory_;
    SceneDestroyer destroyer_;
};

} // namespace arp::scene
