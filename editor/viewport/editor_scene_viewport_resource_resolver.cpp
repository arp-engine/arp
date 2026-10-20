#include "editor/viewport/editor_scene_viewport_resource_resolver.h"

#include "resource/mesh_asset_resources.h"
#include "resource/prefab_asset_resources.h"

namespace arp::editor {
namespace {

template <typename T>
EditorSceneViewportDiagnostic resourceFailure(const resource::ResourceManager& resources,
    const core::pmr::String& id,
    EditorSceneViewportDiagnostic missing,
    EditorSceneViewportDiagnostic notReady,
    EditorSceneViewportDiagnostic wrongType)
{
    const auto handle = resources.find<T>(id);
    if (handle.ready())
        return EditorSceneViewportDiagnostic::None;
    if (handle.state() == resource::ResourceState::Unloaded)
        return missing;
    if (handle.state() != resource::ResourceState::Ready)
        return notReady;
    return wrongType;
}

} // namespace

bool EditorSceneViewportResourceResolution::valid() const noexcept
{
    if (frameDiagnostic != EditorSceneViewportDiagnostic::None)
        return false;
    for (const auto& item : items) {
        if (item.diagnostic != EditorSceneViewportDiagnostic::None)
            return false;
    }
    return true;
}

EditorSceneViewportResourceResolution resolveEditorSceneViewportResources(
    const EditorSceneViewportFrame& frame, const resource::ResourceManager& resources)
{
    EditorSceneViewportResourceResolution result;
    const auto validation = validateEditorSceneViewportFrame(frame);
    if (!validation.valid()) {
        result.frameDiagnostic = validation.diagnostic;
        return result;
    }

    result.items.reserve(frame.drawItems.size());
    for (const auto& draw : frame.drawItems) {
        EditorSceneViewportResolvedItem item;
        item.meshResourceId = draw.meshResourceId;
        item.materialResourceId = draw.materialResourceId;
        const auto mesh = resources.find<resource::MeshAssetResource>(draw.meshResourceId);
        if (!mesh.ready()) {
            item.diagnostic = resourceFailure<resource::MeshAssetResource>(resources,
                draw.meshResourceId,
                EditorSceneViewportDiagnostic::MissingMeshResource,
                EditorSceneViewportDiagnostic::MeshResourceNotReady,
                EditorSceneViewportDiagnostic::MeshResourceWrongType);
            result.items.push_back(std::move(item));
            continue;
        }
        const auto* asset = mesh.get()->mesh();
        if (asset == nullptr || !asset->validForIndexedDraw()) {
            item.diagnostic = EditorSceneViewportDiagnostic::InvalidMeshData;
            result.items.push_back(std::move(item));
            continue;
        }
        if (!mesh.get()->immutableMeshGeometry()) {
            item.diagnostic = EditorSceneViewportDiagnostic::MutableMeshGeometryUnsupported;
            result.items.push_back(std::move(item));
            continue;
        }
        item.meshGeneration = mesh.generation();
        item.vertexCount = asset->vertices.size();
        item.indexCount = asset->indices.size();
        item.submeshCount = asset->submeshes.size();

        const auto material = resources.find<resource::MaterialResource>(draw.materialResourceId);
        if (!material.ready()) {
            item.diagnostic = resourceFailure<resource::MaterialResource>(resources,
                draw.materialResourceId,
                EditorSceneViewportDiagnostic::MissingMaterialResource,
                EditorSceneViewportDiagnostic::MaterialResourceNotReady,
                EditorSceneViewportDiagnostic::MaterialResourceWrongType);
            result.items.push_back(std::move(item));
            continue;
        }
        item.materialGeneration = material.generation();
        const auto* materialData = material.get();
        if (materialData->sourceResourceId.empty()) {
            item.diagnostic = EditorSceneViewportDiagnostic::MaterialSourceMissing;
            result.items.push_back(std::move(item));
            continue;
        }
        const auto source =
            resources.find<resource::ExternalAssetSourceResource>(materialData->sourceResourceId);
        if (!source.ready()) {
            item.diagnostic = resourceFailure<resource::ExternalAssetSourceResource>(resources,
                materialData->sourceResourceId,
                EditorSceneViewportDiagnostic::MaterialSourceMissing,
                EditorSceneViewportDiagnostic::MaterialSourceNotReady,
                EditorSceneViewportDiagnostic::MaterialSourceWrongType);
            result.items.push_back(std::move(item));
            continue;
        }
        item.sourceResourceId = materialData->sourceResourceId;
        item.sourceGeneration = source.generation();

        if (materialData->shaderPackageId.empty()) {
            item.diagnostic = EditorSceneViewportDiagnostic::MaterialShaderPackageMissing;
            result.items.push_back(std::move(item));
            continue;
        }
        const auto shader =
            resources.find<resource::ShaderPackageResource>(materialData->shaderPackageId);
        if (!shader.ready()) {
            item.diagnostic = resourceFailure<resource::ShaderPackageResource>(resources,
                materialData->shaderPackageId,
                EditorSceneViewportDiagnostic::MaterialShaderPackageMissing,
                EditorSceneViewportDiagnostic::MaterialShaderPackageNotReady,
                EditorSceneViewportDiagnostic::MaterialShaderPackageWrongType);
            result.items.push_back(std::move(item));
            continue;
        }
        const auto* shaderData = shader.get();
        if (shaderData->slangPath.empty() || shaderData->spirvPath.empty()
            || shaderData->mslPath.empty() || shaderData->reflectionPath.empty()) {
            item.diagnostic = EditorSceneViewportDiagnostic::ShaderArtifactMetadataMissing;
            result.items.push_back(std::move(item));
            continue;
        }
        const auto materialContract =
            resource::validateMaterialContract(*materialData, *shaderData);
        if (!materialContract.valid()) {
            switch (materialContract.diagnostic) {
            case resource::MaterialContractDiagnostic::UnsupportedVersion:
            case resource::MaterialContractDiagnostic::UnsupportedSurfaceModel:
                item.diagnostic = EditorSceneViewportDiagnostic::MaterialContractUnsupported;
                break;
            case resource::MaterialContractDiagnostic::InvalidParameter:
                item.diagnostic = EditorSceneViewportDiagnostic::MaterialContractInvalid;
                break;
            case resource::MaterialContractDiagnostic::ShaderInterfaceMismatch:
                item.diagnostic = EditorSceneViewportDiagnostic::MaterialShaderInterfaceMismatch;
                break;
            case resource::MaterialContractDiagnostic::None:
                break;
            }
            result.items.push_back(std::move(item));
            continue;
        }

        struct TextureBinding {
            const core::pmr::String* resourceId;
            resource::ResourceGeneration* generation;
        } textureBindings[] = {
            {&materialData->baseColorTextureId, &item.baseColorTextureGeneration},
            {&materialData->normalTextureId, &item.normalTextureGeneration},
            {&materialData->metallicRoughnessTextureId, &item.metallicRoughnessTextureGeneration},
            {&materialData->occlusionTextureId, &item.occlusionTextureGeneration},
            {&materialData->emissiveTextureId, &item.emissiveTextureGeneration},
        };

        bool textureFailure = false;
        for (const auto& binding : textureBindings) {
            if (binding.resourceId->empty())
                continue;
            const auto texture = resources.find<resource::TextureResource>(*binding.resourceId);
            if (!texture.ready()) {
                item.diagnostic = resourceFailure<resource::TextureResource>(resources,
                    *binding.resourceId,
                    EditorSceneViewportDiagnostic::MaterialTextureNotReady,
                    EditorSceneViewportDiagnostic::MaterialTextureNotReady,
                    EditorSceneViewportDiagnostic::MaterialTextureWrongType);
                textureFailure = true;
                break;
            }
            *binding.generation = texture.generation();
        }
        if (textureFailure) {
            result.items.push_back(std::move(item));
            continue;
        }

        item.shaderPackageId = materialData->shaderPackageId;
        item.shaderGeneration = shader.generation();
        item.baseColorTextureId = materialData->baseColorTextureId;
        item.normalTextureId = materialData->normalTextureId;
        item.metallicRoughnessTextureId = materialData->metallicRoughnessTextureId;
        item.occlusionTextureId = materialData->occlusionTextureId;
        item.emissiveTextureId = materialData->emissiveTextureId;
        item.baseColorFactor = {
            materialData->baseColorR,
            materialData->baseColorG,
            materialData->baseColorB,
            materialData->baseColorA,
        };
        item.emissiveFactor = {
            materialData->emissiveR,
            materialData->emissiveG,
            materialData->emissiveB,
        };
        item.metallicFactor = materialData->metallicFactor;
        item.roughnessFactor = materialData->roughnessFactor;
        item.alphaMode = materialData->alphaMode;
        item.alphaCutoff = materialData->alphaCutoff;
        item.doubleSided = materialData->doubleSided;
        result.items.push_back(std::move(item));
    }
    return result;
}

} // namespace arp::editor
