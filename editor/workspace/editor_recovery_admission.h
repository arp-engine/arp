#pragma once

#include "core/encoding/base64.h"
#include "core/ownership/lifetime_epoch.h"

#include <memory>

namespace arp::resource {
class ResourceManager;
}

namespace arp::physics2d {
class PhysicsWorld2D;
}

namespace arp::editor {
class EditorShell;

struct EditorRecoveryAdmissionBudget {
    std::size_t maxObservedBytes = 128u * 1024u * 1024u;
    std::size_t maxScriptHistoryEntries = 4096u;
};

// Owner-thread precommit observation, independent of disk and GPU completion.
// A ticket records the caller's discard policy; it cannot create authorization.
// Covers Editor state and resource-owner identity, not in-place ResourceManager
// inventory changes. That separate publication gate is currently unavailable.
class EditorRecoveryAdmission final {
public:
    class Ticket final {
    public:
        bool ownerAlive() const noexcept { return shell_.isCurrent(); }

    private:
        friend class EditorRecoveryAdmission;
        core::LifetimeEpoch::Ticket shell_, scene_, scripts_, history_;
        std::weak_ptr<resource::ResourceManager> resources_;
        std::weak_ptr<physics2d::PhysicsWorld2D> physics_;
        bool hasPhysics_ = false;
        bool discardChanges_ = false;
        std::uint64_t stateVersion_ = 0;
        std::uint64_t documentEpoch_ = 0;
        EditorRecoveryAdmissionBudget budget_;
        core::ByteBuffer fingerprint_;
    };

    static core::UniquePtr<Ticket> capture(const EditorShell& shell,
        bool discardChanges,
        core::pmr::String& diagnostic,
        EditorRecoveryAdmissionBudget budget = {});
    static bool validate(
        const EditorShell& shell, const Ticket& ticket, core::pmr::String& diagnostic);

private:
    static bool gate(const EditorShell& shell, bool discardChanges, core::pmr::String& diagnostic);
    static bool fingerprint(const EditorShell& shell,
        EditorRecoveryAdmissionBudget budget,
        core::ByteBuffer& destination,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
