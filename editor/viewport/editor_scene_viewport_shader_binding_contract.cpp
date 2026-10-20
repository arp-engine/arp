#include "editor/viewport/editor_scene_viewport_shader_binding_contract.h"

#include <array>

namespace arp::editor {
namespace {

struct RequiredBinding {
    const char* name;
    shader::BindingType type;
};

constexpr std::array<RequiredBinding, kEditorSceneViewportShaderTextureSlotCount> kTextureBindings{{
    {"BaseColorTexture", shader::BindingType::SampledTexture},
    {"NormalTexture", shader::BindingType::SampledTexture},
    {"MetallicRoughnessTexture", shader::BindingType::SampledTexture},
    {"OcclusionTexture", shader::BindingType::SampledTexture},
    {"EmissiveTexture", shader::BindingType::SampledTexture},
}};

constexpr RequiredBinding kSamplerBinding{"LinearSampler", shader::BindingType::Sampler};

std::uint32_t fragmentStageMask() noexcept
{
    return std::uint32_t{1u} << static_cast<std::uint32_t>(shader::ShaderStage::Fragment);
}

EditorSceneViewportShaderBindingDiagnostic readBinding(const shader::ShaderReflection& reflection,
    const RequiredBinding& required,
    EditorSceneViewportShaderDescriptorBinding& output)
{
    const shader::ShaderBinding* match = nullptr;
    for (const auto& binding : reflection.bindings) {
        if (binding.name != required.name)
            continue;
        if (match != nullptr)
            return EditorSceneViewportShaderBindingDiagnostic::DuplicateBinding;
        match = &binding;
    }
    if (match == nullptr)
        return EditorSceneViewportShaderBindingDiagnostic::MissingBinding;
    if (match->type != required.type)
        return EditorSceneViewportShaderBindingDiagnostic::BindingTypeMismatch;
    if (match->arrayCount != 1u)
        return EditorSceneViewportShaderBindingDiagnostic::ArrayCountMismatch;
    if (match->stageMask != fragmentStageMask())
        return EditorSceneViewportShaderBindingDiagnostic::StageMaskMismatch;

    output = {match->set, match->binding, match->arrayCount, match->stageMask};
    return EditorSceneViewportShaderBindingDiagnostic::None;
}

} // namespace

bool EditorSceneViewportMaterialShaderBindings::valid() const noexcept
{
    if (contractVersion != kEditorSceneViewportShaderBindingContractVersion || !sampler.valid())
        return false;
    for (const auto& texture : textures) {
        if (!texture.valid())
            return false;
    }
    for (std::size_t left = 0u; left < textures.size(); ++left) {
        for (std::size_t right = left + 1u; right < textures.size(); ++right) {
            if (textures[left].set == textures[right].set
                && textures[left].binding == textures[right].binding)
                return false;
        }
        if (textures[left].set == sampler.set && textures[left].binding == sampler.binding)
            return false;
    }
    return true;
}

EditorSceneViewportShaderBindingContractResult inspectEditorSceneViewportShaderBindings(
    const shader::ShaderReflection& reflection)
{
    EditorSceneViewportShaderBindingContractResult result;
    if (reflection.reflectionContractVersion != shader::kShaderReflectionContractVersion) {
        result.diagnostic =
            EditorSceneViewportShaderBindingDiagnostic::ReflectionContractUnsupported;
        return result;
    }

    for (std::size_t index = 0u; index < kTextureBindings.size(); ++index) {
        result.diagnostic =
            readBinding(reflection, kTextureBindings[index], result.bindings.textures[index]);
        if (result.diagnostic != EditorSceneViewportShaderBindingDiagnostic::None) {
            result.failedSlot = index;
            return result;
        }
    }
    result.diagnostic = readBinding(reflection, kSamplerBinding, result.bindings.sampler);
    if (result.diagnostic != EditorSceneViewportShaderBindingDiagnostic::None) {
        result.failedSlot = kTextureBindings.size();
        return result;
    }

    if (reflection.bindings.size() != kTextureBindings.size() + 1u) {
        result.diagnostic = EditorSceneViewportShaderBindingDiagnostic::UnexpectedBinding;
        return result;
    }

    result.bindings.contractVersion = kEditorSceneViewportShaderBindingContractVersion;
    if (!result.bindings.valid())
        result.diagnostic = EditorSceneViewportShaderBindingDiagnostic::DescriptorAddressCollision;
    if (result.diagnostic != EditorSceneViewportShaderBindingDiagnostic::None)
        return result;

    for (const auto& binding : reflection.bindings) {
        const auto type = rhi::descriptorTypeFromShaderBinding(binding.type);
        if (!type) {
            result.diagnostic = EditorSceneViewportShaderBindingDiagnostic::RhiLayoutInvalid;
            return result;
        }
        result.rhiReflection.descriptorBindings.push_back({
            binding.set,
            binding.binding,
            *type,
            static_cast<rhi::ShaderStage>(binding.stageMask),
            binding.arrayCount,
            binding.subpassAttachmentIndex,
            binding.depthTexture,
        });
    }
    result.rhiReflection.reflectionContractVersion = reflection.reflectionContractVersion;
    result.rhiReflection.sourceSha256 = reflection.sourceSha256;
    result.rhiReflection.pushConstantByteCount = reflection.pushConstantSizeBytes;
    result.rhiReflection.pushConstantContractVersion = reflection.pushConstantContractVersion;
    result.rhiReflection.pushConstantStages = reflection.pushConstantStages;
    result.rhiReflection.pushConstantWebGpuBindingGroup = reflection.pushConstantWebGpuBindingGroup;
    result.rhiReflection.pushConstantWebGpuBindingIndex = reflection.pushConstantWebGpuBindingIndex;
    result.rhiReflection.pushConstantMetalBufferIndex = reflection.pushConstantMetalBufferIndex;
    for (const auto& member : reflection.pushConstantMembers) {
        auto& converted = result.rhiReflection.pushConstantMembers.emplace_back();
        converted.name = member.name;
        converted.scalarType = member.scalarType;
        converted.scalarWidth = member.scalarWidth;
        converted.vecSize = member.vecSize;
        converted.matrixRows = member.matrixRows;
        converted.matrixCols = member.matrixCols;
        converted.matrixStride = member.matrixStride;
        converted.rowMajor = member.rowMajor;
        converted.arrayDims = member.arrayDims;
        converted.arrayStride = member.arrayStride;
        converted.offset = member.offset;
        converted.size = member.size;
        converted.decorations = member.decorations;
    }
    const auto pipelineLayout =
        rhi::buildPipelineLayoutDescFromShaderReflection(result.rhiReflection);
    if (!rhi::validatePipelineLayoutAgainstShaderReflection(pipelineLayout, result.rhiReflection)
            .success) {
        result.diagnostic = EditorSceneViewportShaderBindingDiagnostic::RhiLayoutInvalid;
        return result;
    }
    return result;
}

const char* editorSceneViewportShaderBindingDiagnosticCode(
    EditorSceneViewportShaderBindingDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportShaderBindingDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-OK";
    case EditorSceneViewportShaderBindingDiagnostic::ReflectionContractUnsupported:
        return "ARP-EDITOR-VIEWPORT-SHADER-REFLECTION-UNSUPPORTED";
    case EditorSceneViewportShaderBindingDiagnostic::MissingBinding:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-MISSING";
    case EditorSceneViewportShaderBindingDiagnostic::DuplicateBinding:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-DUPLICATE";
    case EditorSceneViewportShaderBindingDiagnostic::BindingTypeMismatch:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-TYPE-MISMATCH";
    case EditorSceneViewportShaderBindingDiagnostic::ArrayCountMismatch:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-ARRAY-COUNT";
    case EditorSceneViewportShaderBindingDiagnostic::StageMaskMismatch:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-STAGE-MASK";
    case EditorSceneViewportShaderBindingDiagnostic::DescriptorAddressCollision:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-ADDRESS-COLLISION";
    case EditorSceneViewportShaderBindingDiagnostic::UnexpectedBinding:
        return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-UNDECLARED";
    case EditorSceneViewportShaderBindingDiagnostic::RhiLayoutInvalid:
        return "ARP-EDITOR-VIEWPORT-RHI-LAYOUT-INVALID";
    }
    return "ARP-EDITOR-VIEWPORT-SHADER-BINDING-DIAGNOSTIC-UNKNOWN";
}

} // namespace arp::editor
