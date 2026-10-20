#include "renderer/shader_package_reflection.h"

#include "rhi/gpu_pipeline.h"
#include "shader/shader_pipeline_reflection_validator.h"
#include "shader/shader_reflection_cache.h"

#include <cstdio>

namespace arp::renderer {

namespace {

rhi::ShaderStage rhiStageFor(shader::ShaderStage stage)
{
    switch (stage) {
    case shader::ShaderStage::Vertex:
        return rhi::ShaderStage::Vertex;
    case shader::ShaderStage::Fragment:
        return rhi::ShaderStage::Fragment;
    case shader::ShaderStage::Compute:
        return rhi::ShaderStage::Compute;
    case shader::ShaderStage::TessellationControl:
        return rhi::ShaderStage::TessellationControl;
    case shader::ShaderStage::TessellationEvaluation:
        return rhi::ShaderStage::TessellationEvaluation;
    }
    return rhi::ShaderStage::None;
}

} // namespace

bool hydrateShaderPackageReflectionFromArtifact(rhi::ShaderPackageDesc& package)
{
    package.generatedReflectionHydrated = false;
    if (package.artifacts.reflectionPath.empty()) {
        return false;
    }
    const auto cached =
        shader::ShaderReflectionCache::loadFromFile(package.artifacts.reflectionPath);
    if (!cached.success) {
        std::fprintf(stderr,
            "[shader-reflection] load failed path='%s': %s\n",
            package.artifacts.reflectionPath.c_str(),
            cached.diagnostics.c_str());
        return false;
    }
    const auto artifactValidation =
        shader::ShaderPipelineReflectionValidator::validateGeneratedArtifact(cached.module,
            package.slangModulePath,
            package.artifacts.spirvPath,
            package.artifacts.mslPath,
            package.artifacts.wgslPath);
    if (!artifactValidation.success) {
        std::fprintf(stderr,
            "[shader-reflection] artifact validation failed source='%s': %s\n",
            package.slangModulePath.c_str(),
            artifactValidation.diagnostics.c_str());
        return false;
    }

    rhi::copyArtifactOwnedShaderReflection(package.reflection, cached.module.reflection);
    for (auto& entrypoint : package.entrypoints) {
        for (const auto& reflected : cached.module.reflection.entryPoints) {
            if (reflected.name == entrypoint.entryPoint
                && rhiStageFor(reflected.stage) == entrypoint.stage) {
                entrypoint.computeWorkgroupSizeX = reflected.computeWorkgroupSizeX;
                entrypoint.computeWorkgroupSizeY = reflected.computeWorkgroupSizeY;
                entrypoint.computeWorkgroupSizeZ = reflected.computeWorkgroupSizeZ;
                entrypoint.writesRenderTargetArrayIndex = reflected.writesRenderTargetArrayIndex;
                break;
            }
        }
    }
    package.generatedReflectionHydrated = true;
    return true;
}

} // namespace arp::renderer
