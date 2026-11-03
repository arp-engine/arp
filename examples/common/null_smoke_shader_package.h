#pragma once

#include "renderer/shader_package_reflection.h"

#include <filesystem>

namespace arp::examples {

// Cold Null-validator fixture input. Both hosts consume the same compiler
// publication; no package or reflection is synthesized inside the backend.
inline bool prepareNullSmokeShaderPackage(
    const std::filesystem::path& directory, rhi::ShaderPackageDesc& package)
{
    package.slangModulePath = (directory / "null_hidden_frame_smoke.slang").string().c_str();
    package.entrypoints = {{package.slangModulePath, "vsMain", rhi::ShaderStage::Vertex},
        {package.slangModulePath, "fsMain", rhi::ShaderStage::Fragment}};
    package.artifacts.spirvPath = (directory / "null_hidden_frame_smoke.spv").string().c_str();
    package.artifacts.mslPath = (directory / "null_hidden_frame_smoke.metal").string().c_str();
    package.artifacts.wgslPath = (directory / "null_hidden_frame_smoke.wgsl").string().c_str();
    package.artifacts.reflectionPath =
        (directory / "null_hidden_frame_smoke.reflection.yaml").string().c_str();
    package.reflection.descriptorBindings = {{0,
        0,
        rhi::DescriptorType::UniformBuffer,
        rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment}};
    return renderer::hydrateShaderPackageReflectionFromArtifact(package);
}
} // namespace arp::examples
