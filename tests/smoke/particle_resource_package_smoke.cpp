#include "resource/internal_asset_package.h"
#include "resource/particle_asset_resources.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace {

// UUID constants for smoke test resource IDs
constexpr const char* kParticleEffectUuid = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";

bool package_smoke_write_read_and_validate()
{
    // Build particle effect without external dependencies (shader/material IDs cleared)
    // so the package doesn't need dependency entries.
    arp::resource::ParticleEffectResource effect;
    effect.name = "SmokeSparkle";
    effect.contractVersion = "particle.effect/v1";
    effect.maxParticles = 64;
    effect.maxEmitters = 4;
    effect.qualityProfile = "Medium";
    // Clear shader/material IDs so no external dependencies are declared
    effect.shaderPackageId.clear();
    effect.materialAssetId.clear();

    arp::resource::ParticleEmitterAssetDesc emitter;
    emitter.name = "Burst";
    emitter.maxParticles = 32;
    emitter.burstCount = 8;
    emitter.particleLifetimeSeconds = 1.5f;
    emitter.startSize = 0.5f;
    emitter.endSize = 0.1f;
    // No material/texture binding IDs to keep package dependency graph simple
    emitter.sizeOverLifetime.keys.push_back({0.0f, 0.5f});
    emitter.sizeOverLifetime.keys.push_back({1.0f, 2.0f});
    effect.emitters.push_back(emitter);

    arp::resource::AssetResourcePackageBuildDesc buildDesc;
    buildDesc.resourceId = kParticleEffectUuid;
    buildDesc.alias = "SmokeSparkle";
    buildDesc.status = "Contract Complete";
    buildDesc.envelopePath = "resources/particle-effect.arpres.yaml";

    const auto buildResult = effect.buildPackageEntry(buildDesc);
    if (!buildResult.success) {
        std::fprintf(
            stderr, "FAIL: buildPackageEntry failed: %s\n", buildResult.diagnostics.c_str());
        return false;
    }

    // The particle effect has no external dependencies, so the package only needs
    // the particle effect entry itself.
    arp::resource::InternalAssetPackageDesc packageDesc;
    packageDesc.name = "SmokeParticlePackage";
    packageDesc.status = "Contract Complete";
    packageDesc.entries.push_back(buildResult.entry);

    const auto tempDir = std::filesystem::temp_directory_path() / "arp_particle_smoke";
    std::filesystem::create_directories(tempDir);
    arp::resource::InternalAssetPackageWriter writer;
    const auto writeResult = writer.write(packageDesc, tempDir);
    if (!writeResult.success) {
        std::fprintf(stderr, "FAIL: package write failed: %s\n", writeResult.diagnostics.c_str());
        return false;
    }

    // Read package back
    arp::resource::InternalAssetPackageReader reader;
    const auto readResult = reader.read(tempDir);
    if (!readResult.success) {
        std::fprintf(stderr, "FAIL: package read failed: %s\n", readResult.diagnostics.c_str());
        return false;
    }

    if (readResult.package.entries.size() != 1) {
        std::fprintf(
            stderr, "FAIL: expected 1 entry, got %zu\n", readResult.package.entries.size());
        return false;
    }

    const auto& entry = readResult.package.entries[0];
    if (entry.resourceType != arp::resource::ParticleEffectResource::resourceTypeName()) {
        std::fprintf(stderr, "FAIL: resource type mismatch: %s\n", entry.resourceType.c_str());
        return false;
    }

    if (entry.alias != "SmokeSparkle") {
        std::fprintf(stderr, "FAIL: alias mismatch: %s\n", entry.alias.c_str());
        return false;
    }

    // Particle effect without external bindings has zero dependencies — valid.
    if (!entry.dependencies.empty()) {
        std::fprintf(stderr,
            "FAIL: expected empty dependencies for standalone effect, got %zu\n",
            entry.dependencies.size());
        return false;
    }

    // Cleanup
    std::filesystem::remove_all(tempDir);

    std::printf("PASS: particle resource package smoke: write/read/validate ok\n");
    return true;
}

} // namespace

int main()
{
    if (!package_smoke_write_read_and_validate()) {
        return 1;
    }
    return 0;
}
