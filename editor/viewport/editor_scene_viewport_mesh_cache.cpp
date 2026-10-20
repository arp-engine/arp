#include "editor/viewport/editor_scene_viewport_mesh_cache.h"

#include "resource/mesh_asset_resources.h"

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

    const auto handle = resources.find<resource::MeshAssetResource>(item.meshResourceId);
    if (!handle.ready() || handle.generation() != item.meshGeneration)
        return failure(EditorSceneViewportMeshCacheDiagnostic::ResourceChanged,
            "mesh resource is no longer ready at the resolved generation");
    const auto* resource = handle.get();
    if (!resource->immutableMeshGeometry())
        return failure(EditorSceneViewportMeshCacheDiagnostic::MutableGeometryUnsupported,
            "mutable mesh geometry cannot enter the viewport cache");
    const auto* geometry = resource->mesh();
    if (geometry == nullptr || !geometry->validForIndexedDraw())
        return failure(EditorSceneViewportMeshCacheDiagnostic::InvalidMeshData,
            "mesh geometry is not valid for indexed drawing");
    if (geometry->vertices.size() != item.vertexCount || geometry->indices.size() != item.indexCount
        || geometry->submeshes.size() != item.submeshCount) {
        return failure(EditorSceneViewportMeshCacheDiagnostic::ResolvedMetadataMismatch,
            "mesh geometry no longer matches resolved CPU metadata");
    }

    for (const auto& entry : entries_) {
        if (entry->resourceId == item.meshResourceId && entry->generation == item.meshGeneration) {
            EditorSceneViewportMeshCacheResult result;
            result.mesh = &entry->mesh;
            result.reused = true;
            return result;
        }
    }

    std::uint64_t geometryBytes = 0;
    if (!geometryByteSize(*geometry, geometryBytes))
        return failure(EditorSceneViewportMeshCacheDiagnostic::InvalidMeshData,
            "mesh geometry byte size overflows the cache accounting type");
    if (entries_.size() >= budget_.maxResidentMeshes
        || geometryBytes > budget_.maxResidentGeometryBytes - residentGeometryBytes_) {
        return failure(EditorSceneViewportMeshCacheDiagnostic::BudgetExceeded,
            "viewport mesh cache resident budget is exhausted");
    }

    auto uploaded = renderer::uploadMeshToRhi(device, *geometry);
    if (!uploaded.success)
        return failure(
            EditorSceneViewportMeshCacheDiagnostic::UploadFailed, uploaded.diagnostics.c_str());

    auto entry = core::makeUnique<Entry>();
    entry->resourceId = item.meshResourceId;
    entry->generation = item.meshGeneration;
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
