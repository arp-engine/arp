#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"
#include "core/cpu_allocator.h"
#include "resource/prefab_asset_resources.h"

#include <cstdint>
#include <string_view>

namespace arp::editor {

enum class EditorGltfMaterialMappingDiagnostic : std::uint8_t {
    None,
    MissingIdentity,
    InvalidShaderInterface,
    TextureTableSizeMismatch,
    InvalidTextureReference,
    InvalidTextureResourceId,
    UnsupportedTextureSampler,
    UnsupportedTextureCoordinateSet,
    MaterialMetadataIncomplete,
    UnsupportedMaterialSemantics,
    InvalidMaterialParameters,
};

struct EditorGltfMaterialMappingResult {
    bool success = false;
    EditorGltfMaterialMappingDiagnostic diagnostic = EditorGltfMaterialMappingDiagnostic::None;
    resource::MaterialResource material;
};

// The resource ID vector is index-aligned with the imported glTF texture table.
ARP_API EditorGltfMaterialMappingResult mapGltfPbrMaterial(
    const assets::ImportedMaterialAsset& imported,
    const std::pmr::vector<assets::ImportedTextureAsset>& textures,
    const core::pmr::Vector<core::pmr::String>& textureResourceIds,
    std::string_view sourceResourceId,
    std::string_view shaderPackageId,
    const resource::ShaderPackageResource& shader);

ARP_API const char* editorGltfMaterialMappingDiagnosticCode(
    EditorGltfMaterialMappingDiagnostic diagnostic) noexcept;

} // namespace arp::editor
