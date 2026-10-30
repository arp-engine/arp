#pragma once

#include "editor/viewport/editor_scene_viewport_resource_resolver.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "rhi/frontend/ordered_graphics_execution.h"
#include "rhi/gpu_device.h"

namespace arp::editor {

struct EditorSceneViewportMeshCacheBudget {
    std::size_t maxResidentMeshes = 0;
    std::uint64_t maxResidentGeometryBytes = 0;
};

enum class EditorSceneViewportMeshCacheDiagnostic {
    None,
    InvalidBudget,
    InvalidResolvedItem,
    OwnerMismatch,
    ResourceChanged,
    MutableGeometryUnsupported,
    InvalidMeshData,
    ResolvedMetadataMismatch,
    BudgetExceeded,
    UploadFailed,
};

struct EditorSceneViewportMeshCacheResult {
    EditorSceneViewportMeshCacheDiagnostic diagnostic =
        EditorSceneViewportMeshCacheDiagnostic::None;
    core::pmr::String detail{&core::defaultCpuMemoryResource()};
    const renderer::MeshGpuBuffer* mesh = nullptr;
    bool reused = false;

    bool valid() const noexcept
    {
        return diagnostic == EditorSceneViewportMeshCacheDiagnostic::None && mesh != nullptr;
    }
};

// Owns uploaded geometry for the lifetime of its device. Destroy only after
// submitted work using these buffers has completed, and before the device.
class EditorSceneViewportMeshCache final {
public:
    explicit EditorSceneViewportMeshCache(EditorSceneViewportMeshCacheBudget budget);

    EditorSceneViewportMeshCache(const EditorSceneViewportMeshCache&) = delete;
    EditorSceneViewportMeshCache& operator=(const EditorSceneViewportMeshCache&) = delete;

    EditorSceneViewportMeshCacheResult acquire(rhi::GpuDevice& device,
        const resource::ResourceManager& resources,
        const EditorSceneViewportResolvedItem& item);

    // Caller must complete all device work before releasing borrowed bindings.
    void clearAfterDeviceIdle() { clearResources(); }

    std::size_t residentMeshCount() const noexcept;
    std::uint64_t residentGeometryBytes() const noexcept;
    const char* clearAfterOrderedIssue(const rhi::OriginalOrderedGraphicsExecution&);

private:
    void clearResources()
    {
        entries_.clear();
        ownerDevice_ = nullptr;
        ownerResources_ = nullptr;
        residentGeometryBytes_ = 0u;
    }

    struct Entry {
        core::pmr::String previewId;
        resource::ResourceGeneration previewGeneration = 0;
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        resource::ResourceGeneration generation = 0;
        EditorSceneViewportGeometry geometry = EditorSceneViewportGeometry::Mesh;
        terrain::TerrainTileCoord tile;
        renderer::MeshGpuBuffer mesh;
        std::uint64_t geometryBytes = 0;
    };

    EditorSceneViewportMeshCacheBudget budget_;
    const rhi::GpuDevice* ownerDevice_ = nullptr;
    const resource::ResourceManager* ownerResources_ = nullptr;
    core::pmr::Vector<core::UniquePtr<Entry>> entries_{&core::defaultCpuMemoryResource()};
    std::uint64_t residentGeometryBytes_ = 0;
};

ARP_API const char* editorSceneViewportMeshCacheDiagnosticCode(
    EditorSceneViewportMeshCacheDiagnostic diagnostic) noexcept;

} // namespace arp::editor
