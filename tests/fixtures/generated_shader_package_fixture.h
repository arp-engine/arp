#pragma once

#include "rhi/gpu_pipeline.h"
#include "shader/generated_shader_artifact_set.h"

#include <cassert>
#include <cstdio>

namespace arp::rhi::test {
// Real offline compiler output for declaration/Null fixtures. Preparation is
// test-owned cold IO; no backend lookup, parser or execution bypass is added.
class GeneratedShaderPackageFixture final {
public:
    static void prepare(ShaderPackageDesc& package, const char* publication)
    {
        shader::GeneratedShaderArtifactSetBudget budget{
            4u * 1024u * 1024u, 16u * 1024u * 1024u, 16u * 1024u * 1024u, 32u, 8u};
        core::pmr::String diagnostic;
        auto owner = shader::GeneratedShaderArtifactSet::load(publication,
            budget,
            core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes),
            diagnostic);
        if (!owner)
            std::fprintf(stderr, "contract fixture %s: %s\n", publication, diagnostic.c_str());
        assert(owner);
        const auto bound = validateGeneratedShaderPackageSource(package, owner, publication);
        assert(bound.success);
        copyArtifactOwnedShaderReflection(package.reflection, owner->module().reflection);
        for (auto& entry : package.entrypoints) {
            const auto found = std::find_if(owner->module().reflection.entryPoints.begin(),
                owner->module().reflection.entryPoints.end(),
                [&](const auto& candidate) {
                    return candidate.name == entry.entryPoint;
                });
            assert(found != owner->module().reflection.entryPoints.end());
            entry.computeWorkgroupSizeX = found->computeWorkgroupSizeX;
            entry.computeWorkgroupSizeY = found->computeWorkgroupSizeY;
            entry.computeWorkgroupSizeZ = found->computeWorkgroupSizeZ;
            entry.writesRenderTargetArrayIndex = found->writesRenderTargetArrayIndex;
        }
        package.artifacts.publicationPath = publication;
        package.generatedArtifactSet = std::move(owner);
        package.generatedReflectionHydrated = true;
        assert(validateGeneratedShaderPackageInput(package).success);
    }
};
} // namespace arp::rhi::test
