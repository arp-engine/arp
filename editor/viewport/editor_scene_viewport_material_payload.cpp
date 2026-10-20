#include "editor/viewport/editor_scene_viewport_material_payload.h"

#include "resource/prefab_asset_resources.h"

#include <algorithm>
#include <cmath>

namespace arp::editor {
namespace {

struct TextureSlotSource {
    EditorSceneViewportMaterialTextureSemantic semantic;
    const core::pmr::String* resourceId;
    resource::ResourceGeneration generation;
    EditorSceneViewportTextureColorSpace colorSpace;
};

bool finiteFactors(const EditorSceneViewportMaterialPayload& payload)
{
    const auto finite = [](float value) {
        return std::isfinite(value);
    };
    return std::all_of(payload.baseColorFactor.begin(), payload.baseColorFactor.end(), finite)
        && std::all_of(payload.emissiveFactor.begin(), payload.emissiveFactor.end(), finite)
        && finite(payload.metallicFactor) && finite(payload.roughnessFactor)
        && finite(payload.alphaCutoff);
}

EditorSceneViewportMaterialPayloadResult failure(
    EditorSceneViewportMaterialPayloadDiagnostic diagnostic, const char* detail)
{
    EditorSceneViewportMaterialPayloadResult result;
    result.diagnostic = diagnostic;
    result.detail = detail;
    return result;
}

} // namespace

bool EditorSceneViewportMaterialPayload::valid() const noexcept
{
    if (materialResourceId.empty() || materialGeneration == 0u || sourceResourceId.empty()
        || sourceGeneration == 0u || shaderPackageId.empty() || shaderGeneration == 0u
        || alphaMode.empty() || !shaderBindings.valid() || !finiteFactors(*this))
        return false;

    for (const auto& binding : textures) {
        const bool hasIdentity = !binding.resourceId.empty();
        if (hasIdentity != (binding.generation != 0u) || hasIdentity != (binding.view != nullptr)
            || hasIdentity != (binding.sampler != nullptr))
            return false;
    }
    return true;
}

EditorSceneViewportDescriptorReadinessResult validateEditorSceneViewportDescriptorReadiness(
    const EditorSceneViewportMaterialPayload& payload) noexcept
{
    EditorSceneViewportDescriptorReadinessResult result;
    if (!payload.valid())
        return result;

    for (std::size_t index = 0u; index < payload.textures.size(); ++index) {
        if (!payload.textures[index].present()) {
            result.readiness = EditorSceneViewportDescriptorReadiness::MissingTexture;
            result.failedSlot = index;
            return result;
        }
    }
    result.readiness = EditorSceneViewportDescriptorReadiness::Ready;
    return result;
}

EditorSceneViewportDescriptorWritesResult buildEditorSceneViewportDescriptorWrites(
    const EditorSceneViewportMaterialPayload& payload)
{
    EditorSceneViewportDescriptorWritesResult result;
    if (!validateEditorSceneViewportDescriptorReadiness(payload).ready()) {
        result.diagnostic = EditorSceneViewportDescriptorWritesDiagnostic::MaterialNotReady;
        return result;
    }

    const auto* sharedSampler = payload.textures.front().sampler;
    for (std::size_t index = 1u; index < payload.textures.size(); ++index) {
        if (payload.textures[index].sampler != sharedSampler) {
            result.diagnostic = EditorSceneViewportDescriptorWritesDiagnostic::MaterialNotReady;
            return result;
        }
    }

    for (std::size_t index = 0u; index < payload.textures.size(); ++index) {
        const auto& binding = payload.shaderBindings.textures[index];
        if (!binding.valid()) {
            result.diagnostic = EditorSceneViewportDescriptorWritesDiagnostic::MaterialNotReady;
            result.writes.clear();
            return result;
        }
        auto& output = result.writes.emplace_back();
        output.set = binding.set;
        output.write.binding = binding.binding;
        output.write.type = rhi::DescriptorType::SampledTexture;
        output.write.resource = payload.textures[index].view->id();
        output.write.resourceKind = rhi::DescriptorResourceKind::TextureView;
        output.write.resourceState = rhi::ResourceState::ShaderRead;
    }

    const auto& samplerBinding = payload.shaderBindings.sampler;
    if (!samplerBinding.valid()) {
        result.diagnostic = EditorSceneViewportDescriptorWritesDiagnostic::MaterialNotReady;
        result.writes.clear();
        return result;
    }
    auto& sampler = result.writes.emplace_back();
    sampler.set = samplerBinding.set;
    sampler.write.binding = samplerBinding.binding;
    sampler.write.type = rhi::DescriptorType::Sampler;
    sampler.write.resource = sharedSampler->id();
    sampler.write.resourceKind = rhi::DescriptorResourceKind::Sampler;
    sampler.write.resourceState = rhi::ResourceState::ShaderRead;
    return result;
}

EditorSceneViewportMaterialPayloadResult prepareEditorSceneViewportMaterialPayload(
    const EditorSceneViewportResolvedItem& item,
    const shader::ShaderReflection& reflection,
    rhi::GpuDevice& device,
    const resource::ResourceManager& resources,
    EditorSceneViewportTextureCache& textureCache)
{
    if (item.diagnostic != EditorSceneViewportDiagnostic::None)
        return failure(EditorSceneViewportMaterialPayloadDiagnostic::InvalidResolvedItem,
            "material payload requires a successfully resolved viewport item");
    if (item.materialResourceId.empty() || item.materialGeneration == 0u
        || item.sourceResourceId.empty() || item.sourceGeneration == 0u
        || item.shaderPackageId.empty() || item.shaderGeneration == 0u
        || item.baseColorFactor.size() != 4u || item.emissiveFactor.size() != 3u
        || item.alphaMode.empty())
        return failure(EditorSceneViewportMaterialPayloadDiagnostic::InvalidMaterialMetadata,
            "resolved material metadata is incomplete");

    const auto shaderBindings = inspectEditorSceneViewportShaderBindings(reflection);
    if (!shaderBindings.valid()) {
        auto result =
            failure(EditorSceneViewportMaterialPayloadDiagnostic::ShaderBindingContractFailure,
                editorSceneViewportShaderBindingDiagnosticCode(shaderBindings.diagnostic));
        return result;
    }

    const auto material = resources.find<resource::MaterialResource>(item.materialResourceId);
    const auto source =
        resources.find<resource::ExternalAssetSourceResource>(item.sourceResourceId);
    const auto shader = resources.find<resource::ShaderPackageResource>(item.shaderPackageId);
    if (!material.ready() || material.generation() != item.materialGeneration || !source.ready()
        || source.generation() != item.sourceGeneration || !shader.ready()
        || shader.generation() != item.shaderGeneration)
        return failure(EditorSceneViewportMaterialPayloadDiagnostic::ResourceChanged,
            "material graph changed after viewport resource resolution");

    EditorSceneViewportMaterialPayloadResult result;
    auto& payload = result.payload;
    payload.materialResourceId = item.materialResourceId;
    payload.materialGeneration = item.materialGeneration;
    payload.sourceResourceId = item.sourceResourceId;
    payload.sourceGeneration = item.sourceGeneration;
    payload.shaderPackageId = item.shaderPackageId;
    payload.shaderGeneration = item.shaderGeneration;
    std::copy(
        item.baseColorFactor.begin(), item.baseColorFactor.end(), payload.baseColorFactor.begin());
    std::copy(
        item.emissiveFactor.begin(), item.emissiveFactor.end(), payload.emissiveFactor.begin());
    payload.metallicFactor = item.metallicFactor;
    payload.roughnessFactor = item.roughnessFactor;
    payload.alphaMode = item.alphaMode;
    payload.alphaCutoff = item.alphaCutoff;
    payload.doubleSided = item.doubleSided;
    payload.shaderBindings = shaderBindings.bindings;

    const std::array<TextureSlotSource, kEditorSceneViewportMaterialTextureSlotCount> sources{{
        {EditorSceneViewportMaterialTextureSemantic::BaseColor,
            &item.baseColorTextureId,
            item.baseColorTextureGeneration,
            EditorSceneViewportTextureColorSpace::Srgb},
        {EditorSceneViewportMaterialTextureSemantic::Normal,
            &item.normalTextureId,
            item.normalTextureGeneration,
            EditorSceneViewportTextureColorSpace::Linear},
        {EditorSceneViewportMaterialTextureSemantic::MetallicRoughness,
            &item.metallicRoughnessTextureId,
            item.metallicRoughnessTextureGeneration,
            EditorSceneViewportTextureColorSpace::Linear},
        {EditorSceneViewportMaterialTextureSemantic::Occlusion,
            &item.occlusionTextureId,
            item.occlusionTextureGeneration,
            EditorSceneViewportTextureColorSpace::Linear},
        {EditorSceneViewportMaterialTextureSemantic::Emissive,
            &item.emissiveTextureId,
            item.emissiveTextureGeneration,
            EditorSceneViewportTextureColorSpace::Srgb},
    }};

    for (std::size_t index = 0u; index < sources.size(); ++index) {
        const auto& source = sources[index];
        auto& binding = payload.textures[index];
        binding.semantic = source.semantic;
        binding.colorSpace = source.colorSpace;
        if (source.resourceId->empty()) {
            if (source.generation != 0u)
                return failure(
                    EditorSceneViewportMaterialPayloadDiagnostic::InvalidMaterialMetadata,
                    "absent material texture must not carry a resource generation");
            continue;
        }
        if (source.generation == 0u)
            return failure(EditorSceneViewportMaterialPayloadDiagnostic::InvalidMaterialMetadata,
                "material texture identity requires a resolved resource generation");

        auto cached = textureCache.acquire(
            device, resources, *source.resourceId, source.generation, source.colorSpace);
        if (!cached.valid()) {
            result.diagnostic = EditorSceneViewportMaterialPayloadDiagnostic::TextureCacheFailure;
            result.textureDiagnostic = cached.diagnostic;
            result.detail = cached.detail;
            return result;
        }
        binding.resourceId = *source.resourceId;
        binding.generation = source.generation;
        binding.view = cached.view;
        binding.sampler = cached.sampler;
    }

    if (!payload.valid())
        return failure(EditorSceneViewportMaterialPayloadDiagnostic::InvalidMaterialMetadata,
            "material payload contains non-finite values or incomplete bindings");
    return result;
}

const char* editorSceneViewportMaterialPayloadDiagnosticCode(
    EditorSceneViewportMaterialPayloadDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportMaterialPayloadDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-PAYLOAD-OK";
    case EditorSceneViewportMaterialPayloadDiagnostic::InvalidResolvedItem:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-PAYLOAD-ITEM-INVALID";
    case EditorSceneViewportMaterialPayloadDiagnostic::InvalidMaterialMetadata:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-PAYLOAD-METADATA-INVALID";
    case EditorSceneViewportMaterialPayloadDiagnostic::ShaderBindingContractFailure:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-PAYLOAD-SHADER-BINDING-INVALID";
    case EditorSceneViewportMaterialPayloadDiagnostic::ResourceChanged:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-PAYLOAD-RESOURCE-CHANGED";
    case EditorSceneViewportMaterialPayloadDiagnostic::TextureCacheFailure:
        return "ARP-EDITOR-VIEWPORT-MATERIAL-PAYLOAD-TEXTURE-FAILED";
    }
    return "ARP-EDITOR-VIEWPORT-MATERIAL-PAYLOAD-DIAGNOSTIC-UNKNOWN";
}

} // namespace arp::editor
