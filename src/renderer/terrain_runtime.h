#pragma once

#include "assets/asset_pipeline.h"
#include "pcg/terrain_streamer.h"
#include "terrain/terrain_surface.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace arp::renderer {

/// Renderer-independent terrain runtime. It owns logical terrain streaming
/// state and CPU mesh residency; GPU slots, uploads, retirement and fences
/// remain renderer-owned.
class TerrainRuntime {
public:
    struct Desc {
        // Asset-authoritative bounds. When valid, TerrainRuntime forwards them
        // to TerrainStreamer and ignores worldHalfExtent for chunk placement.
        terrain::TerrainBounds worldBounds;
        float worldHalfExtent = 4000.0f;
        float chunkSize = 64.0f;
        std::uint32_t samplesPerChunk = 33u;
        std::uint32_t activeRadius = 12u;
        float fogDistance = 700.0f;
        float baseHeight = 0.0f;
        std::uint32_t maxChunkGenerationsPerUpdate = 3u;
    };

    using ChunkMeshProvider = std::function<assets::MeshAsset(const pcg::ChunkGenRequest& request)>;
    using ChunkGeneratedCallback =
        std::function<void(const pcg::ChunkGenRequest& request, const assets::MeshAsset& mesh)>;
    using ChunkUnloadedCallback = std::function<void(pcg::ChunkCoord coord)>;

    TerrainRuntime() = default;

    void setTerrainSurface(std::shared_ptr<const terrain::TerrainSurface> surface)
    {
        terrainSurface_ = std::move(surface);
    }

    std::shared_ptr<const terrain::TerrainSurface> terrainSurface() const
    {
        return terrainSurface_;
    }

    bool initialize();
    bool initialize(const Desc& desc, ChunkMeshProvider provider = {});
    void shutdown();

    bool update(float cameraX,
        float cameraZ,
        float cameraForwardX,
        float cameraForwardZ,
        float deltaSeconds);

    void setChunkMeshProvider(ChunkMeshProvider provider)
    {
        chunkMeshProvider_ = std::move(provider);
    }

    /// Called after a generated mesh has been installed in the CPU streamer.
    /// Consumers may derive game-specific decoration from the stable chunk
    /// event, but must not take ownership of the terrain mesh.
    void setChunkGeneratedCallback(ChunkGeneratedCallback callback)
    {
        chunkGeneratedCallback_ = std::move(callback);
    }

    /// Called after the streamer reports that a chunk left the active set.
    /// Renderer GPU retirement remains outside this callback.
    void setChunkUnloadedCallback(ChunkUnloadedCallback callback)
    {
        chunkUnloadedCallback_ = std::move(callback);
    }

    bool initialized() const { return initialized_; }

    const Desc& desc() const { return desc_; }

    const pcg::TerrainStreamer& streamer() const { return streamer_; }

    pcg::TerrainStreamer& streamer() { return streamer_; }

    const assets::MeshAsset* chunkMesh(pcg::ChunkCoord coord) const;
    std::size_t visibleChunkCount() const;

private:
    std::shared_ptr<const terrain::TerrainSurface> terrainSurface_;
    Desc desc_;
    pcg::TerrainStreamer streamer_;
    ChunkMeshProvider chunkMeshProvider_;
    ChunkGeneratedCallback chunkGeneratedCallback_;
    ChunkUnloadedCallback chunkUnloadedCallback_;
    bool initialized_ = false;
};

} // namespace arp::renderer
