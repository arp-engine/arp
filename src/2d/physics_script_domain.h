#pragma once
#include "physics/2d/physics_world_2d.h"

namespace arp::physics2d {
// Explicit semantic IDs map to generation-checked bodies. Script stages a
// bounded batch; native code validates the entire batch before applying it.
class Physics2DScriptDomain final {
public:
    Physics2DScriptDomain(PhysicsWorld2D& world, std::uint32_t capacity)
        : world_(world), capacity_(capacity)
    {
        bindings_.reserve(capacity);
        intents_.reserve(capacity);
        states_.reserve(capacity);
    }

    bool bind(int id, BodyHandle body)
    {
        BodyState state;
        if (id <= 0 || bindings_.size() == capacity_ || !world_.state(body, state) || find(id))
            return fail("ARP-PHYSICS2D-SCRIPT-BINDING");
        bindings_.push_back({id, body});
        return true;
    }

    bool queueVelocity(int id, float x, float y)
    {
        const auto* binding = find(id);
        BodyState state;
        if (!binding || !std::isfinite(x) || !std::isfinite(y) || intents_.size() == capacity_
            || !world_.state(binding->body, state))
            return fail("ARP-PHYSICS2D-SCRIPT-INTENT");
        for (const auto& intent : intents_)
            if (intent.body == binding->body)
                return fail("ARP-PHYSICS2D-SCRIPT-DUPLICATE-INTENT");
        intents_.push_back({binding->body, {x, y}});
        return true;
    }

    bool commitVelocities()
    {
        BodyState state;
        for (const auto& intent : intents_)
            if (!world_.state(intent.body, state)) {
                intents_.clear();
                return fail("ARP-PHYSICS2D-SCRIPT-STALE-BATCH");
            }
        for (const auto& intent : intents_)
            if (!world_.setVelocity(intent.body, intent.velocity)) {
                intents_.clear();
                return fail(world_.diagnostic());
            }
        intents_.clear();
        return true;
    }

    bool snapshot()
    {
        states_.clear();
        for (const auto& binding : bindings_) {
            BodyState state;
            if (!world_.state(binding.body, state))
                return fail("ARP-PHYSICS2D-SCRIPT-STALE-SNAPSHOT");
            states_.push_back({binding.id, state});
        }
        return true;
    }

    int bodyCount() const { return int(world_.telemetry().bodies); }

    int jointCount() const { return int(world_.telemetry().joints); }

    int contactCount() const { return int(world_.events().size()); }

    const auto& states() const { return states_; }

    const auto& events() const { return world_.events(); }

    const auto& diagnostic() const { return diagnostic_; }

private:
    struct Binding {
        int id;
        BodyHandle body;
    };

    struct Intent {
        BodyHandle body;
        core::Vec2 velocity;
    };

    struct State {
        int id;
        BodyState body;
    };

    const Binding* find(int id) const
    {
        for (const auto& binding : bindings_)
            if (binding.id == id)
                return &binding;
        return nullptr;
    }

    bool fail(const char* id)
    {
        diagnostic_ = id;
        return false;
    }

    PhysicsWorld2D& world_;
    std::uint32_t capacity_;
    core::pmr::Vector<Binding> bindings_;
    core::pmr::Vector<Intent> intents_;
    core::pmr::Vector<State> states_;
    core::pmr::String diagnostic_;
};
} // namespace arp::physics2d
