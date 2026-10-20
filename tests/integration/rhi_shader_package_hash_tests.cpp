#include "core/cpu_allocator.h"
#include "rhi/gpu_pipeline.h"

#include <cassert>

namespace {

arp::rhi::DescriptorLayoutDesc makeArrayLayout()
{
    using namespace arp::rhi;

    DescriptorLayoutDesc layout;
    layout.name = "MaterialArrayDescriptors";
    layout.bindings.push_back({
        0,
        DescriptorType::SampledTexture,
        ShaderStage::Fragment,
        4,
        false,
        false,
    });
    layout.bindings.push_back({
        1,
        DescriptorType::UniformBuffer,
        ShaderStage::Vertex,
        1,
        false,
        false,
    });
    return layout;
}

arp::rhi::PipelineLayoutDesc makePipelineLayout()
{
    using namespace arp::rhi;

    auto layout = makeArrayLayout();
    PipelineLayoutDesc pipelineLayout;
    pipelineLayout.descriptorLayouts.push_back(layout);
    pipelineLayout.descriptorSets.push_back({0, layout.name});
    pipelineLayout.pushConstantRanges.push_back({
        ShaderStage::Vertex | ShaderStage::Fragment,
        0,
        96,
    });
    return pipelineLayout;
}

arp::rhi::ShaderPackageDesc makeShaderPackage()
{
    using namespace arp::rhi;

    ShaderPackageDesc package;
    package.label = "MaterialArrayPackage";
    package.slangModulePath = "material_array.slang";
    package.entrypoints.push_back({"material_array.slang", "vsMain", ShaderStage::Vertex});
    package.entrypoints.push_back({"material_array.slang", "fsMain", ShaderStage::Fragment});
    package.artifacts.spirvPath = "material_array.spv";
    package.artifacts.mslPath = "material_array.msl";
    package.artifacts.wgslPath = "material_array.wgsl";
    package.artifacts.reflectionPath = "material_array.reflection.yaml";
    package.reflection.pushConstantByteCount = 96;
    package.reflection.descriptorBindings.push_back({
        0,
        0,
        DescriptorType::SampledTexture,
        ShaderStage::Fragment,
        4,
    });
    package.reflection.descriptorBindings.push_back({
        0,
        1,
        DescriptorType::UniformBuffer,
        ShaderStage::Vertex,
        1,
    });
    return package;
}

} // namespace

int main()
{
    using namespace arp::rhi;

    const auto descriptorLayout = makeArrayLayout();
    assert(validateDescriptorLayout(descriptorLayout).success);

    auto invalidArrayLayout = descriptorLayout;
    invalidArrayLayout.bindings[0].arrayCount = 0;
    assert(!validateDescriptorLayout(invalidArrayLayout).success);

    auto invalidDynamicTexture = descriptorLayout;
    invalidDynamicTexture.bindings[0].dynamicOffset = true;
    const auto invalidDynamicTextureResult = validateDescriptorLayout(invalidDynamicTexture);
    assert(!invalidDynamicTextureResult.success);
    assert(
        invalidDynamicTextureResult.diagnostics.find("dynamic offset") != std::pmr::string::npos);

    auto dynamicUniformLayout = descriptorLayout;
    dynamicUniformLayout.bindings[1].dynamicOffset = true;
    assert(validateDescriptorLayout(dynamicUniformLayout).success);

    const auto pipelineLayout = makePipelineLayout();
    const auto shaderPackage = makeShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);
    assert(validatePipelineLayoutAgainstShaderReflection(pipelineLayout, shaderPackage.reflection)
            .success);

    auto mismatchedArrayPackage = shaderPackage;
    mismatchedArrayPackage.reflection.descriptorBindings[0].arrayCount = 8;
    const auto mismatchedArrayResult = validatePipelineLayoutAgainstShaderReflection(
        pipelineLayout, mismatchedArrayPackage.reflection);
    assert(!mismatchedArrayResult.success);
    assert(mismatchedArrayResult.diagnostics.find("array") != std::pmr::string::npos);

    const auto reflectedLayout =
        buildPipelineLayoutDescFromShaderReflection(shaderPackage.reflection);
    assert(reflectedLayout.descriptorSets.size() == 1u);
    assert(reflectedLayout.descriptorLayouts.size() == 1u);
    assert(reflectedLayout.descriptorLayouts[0].bindings[0].arrayCount == 4u);
    assert(validatePipelineLayoutAgainstShaderReflection(reflectedLayout, shaderPackage.reflection)
            .success);

    const auto hash = stableShaderPackageHash(shaderPackage);
    assert(hash != 0u);
    assert(hash == stableShaderPackageHash(shaderPackage));

    auto changedEntrypoint = shaderPackage;
    changedEntrypoint.entrypoints[1].entryPoint = "fragmentMain";
    assert(hash != stableShaderPackageHash(changedEntrypoint));

    auto changedSpirv = shaderPackage;
    changedSpirv.artifacts.spirvPath = "material_array_debug.spv";
    assert(hash != stableShaderPackageHash(changedSpirv));

    auto changedWgsl = shaderPackage;
    changedWgsl.artifacts.wgslPath = "material_array_alt.wgsl";
    assert(hash != stableShaderPackageHash(changedWgsl));

    auto changedReflection = shaderPackage;
    changedReflection.reflection.descriptorBindings[0].arrayCount = 2;
    assert(hash != stableShaderPackageHash(changedReflection));

    auto changedGpuIndirectAbi = shaderPackage;
    changedGpuIndirectAbi.reflection.metalGpuCountedIndirectContractVersion =
        "MetalGpuCountedIndirect/1";
    changedGpuIndirectAbi.reflection.metalGpuCountedIndirectStatus = "Available";
    changedGpuIndirectAbi.reflection.metalGpuCountedIndirectCountBufferIndex = 7u;
    changedGpuIndirectAbi.reflection.metalGpuCountedIndirectUsesDrawId = true;
    changedGpuIndirectAbi.reflection.metalGpuCountedIndirectMasksOutOfRange = true;
    assert(hash != stableShaderPackageHash(changedGpuIndirectAbi));

    auto changedTessellationAbi = shaderPackage;
    changedTessellationAbi.reflection.metalTessellationContractVersion = "MetalTessellation/1";
    changedTessellationAbi.reflection.metalTessellationStatus = "Available";
    changedTessellationAbi.reflection.metalTessellationVertexEntryPoint = "vsPatch";
    changedTessellationAbi.reflection.metalTessellationFactorBufferIndex = 8u;
    changedTessellationAbi.reflection.metalTessellationControlPointCount = 3u;
    changedTessellationAbi.reflection.metalTessellationPatchType = 0u;
    changedTessellationAbi.reflection.metalTessellationFactorFormat = 0u;
    changedTessellationAbi.reflection.metalTessellationFactorBytesPerPatch = 8u;
    changedTessellationAbi.reflection.metalTessellationFactorInstanceStrideBytes = 8u;
    assert(hash != stableShaderPackageHash(changedTessellationAbi));

    auto changedTessellationStride = changedTessellationAbi;
    changedTessellationStride.reflection.metalTessellationFactorInstanceStrideBytes = 16u;
    assert(stableShaderPackageHash(changedTessellationAbi)
        != stableShaderPackageHash(changedTessellationStride));

    ShaderReflectionDesc gpuAbi;
    assert(!hasMetalGpuCountedIndirectMaskAbi(gpuAbi));
    gpuAbi.metalGpuCountedIndirectContractVersion = kMetalGpuCountedIndirectContractVersion;
    gpuAbi.metalGpuCountedIndirectStatus = "Available";
    gpuAbi.metalGpuCountedIndirectCountBufferIndex = 4u;
    gpuAbi.metalGpuCountedIndirectUsesDrawId = true;
    gpuAbi.metalGpuCountedIndirectMasksOutOfRange = true;
    assert(hasMetalGpuCountedIndirectMaskAbi(gpuAbi));
    gpuAbi.metalGpuCountedIndirectCountBufferIndex = 31u;
    assert(!hasMetalGpuCountedIndirectMaskAbi(gpuAbi));

    ShaderReflectionDesc tessellationAbi;
    tessellationAbi.metalTessellationContractVersion = kMetalTessellationContractVersion;
    tessellationAbi.metalTessellationStatus = "Available";
    tessellationAbi.metalTessellationVertexEntryPoint = "vsPatch";
    tessellationAbi.metalTessellationFactorBufferIndex = 5u;
    tessellationAbi.metalTessellationControlPointCount = 3u;
    tessellationAbi.metalTessellationPatchType = 0u;
    tessellationAbi.metalTessellationFactorFormat = 0u;
    tessellationAbi.metalTessellationFactorBytesPerPatch = 8u;
    tessellationAbi.metalTessellationFactorInstanceStrideBytes = 8u;
    assert(hasMetalTessellationAbi(tessellationAbi, 3u));
    assert(!hasMetalTessellationAbi(tessellationAbi, 4u));

    return 0;
}
