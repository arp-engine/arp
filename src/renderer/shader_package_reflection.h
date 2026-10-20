#pragma once

#include "rhi/gpu_pipeline.h"

namespace arp::renderer {

/// Hydrate only metadata owned by the generated reflection artifact.
/// Logical descriptor bindings remain authored by the RHI package builder and
/// are deliberately not copied from the artifact here.
bool hydrateShaderPackageReflectionFromArtifact(rhi::ShaderPackageDesc& package);

} // namespace arp::renderer
