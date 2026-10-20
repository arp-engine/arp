#include "editor/viewport/editor_gltf_material_mapping.h"

#include <cmath>
#include <string_view>
#include <utility>

namespace arp::editor {
namespace {

using Diagnostic = EditorGltfMaterialMappingDiagnostic;

bool unsupportedExtensionSemantics(const assets::ImportedMaterialAsset& material)
{
    return material.hasUnsupportedMaterialExtensions
        || material.clearcoatTexture != assets::invalidImportedTextureIndex
        || material.clearcoatRoughnessTexture != assets::invalidImportedTextureIndex
        || material.clearcoatNormalTexture != assets::invalidImportedTextureIndex
        || material.specularTexture != assets::invalidImportedTextureIndex
        || material.specularColorTexture != assets::invalidImportedTextureIndex
        || material.sheenColorTexture != assets::invalidImportedTextureIndex
        || material.sheenRoughnessTexture != assets::invalidImportedTextureIndex
        || material.transmissionTexture != assets::invalidImportedTextureIndex
        || material.thicknessTexture != assets::invalidImportedTextureIndex
        || material.iridescenceTexture != assets::invalidImportedTextureIndex
        || material.iridescenceThicknessTexture != assets::invalidImportedTextureIndex
        || material.occlusionStrength != 1.0f || material.clearcoatFactor != 0.0f
        || material.clearcoatRoughnessFactor != 0.0f || material.specularFactor != 1.0f
        || material.specularColorFactor[0] != 1.0f || material.specularColorFactor[1] != 1.0f
        || material.specularColorFactor[2] != 1.0f || material.sheenColorFactor[0] != 0.0f
        || material.sheenColorFactor[1] != 0.0f || material.sheenColorFactor[2] != 0.0f
        || material.sheenRoughnessFactor != 0.0f || material.transmissionFactor != 0.0f
        || material.thicknessFactor != 0.0f || material.attenuationColor[0] != 1.0f
        || material.attenuationColor[1] != 1.0f || material.attenuationColor[2] != 1.0f
        || material.attenuationDistance != 0.0f || material.ior != 1.5f
        || material.iridescenceFactor != 0.0f || material.iridescenceIor != 1.3f
        || material.authoredLeafSurface || material.leafTranslucency != 0.0f
        || material.leafDielectricF0 != 0.04f || material.leafTranslucentSaturation != 1.0f;
}

bool mapTextureReference(const std::uint32_t textureIndex,
    const std::uint32_t coordinateSet,
    const std::pmr::vector<assets::ImportedTextureAsset>& textures,
    const core::pmr::Vector<core::pmr::String>& textureResourceIds,
    std::string_view& out,
    Diagnostic& diagnostic)
{
    if (textureIndex == assets::invalidImportedTextureIndex) {
        if (coordinateSet != 0u) {
            diagnostic = Diagnostic::UnsupportedTextureCoordinateSet;
            return false;
        }
        return true;
    }
    if (textureIndex >= textures.size()) {
        diagnostic = Diagnostic::InvalidTextureReference;
        return false;
    }
    if (coordinateSet != 0u) {
        diagnostic = Diagnostic::UnsupportedTextureCoordinateSet;
        return false;
    }
    if (textures[textureIndex].sampler != assets::invalidImportedTextureIndex) {
        diagnostic = Diagnostic::UnsupportedTextureSampler;
        return false;
    }
    const auto& resourceId = textureResourceIds[textureIndex];
    if (resourceId.empty()) {
        diagnostic = Diagnostic::InvalidTextureResourceId;
        return false;
    }
    const std::string_view id(resourceId.data(), resourceId.size());
    if (!resource::isResourceUuid(id)) {
        diagnostic = Diagnostic::InvalidTextureResourceId;
        return false;
    }
    out = id;
    return true;
}

void assignString(core::pmr::String& destination, const std::string_view source)
{
    destination.assign(source.begin(), source.end());
}

} // namespace

EditorGltfMaterialMappingResult mapGltfPbrMaterial(const assets::ImportedMaterialAsset& imported,
    const std::pmr::vector<assets::ImportedTextureAsset>& textures,
    const core::pmr::Vector<core::pmr::String>& textureResourceIds,
    const std::string_view sourceResourceId,
    const std::string_view shaderPackageId,
    const resource::ShaderPackageResource& shader)
{
    EditorGltfMaterialMappingResult result;
    if (sourceResourceId.empty() || shaderPackageId.empty()
        || !resource::isResourceUuid(sourceResourceId)
        || !resource::isResourceUuid(shaderPackageId)) {
        result.diagnostic = Diagnostic::MissingIdentity;
        return result;
    }
    if (shader.materialInterfaceContract != resource::kPbrMetallicRoughnessContract
        || shader.materialInterfaceVersion != resource::kPbrMetallicRoughnessContractVersion) {
        result.diagnostic = Diagnostic::InvalidShaderInterface;
        return result;
    }
    if (textureResourceIds.size() != textures.size()) {
        result.diagnostic = Diagnostic::TextureTableSizeMismatch;
        return result;
    }
    if (!imported.hasCompleteMaterialImportMetadata) {
        result.diagnostic = Diagnostic::MaterialMetadataIncomplete;
        return result;
    }
    if (unsupportedExtensionSemantics(imported)) {
        result.diagnostic = Diagnostic::UnsupportedMaterialSemantics;
        return result;
    }

    std::string_view mappedTextures[5]{};
    const std::uint32_t textureIndices[5] = {
        imported.baseColorTexture,
        imported.normalTexture,
        imported.metallicRoughnessTexture,
        imported.occlusionTexture,
        imported.emissiveTexture,
    };
    const std::uint32_t coordinateSets[5] = {
        imported.baseColorTextureCoordinateSet,
        imported.normalTextureCoordinateSet,
        imported.metallicRoughnessTextureCoordinateSet,
        imported.occlusionTextureCoordinateSet,
        imported.emissiveTextureCoordinateSet,
    };
    for (std::size_t index = 0; index < 5u; ++index) {
        if (!mapTextureReference(textureIndices[index],
                coordinateSets[index],
                textures,
                textureResourceIds,
                mappedTextures[index],
                result.diagnostic)) {
            return result;
        }
    }

    resource::MaterialResource material;
    material.materialContractVersion = resource::kPbrMetallicRoughnessContractVersion;
    material.surfaceModel = resource::kPbrMetallicRoughnessContract;
    material.sourceResourceId.assign(sourceResourceId.begin(), sourceResourceId.end());
    material.shaderPackageId.assign(shaderPackageId.begin(), shaderPackageId.end());
    material.name.assign(imported.name.begin(), imported.name.end());
    material.baseColorR = imported.baseColorFactor[0];
    material.baseColorG = imported.baseColorFactor[1];
    material.baseColorB = imported.baseColorFactor[2];
    material.baseColorA = imported.baseColorFactor[3];
    material.emissiveR = imported.emissiveFactor[0];
    material.emissiveG = imported.emissiveFactor[1];
    material.emissiveB = imported.emissiveFactor[2];
    material.metallicFactor = imported.metallicFactor;
    material.roughnessFactor = imported.roughnessFactor;
    switch (imported.alphaMode) {
    case assets::ImportedAlphaMode::Opaque:
        material.alphaMode = "OPAQUE";
        break;
    case assets::ImportedAlphaMode::Mask:
        material.alphaMode = "MASK";
        break;
    case assets::ImportedAlphaMode::Blend:
        material.alphaMode = "BLEND";
        break;
    default:
        result.diagnostic = Diagnostic::UnsupportedMaterialSemantics;
        return result;
    }
    material.alphaCutoff = imported.alphaCutoff;
    material.doubleSided = imported.doubleSided;
    assignString(material.baseColorTextureId, mappedTextures[0]);
    assignString(material.normalTextureId, mappedTextures[1]);
    assignString(material.metallicRoughnessTextureId, mappedTextures[2]);
    assignString(material.occlusionTextureId, mappedTextures[3]);
    assignString(material.emissiveTextureId, mappedTextures[4]);

    const auto validation = resource::validateMaterialContract(material, shader);
    if (!validation.valid()) {
        result.diagnostic =
            validation.diagnostic == resource::MaterialContractDiagnostic::ShaderInterfaceMismatch
            ? Diagnostic::InvalidShaderInterface
            : Diagnostic::InvalidMaterialParameters;
        return result;
    }
    result.material = std::move(material);
    result.success = true;
    return result;
}

const char* editorGltfMaterialMappingDiagnosticCode(const Diagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case Diagnostic::None:
        return "ARP-EDITOR-GLTF-MATERIAL-OK";
    case Diagnostic::MissingIdentity:
        return "ARP-EDITOR-GLTF-MATERIAL-IDENTITY-MISSING";
    case Diagnostic::InvalidShaderInterface:
        return "ARP-EDITOR-GLTF-MATERIAL-SHADER-INTERFACE";
    case Diagnostic::TextureTableSizeMismatch:
        return "ARP-EDITOR-GLTF-MATERIAL-TEXTURE-TABLE-MISMATCH";
    case Diagnostic::InvalidTextureReference:
        return "ARP-EDITOR-GLTF-MATERIAL-TEXTURE-REFERENCE";
    case Diagnostic::InvalidTextureResourceId:
        return "ARP-EDITOR-GLTF-MATERIAL-TEXTURE-ID";
    case Diagnostic::UnsupportedTextureSampler:
        return "ARP-EDITOR-GLTF-MATERIAL-SAMPLER-UNSUPPORTED";
    case Diagnostic::UnsupportedTextureCoordinateSet:
        return "ARP-EDITOR-GLTF-MATERIAL-TEXCOORD-UNSUPPORTED";
    case Diagnostic::MaterialMetadataIncomplete:
        return "ARP-EDITOR-GLTF-MATERIAL-METADATA-INCOMPLETE";
    case Diagnostic::UnsupportedMaterialSemantics:
        return "ARP-EDITOR-GLTF-MATERIAL-SEMANTICS-UNSUPPORTED";
    case Diagnostic::InvalidMaterialParameters:
        return "ARP-EDITOR-GLTF-MATERIAL-PARAMETER-INVALID";
    }
    return "ARP-EDITOR-GLTF-MATERIAL-UNKNOWN";
}

} // namespace arp::editor
