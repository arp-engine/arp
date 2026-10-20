#include "editor/viewport/editor_scene_viewport_texture_source.h"

#include "assets/image_decoder.h"
#include "resource/resource_manager.h"

#include <cmath>

namespace arp::editor {
namespace {

EditorViewportTextureSourceResult reject(
    const EditorViewportTextureSourceDiagnostic diagnostic, const char* detail)
{
    EditorViewportTextureSourceResult result;
    result.diagnostic = diagnostic;
    result.detail = detail;
    return result;
}

bool supportedMime(const core::pmr::String& mime)
{
    return mime == "image/png" || mime == "image/jpeg" || mime == "image/webp"
        || mime == "image/vnd.adobe.photoshop";
}

} // namespace

EditorViewportTextureSourceResult decodeEditorViewportTextureSource(
    resource::TextureResource& textureResource,
    const std::size_t textureTableIndex,
    const std::pmr::vector<assets::ImportedTextureAsset>& textureTable,
    const std::pmr::vector<assets::ImportedImageAsset>& imageTable,
    const core::pmr::Vector<core::pmr::String>& textureResourceIds,
    const std::size_t maxDecodedBytes)
{
    const auto& stableId = textureResource.reference().id;
    if (!resource::isResourceUuid(stableId)) {
        return reject(EditorViewportTextureSourceDiagnostic::MissingStableIdentity,
            "TextureResource must have a stable UUID before image binding");
    }
    if (textureTable.size() != textureResourceIds.size()) {
        return reject(EditorViewportTextureSourceDiagnostic::TextureTableSizeMismatch,
            "glTF texture table and stable TextureResource ID table differ in size");
    }
    if (textureTableIndex >= textureTable.size()) {
        return reject(EditorViewportTextureSourceDiagnostic::TextureIndexOutOfRange,
            "glTF texture-table index is out of range");
    }
    if (textureResourceIds[textureTableIndex] != stableId) {
        return reject(EditorViewportTextureSourceDiagnostic::InvalidTextureBinding,
            "glTF texture-table entry does not identify this TextureResource");
    }
    const auto imageIndex = textureTable[textureTableIndex].sourceImage;
    if (imageIndex == assets::invalidImportedImageIndex || imageIndex >= imageTable.size()) {
        return reject(EditorViewportTextureSourceDiagnostic::InvalidImageBinding,
            "glTF texture source image is missing or out of range");
    }
    if (maxDecodedBytes == 0u) {
        return reject(EditorViewportTextureSourceDiagnostic::DecodedByteBudgetMissing,
            "a non-zero decoded image byte budget is required");
    }
    if (textureResource.usage != "color" && textureResource.usage != "normal"
        && textureResource.usage != "data") {
        return reject(EditorViewportTextureSourceDiagnostic::UnsupportedUsage,
            "TextureResource usage must be color, normal, or data");
    }
    const bool expectedSrgb = textureResource.usage == "color";
    if (textureResource.srgb != expectedSrgb) {
        return reject(EditorViewportTextureSourceDiagnostic::ColorSpaceMismatch,
            "TextureResource usage and sRGB metadata are inconsistent");
    }

    const auto& image = imageTable[imageIndex];
    if (image.mimeType.empty()) {
        return reject(EditorViewportTextureSourceDiagnostic::MissingMimeType,
            "embedded image MIME type is required for deterministic decode");
    }
    if (!supportedMime(image.mimeType)) {
        return reject(EditorViewportTextureSourceDiagnostic::UnsupportedMimeType,
            "embedded image MIME type is not supported by the shared decoder");
    }
    if (image.alphaIsTransparency
        && (!std::isfinite(image.alphaTransparencyReference)
            || image.alphaTransparencyReference < 0.0f
            || image.alphaTransparencyReference > 1.0f)) {
        return reject(EditorViewportTextureSourceDiagnostic::InvalidAlphaMetadata,
            "alpha transparency reference must be in the interval [0,1]");
    }

    core::pmr::Vector<std::uint8_t> encodedBytes{&core::defaultCpuMemoryResource()};
    encodedBytes.assign(image.encodedBytes.begin(), image.encodedBytes.end());
    core::pmr::String mimeType(image.mimeType, &core::defaultCpuMemoryResource());
    const auto decoded = assets::decodeImageToRgba8(
        core::ArrayView<const std::uint8_t>(encodedBytes.data(), encodedBytes.size()),
        mimeType,
        textureResource.srgb,
        maxDecodedBytes);
    if (!decoded.success) {
        return reject(
            EditorViewportTextureSourceDiagnostic::ImageDecodeFailed, decoded.diagnostics.c_str());
    }
    if (image.alphaIsTransparency && !decoded.image.alpha) {
        return reject(EditorViewportTextureSourceDiagnostic::InvalidAlphaMetadata,
            "image declares alpha transparency but the encoded image has no alpha channel");
    }
    if (textureResource.alpha != decoded.image.alpha) {
        return reject(EditorViewportTextureSourceDiagnostic::AlphaMetadataMismatch,
            "TextureResource alpha metadata does not match decoded source channels");
    }

    textureResource.decodedWidth = decoded.image.width;
    textureResource.decodedHeight = decoded.image.height;
    textureResource.decodedHasAlpha = decoded.image.alpha;
    textureResource.decodedSrgb = decoded.image.srgb;
    textureResource.decodedRgba8.assign(decoded.image.rgba8.begin(), decoded.image.rgba8.end());
    EditorViewportTextureSourceResult result;
    result.success = true;
    return result;
}

const char* editorViewportTextureSourceDiagnosticCode(
    const EditorViewportTextureSourceDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorViewportTextureSourceDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-SOURCE-OK";
    case EditorViewportTextureSourceDiagnostic::MissingStableIdentity:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-ID-MISSING";
    case EditorViewportTextureSourceDiagnostic::TextureTableSizeMismatch:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-TABLE-MISMATCH";
    case EditorViewportTextureSourceDiagnostic::TextureIndexOutOfRange:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-INDEX-INVALID";
    case EditorViewportTextureSourceDiagnostic::InvalidTextureBinding:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-BINDING-MISMATCH";
    case EditorViewportTextureSourceDiagnostic::InvalidImageBinding:
        return "ARP-EDITOR-VIEWPORT-IMAGE-BINDING-INVALID";
    case EditorViewportTextureSourceDiagnostic::MissingMimeType:
        return "ARP-EDITOR-VIEWPORT-IMAGE-MIME-MISSING";
    case EditorViewportTextureSourceDiagnostic::UnsupportedMimeType:
        return "ARP-EDITOR-VIEWPORT-IMAGE-MIME-UNSUPPORTED";
    case EditorViewportTextureSourceDiagnostic::UnsupportedUsage:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-USAGE-UNSUPPORTED";
    case EditorViewportTextureSourceDiagnostic::ColorSpaceMismatch:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-COLORSPACE-MISMATCH";
    case EditorViewportTextureSourceDiagnostic::AlphaMetadataMismatch:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-ALPHA-MISMATCH";
    case EditorViewportTextureSourceDiagnostic::InvalidAlphaMetadata:
        return "ARP-EDITOR-VIEWPORT-TEXTURE-ALPHA-METADATA-INVALID";
    case EditorViewportTextureSourceDiagnostic::DecodedByteBudgetMissing:
        return "ARP-EDITOR-VIEWPORT-IMAGE-BUDGET-MISSING";
    case EditorViewportTextureSourceDiagnostic::ImageDecodeFailed:
        return "ARP-EDITOR-VIEWPORT-IMAGE-DECODE-FAILED";
    }
    return "ARP-EDITOR-VIEWPORT-TEXTURE-SOURCE-UNKNOWN";
}

} // namespace arp::editor
