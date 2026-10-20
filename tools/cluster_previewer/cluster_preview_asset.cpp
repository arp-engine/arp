#include "tools/cluster_previewer/cluster_preview_asset.h"

#include "assets/glb_static_mesh_importer.h"
#include "assets/image_decoder.h"

#include <algorithm>

namespace arp::tools::cluster_previewer {

std::filesystem::path findProjectRootFromCurrentPath()
{
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "artifacts")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path defaultDownloadedPreviewMeshPath()
{
    return findProjectRootFromCurrentPath() / "artifacts/project-lane/assets/khronos/Duck/Duck.glb";
}

assets::MeshAsset makeProceduralPreviewMesh()
{
    assets::MeshAsset mesh;
    constexpr uint32_t quadCount = 8;
    for (uint32_t vertex = 0; vertex <= quadCount; ++vertex) {
        const float x = static_cast<float>(vertex) - static_cast<float>(quadCount) * 0.5f;
        const float wave = vertex % 2u == 0 ? 0.0f : 0.35f;
        mesh.vertices.push_back({{x, -1.0f, wave}, {0.0f, 0.0f, 1.0f}, {x, 0.0f}});
        mesh.vertices.push_back({{x, 1.0f, -wave}, {0.0f, 0.0f, 1.0f}, {x, 1.0f}});
    }

    for (uint32_t quad = 0; quad < quadCount; ++quad) {
        const auto base = quad * 2u;
        mesh.indices.insert(mesh.indices.end(),
            {
                base,
                base + 1u,
                base + 2u,
                base + 1u,
                base + 3u,
                base + 2u,
            });
    }
    mesh.submeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

PreviewTextureAsset loadBaseColorPreviewTexture(
    const assets::StaticMeshImportResult& imported, const std::filesystem::path& path)
{
    PreviewTextureAsset texture;
    if (imported.mesh.submeshes.empty()) {
        texture.diagnostics = "GLB preview texture requires at least one submesh";
        return texture;
    }

    const auto materialSlot = imported.mesh.submeshes.front().materialSlot;
    if (materialSlot >= imported.materials.size()) {
        texture.diagnostics = "GLB preview texture material slot is out of range";
        return texture;
    }

    const auto textureIndex = imported.materials[materialSlot].baseColorTexture;
    if (textureIndex == assets::invalidImportedTextureIndex) {
        texture.diagnostics = "GLB preview material has no baseColorTexture";
        return texture;
    }
    if (textureIndex >= imported.textures.size()) {
        texture.diagnostics = "GLB preview baseColorTexture index is out of range";
        return texture;
    }

    const auto imageIndex = imported.textures[textureIndex].sourceImage;
    if (imageIndex == assets::invalidImportedImageIndex || imageIndex >= imported.images.size()) {
        texture.diagnostics = "GLB preview baseColorTexture source image is out of range";
        return texture;
    }

    const auto& image = imported.images[imageIndex];
    if (image.encodedBytes.empty()) {
        texture.diagnostics = "GLB preview baseColorTexture image is not embedded";
        return texture;
    }

    const auto decoded = assets::decodeImageToRgba8(image.encodedBytes, image.mimeType, true);
    if (!decoded.success) {
        texture.diagnostics = "GLB preview baseColorTexture decode failed: ";
        texture.diagnostics += decoded.diagnostics;
        return texture;
    }

    texture.valid = true;
    texture.width = decoded.image.width;
    texture.height = decoded.image.height;
    texture.srgb = decoded.image.srgb;
    texture.sourceLabel = path.filename().string();
    if (!image.name.empty()) {
        texture.sourceLabel += ":";
        texture.sourceLabel += image.name;
    } else if (!image.uri.empty()) {
        texture.sourceLabel += ":";
        texture.sourceLabel += image.uri;
    }
    texture.rgba8 = decoded.image.rgba8;
    return texture;
}

PreviewMeshLoadResult loadPreviewMesh(const std::filesystem::path& requestedPath)
{
    PreviewMeshLoadResult result;
    const auto path = requestedPath.empty() ? defaultDownloadedPreviewMeshPath() : requestedPath;

    if (!path.empty() && std::filesystem::exists(path)) {
        const auto imported = assets::importStaticMeshFromGlbFile(path);
        if (imported.success && imported.mesh.validForIndexedDraw()) {
            result.success = true;
            result.usedFallback = false;
            result.sourceLabel = path.filename().string();
            result.mesh = imported.mesh;
            result.baseColorTexture = loadBaseColorPreviewTexture(imported, path);
            return result;
        }
        result.diagnostics = "GLB import failed: " + imported.diagnostics;
    } else {
        result.diagnostics = "preview mesh path not found: " + path.string();
    }

    result.mesh = makeProceduralPreviewMesh();
    result.success = result.mesh.validForIndexedDraw();
    result.usedFallback = true;
    result.sourceLabel = "Procedural fallback strip";
    return result;
}

} // namespace arp::tools::cluster_previewer
