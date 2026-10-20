// Hidden smoke test for trail/beam particle system family dispatch.
// Verifies: emitter creation with trail/beam visual family,
//           trailIndex ordering, particle telemetry, draw plan generation.

#include "renderer/particles/particle_draw_plan.h"
#include "renderer/particles/particle_system.h"
#include "renderer/scene/scene.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

bool hidden_smoke_trail_emitter_basics()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{256, 8});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "TrailEmitter";
    desc.maxParticles = 64;
    desc.spawnRatePerSecond = 15.0f;
    desc.particleLifetimeSeconds = 1.5f;
    desc.looping = true;
    desc.seed = 100;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.velocityMin = {0.5f, 0.0f, 0.0f};
    desc.velocityMax = {1.0f, 0.0f, 0.0f};
    desc.startSize = 0.3f;
    desc.endSize = 0.05f;
    desc.startColor = {1.0f, 0.5f, 0.2f, 1.0f};
    desc.endColor = {1.0f, 0.2f, 0.0f, 0.0f};
    desc.visualFamily = arp::renderer::ParticleVisualFamily::Trail;
    desc.materialBinding.shaderFamilyId = "ParticleTrail";
    desc.materialBinding.isTrail = true;
    desc.trailWidth = 0.15f;
    desc.trailMaxSegments = 32;

    const auto result = system.addEmitter(desc);
    if (!result.success) {
        std::fprintf(stderr, "FAIL: trail addEmitter failed: %s\n", result.diagnostics.c_str());
        return false;
    }

    // Tick 30 frames
    for (int frame = 0; frame < 30; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    const auto telem = system.telemetry();
    if (telem.aliveParticles == 0) {
        std::fprintf(stderr, "FAIL: trail emitter produced no particles\n");
        return false;
    }

    // Verify all particles have the correct visual family
    bool allTrail = true;
    uint32_t maxTrailIndex = 0;
    for (const auto& p : system.particles()) {
        if (p.visualFamily != arp::renderer::ParticleVisualFamily::Trail) {
            allTrail = false;
            std::fprintf(stderr,
                "FAIL: trail particle has wrong visual family (got %u)\n",
                static_cast<uint32_t>(p.visualFamily));
            break;
        }
        if (p.trailIndex > maxTrailIndex) {
            maxTrailIndex = p.trailIndex;
        }
    }
    if (!allTrail) {
        return false;
    }
    if (maxTrailIndex == 0 && telem.spawnedThisFrame > 0) {
        std::fprintf(stderr, "FAIL: trail particles should have non-zero trailIndex\n");
        return false;
    }

    std::printf("PASS: trail hidden smoke: aliveParticles=%u trailIndexMax=%u spawned=%u\n",
        telem.aliveParticles,
        maxTrailIndex,
        telem.spawnedThisFrame);
    return true;
}

bool hidden_smoke_beam_emitter_basics()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{128, 4});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "BeamEmitter";
    desc.maxParticles = 8;
    desc.burstCount = 2;
    desc.spawnRatePerSecond = 0.0f;
    desc.particleLifetimeSeconds = 2.0f;
    desc.looping = false;
    desc.seed = 200;
    desc.origin = {0.0f, 2.0f, 0.0f};
    desc.velocityMin = {0.0f, 0.0f, 0.0f};
    desc.velocityMax = {0.0f, 0.0f, 0.0f};
    desc.startSize = 0.2f;
    desc.endSize = 0.2f;
    desc.startColor = {0.2f, 0.6f, 1.0f, 1.0f};
    desc.endColor = {0.1f, 0.3f, 0.8f, 0.0f};
    desc.visualFamily = arp::renderer::ParticleVisualFamily::Beam;
    desc.materialBinding.shaderFamilyId = "ParticleBeam";
    desc.materialBinding.isBeam = true;
    desc.beamLength = 3.0f;

    const auto result = system.addEmitter(desc);
    if (!result.success) {
        std::fprintf(stderr, "FAIL: beam addEmitter failed: %s\n", result.diagnostics.c_str());
        return false;
    }

    // Single tick should fire the burst of 2 beam endpoints
    system.update(arp::renderer::ParticleFrameInput{0.0166f});

    const auto telem = system.telemetry();
    if (telem.aliveParticles < 2) {
        std::fprintf(stderr,
            "FAIL: beam emitter did not produce 2 endpoint particles (got %u)\n",
            telem.aliveParticles);
        return false;
    }

    // Verify visual family
    for (const auto& p : system.particles()) {
        if (p.visualFamily != arp::renderer::ParticleVisualFamily::Beam) {
            std::fprintf(stderr,
                "FAIL: beam particle has wrong visual family (got %u)\n",
                static_cast<uint32_t>(p.visualFamily));
            return false;
        }
    }

    std::printf("PASS: beam hidden smoke: aliveParticles=%u spawned=%u\n",
        telem.aliveParticles,
        telem.spawnedThisFrame);
    return true;
}

bool hidden_smoke_trail_draw_plan_produces_geometry()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{128, 4});

    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "TrailDrawEmitter";
    desc.maxParticles = 32;
    desc.spawnRatePerSecond = 10.0f;
    desc.particleLifetimeSeconds = 2.0f;
    desc.looping = true;
    desc.seed = 300;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.velocityMin = {1.0f, 0.0f, 0.0f};
    desc.velocityMax = {2.0f, 0.0f, 0.0f};
    desc.visualFamily = arp::renderer::ParticleVisualFamily::Trail;
    desc.materialBinding.shaderFamilyId = "ParticleTrail";
    desc.materialBinding.isTrail = true;
    desc.trailWidth = 0.35f;

    system.addEmitter(desc);

    // Tick 20 frames to accumulate enough particles for trail segments
    for (int frame = 0; frame < 20; ++frame) {
        system.update(arp::renderer::ParticleFrameInput{0.0166f});
    }

    // Build dummy scene constants for draw plan
    arp::renderer::SceneConstants scene{};
    for (int i = 0; i < 16; ++i) {
        scene.viewProjection[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    scene.cameraPosition[0] = 0.0f;
    scene.cameraPosition[1] = 0.0f;
    scene.cameraPosition[2] = 10.0f;

    const auto plan = arp::renderer::buildTrailBeamDrawPlan(
        system.particles(), scene, arp::renderer::ParticleVisualFamily::Trail);

    if (!plan.success) {
        std::fprintf(stderr, "FAIL: trail draw plan failed: %s\n", plan.diagnostics.c_str());
        return false;
    }
    if (plan.vertices.empty()) {
        std::fprintf(stderr, "FAIL: trail draw plan produced no vertices\n");
        return false;
    }
    if (plan.commands.empty()) {
        std::fprintf(stderr, "FAIL: trail draw plan has no commands\n");
        return false;
    }
    if (plan.visualFamily != arp::renderer::ParticleVisualFamily::Trail) {
        std::fprintf(stderr, "FAIL: trail draw plan has wrong visual family\n");
        return false;
    }
    if (plan.pushConstants.trailWidth < 0.34f) {
        std::fprintf(stderr,
            "FAIL: trail draw plan lost configured width (got %.3f)\n",
            plan.pushConstants.trailWidth);
        return false;
    }

    std::printf("PASS: trail draw plan: vertexCount=%zu commandCount=%zu bytes=%llu\n",
        plan.vertices.size(),
        plan.commands.size(),
        static_cast<unsigned long long>(plan.verticesSizeBytes()));
    return true;
}

bool hidden_smoke_beam_draw_plan_produces_geometry()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{128, 4});

    // Create one emitter that spawns exactly 2 particles (beam endpoints)
    arp::renderer::ParticleEmitterDesc desc;
    desc.name = "BeamDrawEmitter";
    desc.maxParticles = 2;
    desc.burstCount = 2;
    desc.spawnRatePerSecond = 0.0f;
    desc.particleLifetimeSeconds = 1.0f;
    desc.looping = false;
    desc.seed = 400;
    desc.origin = {0.0f, 2.0f, 0.0f};
    desc.velocityMin = {3.0f, 0.0f, 0.0f};
    desc.velocityMax = {3.0f, 0.0f, 0.0f};
    desc.visualFamily = arp::renderer::ParticleVisualFamily::Beam;
    desc.materialBinding.shaderFamilyId = "ParticleBeam";
    desc.materialBinding.isBeam = true;
    desc.beamLength = 4.5f;

    system.addEmitter(desc);
    system.update(arp::renderer::ParticleFrameInput{0.0166f});

    arp::renderer::SceneConstants scene{};
    for (int i = 0; i < 16; ++i) {
        scene.viewProjection[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    const auto plan = arp::renderer::buildTrailBeamDrawPlan(
        system.particles(), scene, arp::renderer::ParticleVisualFamily::Beam);

    if (!plan.success) {
        std::fprintf(stderr, "FAIL: beam draw plan failed: %s\n", plan.diagnostics.c_str());
        return false;
    }
    if (plan.vertices.empty()) {
        std::fprintf(stderr, "FAIL: beam draw plan produced no vertices\n");
        return false;
    }
    if (plan.pushConstants.beamLength < 4.4f) {
        std::fprintf(stderr,
            "FAIL: beam draw plan lost configured length (got %.3f)\n",
            plan.pushConstants.beamLength);
        return false;
    }

    std::printf("PASS: beam draw plan: vertexCount=%zu beamLength=%.3f\n",
        plan.vertices.size(),
        plan.pushConstants.beamLength);
    return true;
}

} // namespace

int main()
{
    bool pass = true;
    pass = hidden_smoke_trail_emitter_basics() && pass;
    pass = hidden_smoke_beam_emitter_basics() && pass;
    pass = hidden_smoke_trail_draw_plan_produces_geometry() && pass;
    pass = hidden_smoke_beam_draw_plan_produces_geometry() && pass;

    if (!pass) {
        std::fprintf(stderr, "HIDDEN SMOKE FAILED\n");
        return EXIT_FAILURE;
    }
    std::printf("HIDDEN SMOKE PASSED\n");
    return EXIT_SUCCESS;
}
