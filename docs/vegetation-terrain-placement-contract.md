# VegetationTerrainPlacement/1

`InstanceData.y` is the terrain anchor height; `burialY` is the independently
authored vertical offset. Production instance placement obtains its anchor
from the same immutable TerrainSurface snapshot used by terrain streaming.
Procedural scatter/biome selection may retain its deterministic source recipe,
but must not infer the rendered lattice from a fixed procedural grid spacing.

`InstanceData::anchorToSurface` samples world X/Z and changes Y only on Ready
with a finite height. Failure preserves the instance and returns the surface
status (nonfinite Ready data becomes InvalidData). Callers must reject the
unavailable placement with a deterministic diagnostic, never use an old Y or
zero-height fallback. Burial, tilt, scale, species and wind metadata remain
unchanged. Snapshot lifetime covers the entire bounded chunk generation.
Scatter candidates outside the authored surface bounds are excluded before
placement; cluster children may cross the world boundary. In-bounds unavailable
or invalid samples remain errors and may not silently remove vegetation.

Null/Vulkan/Metal/WebGPU consume identical CPU instance bytes; there is no
backend-specific adjustment, resource or shader ABI change. Validation covers
the authored asset's off-grid sample points and unavailable/invalid surfaces,
then production native Metal visual smoke. This contract does not claim that
coarser terrain LOD interpolation exactly matches the finest authored surface.
