#include "physics/physics_types.h"

namespace arp::physics {

// Collision layer-pair matrix (versioned contract).
//
// Rows = layer A, columns = layer B. Entry true means the pair collides
// physically. Triggers never collide physically (they only emit events);
// characters collide with Static/Dynamic/Character but not with triggers.
// The matrix is symmetric; we only store one triangle and mirror it.
//
// Any change here MUST be covered by physics_l2_contract_tests.cpp.
bool layersCollide(CollisionLayer a, CollisionLayer b)
{
    const auto A = static_cast<std::uint16_t>(a);
    const auto B = static_cast<std::uint16_t>(b);
    const auto S = static_cast<std::uint16_t>(CollisionLayer::Static);
    const auto D = static_cast<std::uint16_t>(CollisionLayer::Dynamic);
    const auto T = static_cast<std::uint16_t>(CollisionLayer::Trigger);
    const auto C = static_cast<std::uint16_t>(CollisionLayer::Character);

    // Triggers never produce physical response.
    if (A == T || B == T)
        return false;

    // Static vs Static: no collision (both immovable).
    if (A == S && B == S)
        return false;

    // Everything else (Static/Dynamic/Character cross pairs) collides.
    (void)D;
    (void)C;
    return true;
}

} // namespace arp::physics
