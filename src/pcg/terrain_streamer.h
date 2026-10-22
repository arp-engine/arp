#pragma once
// Legacy source compatibility; internal consumers use the terrain domain.
#include "3d/terrain/terrain_streamer.h"

namespace arp::pcg {
using terrain::ChunkCoord;
using terrain::ChunkGenRequest;
using terrain::ChunkUnloadEvent;
using terrain::defaultLodTable;
using terrain::HeightSampler;
using terrain::kDefaultHysteresisMargin;
using terrain::LodLevel;
using terrain::TerrainChunk;
using terrain::TerrainStreamer;
using terrain::TerrainStreamerDesc;
} // namespace arp::pcg
