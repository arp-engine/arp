#include "renderer/terrain_gpu_runtime.h"

#include "renderer/terrain_gpu_rhi_uploader.h"
#include "resource/terrain_asset_resource.h"

#include <algorithm>
#include <cmath>

namespace arp::renderer {
namespace {

core::pmr::String terrainGpuDiagnostic(const char* message)
{
    return core::pmr::String(message, &core::defaultCpuMemoryResource());
}

std::uint32_t findUnleasedTileLayer(
    const core::pmr::UnorderedSet<std::uint32_t>& leasedLayers, std::uint32_t capacity)
{
    for (std::uint32_t layer = 0u; layer < capacity; ++layer) {
        if (leasedLayers.find(layer) == leasedLayers.end()) {
            return layer;
        }
    }
    return capacity;
}

} // namespace

bool TerrainGpuTilePayload::valid() const noexcept
{
    if (!worldBounds.valid() || samplesPerEdge < 2u || resourceGeneration == 0u
        || heights == nullptr || weightPageA == nullptr || weightPageB == nullptr) {
        return false;
    }
    const auto expected = static_cast<std::size_t>(samplesPerEdge) * samplesPerEdge;
    if (heightCount != expected || weightPageAByteCount != expected * 4u
        || weightPageBByteCount != expected * 4u) {
        return false;
    }
    for (std::size_t index = 0u; index < heightCount; ++index) {
        if (!std::isfinite(heights[index])) {
            return false;
        }
    }
    return true;
}

bool terrainGpuTilePayloadFromAsset(const resource::TerrainAssetResource& asset,
    terrain::TerrainTileCoord coord,
    TerrainGpuTilePayload& outPayload)
{
    outPayload = {};
    resource::TerrainAssetTileReadView view;
    if (!asset.tileReadView(coord, view)) {
        return false;
    }
    const auto assetBounds = asset.bounds();
    const float minX = assetBounds.minX + static_cast<float>(coord.x) * view.tileWorldSize;
    const float minZ = assetBounds.minZ + static_cast<float>(coord.z) * view.tileWorldSize;
    outPayload.coord = coord;
    outPayload.worldBounds = {minX, minX + view.tileWorldSize, minZ, minZ + view.tileWorldSize};
    outPayload.samplesPerEdge = view.samplesPerEdge;
    outPayload.resourceGeneration = view.generation;
    outPayload.heights = view.heights;
    outPayload.heightCount = view.heightCount;
    outPayload.weightPageA = view.weightPageA;
    outPayload.weightPageAByteCount = view.weightPageAByteCount;
    outPayload.weightPageB = view.weightPageB;
    outPayload.weightPageBByteCount = view.weightPageBByteCount;
    return outPayload.valid();
}

bool TerrainGpuRuntime::syncChunk(terrain::ChunkCoord coord,
    const assets::MeshAsset& mesh,
    int lodLevel,
    std::uint64_t meshRevision,
    std::uint64_t submissionSerial,
    const UploadMesh& upload,
    const UpdateMesh& update,
    const UploadMesh& replace)
{
    if (!mesh.validForIndexedDraw() || !upload) {
        return false;
    }

    auto it = slots_.find(coord);
    if (it != slots_.end() && it->second.terrainValid) {
        if (update && update(mesh, it->second.terrainBuffer)) {
            it->second.meshRevision = meshRevision;
            it->second.lodLevel = lodLevel;
            return true;
        }
        if (!replace || !retireQueue_.canRetire())
            return false;
        MeshGpuBuffer replacement;
        if (!replace(mesh, replacement))
            return false;
        Slot retired = std::move(it->second);
        if (!retireQueue_.retire(submissionSerial, std::move(retired)))
            return false;
        it->second.terrainBuffer = std::move(replacement);
        it->second.meshRevision = meshRevision;
        it->second.lodLevel = lodLevel;
        it->second.terrainValid = true;
        return true;
    }

    Slot& slot = slots_[coord];
    if (!upload(mesh, slot.terrainBuffer)) {
        if (!slot.terrainValid) {
            slots_.erase(coord);
        }
        return false;
    }
    slot.meshRevision = meshRevision;
    slot.lodLevel = lodLevel;
    slot.terrainValid = true;
    return true;
}

bool TerrainGpuRuntime::retireInactive(
    const core::pmr::Vector<terrain::ChunkCoord>& activeCoords, std::uint64_t submissionSerial)
{
    std::size_t retireCount = 0u;
    for (const auto& [coord, slot] : slots_) {
        (void)slot;
        if (std::find(activeCoords.begin(), activeCoords.end(), coord) == activeCoords.end())
            ++retireCount;
    }
    if (retireCount != 0u && !retireQueue_.canRetire(retireCount))
        return false;
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (std::find(activeCoords.begin(), activeCoords.end(), it->first) == activeCoords.end()) {
            if (!retireQueue_.retire(submissionSerial, std::move(it->second)))
                return false;
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

std::size_t TerrainGpuRuntime::inactiveSlotCount(
    const core::pmr::Vector<terrain::ChunkCoord>& activeCoords) const noexcept
{
    std::size_t count = 0u;
    for (const auto& [coord, slot] : slots_) {
        (void)slot;
        if (std::find(activeCoords.begin(), activeCoords.end(), coord) == activeCoords.end()) {
            ++count;
        }
    }
    return count;
}

bool TerrainGpuRuntime::slotMayRequireReplacementRetirement(
    const terrain::ChunkCoord coord, const int requestedLodLevel) const noexcept
{
    const auto found = slots_.find(coord);
    return found != slots_.end() && found->second.terrainValid
        && found->second.lodLevel != requestedLodLevel;
}

bool TerrainGpuRuntime::syncTile(const TerrainGpuTilePayload& tile,
    std::uint64_t submissionSerial,
    const UploadTile& upload,
    const UploadTile& replace)
{
    if (!tile.valid() || !upload) {
        return false;
    }

    const auto found = tileSlots_.find(tile.coord);
    if (found != tileSlots_.end()) {
        const auto& active = found->second.pageTableEntry;
        if (active.resourceGeneration == tile.resourceGeneration) {
            return true;
        }
        if (!replace || !tileRetireQueue_.canRetire()) {
            return false;
        }
        TerrainGpuTileBackendPayload replacementPages;
        if (!replace(tile, replacementPages) || !replacementPages.valid()) {
            return false;
        }
        TileSlot retired = std::move(found->second);
        if (!tileRetireQueue_.retire(submissionSerial, std::move(retired)))
            return false;
        found->second.pageTableEntry = {
            tile.coord, tile.worldBounds, tile.resourceGeneration, replacementPages};
        return true;
    }

    TerrainGpuTileBackendPayload pages;
    if (!upload(tile, pages) || !pages.valid()) {
        return false;
    }
    TileSlot slot;
    slot.pageTableEntry = {tile.coord, tile.worldBounds, tile.resourceGeneration, pages};
    tileSlots_.emplace(tile.coord, std::move(slot));
    return true;
}

bool TerrainGpuRuntime::syncTileWithRhi(const TerrainGpuTilePayload& tile,
    std::uint64_t submissionSerial,
    TerrainGpuRhiUploader& uploader,
    core::pmr::String& diagnostics,
    bool publishPageTableSnapshot)
{
    diagnostics.clear();
    if (!tile.valid() || submissionSerial == 0u || !uploader.ready()
        || uploader.maxTilePages() == 0u) {
        diagnostics = terrainGpuDiagnostic(
            "TerrainAsset RHI tile sync requires a valid tile, uploader, and submission serial");
        return false;
    }
    const auto found = tileSlots_.find(tile.coord);
    if (found != tileSlots_.end()
        && found->second.pageTableEntry.resourceGeneration == tile.resourceGeneration) {
        return true;
    }

    if (found != tileSlots_.end() && !tileRetireQueue_.canRetire()) {
        diagnostics =
            terrainGpuDiagnostic("TerrainAsset RHI tile retirement capacity is exhausted");
        return false;
    }
    const auto layer = findUnleasedTileLayer(leasedTileLayers_, uploader.maxTilePages());
    if (layer == uploader.maxTilePages()) {
        diagnostics = terrainGpuDiagnostic(
            "TerrainAsset RHI tile sync has no unleased texture-array layer; wait for retired submissions before retrying");
        return false;
    }
    const auto uploaded = uploader.upload(tile, layer, submissionSerial);
    if (!uploaded.success || !uploaded.pages.valid()) {
        diagnostics = uploaded.diagnostics;
        return false;
    }

    auto candidateTable = pageTable();
    const TerrainGpuPageTableEntry candidate{
        tile.coord, tile.worldBounds, tile.resourceGeneration, uploaded.pages};
    const auto candidateIt = std::find_if(candidateTable.begin(),
        candidateTable.end(),
        [&tile](const TerrainGpuPageTableEntry& entry) {
            return entry.coord == tile.coord;
        });
    if (candidateIt == candidateTable.end()) {
        candidateTable.push_back(candidate);
    } else {
        *candidateIt = candidate;
    }
    if (publishPageTableSnapshot) {
        const auto pageTableWritten = uploader.writePageTable(candidateTable, submissionSerial);
        if (!pageTableWritten.success) {
            diagnostics = pageTableWritten.diagnostics;
            return false;
        }
    }

    leasedTileLayers_.insert(layer);
    if (found == tileSlots_.end()) {
        TileSlot slot;
        slot.pageTableEntry = candidate;
        tileSlots_.emplace(tile.coord, std::move(slot));
        return true;
    }
    TileSlot retired = std::move(found->second);
    if (!tileRetireQueue_.retire(submissionSerial, std::move(retired))) {
        diagnostics =
            terrainGpuDiagnostic("TerrainAsset RHI tile retirement capacity is exhausted");
        return false;
    }
    found->second.pageTableEntry = candidate;
    return true;
}

bool TerrainGpuRuntime::publishPageTableWithRhi(
    std::uint64_t submissionSerial, TerrainGpuRhiUploader& uploader, core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (submissionSerial == 0u || !uploader.ready()) {
        diagnostics = terrainGpuDiagnostic(
            "TerrainAsset RHI page-table publication requires a ready uploader and submission serial");
        return false;
    }
    const auto written = uploader.writePageTable(pageTable(), submissionSerial);
    if (!written.success) {
        diagnostics = written.diagnostics;
        return false;
    }
    return true;
}

bool TerrainGpuRuntime::retireInactiveTiles(
    const core::pmr::Vector<terrain::TerrainTileCoord>& activeCoords,
    std::uint64_t submissionSerial)
{
    std::size_t retireCount = 0u;
    for (const auto& [coord, slot] : tileSlots_) {
        (void)slot;
        if (std::find(activeCoords.begin(), activeCoords.end(), coord) == activeCoords.end())
            ++retireCount;
    }
    if (retireCount != 0u && !tileRetireQueue_.canRetire(retireCount))
        return false;
    for (auto it = tileSlots_.begin(); it != tileSlots_.end();) {
        if (std::find(activeCoords.begin(), activeCoords.end(), it->first) == activeCoords.end()) {
            if (!tileRetireQueue_.retire(submissionSerial, std::move(it->second)))
                return false;
            it = tileSlots_.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

std::size_t TerrainGpuRuntime::inactiveTileCount(
    const core::pmr::Vector<terrain::TerrainTileCoord>& activeCoords) const noexcept
{
    std::size_t count = 0u;
    for (const auto& [coord, slot] : tileSlots_) {
        (void)slot;
        if (std::find(activeCoords.begin(), activeCoords.end(), coord) == activeCoords.end()) {
            ++count;
        }
    }
    return count;
}

bool TerrainGpuRuntime::retireInactiveTilesWithRhi(
    const core::pmr::Vector<terrain::TerrainTileCoord>& activeCoords,
    std::uint64_t submissionSerial,
    TerrainGpuRhiUploader& uploader,
    core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (submissionSerial == 0u || !uploader.ready()) {
        diagnostics = terrainGpuDiagnostic(
            "TerrainAsset RHI tile retirement requires a ready uploader and submission serial");
        return false;
    }
    auto candidateTable = pageTable();
    candidateTable.erase(std::remove_if(candidateTable.begin(),
                             candidateTable.end(),
                             [&activeCoords](const TerrainGpuPageTableEntry& entry) {
                                 return std::find(
                                            activeCoords.begin(), activeCoords.end(), entry.coord)
                                     == activeCoords.end();
                             }),
        candidateTable.end());
    if (candidateTable.size() == tileSlots_.size()) {
        return true;
    }
    const auto pageTableWritten = uploader.writePageTable(candidateTable, submissionSerial);
    if (!pageTableWritten.success) {
        diagnostics = pageTableWritten.diagnostics;
        return false;
    }
    return retireInactiveTiles(activeCoords, submissionSerial);
}

const TerrainGpuPageTableEntry* TerrainGpuRuntime::pageTableEntry(
    terrain::TerrainTileCoord coord) const
{
    const auto found = tileSlots_.find(coord);
    return found == tileSlots_.end() ? nullptr : &found->second.pageTableEntry;
}

core::pmr::Vector<TerrainGpuPageTableEntry> TerrainGpuRuntime::pageTable() const
{
    core::pmr::Vector<TerrainGpuPageTableEntry> entries{&core::defaultCpuMemoryResource()};
    entries.reserve(tileSlots_.size());
    for (const auto& entry : tileSlots_) {
        entries.push_back(entry.second.pageTableEntry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.coord.x != rhs.coord.x)
            return lhs.coord.x < rhs.coord.x;
        return lhs.coord.z < rhs.coord.z;
    });
    return entries;
}

const TerrainGpuRuntime::Slot* TerrainGpuRuntime::slot(terrain::ChunkCoord coord) const
{
    const auto it = slots_.find(coord);
    return it != slots_.end() ? &it->second : nullptr;
}

TerrainGpuRuntime::Slot* TerrainGpuRuntime::slot(terrain::ChunkCoord coord)
{
    const auto it = slots_.find(coord);
    return it != slots_.end() ? &it->second : nullptr;
}

core::pmr::Vector<terrain::ChunkCoord> TerrainGpuRuntime::activeCoords() const
{
    core::pmr::Vector<terrain::ChunkCoord> coords{&core::defaultCpuMemoryResource()};
    coords.reserve(slots_.size());
    for (const auto& entry : slots_) {
        coords.push_back(entry.first);
    }
    std::sort(coords.begin(), coords.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.x != rhs.x)
            return lhs.x < rhs.x;
        return lhs.z < rhs.z;
    });
    return coords;
}

std::size_t TerrainGpuRuntime::reapCompleted(std::uint64_t completedFrameSerial)
{
    return retireQueue_.reapCompleted(completedFrameSerial)
        + tileRetireQueue_.reapCompleted(completedFrameSerial, [this](const TileSlot& retired) {
              leasedTileLayers_.erase(retired.pageTableEntry.pages.weightArrayLayer);
          });
}

std::size_t TerrainGpuRuntime::reapAll()
{
    return retireQueue_.reapAll() + tileRetireQueue_.reapAll([this](const TileSlot& retired) {
        leasedTileLayers_.erase(retired.pageTableEntry.pages.weightArrayLayer);
    });
}

void TerrainGpuRuntime::clear()
{
    (void)reapAll();
    slots_.clear();
    tileSlots_.clear();
    leasedTileLayers_.clear();
}

} // namespace arp::renderer
