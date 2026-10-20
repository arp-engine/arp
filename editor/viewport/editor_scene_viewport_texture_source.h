#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"
#include "resource/prefab_asset_resources.h"

#include <cstddef>

namespace arp::editor {

enum class EditorViewportTextureSourceDiagnostic {
    None,
    MissingStableIdentity,
    TextureTableSizeMismatch,
    TextureIndexOutOfRange,
    InvalidTextureBinding,
    InvalidImageBinding,
    MissingMimeType,
    UnsupportedMimeType,
    UnsupportedUsage,
    ColorSpaceMismatch,
    AlphaMetadataMismatch,
    InvalidAlphaMetadata,
    DecodedByteBudgetMissing,
    ImageDecodeFailed,
};

struct EditorViewportTextureSourceResult {
    bool success = false;
    EditorViewportTextureSourceDiagnostic diagnostic = EditorViewportTextureSourceDiagnostic::None;
    core::pmr::String detail{&core::defaultCpuMemoryResource()};
};

// Resolve the authored table relations and attach bounded CPU pixels atomically.
ARP_API EditorViewportTextureSourceResult decodeEditorViewportTextureSource(
    resource::TextureResource& textureResource,
    std::size_t textureTableIndex,
    const std::pmr::vector<assets::ImportedTextureAsset>& textureTable,
    const std::pmr::vector<assets::ImportedImageAsset>& imageTable,
    const core::pmr::Vector<core::pmr::String>& textureResourceIds,
    std::size_t maxDecodedBytes);

ARP_API const char* editorViewportTextureSourceDiagnosticCode(
    EditorViewportTextureSourceDiagnostic diagnostic) noexcept;

} // namespace arp::editor
