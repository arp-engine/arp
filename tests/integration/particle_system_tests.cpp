#include "3d/camera_parameters.h"
#include "renderer/particles/particle_draw_plan.h"
#include "renderer/particles/particle_system.h"
#include "renderer/scene/scene.h"

#include <cassert>
#include <cstdint>

namespace {

arp::renderer::ParticleEmitterDesc loopingEmitter()
{
    arp::renderer::ParticleEmitterDesc emitter;
    emitter.name = "CampfireSparks";
    emitter.maxParticles = 8;
    emitter.spawnRatePerSecond = 4.0f;
    emitter.particleLifetimeSeconds = 1.0f;
    emitter.looping = true;
    emitter.seed = 17;
    emitter.origin = {1.0f, 2.0f, 3.0f};
    emitter.velocityMin = {-0.25f, 1.0f, -0.25f};
    emitter.velocityMax = {0.25f, 1.5f, 0.25f};
    emitter.startSize = 0.2f;
    emitter.endSize = 0.05f;
    emitter.startColor = {1.0f, 0.72f, 0.24f, 1.0f};
    emitter.endColor = {0.25f, 0.08f, 0.02f, 0.0f};
    return emitter;
}

void particle_system_respects_fixed_capacity()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{
        16,
        2,
    });

    const auto first = system.addEmitter(loopingEmitter());
    assert(first.success);
    assert(first.handle.valid());

    auto secondDesc = loopingEmitter();
    secondDesc.name = "Dust";
    secondDesc.maxParticles = 16;
    const auto second = system.addEmitter(secondDesc);

    assert(!second.success);
    assert(system.telemetry().capacity == 16);
    assert(system.telemetry().emitterCount == 1);
    assert(system.telemetry().aliveParticles == 0);
}

void particle_system_spawns_deterministically()
{
    arp::renderer::ParticleSystem a(arp::renderer::ParticleSystemDesc{32, 2});
    arp::renderer::ParticleSystem b(arp::renderer::ParticleSystemDesc{32, 2});

    assert(a.addEmitter(loopingEmitter()).success);
    assert(b.addEmitter(loopingEmitter()).success);

    a.update(arp::renderer::ParticleFrameInput{0.5f});
    b.update(arp::renderer::ParticleFrameInput{0.5f});

    assert(a.particles().size() == 2);
    assert(b.particles().size() == 2);
    assert(a.particles()[0].position.x == b.particles()[0].position.x);
    assert(a.particles()[0].velocity.y == b.particles()[0].velocity.y);
    assert(a.particles()[1].seed == b.particles()[1].seed);
}

void looping_particle_system_emits_initial_burst()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto emitter = loopingEmitter();
    emitter.burstCount = 5;
    emitter.spawnRatePerSecond = 4.0f;
    assert(system.addEmitter(emitter).success);

    system.update(arp::renderer::ParticleFrameInput{0.0f});

    assert(system.telemetry().aliveParticles == 5);
    assert(system.telemetry().spawnedThisFrame == 5);
}

void scheduled_particle_bursts_respect_time_and_cycles()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto emitter = loopingEmitter();
    emitter.name = "ScheduledBurst";
    emitter.maxParticles = 16;
    emitter.looping = false;
    emitter.spawnRatePerSecond = 0.0f;
    emitter.burstCount = 0;
    emitter.burstSchedule.push_back(arp::renderer::ParticleBurstDesc{
        0.25f,
        2u,
        2u,
        2u,
        0.5f,
        1.0f,
    });
    assert(system.addEmitter(emitter).success);

    system.update(arp::renderer::ParticleFrameInput{0.1f});
    assert(system.telemetry().aliveParticles == 0);
    system.update(arp::renderer::ParticleFrameInput{0.2f});
    assert(system.telemetry().aliveParticles == 2);
    system.update(arp::renderer::ParticleFrameInput{0.5f});
    assert(system.telemetry().aliveParticles == 4);
}

void particle_system_retires_expired_particles()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto emitter = loopingEmitter();
    emitter.looping = false;
    emitter.burstCount = 0;
    emitter.spawnRatePerSecond = 0.0f;
    emitter.particleLifetimeSeconds = 0.25f;
    const auto result = system.addEmitter(emitter);
    assert(result.success);

    system.triggerBurst(result.handle, 3);
    system.update(arp::renderer::ParticleFrameInput{0.0f});
    assert(system.telemetry().aliveParticles == 3);

    system.update(arp::renderer::ParticleFrameInput{0.5f});
    assert(system.telemetry().aliveParticles == 0);
    assert(system.particles().empty());
}

void mobile_profile_caps_are_bounded()
{
    const auto low =
        arp::renderer::particleSystemDescForProfile(arp::renderer::ParticleQualityProfile::Low);
    const auto medium =
        arp::renderer::particleSystemDescForProfile(arp::renderer::ParticleQualityProfile::Medium);
    const auto desktop =
        arp::renderer::particleSystemDescForProfile(arp::renderer::ParticleQualityProfile::Desktop);

    assert(low.maxParticles <= 1024);
    assert(low.maxEmitters <= medium.maxEmitters);
    assert(medium.maxParticles < desktop.maxParticles);
}

void particle_system_rejects_invalid_emitter_contract()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});

    auto zeroCapacity = loopingEmitter();
    zeroCapacity.maxParticles = 0;
    const auto r1 = system.addEmitter(zeroCapacity);
    assert(!r1.success);
    assert(r1.diagnostics.find("maxParticles must be > 0") != std::pmr::string::npos);

    auto zeroLifetime = loopingEmitter();
    zeroLifetime.particleLifetimeSeconds = 0.0f;
    const auto r2 = system.addEmitter(zeroLifetime);
    assert(!r2.success);
    assert(r2.diagnostics.find("particleLifetimeSeconds must be > 0") != std::pmr::string::npos);

    auto exceedsBudget = loopingEmitter();
    exceedsBudget.maxParticles = 64;
    const auto r3 = system.addEmitter(exceedsBudget);
    assert(!r3.success);
    assert(r3.diagnostics.find("exceeds system budget") != std::pmr::string::npos);
}

void particle_system_curve_sampling_over_lifetime()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto desc = loopingEmitter();
    desc.spawnRatePerSecond = 0.0f;
    desc.burstCount = 1;
    desc.startSize = 1.0f;
    desc.endSize = 1.0f;
    desc.particleLifetimeSeconds = 2.0f;
    desc.sizeOverLifetime.keys.push_back({0.0f, 0.5f});
    desc.sizeOverLifetime.keys.push_back({1.0f, 2.0f});
    const auto result = system.addEmitter(desc);
    assert(result.success);

    // First update: spawn occurs after retireAndSimulate, so new particle retains startSize
    system.update(arp::renderer::ParticleFrameInput{0.0f});
    assert(system.particles().size() == 1);
    assert(system.particles()[0].size == 1.0f);

    // Second update: particle ages to 0.5s, normalizedAge=0.25, curve sampled at 0.25 -> 0.875
    system.update(arp::renderer::ParticleFrameInput{0.5f});
    assert(system.particles()[0].size == 0.875f);

    // Third update: particle ages to 1.0s, normalizedAge=0.5, curve sampled at 0.5 -> 1.25
    system.update(arp::renderer::ParticleFrameInput{0.5f});
    assert(system.particles()[0].size == 1.25f);
}

void particle_system_emitter_enable_disable()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto desc = loopingEmitter();
    desc.maxParticles = 16;
    desc.spawnRatePerSecond = 10.0f;
    const auto result = system.addEmitter(desc);
    assert(result.success);

    system.update(arp::renderer::ParticleFrameInput{1.0f});
    assert(system.telemetry().aliveParticles == 10);
    assert(system.telemetry().simulatedEmitters == 1);

    system.disableEmitter(result.handle);
    system.update(arp::renderer::ParticleFrameInput{1.0f});
    assert(system.telemetry().simulatedEmitters == 0);

    system.enableEmitter(result.handle);
    system.update(arp::renderer::ParticleFrameInput{1.0f});
    assert(system.telemetry().simulatedEmitters == 1);
}

void particle_system_bounds_update()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto desc = loopingEmitter();
    desc.spawnRatePerSecond = 0.0f;
    desc.burstCount = 2;
    desc.origin = {1.0f, 2.0f, 3.0f};
    desc.velocityMin = {0.0f, 0.0f, 0.0f};
    desc.velocityMax = {0.0f, 0.0f, 0.0f};
    assert(system.addEmitter(desc).success);

    system.update(arp::renderer::ParticleFrameInput{0.0f});
    const auto bounds = system.bounds();
    assert(bounds.valid());
    assert(bounds.center.x == 1.0f);
    assert(bounds.center.y == 2.0f);
    assert(bounds.center.z == 3.0f);
    assert(bounds.extents.x == 0.0f);
}

void particle_system_applies_gravity_acceleration()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto desc = loopingEmitter();
    desc.spawnRatePerSecond = 0.0f;
    desc.burstCount = 1;
    desc.particleLifetimeSeconds = 2.0f;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.velocityMin = {0.0f, 0.0f, 0.0f};
    desc.velocityMax = {0.0f, 0.0f, 0.0f};
    desc.gravityAcceleration = -9.81f;
    assert(system.addEmitter(desc).success);

    system.update(arp::renderer::ParticleFrameInput{0.0f});
    assert(system.particles().size() == 1);
    system.update(arp::renderer::ParticleFrameInput{0.5f});
    assert(system.particles()[0].velocity.y < -4.8f);
    assert(system.particles()[0].position.y < -2.4f);
}

void particle_system_world_space_emission_decouples_from_host_motion()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto desc = loopingEmitter();
    desc.spawnRatePerSecond = 0.0f;
    desc.burstCount = 1;
    desc.particleLifetimeSeconds = 2.0f;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.velocityMin = {0.0f, 0.0f, 0.0f};
    desc.velocityMax = {0.0f, 0.0f, 0.0f};
    desc.worldSpaceEmission = true;
    assert(system.addEmitter(desc).success);

    system.update(arp::renderer::ParticleFrameInput{0.0f});
    assert(system.particles().size() == 1);

    system.update(arp::renderer::ParticleFrameInput{
        1.0f,
        false,
        {5.0f, 0.0f, 0.0f},
    });
    assert(system.particles().size() == 1);
    assert(system.particles()[0].position.x == 0.0f);
}

void particle_system_local_space_emission_follows_host_motion()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{32, 2});
    auto desc = loopingEmitter();
    desc.spawnRatePerSecond = 0.0f;
    desc.burstCount = 1;
    desc.particleLifetimeSeconds = 2.0f;
    desc.origin = {0.0f, 0.0f, 0.0f};
    desc.velocityMin = {0.0f, 0.0f, 0.0f};
    desc.velocityMax = {0.0f, 0.0f, 0.0f};
    desc.worldSpaceEmission = false;
    assert(system.addEmitter(desc).success);

    system.update(arp::renderer::ParticleFrameInput{0.0f});
    assert(system.particles().size() == 1);

    system.update(arp::renderer::ParticleFrameInput{
        1.0f,
        false,
        {5.0f, 0.0f, 0.0f},
    });
    assert(system.particles().size() == 1);
    assert(system.particles()[0].position.x == 5.0f);
}

void particle_system_telemetry_counts_drops_and_budget_denials()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{4, 2});
    auto desc = loopingEmitter();
    desc.maxParticles = 2;
    desc.spawnRatePerSecond = 0.0f;
    desc.burstCount = 0;
    const auto result = system.addEmitter(desc);
    assert(result.success);

    system.triggerBurst(result.handle, 10);
    system.update(arp::renderer::ParticleFrameInput{0.0f});
    assert(system.telemetry().spawnedThisFrame == 2);
    assert(system.telemetry().droppedSpawns == 8);
    assert(system.telemetry().budgetDenials == 1);
}

void particle_system_allocator_uses_core_pmr()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{8, 2});
    auto desc = loopingEmitter();
    desc.name = "TestEmitter";
    desc.materialBinding.materialAssetId = "test-material";
    const auto result = system.addEmitter(desc);
    assert(result.success);
    system.update(arp::renderer::ParticleFrameInput{0.25f});
    assert(system.telemetry().aliveParticles == 1);
}

void visual_family_from_binding_maps_shader_family_id()
{
    assert(static_cast<uint32_t>(arp::renderer::ParticleVisualFamily::SpriteAlpha) == 1u);
    assert(static_cast<uint32_t>(arp::renderer::ParticleVisualFamily::SpriteAdditive) == 2u);
    assert(static_cast<uint32_t>(arp::renderer::ParticleVisualFamily::LightningThin) == 3u);
    assert(static_cast<uint32_t>(arp::renderer::ParticleVisualFamily::StormCloud) == 4u);
    assert(static_cast<uint32_t>(arp::renderer::ParticleVisualFamily::FallbackLegacy) == 5u);

    arp::renderer::ParticleMaterialBinding binding;
    binding.shaderFamilyId = "ParticleSpriteAlpha";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::SpriteAlpha);

    binding.shaderFamilyId = "ParticleSpriteAdditive";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::SpriteAdditive);

    binding.shaderFamilyId = "ParticleLightningThin";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::LightningThin);

    binding.shaderFamilyId = "ParticleStormCloud";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::StormCloud);

    binding.shaderFamilyId = "ParticleFallbackLegacy";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::FallbackLegacy);
}

void visual_family_from_binding_falls_back_to_blend_mode()
{
    arp::renderer::ParticleMaterialBinding binding;
    binding.shaderFamilyId = "";
    binding.blendMode = "Additive";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::SpriteAdditive);

    binding.blendMode = "Alpha";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::SpriteAlpha);
}

void visual_family_from_binding_returns_unknown_for_unrecognized()
{
    arp::renderer::ParticleMaterialBinding binding;
    binding.shaderFamilyId = "UnknownShader";
    binding.blendMode = "UnknownBlend";
    assert(arp::renderer::visualFamilyFromBinding(binding)
        == arp::renderer::ParticleVisualFamily::Unknown);
}

void radial_burst_zero_speed_rejected_by_emitter_contract()
{
    arp::renderer::ParticleSystem system(arp::renderer::ParticleSystemDesc{16, 1});

    arp::renderer::ParticleEmitterDesc emitter;
    emitter.name = "RadialBurstRejected";
    emitter.maxParticles = 8;
    emitter.spawnRatePerSecond = 0.0f;
    emitter.burstCount = 4;
    emitter.particleLifetimeSeconds = 2.0f;
    emitter.looping = false;
    emitter.seed = 42;
    emitter.shape = arp::renderer::ParticleEmitterDesc::EmitterShape::RadialBurst;
    emitter.sphereRadius = 2.0f;
    emitter.velocityMin = {0.0f, 0.0f, 0.0f};
    emitter.velocityMax = {0.0f, 0.0f, 0.0f};
    emitter.startSize = 0.1f;
    emitter.startColor = {1.0f, 1.0f, 1.0f, 1.0f};

    const auto result = system.addEmitter(emitter);
    assert(!result.success);
    assert(result.diagnostics.find("RadialBurst requires |velocityMin| > 0")
        != std::pmr::string::npos);
}

void build_particle_draw_plan_set_groups_by_visual_family()
{
    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);

    std::pmr::vector<arp::renderer::ParticleInstance> particles;
    arp::renderer::ParticleInstance alphaParticle;
    alphaParticle.position = {0.0f, 1.0f, 2.0f};
    alphaParticle.color = {1.0f, 0.6f, 0.2f, 0.8f};
    alphaParticle.size = 0.5f;
    alphaParticle.lifetimeSeconds = 1.0f;
    alphaParticle.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAlpha;
    particles.push_back(alphaParticle);

    arp::renderer::ParticleInstance additiveParticle;
    additiveParticle.position = {1.0f, 2.0f, 3.0f};
    additiveParticle.color = {0.2f, 0.6f, 1.0f, 0.5f};
    additiveParticle.size = 0.3f;
    additiveParticle.lifetimeSeconds = 1.0f;
    additiveParticle.visualFamily = arp::renderer::ParticleVisualFamily::SpriteAdditive;
    particles.push_back(additiveParticle);

    const auto drawSet = arp::renderer::buildParticleDrawPlanSet(particles, scene, 64u, true);
    assert(drawSet.success);
    assert(drawSet.plans.size() == 2u);

    bool foundAlpha = false;
    bool foundAdditive = false;
    for (const auto& plan : drawSet.plans) {
        if (plan.visualFamily == arp::renderer::ParticleVisualFamily::SpriteAlpha) {
            foundAlpha = true;
            assert(plan.vertices.size() == 6u);
            assert(plan.pushConstants.visualFamily
                == static_cast<std::uint32_t>(arp::renderer::ParticleVisualFamily::SpriteAlpha));
        }
        if (plan.visualFamily == arp::renderer::ParticleVisualFamily::SpriteAdditive) {
            foundAdditive = true;
            assert(plan.vertices.size() == 6u);
            assert(plan.pushConstants.visualFamily
                == static_cast<std::uint32_t>(arp::renderer::ParticleVisualFamily::SpriteAdditive));
        }
    }
    assert(foundAlpha);
    assert(foundAdditive);
}

void particle_draw_plan_push_constants_preserve_lightning_family()
{
    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);

    std::pmr::vector<arp::renderer::ParticleInstance> particles;
    arp::renderer::ParticleInstance lightning;
    lightning.position = {0.0f, 1.0f, 2.0f};
    lightning.color = {0.98f, 0.62f, 0.33f, 1.0f};
    lightning.size = 0.75f;
    lightning.lifetimeSeconds = 1.0f;
    lightning.visualFamily = arp::renderer::ParticleVisualFamily::LightningThin;
    particles.push_back(lightning);

    const auto drawSet = arp::renderer::buildParticleDrawPlanSet(particles, scene, 64u, true);
    assert(drawSet.success);
    assert(drawSet.plans.size() == 1u);
    assert(
        drawSet.plans.front().visualFamily == arp::renderer::ParticleVisualFamily::LightningThin);
    assert(drawSet.plans.front().pushConstants.visualFamily
        == static_cast<std::uint32_t>(arp::renderer::ParticleVisualFamily::LightningThin));

    const auto legacyPlan = arp::renderer::buildParticleDrawPlan(particles, scene, 64u, true);
    assert(legacyPlan.success);
    assert(legacyPlan.visualFamily == arp::renderer::ParticleVisualFamily::LightningThin);
    assert(legacyPlan.pushConstants.visualFamily
        == static_cast<std::uint32_t>(arp::renderer::ParticleVisualFamily::LightningThin));
}

void particle_draw_plan_push_constants_preserve_storm_cloud_family()
{
    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto scene = arp::renderer::SceneConstants::fromCamera(camera);

    std::pmr::vector<arp::renderer::ParticleInstance> particles;
    arp::renderer::ParticleInstance cloud;
    cloud.position = {0.0f, 5.0f, 0.0f};
    cloud.color = {0.06f, 0.04f, 0.10f, 0.95f};
    cloud.size = 3.0f;
    cloud.lifetimeSeconds = 3.5f;
    cloud.visualFamily = arp::renderer::ParticleVisualFamily::StormCloud;
    particles.push_back(cloud);

    const auto drawSet = arp::renderer::buildParticleDrawPlanSet(particles, scene, 64u, true);
    assert(drawSet.success);
    assert(drawSet.plans.size() == 1u);
    assert(drawSet.plans.front().visualFamily == arp::renderer::ParticleVisualFamily::StormCloud);
    assert(drawSet.plans.front().pushConstants.visualFamily
        == static_cast<std::uint32_t>(arp::renderer::ParticleVisualFamily::StormCloud));

    const auto legacyPlan = arp::renderer::buildParticleDrawPlan(particles, scene, 64u, true);
    assert(legacyPlan.success);
    assert(legacyPlan.visualFamily == arp::renderer::ParticleVisualFamily::StormCloud);
    assert(legacyPlan.pushConstants.visualFamily
        == static_cast<std::uint32_t>(arp::renderer::ParticleVisualFamily::StormCloud));
}

} // namespace

int main()
{
    particle_system_respects_fixed_capacity();
    particle_system_spawns_deterministically();
    looping_particle_system_emits_initial_burst();
    scheduled_particle_bursts_respect_time_and_cycles();
    particle_system_retires_expired_particles();
    mobile_profile_caps_are_bounded();
    particle_system_rejects_invalid_emitter_contract();
    particle_system_curve_sampling_over_lifetime();
    particle_system_emitter_enable_disable();
    particle_system_bounds_update();
    particle_system_applies_gravity_acceleration();
    particle_system_world_space_emission_decouples_from_host_motion();
    particle_system_local_space_emission_follows_host_motion();
    particle_system_telemetry_counts_drops_and_budget_denials();
    particle_system_allocator_uses_core_pmr();
    visual_family_from_binding_maps_shader_family_id();
    visual_family_from_binding_falls_back_to_blend_mode();
    visual_family_from_binding_returns_unknown_for_unrecognized();
    build_particle_draw_plan_set_groups_by_visual_family();
    particle_draw_plan_push_constants_preserve_lightning_family();
    particle_draw_plan_push_constants_preserve_storm_cloud_family();
    radial_burst_zero_speed_rejected_by_emitter_contract();
    return 0;
}
