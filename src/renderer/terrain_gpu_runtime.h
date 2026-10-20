#pragma once

#include "core/cpu_allocator.h"
#include "core/runtime/gpu_resource_retire_queue.h"
#include "pcg/terrain_streamer.h"
#include "renderer/mesh/mesh_gpu_buffer.h"
#include "terrain/terrain_surface.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace arp::resource {
class TerrainAssetResource;
}

namespace arp::renderer {

class TerrainGpuRhiUploader;

/// Immutable CPU payload for one validated TerrainAsset tile. This is the
/// renderer's L2 upload contract: formats and byte counts are fixed by
/// terrain.asset/v1, and no native texture handle crosses this boundary.
struct TerrainGpuTilePayload final {
    terrain::TerrainTileCoord coord;
    terrain::TerrainBounds worldBounds;
    std::uint32_t samplesPerEdge = 0u;
    std::uint64_t resourceGeneration = 0u;
    const float* heights = nullptr; // R32_SFLOAT, row-major
    std::size_t heightCount = 0u;
    const std::uint8_t* weightPageA = nullptr; // RGBA8_UNORM
    std::size_t weightPageAByteCount = 0u;
    const std::uint8_t* weightPageB = nullptr; // RGBA8_UNORM
    std::size_t weightPageBByteCount = 0u;

    bool valid() const noexcept;
};

/// Backend-private opaque page identities. They are assigned only by a
/// lowering callback after real texture creation and upload succeed.
struct TerrainGpuTileBackendPayload final {
    std::uint64_t heightPage = 0u;
    std::uint64_t weightPageA = 0u;
    std::uint64_t weightPageB = 0u;
    std::uint32_t weightArrayLayer = 0u;

    bool valid() const noexcept
    {
        return heightPage != 0u && weightPageA != 0u && weightPageB != 0u;
    }
};

/// CPU mirror of the shader page table. Tile coordinates and world bounds are
/// explicit so sampling cannot silently fall back to a global world UV.
struct TerrainGpuPageTableEntry final {
    terrain::TerrainTileCoord coord;
    terrain::TerrainBounds worldBounds;
    std::uint64_t resourceGeneration = 0u;
    TerrainGpuTileBackendPayload pages;
};

/// Translates an already validated TerrainAsset tile into the renderer L2
/// payload without exposing resource-manager state to a backend adapter.
bool terrainGpuTilePayloadFromAsset(const resource::TerrainAssetResource& asset,
    terrain::TerrainTileCoord coord,
    TerrainGpuTilePayload& outPayload);

/// Renderer-owned GPU residency for streamed terrain chunks.
///
/// TerrainRuntime owns the streamer and CPU MeshAsset values. This class owns
/// only the GPU-facing chunk slots and their deferred destruction. Upload and
/// update are supplied by the backend adapter so this layer stays independent
/// of a concrete RenderSystem or native RHI device.
class TerrainGpuRuntime {
private:
    struct TileSlot;

public:
    struct Slot {
        MeshGpuBuffer terrainBuffer;
        // CPU TerrainChunk::meshRevision represented by terrainBuffer.
        std::uint64_t meshRevision = 0u;
        int lodLevel = 0;
        bool terrainValid = false;
    };

    using UploadMesh = std::function<bool(const assets::MeshAsset&, MeshGpuBuffer&)>;
    using UpdateMesh = std::function<bool(const assets::MeshAsset&, MeshGpuBuffer&)>;
    using UploadTile =
        std::function<bool(const TerrainGpuTilePayload&, TerrainGpuTileBackendPayload&)>;

    bool syncChunk(pcg::ChunkCoord coord,
        const assets::MeshAsset& mesh,
        int lodLevel,
        std::uint64_t meshRevision,
        std::uint64_t submissionSerial,
        const UploadMesh& upload,
        const UpdateMesh& update,
        const UploadMesh& replace);

    bool setRetirementCapacityBudget(std::shared_ptr<core::AtomicCapacityBudget> budget) noexcept
    {
        return retireQueue_.setCapacityBudget(budget)
            && tileRetireQueue_.setCapacityBudget(std::move(budget));
    }

    bool retireInactive(
        const core::pmr::Vector<pcg::ChunkCoord>& activeCoords, std::uint64_t submissionSerial);
    std::size_t inactiveSlotCount(
        const core::pmr::Vector<pcg::ChunkCoord>& activeCoords) const noexcept;
    /// Returns whether syncing this existing coordinate may enter the
    /// update-fallback replacement path and therefore retire one old slot.
    /// It is an upper-bound query only: a successful in-place update consumes
    /// no retirement capacity.
    bool slotMayRequireReplacementRetirement(
        pcg::ChunkCoord coord, int requestedLodLevel) const noexcept;

    /// Upload or atomically replace the three GPU pages for a TerrainAsset
    /// tile. Failure does not disturb an existing tile generation.
    bool syncTile(const TerrainGpuTilePayload& tile,
        std::uint64_t submissionSerial,
        const UploadTile& upload,
        const UploadTile& replace);

    /// Concrete RHI path. A candidate tile is uploaded into an unleased array
    /// layer. Callers that stage several tiles in one frame can defer page-table
    /// publication and publish the complete table once with
    /// publishPageTableWithRhi(). An old layer remains leased until the strict
    /// retirement serial has completed, so it cannot be overwritten by a
    /// newer TerrainAsset generation while an older frame may still sample it.
    bool syncTileWithRhi(const TerrainGpuTilePayload& tile,
        std::uint64_t submissionSerial,
        TerrainGpuRhiUploader& uploader,
        core::pmr::String& diagnostics,
        bool publishPageTableSnapshot = true);

    /// Publish one immutable snapshot for all currently staged tile pages.
    /// This is the production streaming path: one publication per frame keeps
    /// snapshot retirement bounded and prevents partially resident tables.
    bool publishPageTableWithRhi(std::uint64_t submissionSerial,
        TerrainGpuRhiUploader& uploader,
        core::pmr::String& diagnostics);

    /// Retire asset pages that are no longer active. Their opaque backend
    /// payload is destroyed only after the strict submission-serial boundary.
    bool retireInactiveTiles(const core::pmr::Vector<terrain::TerrainTileCoord>& activeCoords,
        std::uint64_t submissionSerial);
    std::size_t inactiveTileCount(
        const core::pmr::Vector<terrain::TerrainTileCoord>& activeCoords) const noexcept;

    bool retireInactiveTilesWithRhi(
        const core::pmr::Vector<terrain::TerrainTileCoord>& activeCoords,
        std::uint64_t submissionSerial,
        TerrainGpuRhiUploader& uploader,
        core::pmr::String& diagnostics);

    const TerrainGpuPageTableEntry* pageTableEntry(terrain::TerrainTileCoord coord) const;
    core::pmr::Vector<TerrainGpuPageTableEntry> pageTable() const;

    const Slot* slot(pcg::ChunkCoord coord) const;
    Slot* slot(pcg::ChunkCoord coord);

    std::size_t activeSlotCount() const noexcept { return slots_.size(); }

    std::size_t activeTileCount() const noexcept { return tileSlots_.size(); }

    std::size_t retiredSlotCount() const noexcept
    {
        return retireQueue_.size() + tileRetireQueue_.size();
    }

    core::runtime::GpuResourceRetireQueueStats retirementStats() const noexcept
    {
        const auto slots = retireQueue_.stats();
        const auto tiles = tileRetireQueue_.stats();
        return {
            slots.pendingCount + tiles.pendingCount,
            slots.peakPendingCount + tiles.peakPendingCount,
            slots.totalRetired + tiles.totalRetired,
            slots.totalReaped + tiles.totalReaped,
        };
    }

    std::size_t reapCompleted(std::uint64_t completedFrameSerial);
    std::size_t reapAll();

    /// Shutdown-only after the caller has waited for GPU idle. It releases
    /// all page-layer leases together with the retired ownership records.
    void clear();

    core::pmr::Vector<pcg::ChunkCoord> activeCoords() const;

private:
    struct TileSlot final {
        TerrainGpuPageTableEntry pageTableEntry;
    };

    core::pmr::UnorderedMap<pcg::ChunkCoord, Slot> slots_{&core::defaultCpuMemoryResource()};
    core::pmr::UnorderedMap<terrain::TerrainTileCoord, TileSlot, terrain::TerrainTileCoordHash>
        tileSlots_{&core::defaultCpuMemoryResource()};
    arp::core::runtime::GpuResourceRetireQueue<Slot> retireQueue_;
    arp::core::runtime::GpuResourceRetireQueue<TileSlot> tileRetireQueue_;
    core::pmr::UnorderedSet<std::uint32_t> leasedTileLayers_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::renderer
