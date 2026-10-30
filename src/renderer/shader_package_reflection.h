#pragma once

#include "rhi/gpu_pipeline.h"
#include "shader/generated_shader_artifact_set.h"

namespace arp::renderer {

/// Hydrate only metadata owned by the generated reflection artifact.
/// Logical descriptor bindings remain authored by the RHI package builder and
/// are deliberately not copied from the artifact here.
bool hydrateShaderPackageReflectionFromArtifact(rhi::ShaderPackageDesc& package);
ARP_API bool hydrateShaderPackageReflectionFromArtifact(
    rhi::ShaderPackageDesc&, core::pmr::String& diagnostic);

struct ShaderPackagePreparationDesc {
    std::filesystem::path publication;
    shader::GeneratedShaderArtifactSetBudget budget;
    core::SharedPtr<const shader::GeneratedShaderArtifactSet> prepared;
};

// Explicit acquired source or immutable owner; no IO when prepared is supplied.
ARP_API bool hydrateShaderPackageReflectionFromArtifact(
    rhi::ShaderPackageDesc&, const ShaderPackagePreparationDesc&, core::pmr::String& diagnostic);

} // namespace arp::renderer
