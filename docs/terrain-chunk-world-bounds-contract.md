# TerrainChunkWorldBounds/1

Status: Contract Complete + scoped Metal Backend Validated through WoCC's
2026-09-24 negative-origin scene and 300-frame moving-camera smoke. Other native
backends were not run; this is not Platform Validated or Production Ready.

`TerrainStreamer::chunkWorldBounds` is the shared CPU mapping from a resident
chunk coordinate to its world-space XZ footprint. Callers must use an initialized
streamer and an in-bounds coordinate. Terrain mesh generation and decoration
scatter consume this same mapping.

With authoritative `worldBounds`, chunk indices are zero-based and each minimum
is `worldBounds.min + coord * chunkSize`. With the existing legacy symmetric
configuration, signed chunk indices retain `coord * chunkSize`. Each maximum
is the corresponding minimum plus `chunkSize`. No clipping, centering, or
camera-dependent offset is applied. Height sampling remains the responsibility
of the authoritative TerrainSurface.

This allocation-free CPU mapping is Equivalent on Null, Vulkan, Metal and
WebGPU; it adds no GPU descriptors, resource states, capabilities or lifetimes.
Tests cover negative and asymmetric origins, adjacent boundaries, legacy signed
coordinates, and equality between generated mesh corners and scatter bounds.
WoCC must consume it for immediate, deferred and regenerated instance vegetation
and for the legacy merged-mesh consumer.
