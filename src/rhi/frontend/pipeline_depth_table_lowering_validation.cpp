#include "rhi/frontend/pipeline_depth_table_lowering_validation.h"

namespace arp::rhi {
ValidationResult OriginalDepthTableExpansionInputContract::populateLogicalPackage(
    const Artifact& artifact, ShaderPackageDesc& output)
{
    output = {};
    ShaderPackageDesc package;
    package.label = "OriginalDepthTableExpansionInput/1";
    package.slangModulePath = "original.slang";
    // These are logical original ABI facts. Physical targets/maps have their
    // own retained owner and are deliberately absent from ordinary paths.
    copyArtifactOwnedShaderReflection(package.reflection, artifact.entry(0u).physical.reflection);
    package.reflection.sourceSha256 = artifact.originalSourceSha256();
    for (const auto& binding : artifact.logicalBindings()) {
        const auto type = descriptorTypeFromShaderBinding(binding.type);
        if (!type)
            return {false, "RHI-ORIGINAL-TABLE-INPUT-BINDING-TYPE"};
        package.reflection.descriptorBindings.push_back({binding.set,
            binding.binding,
            *type,
            static_cast<ShaderStage>(binding.stageMask),
            binding.arrayCount,
            binding.subpassAttachmentIndex,
            binding.depthTexture});
    }
    for (std::size_t i = 0u; i < artifact.entryCount(); ++i) {
        const auto& entries = artifact.entry(i).physical.reflection.entryPoints;
        if (entries.size() != 1u)
            return {false, "RHI-ORIGINAL-TABLE-INPUT-ENTRY"};
        const auto& source = entries.front();
        const auto stage = shaderStageFromCompilerStage(source.stage);
        if (!stage)
            return {false, "RHI-ORIGINAL-TABLE-INPUT-STAGE"};
        ShaderEntrypoint entry;
        entry.modulePath = package.slangModulePath;
        entry.entryPoint = source.name;
        entry.stage = *stage;
        entry.computeWorkgroupSizeX = source.computeWorkgroupSizeX;
        entry.computeWorkgroupSizeY = source.computeWorkgroupSizeY;
        entry.computeWorkgroupSizeZ = source.computeWorkgroupSizeZ;
        entry.writesRenderTargetArrayIndex = source.writesRenderTargetArrayIndex;
        package.entrypoints.push_back(std::move(entry));
    }
    const auto valid = validateShaderPackageInterface(package);
    if (!valid.success)
        return valid;
    output = std::move(package);
    return {true, {}};
}
} // namespace arp::rhi
