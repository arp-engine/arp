#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/ownership/lifetime_epoch.h"

#include <utility>

namespace arp::resource {
class ResourceManager;
}

namespace arp::physics2d {
class PhysicsWorld2D;
}

namespace arp::editor {

// One authoring workspace's CPU service ownership. Callbacks retain this
// session, never the Shell. Mutation/observation require the owner thread.
class EditorSceneServiceSession final {
public:
    EditorSceneServiceSession(core::SharedPtr<resource::ResourceManager> resources,
        core::SharedPtr<physics2d::PhysicsWorld2D> physics)
        : resources_(std::move(resources)), physics_(std::move(physics))
    {}

    EditorSceneServiceSession(const EditorSceneServiceSession&) = delete;
    EditorSceneServiceSession& operator=(const EditorSceneServiceSession&) = delete;

    resource::ResourceManager* resources() const noexcept { return resources_.get(); }

    core::SharedPtr<physics2d::PhysicsWorld2D> physics() const noexcept { return physics_; }

    core::LifetimeEpoch::Ticket ownerTicket() const noexcept { return epoch_.ticket(); }

private:
    friend class EditorShell;

    // Only same-workspace package/reload publication updates an existing
    // session. A project switch creates a different session before commit.
    void publishOwners(core::SharedPtr<resource::ResourceManager> resources,
        core::SharedPtr<physics2d::PhysicsWorld2D> physics) noexcept
    {
        if (resources_ == resources && physics_ == physics)
            return;
        resources_ = std::move(resources);
        physics_ = std::move(physics);
        epoch_.advance();
    }

    core::SharedPtr<resource::ResourceManager> resources_;
    core::SharedPtr<physics2d::PhysicsWorld2D> physics_;
    core::LifetimeEpoch epoch_;
};
} // namespace arp::editor
