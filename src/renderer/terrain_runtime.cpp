#include "renderer/terrain_runtime.h"

#include <algorithm>
#include <limits>

namespace arp::renderer {

bool TerrainRuntime::initialize()
{
    return initialize(Desc{});
}

bool TerrainRuntime::initialize(const Desc& desc, ChunkMeshProvider provider)
{
    if (terrainSurface_ == nullptr || (!desc.worldBounds.valid() && desc.worldHalfExtent <= 0.0f)
        || desc.chunkSize <= 0.0f || desc.samplesPerChunk < 2u || desc.activeRadius == 0u) {
        return false;
    }
    const auto surfaceBounds = terrainSurface_->bounds();
    const auto runtimeBounds = desc.worldBounds.valid()
        ? desc.worldBounds
        : terrain::TerrainBounds{-desc.worldHalfExtent,
              desc.worldHalfExtent,
              -desc.worldHalfExtent,
              desc.worldHalfExtent};
    const float sampleX = (runtimeBounds.minX + runtimeBounds.maxX) * 0.5f;
    const float sampleZ = (runtimeBounds.minZ + runtimeBounds.maxZ) * 0.5f;
    if (!surfaceBounds.contains(runtimeBounds.minX, runtimeBounds.minZ)
        || !surfaceBounds.contains(runtimeBounds.maxX, runtimeBounds.maxZ)
        || !terrainSurface_->sample(sampleX, sampleZ).ready()) {
        return false;
    }

    if (initialized_) {
        streamer_.shutdown();
    }
    desc_ = desc;
    chunkMeshProvider_ = std::move(provider);

    pcg::TerrainStreamerDesc streamerDesc;
    streamerDesc.worldHalfExtent = desc.worldHalfExtent;
    streamerDesc.worldBounds = desc.worldBounds;
    streamerDesc.chunkSize = desc.chunkSize;
    streamerDesc.samplesPerChunk = desc.samplesPerChunk;
    streamerDesc.activeRadius = desc.activeRadius;
    streamerDesc.distanceLodWhenFullyResident = desc.distanceLodWhenFullyResident;
    streamerDesc.fogDistance = desc.fogDistance;
    streamerDesc.baseHeight = desc.baseHeight;
    streamerDesc.heightSampler = [this](float worldX, float worldZ) {
        if (terrainSurface_ == nullptr) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        const auto sampled = terrainSurface_->sample(worldX, worldZ);
        return sampled.ready() ? sampled.sample.height : std::numeric_limits<float>::quiet_NaN();
    };

    initialized_ = streamer_.initialize(streamerDesc);
    return initialized_;
}

void TerrainRuntime::shutdown()
{
    streamer_.shutdown();
    initialized_ = false;
}

bool TerrainRuntime::update(
    float cameraX, float cameraZ, float cameraForwardX, float cameraForwardZ, float deltaSeconds)
{
    if (!initialized_ || terrainSurface_ == nullptr) {
        return false;
    }

    streamer_.update(cameraX, cameraZ, cameraForwardX, cameraForwardZ, deltaSeconds);

    const auto budget =
        std::min<std::size_t>(desc_.maxChunkGenerationsPerUpdate, streamer_.genRequests().size());
    for (std::size_t index = 0; index < budget; ++index) {
        const auto& request = streamer_.genRequests()[index];
        auto mesh = chunkMeshProvider_
            ? chunkMeshProvider_(request)
            : streamer_.generateChunkMesh(
                  request.coord, request.lodLevel, request.skirtEdgeMask, request.stitchEdgeMask);
        streamer_.onChunkGenerated(request.coord, std::move(mesh), request.lodLevel);
        if (chunkGeneratedCallback_) {
            const auto it = streamer_.activeChunks().find(request.coord);
            if (it != streamer_.activeChunks().end() && !it->second.genPending
                && it->second.mesh.validForIndexedDraw()) {
                chunkGeneratedCallback_(request, it->second.mesh);
            }
        }
    }
    if (chunkUnloadedCallback_) {
        for (const auto& event : streamer_.unloadEvents()) {
            chunkUnloadedCallback_(event.coord);
        }
    }
    return true;
}

const assets::MeshAsset* TerrainRuntime::chunkMesh(pcg::ChunkCoord coord) const
{
    const auto it = streamer_.activeChunks().find(coord);
    if (it == streamer_.activeChunks().end() || it->second.genPending
        || it->second.mesh.vertices.empty()) {
        return nullptr;
    }
    return &it->second.mesh;
}

std::size_t TerrainRuntime::visibleChunkCount() const
{
    std::size_t count = 0;
    for (const auto& [coord, chunk] : streamer_.activeChunks()) {
        (void)coord;
        if (!chunk.genPending && !chunk.mesh.vertices.empty()) {
            ++count;
        }
    }
    return count;
}

} // namespace arp::renderer
