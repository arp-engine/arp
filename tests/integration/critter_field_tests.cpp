#include "pcg/critter_field.h"
#include "pcg/terrain_heightfield.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

using arp::core::CritterField;
using arp::core::CritterSpecies;
using arp::core::CritterState;
using arp::core::TerrainHeightField;

void initialize_assigns_species_and_counts()
{
    CritterField field;
    field.initialize(42u);
    int rabbits = 0, squirrels = 0, birds = 0;
    for (const auto& c : field.critters()) {
        switch (c.species) {
        case CritterSpecies::Rabbit:
            ++rabbits;
            break;
        case CritterSpecies::Squirrel:
            ++squirrels;
            break;
        case CritterSpecies::Bird:
            ++birds;
            break;
        }
    }
    // Total must equal the pool size.
    assert(static_cast<std::uint32_t>(rabbits + squirrels + birds) == CritterField::kCount);
    // Each species should have at least one member (16 critters, 45/30/25 split).
    assert(rabbits > 0);
    assert(squirrels > 0);
    assert(birds > 0);
}

void update_relocates_critters_near_player()
{
    CritterField field;
    field.initialize(1u);
    auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    // After update, at least some critters should be visible (relocated near player).
    field.update(0.0f, 0.0f, 0.1f, hf);
    int visible = 0;
    for (const auto& c : field.critters()) {
        if (c.visible)
            ++visible;
    }
    // At least a few should have found valid ground.
    assert(visible > 0);
}

void flee_activates_when_player_close()
{
    CritterField field;
    field.initialize(7u);
    auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    // First update to relocate critters near player.
    field.update(0.0f, 0.0f, 0.5f, hf);
    // Find a visible critter and place the player right on top of it.
    float cx = 0.0f, cz = 0.0f;
    bool found = false;
    for (const auto& c : field.critters()) {
        if (c.visible) {
            cx = c.x;
            cz = c.z;
            found = true;
            break;
        }
    }
    assert(found);
    // Player stands on the critter → it should flee (moving=true, speed>0).
    field.update(cx, cz, 0.05f, hf);
    bool anyFleeing = false;
    for (const auto& c : field.critters()) {
        if (c.visible && c.moving && c.speed > 2.0f) {
            anyFleeing = true;
            break;
        }
    }
    assert(anyFleeing);
}

void wander_produces_movement_over_time()
{
    CritterField field;
    field.initialize(3u);
    auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    // Run several updates; critters should move around.
    float prevX = 0.0f, prevZ = 0.0f;
    field.update(0.0f, 0.0f, 0.1f, hf);
    for (const auto& c : field.critters()) {
        if (c.visible) {
            prevX = c.x;
            prevZ = c.z;
            break;
        }
    }
    bool moved = false;
    for (int i = 0; i < 50; ++i) {
        field.update(0.0f, 0.0f, 0.1f, hf);
        for (const auto& c : field.critters()) {
            if (c.visible) {
                if (std::fabs(c.x - prevX) > 0.01f || std::fabs(c.z - prevZ) > 0.01f) {
                    moved = true;
                    break;
                }
            }
        }
        if (moved)
            break;
    }
    assert(moved);
}

void critters_stay_within_world_bounds()
{
    CritterField field;
    field.initialize(99u);
    auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    // Run many updates; no critter should leave the world bounds.
    for (int i = 0; i < 200; ++i) {
        field.update(0.0f, 0.0f, 0.1f, hf);
    }
    for (const auto& c : field.critters()) {
        if (!c.visible)
            continue;
        // WoCC terrain is ±50; critters have edgeMargin=8, so ±42.
        assert(c.x >= -45.0f && c.x <= 45.0f);
        assert(c.z >= -45.0f && c.z <= 45.0f);
    }
}

void determinism_same_seed_same_behavior()
{
    CritterField a, b;
    a.initialize(123u);
    b.initialize(123u);
    auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    // Run identical updates.
    for (int i = 0; i < 20; ++i) {
        a.update(0.0f, 0.0f, 0.1f, hf);
        b.update(0.0f, 0.0f, 0.1f, hf);
    }
    // States must match exactly.
    for (std::size_t i = 0; i < a.critters().size(); ++i) {
        const auto& ca = a.critters()[i];
        const auto& cb = b.critters()[i];
        assert(ca.x == cb.x);
        assert(ca.z == cb.z);
        assert(ca.heading == cb.heading);
        assert(ca.species == cb.species);
        assert(ca.visible == cb.visible);
    }
}

void different_seeds_different_behavior()
{
    CritterField a, b;
    a.initialize(1u);
    b.initialize(2u);
    auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    a.update(0.0f, 0.0f, 0.1f, hf);
    b.update(0.0f, 0.0f, 0.1f, hf);
    // At least one critter should differ.
    bool differ = false;
    for (std::size_t i = 0; i < a.critters().size(); ++i) {
        if (a.critters()[i].x != b.critters()[i].x || a.critters()[i].z != b.critters()[i].z) {
            differ = true;
            break;
        }
    }
    assert(differ);
}

class GroundSurface final : public arp::terrain::TerrainSurface {
public:
    const TerrainHeightField& field;
    mutable unsigned probes = 0u;
    arp::terrain::TerrainSurfaceQueryStatus status = arp::terrain::TerrainSurfaceQueryStatus::Ready;
    float invalidHeight = 0.0f;
    unsigned invalidProbe = 0u;

    explicit GroundSurface(const TerrainHeightField& source) : field(source) {}

    arp::terrain::TerrainBounds bounds() const noexcept override { return {-180, 180, -180, 900}; }

    std::uint64_t generation() const noexcept override { return 1u; }

    arp::terrain::TerrainSurfaceQuery sample(float x, float z) const override
    {
        const unsigned probe = probes++ % 3u;
        arp::terrain::TerrainSurfaceQuery result;
        result.status = status;
        result.sample.height = invalidProbe == probe + 1u ? invalidHeight : field.sample(x, z);
        return result;
    }
};

void standard_surface_preserves_simulation()
{
    const auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    GroundSurface surface(hf);
    CritterField legacy, standard;
    legacy.initialize(123u);
    standard.initialize(123u);
    for (unsigned tick = 0; tick < 100u; ++tick) {
        legacy.update(0, 0, 0.1f, hf);
        standard.update(0, 0, 0.1f, surface);
        for (unsigned i = 0; i < CritterField::kCount; ++i) {
            const auto& a = legacy.critters()[i];
            const auto& b = standard.critters()[i];
            assert(a.x == b.x && a.z == b.z && a.heading == b.heading);
            assert(a.speed == b.speed && a.turnT == b.turnT && a.hopPhase == b.hopPhase);
            assert(a.baseY == b.baseY && a.moving == b.moving && a.visible == b.visible);
            assert(a.species == b.species);
        }
    }
}

void missing_and_nonfinite_probes_hide_with_bounded_work()
{
    const auto hf = TerrainHeightField::makeDefaultWoCC(-0.5f);
    GroundSurface surface(hf);
    CritterField field;
    field.initialize(1u);
    field.update(0, 0, 0.1f, surface);
    bool wasVisible = false;
    for (const auto& c : field.critters())
        wasVisible |= c.visible;
    assert(wasVisible);
    const auto rejected = [&] {
        surface.probes = 0u;
        field.update(0, 0, 0.1f, surface);
        for (const auto& c : field.critters())
            assert(!c.visible);
        // At most one validity test plus eight relocation attempts per animal,
        // each using three probes. No retry loop depends on resource readiness.
        assert(surface.probes <= CritterField::kCount * 9u * 3u);
    };
    for (const auto status : {arp::terrain::TerrainSurfaceQueryStatus::OutOfBounds,
             arp::terrain::TerrainSurfaceQueryStatus::TileUnavailable,
             arp::terrain::TerrainSurfaceQueryStatus::InvalidData}) {
        surface.status = status;
        rejected();
    }
    surface.status = arp::terrain::TerrainSurfaceQueryStatus::Ready;
    for (const float height : {std::numeric_limits<float>::quiet_NaN(),
             std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity()}) {
        surface.invalidHeight = height;
        for (unsigned probe = 1u; probe <= 3u; ++probe) {
            surface.invalidProbe = probe;
            rejected();
        }
    }
    surface.invalidProbe = 0u;
    field.update(0, 0, 0.1f, surface);
    bool recovered = false;
    for (const auto& c : field.critters())
        recovered |= c.visible;
    assert(recovered);
}

} // namespace

int main()
{
    initialize_assigns_species_and_counts();
    update_relocates_critters_near_player();
    flee_activates_when_player_close();
    wander_produces_movement_over_time();
    critters_stay_within_world_bounds();
    determinism_same_seed_same_behavior();
    different_seeds_different_behavior();
    standard_surface_preserves_simulation();
    missing_and_nonfinite_probes_hide_with_bounded_work();
    return 0;
}
