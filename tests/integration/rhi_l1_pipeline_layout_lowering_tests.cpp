#include "core/cpu_allocator.h"
#include "rhi/semantics/native_pipeline_layout_lowering.h"

#include <cassert>

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi_l1;

    PipelineLayoutDesc layout;
    layout.descriptorSets.push_back({0, "FrameDescriptors"});
    layout.descriptorSets.push_back({1, "MaterialDescriptors"});
    layout.pushConstantRanges.push_back({ShaderStage::Vertex | ShaderStage::Fragment, 0, 128});

    auto vulkan = nativeBackendCapabilityContract(Backend::Vulkan);
    vulkan.maxPushConstantSizeBytes = 256;

    const auto vulkanLayout = lowerPipelineLayout(vulkan, layout);
    assert(vulkanLayout.success);
    assert(vulkanLayout.backend == Backend::Vulkan);
    assert(vulkanLayout.fallbackMode == FallbackMode::Native);
    assert(vulkanLayout.descriptorModel == DescriptorBackendModel::VulkanDescriptorSet);
    assert(vulkanLayout.descriptorSetSlotCount == 2u);
    assert(vulkanLayout.pushConstantRangeCount == 1u);
    assert(vulkanLayout.pushConstantBytes == 128u);
    assert(!vulkanLayout.nativeHandleExposedToUpperLayer);
    assert(hasNativePipelineLayoutSemantic(
        vulkanLayout, NativePipelineLayoutSemantic::VulkanPipelineLayout));
    assert(hasNativePipelineLayoutSemantic(
        vulkanLayout, NativePipelineLayoutSemantic::VulkanDescriptorSetLayouts));
    assert(hasNativePipelineLayoutSemantic(
        vulkanLayout, NativePipelineLayoutSemantic::VulkanNativePushConstantRange));

    auto limitedVulkan = vulkan;
    limitedVulkan.maxPushConstantSizeBytes = 64;
    const auto oversizedVulkan = lowerPipelineLayout(limitedVulkan, layout);
    assert(!oversizedVulkan.success);
    assert(oversizedVulkan.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(oversizedVulkan.diagnostics.find("push constant") != std::pmr::string::npos);

    const auto metalLayout =
        lowerPipelineLayout(nativeBackendCapabilityContract(Backend::Metal), layout);
    assert(metalLayout.success);
    assert(metalLayout.backend == Backend::Metal);
    assert(metalLayout.fallbackMode == FallbackMode::Emulated);
    assert(metalLayout.descriptorModel == DescriptorBackendModel::MetalArgumentBinding);
    assert(hasNativePipelineLayoutSemantic(
        metalLayout, NativePipelineLayoutSemantic::MetalPipelineLayout));
    assert(hasNativePipelineLayoutSemantic(
        metalLayout, NativePipelineLayoutSemantic::MetalArgumentBindingLayout));
    assert(hasNativePipelineLayoutSemantic(
        metalLayout, NativePipelineLayoutSemantic::MetalPushConstantUploadBuffer));

    const auto webgpuLayout =
        lowerPipelineLayout(nativeBackendCapabilityContract(Backend::WebGPU), layout);
    assert(webgpuLayout.success);
    assert(webgpuLayout.backend == Backend::WebGPU);
    assert(webgpuLayout.fallbackMode == FallbackMode::Emulated);
    assert(webgpuLayout.descriptorModel == DescriptorBackendModel::BackendOwnedDescriptorGroup);
    assert(!webgpuLayout.nativeHandleExposedToUpperLayer);
    assert(webgpuLayout.backendVocabularyHiddenFromUpperLayer);
    assert(webgpuLayout.backendOwnedPushConstantSystemDescriptors);
    assert(webgpuLayout.boundedPushConstantUploadRing);
    assert(webgpuLayout.pushConstantUploadRingBytes == 128u);
    assert(hasNativePipelineLayoutSemantic(
        webgpuLayout, NativePipelineLayoutSemantic::WebGpuPipelineLayout));
    assert(hasNativePipelineLayoutSemantic(
        webgpuLayout, NativePipelineLayoutSemantic::WebGpuBindGroupLayouts));
    assert(hasNativePipelineLayoutSemantic(
        webgpuLayout, NativePipelineLayoutSemantic::WebGpuSystemPushConstantBindGroup));
    assert(hasNativePipelineLayoutSemantic(
        webgpuLayout, NativePipelineLayoutSemantic::WebGpuPushConstantUploadRing));

    const auto nullLayout =
        lowerPipelineLayout(nativeBackendCapabilityContract(Backend::Null), layout);
    assert(nullLayout.success);
    assert(nullLayout.backend == Backend::Null);
    assert(nullLayout.fallbackMode == FallbackMode::Native);
    assert(nullLayout.descriptorModel == DescriptorBackendModel::NullRecording);
    assert(hasNativePipelineLayoutSemantic(
        nullLayout, NativePipelineLayoutSemantic::NullRecordedPipelineLayout));

    auto invalid = layout;
    invalid.descriptorSets.push_back({1, "DuplicateMaterialDescriptors"});
    const auto invalidDecision = lowerPipelineLayout(vulkan, invalid);
    assert(!invalidDecision.success);
    assert(invalidDecision.fallbackMode == FallbackMode::DiagnosticFailure);
    assert(invalidDecision.diagnostics.find("pipeline layout") != std::pmr::string::npos);

    return 0;
}
