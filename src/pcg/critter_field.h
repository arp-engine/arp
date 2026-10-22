#pragma once

// CritterField — ambient wildlife AI and movement.
//
// Ported from world-of-claudecraft/src/render/critters.ts. A small pool of
// critters (rabbits, squirrels, birds) that follows the player around the
// world. Each critter wanders randomly and flees when the player gets close.
//
// Design:
//   - Pure simulation: the field holds critter state and advances it each
//     frame given a player position and delta time. No rendering concerns.
//   - Deterministic: all randomness flows through a seeded mulberry32 RNG,
//     so the same seed yields the same behavior.
//   - Terrain-aware: critters query TerrainSurface (or a legacy heightfield) for
//     slope validity. They reject moves onto steep slopes or below water.
//   - Bounded: fixed pool size (kCritterCount), no per-frame allocation.

#include "3d/terrain/terrain_surface.h"
#include "core/hash/deterministic_hash.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "pcg/terrain_heightfield.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace arp::core {

/// Critter species — determines geometry, speed, and color.
enum class CritterSpecies : std::uint8_t {
    Rabbit,
    Squirrel,
    Bird,
};

/// CritterState — the per-instance simulation state for one critter.
struct CritterState {
    float x = 0.0f;        // world X
    float z = 0.0f;        // world Z
    float heading = 0.0f;  // yaw radians (0 = +X)
    float speed = 0.0f;    // current speed (yards/sec)
    float turnT = 0.0f;    // time until next wander decision
    float hopPhase = 0.0f; // hop/bob animation phase
    float baseY = 0.0f;    // hover offset (birds > 0)
    bool moving = false;   // currently moving (vs idle)
    bool visible = false;  // active and visible this frame
    CritterSpecies species = CritterSpecies::Rabbit;
};

/// Mulberry32 — seeded PRNG matching the reference (rng.ts:26).
/// Deterministic across platforms (32-bit unsigned arithmetic).
struct Mulberry32 {
    std::uint32_t state = 0u;

    void seed(std::uint32_t s) noexcept
    {
        state = s;
        if (state == 0u)
            state = 0x9e3779b9u;
    }

    float next() noexcept
    {
        state += 0x6d2b79f5u;
        std::uint32_t t = state;
        t = imul32(t ^ (t >> 15), t | 1u);
        t ^= t + imul32(t ^ (t >> 7), t | 61u);
        return static_cast<float>((t ^ (t >> 14))) / 4294967296.0f;
    }
};

/// CritterFieldConfig — tunable parameters for the critter field.
struct CritterFieldConfig {
    /// Pool size (reference: 16 on high tier, 7 on low).
    static constexpr std::uint32_t kCritterCount = 16u;

    /// Distance at which critters flee from the player.
    float fleeDistance = 6.0f;

    /// Beyond this distance from the player, a critter relocates.
    float cullRadius = 36.0f;

    /// Relocate ring inner/outer radius (yards from player).
    float spawnMin = 16.0f;
    float spawnMax = 30.0f;

    /// World bounds margin (critters stay this far from the edge).
    float edgeMargin = 8.0f;

    /// Max walkable slope (rise/run).
    float maxWalkSlope = 1.5f;

    /// Water level — critters must stay above waterLevel + dryMargin.
    float waterLevel = -4.5f;
    float dryMargin = 0.8f;

    /// World bounds (X symmetric, Z asymmetric like the reference).
    float worldMaxX = 180.0f;
    float worldMinZ = -180.0f;
    float worldMaxZ = 900.0f;
};

/// CritterField — the ambient wildlife pool.
///
/// Construct with a seed, then call update(playerX, playerZ, dt) each frame.
/// After update, read critters() to get the current state for rendering.
class CritterField {
public:
    static constexpr std::uint32_t kCount = CritterFieldConfig::kCritterCount;

    CritterField() = default;

    /// Initialize the field with a seed. Must be called before update().
    void initialize(std::uint32_t seed) noexcept
    {
        rng_.seed(seed ^ 0x6c12a7u);
        // Assign species: 45% rabbit, 30% squirrel, 25% bird.
        for (std::uint32_t i = 0u; i < kCount; ++i) {
            const float r = rng_.next();
            critters_[i].species = (r < 0.45f) ? CritterSpecies::Rabbit
                : (r < 0.75f)                  ? CritterSpecies::Squirrel
                                               : CritterSpecies::Bird;
            critters_[i].heading = rng_.next() * 6.2831853f;
            critters_[i].turnT = 0.5f + rng_.next() * 2.0f;
            if (critters_[i].species == CritterSpecies::Bird) {
                critters_[i].baseY = 0.25f + rng_.next() * 0.4f;
            }
            critters_[i].visible = false;
        }
        initialized_ = true;
    }

    /// Legacy heightfield entry point; both sources share the same simulation.
    void update(
        float playerX, float playerZ, float dt, const TerrainHeightField& heightField) noexcept
    {
        updateGround(playerX, playerZ, dt, [&heightField](float x, float z, float& height) {
            height = heightField.sample(x, z);
            return std::isfinite(height);
        });
    }

    /// Unavailable or nonfinite surface probes reject ground without substitution.
    void update(float playerX, float playerZ, float dt, const terrain::TerrainSurface& surface)
    {
        updateGround(playerX, playerZ, dt, [&surface](float x, float z, float& height) {
            const auto query = surface.sample(x, z);
            height = query.sample.height;
            return query.ready() && std::isfinite(height);
        });
    }

    /// Read-only access to the critter pool.
    const std::array<CritterState, kCount>& critters() const noexcept { return critters_; }

    std::array<CritterState, kCount>& critters() noexcept { return critters_; }

    /// Configuration access (for tuning / testing).
    CritterFieldConfig& config() noexcept { return config_; }

    const CritterFieldConfig& config() const noexcept { return config_; }

private:
    Mulberry32 rng_;
    CritterFieldConfig config_;
    std::array<CritterState, kCount> critters_;
    bool initialized_ = false;

    template <typename GroundQuery>
    void updateGround(float playerX, float playerZ, float dt, const GroundQuery& ground)
    {
        if (!initialized_)
            return;

        for (std::uint32_t i = 0u; i < kCount; ++i) {
            auto& c = critters_[i];
            const float dx = c.x - playerX;
            const float dz = c.z - playerZ;
            const float dist = std::sqrt(dx * dx + dz * dz);

            // Relocate if too far from player or on invalid ground.
            if (dist > config_.cullRadius || !isValidGround(c.x, c.z, ground)) {
                relocate(c, playerX, playerZ, ground);
                continue;
            }

            // Flee or wander.
            bool fleeing = false;
            if (dist < config_.fleeDistance) {
                c.heading = std::atan2(dz, dx); // face away from player
                c.moving = true;
                fleeing = true;
            } else {
                c.turnT -= dt;
                if (c.turnT <= 0.0f) {
                    c.moving = rng_.next() > 0.35f;
                    if (c.moving)
                        c.heading += (rng_.next() - 0.5f) * 2.2f;
                    c.turnT = 0.6f + rng_.next() * 2.4f;
                }
            }

            // Speed.
            const float baseSpeed = (c.species == CritterSpecies::Bird) ? 2.4f : 1.5f;
            c.speed = c.moving ? (fleeing ? baseSpeed * 2.4f : baseSpeed) : 0.0f;

            // Move.
            const float nx = c.x + std::cos(c.heading) * c.speed * dt;
            const float nz = c.z + std::sin(c.heading) * c.speed * dt;
            if (isValidGround(nx, nz, ground)) {
                c.x = nx;
                c.z = nz;
                c.hopPhase += dt * ((c.species == CritterSpecies::Bird) ? 18.0f : 9.0f);
            } else {
                // Blocked — U-turn + wait.
                c.heading += 3.14159265f + (rng_.next() - 0.5f);
                c.turnT = 0.4f + rng_.next();
            }

            c.baseY = (c.species == CritterSpecies::Bird)
                ? (0.25f + std::fabs(std::sin(c.hopPhase * 0.3f)) * 0.4f)
                : 0.0f;
            c.visible = true;
        }
    }

    template <typename GroundQuery>
    bool isValidGround(float x, float z, const GroundQuery& ground) const
    {
        if (!std::isfinite(x) || !std::isfinite(z))
            return false;
        // World bounds (with edge margin).
        if (x < -config_.worldMaxX + config_.edgeMargin
            || x > config_.worldMaxX - config_.edgeMargin
            || z < config_.worldMinZ + config_.edgeMargin
            || z > config_.worldMaxZ - config_.edgeMargin) {
            return false;
        }
        // Slope check.
        const float eps = 0.35f;
        float h, heightX, heightZ;
        if (!ground(x, z, h) || !ground(x + eps, z, heightX) || !ground(x, z + eps, heightZ))
            return false;
        const float hx = heightX - h;
        const float hz = heightZ - h;
        const float slope = std::sqrt(hx * hx + hz * hz) / eps;
        if (!std::isfinite(slope) || slope > config_.maxWalkSlope)
            return false;
        // Dry ground.
        if (h < config_.waterLevel + config_.dryMargin)
            return false;
        return true;
    }

    template <typename GroundQuery>
    void relocate(CritterState& c, float px, float pz, const GroundQuery& ground)
    {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const float angle = rng_.next() * 6.2831853f;
            const float radius =
                config_.spawnMin + rng_.next() * (config_.spawnMax - config_.spawnMin);
            const float x = px + std::cos(angle) * radius;
            const float z = pz + std::sin(angle) * radius;
            if (isValidGround(x, z, ground)) {
                c.x = x;
                c.z = z;
                c.heading = rng_.next() * 6.2831853f;
                c.turnT = 0.5f + rng_.next() * 2.0f;
                c.moving = false;
                c.visible = true;
                return;
            }
        }
        c.visible = false; // all attempts failed; hide until next tick
    }
};

} // namespace arp::core
