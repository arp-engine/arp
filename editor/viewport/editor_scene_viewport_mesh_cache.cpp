#include "editor/viewport/editor_scene_viewport_mesh_cache.h"

#include "3d/terrain/terrain_streamer.h"
#include "editor/workspace/editor_terrain_preview.h"
#include "resource/mesh_asset_resources.h"
#include "resource/terrain_asset_resource.h"

#include <limits>

namespace arp::editor {
namespace {

EditorSceneViewportMeshCacheResult failure(
    EditorSceneViewportMeshCacheDiagnostic diagnostic, const char* detail)
{
    EditorSceneViewportMeshCacheResult result;
    result.diagnostic = diagnostic;
    result.detail = detail;
    return result;
}

bool geometryByteSize(const assets::MeshAsset& mesh, std::uint64_t& result)
{
    constexpr auto vertexStride = sizeof(assets::MeshVertex);
    constexpr auto indexStride = sizeof(std::uint32_t);
    if (mesh.vertices.size() > std::numeric_limits<std::uint64_t>::max() / vertexStride
        || mesh.indices.size() > std::numeric_limits<std::uint64_t>::max() / indexStride) {
        return false;
    }
    const auto vertexBytes = static_cast<std::uint64_t>(mesh.vertices.size()) * vertexStride;
    const auto indexBytes = static_cast<std::uint64_t>(mesh.indices.size()) * indexStride;
    if (indexBytes > std::numeric_limits<std::uint64_t>::max() - vertexBytes)
        return false;
    result = vertexBytes + indexBytes;
    return true;
}

} // namespace

EditorSceneViewportMeshCache::EditorSceneViewportMeshCache(
    EditorSceneViewportMeshCacheBudget budget)
    : budget_(budget)
{}

EditorSceneViewportMeshCacheResult EditorSceneViewportMeshCache::acquire(rhi::GpuDevice& device,
    const resource::ResourceManager& resources,
    const EditorSceneViewportResolvedItem& item)
{
    if (budget_.maxResidentMeshes == 0 || budget_.maxResidentGeometryBytes == 0)
        return failure(EditorSceneViewportMeshCacheDiagnostic::InvalidBudget,
            "mesh cache requires explicit non-zero resident limits");
    if (item.diagnostic != EditorSceneViewportDiagnostic::None || item.meshResourceId.empty()
        || item.meshGeneration == 0) {
        return failure(EditorSceneViewportMeshCacheDiagnostic::InvalidResolvedItem,
            "resolved mesh identity or generation is invalid");
    }
    if ((ownerDevice_ != nullptr && ownerDevice_ != &device)
        || (ownerResources_ != nullptr && ownerResources_ != &resources)) {
        return failure(EditorSceneViewportMeshCacheDiagnostic::OwnerMismatch,
            "mesh cache cannot be reused across device or resource-manager owners");
    }

    // Admit from checked counts before constructing any terrain geometry.
    constexpr auto vertexStride = sizeof(assets::MeshVertex);
    if (item.vertexCount > std::numeric_limits<std::uint64_t>::max() / vertexStride
        || item.indexCount > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t))
        return failure(
            EditorSceneViewportMeshCacheDiagnostic::InvalidMeshData, "geometry size overflow");
    const auto vertexBytes = static_cast<std::uint64_t>(item.vertexCount) * vertexStride;
    const auto indexBytes = static_cast<std::uint64_t>(item.indexCount) * sizeof(std::uint32_t);
    if (indexBytes > std::numeric_limits<std::uint64_t>::max() - vertexBytes)
        return failure(
            EditorSceneViewportMeshCacheDiagnostic::InvalidMeshData, "geometry size overflow");
    const auto geometryBytes = vertexBytes + indexBytes;
    const assets::MeshAsset* geometry = nullptr;
    assets::MeshAsset terrainMesh;
    resource::ResourceHandle<resource::TerrainAssetResource> terrainHandle;
    if (item.geometry == EditorSceneViewportGeometry::TerrainTile) {
        terrainHandle = resources.find<resource::TerrainAssetResource>(item.meshResourceId);
        resource::TerrainAssetTileReadView view;
        if (!terrainHandle.ready() || terrainHandle.generation() != item.meshGeneration
            || !terrainHandle.get()->tileReadView(item.tile, view) || !view.geometryValid()
            || ((terrainHandle.get()->hasSurfaceWeights() || terrainHandle.get()->hasVegetation())
                && (item.terrainPreviewMaterialResourceId.empty()
                    || resolveEditorTerrainPreviewMaterial(resources,
                           item.meshResourceId,
                           terrainHandle.generation(),
                           item.terrainPreviewMaterialResourceId,
                           item.tile)
                        != item.materialResourceId)))
            return failure(EditorSceneViewportMeshCacheDiagnostic::ResourceChanged,
                "terrain generation or tile changed");
        const auto samples = static_cast<std::uint64_t>(view.samplesPerEdge);
        const auto cells = samples - 1u;
        if (samples * samples > UINT32_MAX
            || cells * cells > std::numeric_limits<std::uint64_t>::max() / 6u
            || item.vertexCount != samples * samples || item.indexCount != cells * cells * 6u
            || item.submeshCount != 1u)
            return failure(EditorSceneViewportMeshCacheDiagnostic::ResolvedMetadataMismatch,
                "terrain tile counts changed");
    } else {
        const auto handle = resources.find<resource::MeshAssetResource>(item.meshResourceId);
        if (!handle.ready() || handle.generation() != item.meshGeneration)
            return failure(EditorSceneViewportMeshCacheDiagnostic::ResourceChanged,
                "mesh resource is no longer ready at the resolved generation");
        const auto* resource = handle.get();
        if (!resource->immutableMeshGeometry())
            return failure(EditorSceneViewportMeshCacheDiagnostic::MutableGeometryUnsupported,
                "mutable mesh geometry cannot enter the viewport cache");
        geometry = resource->mesh();
    }
    const auto previewGeneration = item.terrainPreviewMaterialResourceId.empty()
        ? 0
        : resources.find<resource::MaterialResource>(item.terrainPreviewMaterialResourceId)
              .generation();
    for (const auto& entry : entries_) {
        if (entry->resourceId == item.meshResourceId && entry->generation == item.meshGeneration
            && entry->previewId == item.terrainPreviewMaterialResourceId
            && entry->previewGeneration == previewGeneration && entry->geometry == item.geometry
            && entry->tile == item.tile) {
            EditorSceneViewportMeshCacheResult result;
            result.mesh = &entry->mesh;
            result.reused = true;
            return result;
        }
    }
    if (entries_.size() >= budget_.maxResidentMeshes
        || geometryBytes > budget_.maxResidentGeometryBytes - residentGeometryBytes_)
        return failure(EditorSceneViewportMeshCacheDiagnostic::BudgetExceeded,
            "viewport mesh cache resident budget is exhausted");
    if (item.geometry == EditorSceneViewportGeometry::TerrainTile) {
        const auto* asset = terrainHandle.get();
        const auto surface = asset->snapshotSurface();
        terrain::TerrainStreamerDesc desc;
        desc.worldBounds = asset->bounds();
        desc.chunkSize = asset->tileWorldSize();
        desc.samplesPerChunk = asset->samplesPerEdge();
        desc.baseHeight = 0.0f;
        desc.heightSampler = [surface](float x, float z) {
            const auto query = surface->sample(x, z);
            return query.ready() ? query.sample.height : std::numeric_limits<float>::quiet_NaN();
        };
        terrain::TerrainStreamer builder;
        if (!builder.initialize(desc))
            return failure(EditorSceneViewportMeshCacheDiagnostic::InvalidMeshData,
                "terrain mesh grid rejected");
        terrainMesh = builder.generateChunkMesh({item.tile.x, item.tile.z}, 0, 0u, 0u);
        if (!item.terrainPreviewMaterialResourceId.empty()) {
            const auto root =
                resources.find<resource::MaterialResource>(item.terrainPreviewMaterialResourceId);
            const auto* preview = root.ready()
                ? dynamic_cast<const EditorTerrainPreviewMaterial*>(root.get())
                : nullptr;
            if (!preview || preview->resolution < 2)
                return failure(EditorSceneViewportMeshCacheDiagnostic::ResourceChanged,
                    "preview material changed");
            const float scale = float(preview->resolution - 1) / preview->resolution;
            const float offset = 0.5f / preview->resolution;
            for (auto& vertex : terrainMesh.vertices) {
                vertex.uv[0] = vertex.uv[0] * scale + offset;
                vertex.uv[1] = vertex.uv[1] * scale + offset;
            }
        }
        geometry = &terrainMesh;
    }
    if (!geometry || !geometry->validForIndexedDraw())
        return failure(
            EditorSceneViewportMeshCacheDiagnostic::InvalidMeshData, "invalid indexed geometry");
    std::uint64_t actualBytes = 0;
    if (geometry->vertices.size() != item.vertexCount || geometry->indices.size() != item.indexCount
        || geometry->submeshes.size() != item.submeshCount
        || !geometryByteSize(*geometry, actualBytes) || actualBytes != geometryBytes)
        return failure(EditorSceneViewportMeshCacheDiagnostic::ResolvedMetadataMismatch,
            "resolved geometry counts changed");

    auto uploaded = renderer::uploadMeshToRhi(device, *geometry);
    if (!uploaded.success)
        return failure(
            EditorSceneViewportMeshCacheDiagnostic::UploadFailed, uploaded.diagnostics.c_str());

    auto entry = core::makeUnique<Entry>();
    entry->resourceId = item.meshResourceId;
    entry->previewId = item.terrainPreviewMaterialResourceId;
    entry->previewGeneration = previewGeneration;
    entry->generation = item.meshGeneration;
    entry->geometry = item.geometry;
    entry->tile = item.tile;
    entry->mesh = std::move(uploaded.mesh);
    entry->geometryBytes = geometryBytes;
    const auto* cachedMesh = &entry->mesh;
    entries_.push_back(std::move(entry));
    residentGeometryBytes_ += geometryBytes;
    ownerDevice_ = &device;
    ownerResources_ = &resources;

    EditorSceneViewportMeshCacheResult result;
    result.mesh = cachedMesh;
    return result;
}

std::size_t EditorSceneViewportMeshCache::residentMeshCount() const noexcept
{
    return entries_.size();
}

std::uint64_t EditorSceneViewportMeshCache::residentGeometryBytes() const noexcept
{
    return residentGeometryBytes_;
}

const char* editorSceneViewportMeshCacheDiagnosticCode(
    EditorSceneViewportMeshCacheDiagnostic diagnostic) noexcept
{
    switch (diagnostic) {
    case EditorSceneViewportMeshCacheDiagnostic::None:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-OK";
    case EditorSceneViewportMeshCacheDiagnostic::InvalidBudget:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-BUDGET-INVALID";
    case EditorSceneViewportMeshCacheDiagnostic::InvalidResolvedItem:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-RESOLVED-ITEM-INVALID";
    case EditorSceneViewportMeshCacheDiagnostic::OwnerMismatch:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-OWNER-MISMATCH";
    case EditorSceneViewportMeshCacheDiagnostic::ResourceChanged:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-RESOURCE-CHANGED";
    case EditorSceneViewportMeshCacheDiagnostic::MutableGeometryUnsupported:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-MUTABLE-GEOMETRY-UNSUPPORTED";
    case EditorSceneViewportMeshCacheDiagnostic::InvalidMeshData:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-GEOMETRY-INVALID";
    case EditorSceneViewportMeshCacheDiagnostic::ResolvedMetadataMismatch:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-METADATA-MISMATCH";
    case EditorSceneViewportMeshCacheDiagnostic::BudgetExceeded:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-BUDGET-EXCEEDED";
    case EditorSceneViewportMeshCacheDiagnostic::UploadFailed:
        return "ARP-EDITOR-VIEWPORT-MESH-CACHE-UPLOAD-FAILED";
    }
    return "ARP-EDITOR-VIEWPORT-MESH-CACHE-DIAGNOSTIC-UNKNOWN";
}

} // namespace arp::editor
