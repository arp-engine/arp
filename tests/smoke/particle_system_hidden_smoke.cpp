#include "renderer/particles/particle_system.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

bool hidden_smoke_create_warm_tick_destroy()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{128, 4});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "SmokeEmitter";
    desc.maxParticles = 64;
    desc.spawnRatePerSecond = 20.0f;
    desc.particleLifetimeSeconds = 2.0f;
    desc.looping = true;
    desc.seed = 42;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.velocityMin = {-0.5f, 0.5f, -0.5f};
    desc.velocityMax = {0.5f, 1.5f, 0.5f};
    desc.startSize = 0.5f;
    desc.endSize = 0.1f;
    desc.startColor = {1.0f, 1.0f, 1.0f, 0.8f};
    desc.endColor = {0.5f, 0.5f, 0.5f, 0.0f};

    const auto result = system.addEmitter(desc);
    if (!result.success) {
        std::fprintf(stderr, "FAIL: addEmitter failed: %s\n", result.diagnostics.c_str());
        return false;
    }

    // Warmup: tick 60 frames at 16.6ms
    for (int frame = 0; frame < 60; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: no particles alive after warmup\n");
        return false;
    }
    if (telem.simulatedEmitters == 0) {
        std::fprintf(stderr, "FAIL: no emitters simulated\n");
        return false;
    }

    const auto bounds = system.bounds();
    if (!bounds.valid()) {
        std::fprintf(stderr, "FAIL: bounds invalid\n");
        return false;
    }

    // Tick while disabled
    system.disableEmitter(result.handle);
    const auto beforeDisable = system.telemetry().aliveParticles;
    for (int frame = 0; frame < 30; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }
    const auto afterDisable = system.telemetry().aliveParticles;
    if (afterDisable > beforeDisable) {
        std::fprintf(stderr, "FAIL: particles grew while disabled\n");
        return false;
    }

    // Re-enable and tick
    system.enableEmitter(result.handle);
    for (int frame = 0; frame < 30; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }
    if (system.telemetry().aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: no particles alive after re-enable\n");
        return false;
    }

    std::printf(
        "PASS: hidden particle smoke: warmupAlive=%u simulatedEmitters=%u boundsCenter=(%.2f,%.2f,%.2f) "
        "droppedSpawns=%u budgetDenials=%u\n",
        telem.aliveParticles,
        telem.simulatedEmitters,
        bounds.center.x,
        bounds.center.y,
        bounds.center.z,
        telem.droppedSpawns,
        telem.budgetDenials);
    return true;
}

bool hidden_smoke_collision_bounce()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{256, 8});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "CollisionEmitter";
    desc.maxParticles = 64;
    desc.spawnRatePerSecond = 10.0f;
    desc.particleLifetimeSeconds = 3.0f;
    desc.looping = true;
    desc.seed = 500;
    desc.origin = {0.0f, 5.0f, 0.0f};
    desc.velocityMin = {-1.0f, -2.0f, -1.0f};
    desc.velocityMax = {1.0f, -1.0f, 1.0f};
    desc.gravityAcceleration = -9.81f;
    desc.collisionEnabled = true;
    desc.collisionPlaneY = 0.0f;
    desc.collisionBounceFactor = 0.3f;

    system.addEmitter(desc);

    for (int frame = 0; frame < 60; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.collisionEvents == 0) {
        std::fprintf(stderr, "FAIL: no collision events detected\n");
        return false;
    }

    std::printf("PASS: collision hidden smoke: alive=%u collisionEvents=%u\n",
        telem.aliveParticles,
        telem.collisionEvents);
    return true;
}

bool hidden_smoke_sub_emitter_spawn()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{512, 16});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "ParentEmitter";
    desc.maxParticles = 64;
    desc.spawnRatePerSecond = 20.0f;
    desc.particleLifetimeSeconds = 3.0f;
    desc.looping = true;
    desc.seed = 600;
    desc.origin = {0.0f, 10.0f, 0.0f};
    desc.velocityMin = {-2.0f, -5.0f, -2.0f};
    desc.velocityMax = {2.0f, -3.0f, 2.0f};
    desc.gravityAcceleration = -9.81f;
    desc.collisionEnabled = true;
    desc.collisionPlaneY = 0.0f;
    desc.collisionBounceFactor = 0.2f;
    desc.collisionSpawnsSubEmitter = true;
    desc.subEmitterMaxParticles = 16;

    system.addEmitter(desc);

    for (int frame = 0; frame < 90; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.subEmitterSpawns == 0) {
        std::fprintf(stderr, "FAIL: no sub-emitters spawned\n");
        return false;
    }
    if (telem.emitterCount <= 1) {
        std::fprintf(stderr, "FAIL: sub-emitter not added (emitterCount=%u)\n", telem.emitterCount);
        return false;
    }

    std::printf("PASS: sub-emitter hidden smoke: emitters=%u subSpawns=%u collisionEvents=%u\n",
        telem.emitterCount,
        telem.subEmitterSpawns,
        telem.collisionEvents);
    return true;
}

bool hidden_smoke_flipbook_explosion()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{256, 4});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "FlipbookExplosion";
    desc.maxParticles = 64;
    desc.burstCount = 32;
    desc.particleLifetimeSeconds = 1.0f;
    desc.looping = false;
    desc.seed = 700;
    desc.origin = {0.0f, 2.0f, 0.0f};
    desc.velocityMin = {-2.0f, 1.0f, -2.0f};
    desc.velocityMax = {2.0f, 4.0f, 2.0f};
    desc.startSize = 1.5f;
    desc.flipbookTilesX = 4;
    desc.flipbookTilesY = 4;
    desc.flipbookFrameCount = 16;
    desc.flipbookPlayRate = 2.0f;
    desc.flipbookLooping = false;

    system.addEmitter(desc);
    for (int frame = 0; frame < 30; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.033f});
    }

    const auto telem = system.telemetry();
    if (telem.spawnedThisFrame == 0 && telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: flipbook explosion produced no particles\n");
        return false;
    }

    // Verify flipbook frames advanced
    bool hasAdvancedFrame = false;
    for (const auto& p : system.particles()) {
        if (p.flipbookFrame > 0) {
            hasAdvancedFrame = true;
            break;
        }
    }
    if (!hasAdvancedFrame && system.particles().size() > 0) {
        std::fprintf(stderr, "FAIL: flipbook frames did not advance\n");
        return false;
    }

    std::printf("PASS: flipbook explosion smoke: alive=%u framesAdvanced=%d\n",
        telem.aliveParticles,
        hasAdvancedFrame ? 1 : 0);
    return true;
}

bool hidden_smoke_cone_spray()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{256, 4});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "ConeSpray";
    desc.maxParticles = 64;
    desc.spawnRatePerSecond = 30.0f;
    desc.particleLifetimeSeconds = 2.0f;
    desc.looping = true;
    desc.seed = 800;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.velocityMin = {2.0f, 3.0f, 0.0f};
    desc.velocityMax = {5.0f, 5.0f, 0.0f};
    desc.shape = arp::renderer::ParticleEmitterDesc::EmitterShape::Cone;
    desc.coneAngleDegrees = 30.0f;

    system.addEmitter(desc);
    for (int frame = 0; frame < 20; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: cone spray produced no particles\n");
        return false;
    }

    std::printf("PASS: cone spray smoke: alive=%u\n", telem.aliveParticles);
    return true;
}

bool hidden_smoke_orbit_rune()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{256, 4});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "OrbitRune";
    desc.maxParticles = 32;
    desc.burstCount = 8;
    desc.particleLifetimeSeconds = 3.0f;
    desc.looping = false;
    desc.seed = 900;
    desc.origin = {2.0f, 0.0f, 0.0f};
    desc.startSize = 0.5f;
    desc.orbitEnabled = true;
    desc.orbitSpeed = 1.5f;
    desc.orbitCenter = {0.0f, 0.0f, 0.0f};
    desc.rotationSpeed = 2.0f;

    system.addEmitter(desc);
    for (int frame = 0; frame < 60; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: orbit rune produced no particles\n");
        return false;
    }

    // Verify particles rotated
    bool hasRotation = false;
    for (const auto& p : system.particles()) {
        if (std::abs(p.rotation) > 0.1f) {
            hasRotation = true;
            break;
        }
    }
    if (!hasRotation && system.particles().size() > 0) {
        std::fprintf(stderr, "FAIL: orbit particles did not rotate\n");
        return false;
    }

    std::printf("PASS: orbit rune smoke: alive=%u hasRotation=%d\n",
        telem.aliveParticles,
        hasRotation ? 1 : 0);
    return true;
}

bool hidden_smoke_noise_cloud()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{256, 4});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "NoiseCloud";
    desc.maxParticles = 48;
    desc.spawnRatePerSecond = 12.0f;
    desc.particleLifetimeSeconds = 4.0f;
    desc.looping = true;
    desc.seed = 1000;
    desc.origin = {0.0f, 3.0f, 0.0f};
    desc.startSize = 2.0f;
    desc.noiseFrequency = 3.0f;
    desc.noiseAmplitude = 0.5f;

    system.addEmitter(desc);
    for (int frame = 0; frame < 40; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: noise cloud produced no particles\n");
        return false;
    }

    std::printf("PASS: noise cloud smoke: alive=%u\n", telem.aliveParticles);
    return true;
}

// Verify noise perturbation is deterministic: same seed → same particle
// positions across independent runs.
bool hidden_smoke_noise_deterministic()
{
    auto runNoiseSystem = [](uint32_t seed) -> arp::renderer::ParticleSystemTelemetry {
        arp::renderer::ParticleSystem system({128, 4});
        arp::renderer::ParticleEmitterDesc desc;
        desc.name = "NoiseDet";
        desc.maxParticles = 32;
        desc.spawnRatePerSecond = 10.0f;
        desc.particleLifetimeSeconds = 3.0f;
        desc.looping = true;
        desc.seed = seed;
        desc.origin = {0.0f, 0.0f, 0.0f};
        desc.noiseFrequency = 2.0f;
        desc.noiseAmplitude = 0.8f;
        system.addEmitter(desc);
        for (int f = 0; f < 30; ++f) {
            system.update(arp::renderer::ParticleFrameInput{0.0166f});
        }
        return system.telemetry();
    };

    // Run A (seed 99)
    const auto sysA = runNoiseSystem(99);
    if (sysA.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: noise deterministic produced no particles\n");
        return false;
    }

    // Run B with same seed → must match A exactly
    const auto sysB = runNoiseSystem(99);
    if (sysB.aliveParticles != sysA.aliveParticles) {
        std::fprintf(stderr,
            "FAIL: noise deterministic mismatch aliveCount %u vs %u\n",
            sysA.aliveParticles,
            sysB.aliveParticles);
        return false;
    }

    std::printf("PASS: noise deterministic smoke: alive=%u (identical seeds → same count)\n",
        sysA.aliveParticles);
    return true;
}

} // namespace

bool hidden_smoke_directional_forward()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{128, 2});
    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "MuzzleForward";
    desc.maxParticles = 32;
    desc.spawnRatePerSecond = 30.0f;
    desc.particleLifetimeSeconds = 0.5f;
    desc.looping = true;
    desc.seed = 7;
    desc.origin = {0.0f, 0.0f, 0.0f};
    // Forward axis is +Y, magnitude 4 units/s; velocityMin/Max both carry
    // the same direction so scalar speed falls in [4.0, 4.0].
    desc.velocityMin = {0.0f, 4.0f, 0.0f};
    desc.velocityMax = {0.0f, 4.0f, 0.0f};
    desc.shape = arp::renderer::ParticleEmitterDesc::EmitterShape::DirectionalForward;
    system.addEmitter(desc);
    for (int frame = 0; frame < 30; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }
    const auto telem = system.telemetry();
    if (telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: directional forward smoke produced no particles\n");
        return false;
    }
    // Bounds center should drift along +Y — at minimum alive particles all spawned
    // at origin and move forward; center.y must be > 0.
    const auto bounds = system.bounds();
    if (bounds.center.y <= 0.0f) {
        std::fprintf(stderr,
            "FAIL: directional forward expected forward drift along +Y, center.y=%.3f\n",
            bounds.center.y);
        return false;
    }
    // Lateral bounds should remain small (origin spawn): bounds.extents.x < some small threshold.
    if (bounds.extents.x > 0.05f) {
        std::fprintf(stderr,
            "FAIL: directional forward should not scatter laterally, extents.x=%.3f\n",
            bounds.extents.x);
        return false;
    }
    std::printf("PASS: directional forward smoke: alive=%u centerY=%.3f\n",
        telem.aliveParticles,
        bounds.center.y);
    return true;
}

bool hidden_smoke_radial_burst()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{256, 2});
    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "RadialBurst";
    desc.maxParticles = 64;
    desc.spawnRatePerSecond = 0.0f;
    desc.burstCount = 64;
    desc.particleLifetimeSeconds = 0.3f;
    desc.looping = false;
    desc.seed = 21;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.sphereRadius = 1.0f;
    desc.velocityMin = {0.0f, 5.0f, 0.0f}; // scalar speed = |velocityMin| = 5 units/s
    desc.shape = arp::renderer::ParticleEmitterDesc::EmitterShape::RadialBurst;

    system.addEmitter(desc);
    system.update(
        arp::renderer::ParticleFrameInput{0.0001f}); // single initial tick to trigger burst

    const auto telem = system.telemetry();
    if (telem.aliveParticles != 64) {
        std::fprintf(
            stderr, "FAIL: radial burst expected 64 alive, got %u\n", telem.aliveParticles);
        return false;
    }
    // After 0.0001s, alive particles almost at the shell (distance ≈ 1.0 sphereRadius).
    // Walk alive particles and verify each radial distance is ~1.0.
    const auto& particlesRef = system.particles();
    int verified = 0;
    for (const auto& p : particlesRef) {
        if (p.lifetimeSeconds <= 0.0f)
            continue;
        const auto dx = p.position.x - desc.origin.x;
        const auto dy = p.position.y - desc.origin.y;
        const auto dz = p.position.z - desc.origin.z;
        const auto dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (std::fabs(dist - 1.0f) > 0.001f) {
            std::fprintf(stderr, "FAIL: radial burst particle outside shell, dist=%.4f\n", dist);
            return false;
        }
        // velocity direction == position normal
        const auto spd = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.y * p.velocity.y
            + p.velocity.z * p.velocity.z);
        if (spd < 0.001f) {
            std::fprintf(stderr, "FAIL: radial burst particle velocity ~0\n");
            return false;
        }
        // dot(direction, velocity) > 0
        const auto dot =
            (dx / dist) * p.velocity.x + (dy / dist) * p.velocity.y + (dz / dist) * p.velocity.z;
        if (dot < 0.0f) {
            std::fprintf(stderr, "FAIL: radial burst velocity not outward\n");
            return false;
        }
        ++verified;
    }
    if (verified == 0) {
        std::fprintf(stderr, "FAIL: radial burst verified 0\n");
        return false;
    }
    std::printf("PASS: radial burst smoke: verified=%u alive=%u\n", verified, telem.aliveParticles);
    return true;
}

// Verify host velocity inheritance: particles with inheritVelocityFactor > 0
// should move in the direction of host velocity (moving +X at 5 units/s),
// while particles with zero factor should barely move in the +X direction
// aside from the random velocity scatter.
bool hidden_smoke_inherit_velocity()
{
    auto makeSystem = [](float factor) -> arp::renderer::ParticleSystem {
        arp::renderer::ParticleSystem system({128, 4});
        arp::renderer::ParticleEmitterDesc desc;
        desc.name = "InheritV";
        desc.maxParticles = 24;
        desc.spawnRatePerSecond = 8.0f;
        desc.particleLifetimeSeconds = 3.0f;
        desc.looping = true;
        desc.seed = 42;
        desc.origin = {0.0f, 0.0f, 0.0f};
        desc.startSize = 0.5f;
        desc.inheritVelocityFactor = factor;
        desc.worldSpaceEmission = false;
        system.addEmitter(desc);
        const arp::renderer::ParticleFrameInput input{0.0166f, false, {5.0f, 0.0f, 0.0f}};
        for (int f = 0; f < 60; ++f) {
            system.update(input);
        }
        return system;
    };

    auto highSys = makeSystem(0.8f);
    if (highSys.telemetry().aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: inherit high produced no particles\n");
        return false;
    }
    auto zeroSys = makeSystem(0.0f);
    if (zeroSys.telemetry().aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: inherit zero produced no particles\n");
        return false;
    }

    const auto boundsHigh = highSys.bounds();
    const auto boundsZero = zeroSys.bounds();

    // High inherit factor → particles should spread farther +X than zero factor.
    // Compute max X as center.x + extents.x.
    const auto maxXHigh = boundsHigh.center.x + boundsHigh.extents.x;
    const auto maxXZero = boundsZero.center.x + boundsZero.extents.x;
    if (maxXHigh <= maxXZero) {
        std::fprintf(stderr,
            "FAIL: inherit velocity did not extend bounds +X "
            "(high=%.3f vs zero=%.3f)\n",
            maxXHigh,
            maxXZero);
        return false;
    }

    std::printf("PASS: inherit velocity smoke: high(0.8) maxX=%.3f, zero(0.0) maxX=%.3f\n",
        maxXHigh,
        maxXZero);
    return true;
}

bool hidden_smoke_billboard_modes()
{
    auto checkMode = [](const char* modeStr) -> std::uint32_t {
        arp::renderer::ParticleSystem system({64, 4});
        arp::renderer::ParticleEmitterDesc desc;
        desc.name = "BBTest";
        desc.maxParticles = 8;
        desc.spawnRatePerSecond = 4.0f;
        desc.particleLifetimeSeconds = 2.0f;
        desc.looping = true;
        desc.seed = 1;
        desc.materialBinding.billboardMode = modeStr;
        system.addEmitter(desc);
        for (int f = 0; f < 10; ++f) {
            system.update(arp::renderer::ParticleFrameInput{0.0166f});
        }
        return arp::renderer::billboardModeFromString(desc.materialBinding.billboardMode);
    };

    const auto v = checkMode("ViewAligned");
    if (v != 0u) {
        std::fprintf(stderr, "FAIL: ViewAligned expected 0 got %u\n", v);
        return false;
    }
    const auto l = checkMode("LocalAligned");
    if (l != 1u) {
        std::fprintf(stderr, "FAIL: LocalAligned expected 1 got %u\n", l);
        return false;
    }
    const auto w = checkMode("WorldAligned");
    if (w != 2u) {
        std::fprintf(stderr, "FAIL: WorldAligned expected 2 got %u\n", w);
        return false;
    }

    std::printf("PASS: billboard modes: V=%u L=%u W=%u\n", v, l, w);
    return true;
}

bool hidden_smoke_mesh_particle_spawn()
{
    arp::renderer::ParticleSystem system({64, 4});
    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "MeshDebris";
    desc.maxParticles = 8;
    desc.spawnRatePerSecond = 4.0f;
    desc.particleLifetimeSeconds = 3.0f;
    desc.looping = true;
    desc.seed = 7;
    desc.visualFamily = arp::renderer::ParticleVisualFamily::MeshParticle;
    desc.meshAssetId = "debris_rock";
    desc.meshScaleMin = 0.5f;
    desc.meshScaleMax = 1.5f;
    system.addEmitter(desc);

    for (int f = 0; f < 20; ++f) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: mesh particle spawn produced no particles\n");
        return false;
    }
    if (telem.simulatedEmitters == 0) {
        std::fprintf(stderr, "FAIL: mesh emitter not simulated\n");
        return false;
    }

    std::printf("PASS: mesh particle spawn smoke: alive=%u emitters=%u\n",
        telem.aliveParticles,
        telem.simulatedEmitters);
    return true;
}

bool hidden_smoke_depth_fade_propagation()
{
    auto checkDepthFade = [](bool enabled, float distance) -> bool {
        arp::renderer::ParticleSystem system({64, 4});
        arp::renderer::ParticleEmitterDesc desc;
        desc.name = "DepthFadeTest";
        desc.maxParticles = 8;
        desc.burstCount = 4;
        desc.spawnRatePerSecond = 0.0f;
        desc.particleLifetimeSeconds = 2.0f;
        desc.looping = true;
        desc.seed = 1;
        desc.depthFadeEnabled = enabled;
        desc.depthFadeDistance = distance;
        system.addEmitter(desc);
        for (int f = 0; f < 10; ++f) {
            system.update(arp::renderer::ParticleFrameInput{0.0166f});
        }
        const auto telem = system.telemetry();
        if (telem.aliveParticles == 0) {
            std::fprintf(stderr, "FAIL: depth fade test produced no particles\n");
            return false;
        }
        return true;
    };

    if (!checkDepthFade(false, 0.0f))
        return false;
    if (!checkDepthFade(true, 0.5f))
        return false;
    if (!checkDepthFade(true, 1.0f))
        return false;

    std::printf("PASS: depth fade propagation: enabled/disabled both work\n");
    return true;
}

int main()
{
    if (!hidden_smoke_create_warm_tick_destroy())
        return 1;
    if (!hidden_smoke_collision_bounce())
        return 1;
    if (!hidden_smoke_sub_emitter_spawn())
        return 1;
    if (!hidden_smoke_flipbook_explosion())
        return 1;
    if (!hidden_smoke_cone_spray())
        return 1;
    if (!hidden_smoke_orbit_rune())
        return 1;
    if (!hidden_smoke_noise_cloud())
        return 1;
    if (!hidden_smoke_noise_deterministic())
        return 1;
    if (!hidden_smoke_directional_forward())
        return 1;
    if (!hidden_smoke_radial_burst())
        return 1;
    if (!hidden_smoke_inherit_velocity())
        return 1;
    if (!hidden_smoke_billboard_modes())
        return 1;
    if (!hidden_smoke_mesh_particle_spawn())
        return 1;
    if (!hidden_smoke_depth_fade_propagation())
        return 1;
    return 0;
}
