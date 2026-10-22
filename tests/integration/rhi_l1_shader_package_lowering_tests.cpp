#include "core/memory/cpu_allocator.h"
#include "rhi/semantics/native_shader_package_lowering.h"

#include <cassert>

namespace {

arp::rhi::PipelineLayoutDesc makePipelineLayout()
{
    using namespace arp::rhi;

    PipelineLayoutDesc layout;
    layout.descriptorSets.push_back({0, "FrameDescriptors"});
    layout.descriptorTables.push_back(
        {1, "MaterialTextureTable", DescriptorType::SampledTexture, ShaderStage::Fragment, 4});
    layout.pushConstantRanges.push_back(
        {ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute, 0, 128});
    return layout;
}

arp::rhi::ShaderPackageDesc makeShaderPackage()
{
    using namespace arp::rhi;

    ShaderPackageDesc package;
    package.label = "SceneShaderPackage";
    package.slangModulePath = "scene.slang";
    package.entrypoints.push_back({"scene.slang", "vsMain", ShaderStage::Vertex});
    package.entrypoints.push_back({"scene.slang", "fsMain", ShaderStage::Fragment});
    package.entrypoints.push_back({"scene.slang", "csMain", ShaderStage::Compute});
    package.artifacts.spirvPath = "scene.spv";
    package.artifacts.mslPath = "scene.metal";
    package.artifacts.reflectionPath = "scene.reflection.yaml";
    package.reflection.pushConstantByteCount = 128;
    package.reflection.descriptorBindings.push_back(
        {0, 0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    package.reflection.descriptorBindings.push_back(
        {1, 0, DescriptorType::SampledTexture, ShaderStage::Fragment});
    return package;
}

std::pmr::vector<arp::rhi::DescriptorLayoutDesc> makeDescriptorLayouts()
{
    using namespace arp::rhi;

    DescriptorLayoutDesc frameLayout;
    frameLayout.name = "FrameDescriptors";
    frameLayout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    return {frameLayout};
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    const auto layout = makePipelineLayout();
    const auto package = makeShaderPackage();
    const auto descriptorLayouts = makeDescriptorLayouts();

    auto vulkan = nativeBackendCapabilityContract(Backend::Vulkan);
    vulkan.maxPushConstantSizeBytes = 256;
    const auto vulkanShader = lowerShaderPackage(vulkan, package, layout, descriptorLayouts);
    assert(vulkanShader.success);
    assert(vulkanShader.backend == Backend::Vulkan);
    assert(vulkanShader.fallbackMode == FallbackMode::Native);
    assert(vulkanShader.entrypointCount == 3u);
    assert(vulkanShader.descriptorBindingCount == 2u);
    assert(vulkanShader.descriptorLayoutCount == 1u);
    assert(vulkanShader.pushConstantBytes == 128u);
    assert(vulkanShader.spirvArtifactConsumed);
    assert(!vulkanShader.mslArtifactConsumed);
    assert(vulkanShader.reflectionArtifactConsumed);
    assert(!vulkanShader.nativeHandleExposedToUpperLayer);
    assert(hasNativeShaderPackageSemantic(
        vulkanShader, NativeShaderPackageSemantic::SlangSourceModule));
    assert(hasNativeShaderPackageSemantic(
        vulkanShader, NativeShaderPackageSemantic::SlangReflectionMetadata));
    assert(hasNativeShaderPackageSemantic(
        vulkanShader, NativeShaderPackageSemantic::VulkanShaderModuleSpirv));
    assert(hasNativeShaderPackageSemantic(
        vulkanShader, NativeShaderPackageSemantic::VulkanPipelineShaderStageEntrypoints));

    auto limitedVulkan = vulkan;
    limitedVulkan.maxPushConstantSizeBytes = 64;
    const auto oversizedVulkan =
        lowerShaderPackage(limitedVulkan, package, layout, descriptorLayouts);
    assert(!oversizedVulkan.success);
    assert(oversizedVulkan.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(oversizedVulkan.diagnostics.find("push constant") != std::pmr::string::npos);

    const auto metalShader = lowerShaderPackage(
        nativeBackendCapabilityContract(Backend::Metal), package, layout, descriptorLayouts);
    assert(metalShader.success);
    assert(metalShader.backend == Backend::Metal);
    assert(metalShader.fallbackMode == FallbackMode::Native);
    assert(!metalShader.spirvArtifactConsumed);
    assert(metalShader.mslArtifactConsumed);
    assert(metalShader.reflectionArtifactConsumed);
    assert(
        hasNativeShaderPackageSemantic(metalShader, NativeShaderPackageSemantic::MetalLibraryMsl));
    assert(hasNativeShaderPackageSemantic(
        metalShader, NativeShaderPackageSemantic::MetalFunctionEntrypoints));

    const auto webgpuShader = lowerShaderPackage(
        nativeBackendCapabilityContract(Backend::WebGPU), package, layout, descriptorLayouts);
    assert(webgpuShader.success);
    assert(webgpuShader.backend == Backend::WebGPU);
    assert(webgpuShader.fallbackMode == FallbackMode::Emulated);
    assert(!webgpuShader.spirvArtifactConsumed);
    assert(!webgpuShader.mslArtifactConsumed);
    assert(webgpuShader.reflectionArtifactConsumed);
    assert(hasNativeShaderPackageSemantic(
        webgpuShader, NativeShaderPackageSemantic::WebGpuWgslTranspiledFromSlang));
    assert(hasNativeShaderPackageSemantic(
        webgpuShader, NativeShaderPackageSemantic::WebGpuBindGroupReflection));
    assert(hasNativeShaderPackageSemantic(
        webgpuShader, NativeShaderPackageSemantic::WebGpuPushConstantEmulationReflection));

    const auto nullShader = lowerShaderPackage(
        nativeBackendCapabilityContract(Backend::Null), package, layout, descriptorLayouts);
    assert(nullShader.success);
    assert(nullShader.backend == Backend::Null);
    assert(nullShader.fallbackMode == FallbackMode::Native);
    assert(hasNativeShaderPackageSemantic(
        nullShader, NativeShaderPackageSemantic::NullRecordedShaderPackage));

    auto missingMsl = package;
    missingMsl.artifacts.mslPath.clear();
    const auto invalidArtifact = lowerShaderPackage(vulkan, missingMsl, layout, descriptorLayouts);
    assert(!invalidArtifact.success);
    assert(invalidArtifact.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidArtifact.diagnostics.find("MSL") != std::pmr::string::npos);

    auto nativeLeak = package;
    nativeLeak.nativeHandleExposedToUpperLayer = true;
    const auto invalidLeak = lowerShaderPackage(vulkan, nativeLeak, layout, descriptorLayouts);
    assert(!invalidLeak.success);
    assert(invalidLeak.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidLeak.diagnostics.find("native") != std::pmr::string::npos);

    auto unknownSetReflection = package;
    unknownSetReflection.reflection.descriptorBindings.push_back(
        {7, 0, DescriptorType::UniformBuffer, ShaderStage::Compute});
    const auto invalidReflection =
        lowerShaderPackage(vulkan, unknownSetReflection, layout, descriptorLayouts);
    assert(!invalidReflection.success);
    assert(invalidReflection.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidReflection.diagnostics.find("reflection") != std::pmr::string::npos);

    auto mismatchedSetBindingType = package;
    mismatchedSetBindingType.reflection.descriptorBindings[0].type = DescriptorType::StorageBuffer;
    const auto invalidBindingType =
        lowerShaderPackage(vulkan, mismatchedSetBindingType, layout, descriptorLayouts);
    assert(!invalidBindingType.success);
    assert(invalidBindingType.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidBindingType.diagnostics.find("descriptor layout") != std::pmr::string::npos);

    auto mismatchedSetBindingStage = package;
    mismatchedSetBindingStage.reflection.descriptorBindings[0].stages = ShaderStage::Fragment;
    const auto invalidBindingStage =
        lowerShaderPackage(vulkan, mismatchedSetBindingStage, layout, descriptorLayouts);
    assert(!invalidBindingStage.success);
    assert(invalidBindingStage.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidBindingStage.diagnostics.find("descriptor layout") != std::pmr::string::npos);

    return 0;
}
