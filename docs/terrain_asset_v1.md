# TerrainAsset v1

## Status

- Contract Complete: TerrainSurface, `terrain.asset/v1` YAML manifest, fixed binary tile layouts, typed ResourceManager loading, deterministic PCG baking, explicit world-bound scene preparation, static vegetation page lifecycle, TerrainAsset tile read views, GPU tile/page-table lifecycle, tile-local PBR page-table shader ABI, and focused contract tests are implemented. The RHI lowerer creates height/weight texture arrays, uploads an explicit tile layer, publishes a complete immutable page-table generation, keeps prior page-table and texture-layer generations through the strict submission boundary, and writes the four PBR bindings (weight A/B, page table, page-table parameters) from the submission's explicit page-table snapshot. Snapshot capacity is bounded and rejects publication until the completed submission serial reaps an old generation. Physics consumes the same TerrainSurface through an explicit rectangular-bounds adapter; unavailable data rejects the replacement and leaves the prior native Jolt collider live. DDGI materializes the same immutable TerrainSurface into an explicit rectangular R32 height grid, uploads it through a backend-owned texture payload, and samples it through binding 7; its min/max hierarchy is derived from those exact source texels rather than an analytic height generator. The obsolete analytic DDGI terrain payload is removed. Static vegetation retains the fixed 48-byte asset record and is strictly lowered to the established 64-byte cull/instance ABI; unsupported non-uniform X/Z scale, invalid rotation, or an unrepresentable per-page instance ordinal fail the page.
- Backend Validated: partially complete. Jolt validates the TerrainSurface collider path. Null validates the terrain texture upload, page-table and descriptor sequence, the static-vegetation cull-ABI buffer creation, plus the DDGI baked-height payload and the hidden `unit-ddgi` execution chain. Vulkan hidden-window smoke validates native array-image creation, two explicit array-layer uploads, page-table buffer writes, PBR page descriptor writes, and the full-array descriptor-layout initialization required by Vulkan validation. WoCC explicitly loads and binds the baked TerrainAsset/catalog; GameScene selects its immutable surface for physics, player movement, authoritative-server movement, Foot IK, script height queries and DDGI. RenderSession uploads active tile pages, captures an immutable page-table snapshot into the render submission, and lowers catalog-validated static instance pages to the established instance draw ABI. The PBR path refuses terrain draws without that captured page table. On 2026-08-31, script-scene Vulkan submitted eight frames and captured frame 2 at `D:/wocc/build/win-auto-debug/terrain_asset_vulkan_backend_validated_capture.rdc`. The paired page log `D:/wocc/build/win-auto-debug/terrain_asset_vulkan_backend_validated.err.log` records generation 1 through 7 with every active tile published (`staged=1`, `complete=1`, `ready=1`), from two through ten asset pages and 59 through 258 lowered instances, with no Vulkan error or undefined-layout diagnostic. The repeatable replay probe `D:/wocc/tools/renderdoc_terrain_asset_probe.py` wrote `D:/wocc/build/win-auto-debug/terrain_asset_renderdoc_evidence.txt`: the terrain fragment reflection binds `WeightPageA`/`WeightPageB` at 24/25 and `TerrainPageTable`/`TerrainPageTableConfig` at 29/30; both bound weight arrays are 129 x 129 x 16; the captured page table has three entries on layers 0, 1 and 2 with bounds [-300,-150] x [-300,-150], [-300,-150] x [-150,0], and [-150,0] x [-300,-150]; and the last color target before present is 2048 x 1152 with sampled average luminance 0.7917. The PBR event does not bind height pages by design, so RenderDoc cannot identify that array at this event; native upload smoke and the shared TerrainSurface/DDGI contracts remain the height-page evidence. Likewise, the capture gives generic resource labels and no visible foliage draw, so it cannot uniquely identify a static-vegetation instance page. RenderDoc's extracted thumbnail at `D:/wocc/build/win-auto-debug/terrain_asset_vulkan_backend_validated_thumbnail.png` is a non-black submitted Vulkan frame containing terrain geometry, distant terrain, water, character, campfire and HUD. This is Vulkan backend evidence for TerrainAsset page upload, page-table publication and PBR descriptor consumption. It does not validate terrain visual quality or visible catalog vegetation in this camera, which remain unvalidated visual claims.
- Platform Validated: not complete. Metal and WebGPU do not have equivalent TerrainAsset page-table or instance payloads.
- Production Ready: not complete.

## Ownership

`TerrainAssetResource` owns immutable baked heights, two RGBA8 weight pages,
and static vegetation layout. It validates resource IDs, catalog prototypes,
material dependencies, tile coverage, block sizes, hashes, binary formats,
vegetation bounds, and shared height borders before publication.

`TerrainSurface` is the only CPU query interface. Queries outside the declared
bounds, against absent tiles, or against invalid payloads return an explicit
status and never clamp to a fabricated terrain value.

GameScene resolves the surface from its authored TerrainComponent resource ID.
If a TerrainAsset is authored but not ready, it does not fall back to
WoccTerrainProvider: physics preparation fails, DDGI is disabled with a stable
diagnostic, and movement rejects unavailable tiles. The analytic heightfield
remains only an explicit Legacy adapter for scenes without a TerrainAsset ID,
with finite declared scene bounds.

`TerrainComponent` serializes only `terrainResourceId`. `TerrainSceneBinding`
resolves it during scene preparation to private terrain/material handles and
metadata derived from the resource. It publishes an owning surface snapshot,
so a ResourceManager reload cannot invalidate the prior submitted generation.
TerrainRuntime uses the asset's declared bounds as its chunk origin and rejects
an extent that is not exactly divisible by the configured chunk size.

Static vegetation instances contain only prototype ID, transform, scale, and
variation seed. Catalog resources own model/material/behavior meaning; GPU
residency, LOD, wind, collision creation, and retirement remain runtime work.
`TerrainVegetationRuntime` consumes active asset tile pages only with the
ready `VegetationCatalogResource` named by the asset manifest, validates every
prototype before publication, groups immutable instances by prototype, and
serially retires replaced or deactivated pages.
Interactive or replicated objects remain prefab/scene/game-state data.

`TerrainGpuRuntime` carries each validated height tile and its two weight pages
through an explicit upload callback for backend-independent use, or through
`TerrainGpuRhiUploader` for the concrete RHI path. The latter always uploads a
candidate to an unleased texture-array layer and publishes a complete immutable
page-table generation. Replaced layers and page-table buffers remain leased
until completion is strictly greater than their retirement serial. A render
submission captures `PageTableSnapshot` and passes it to the PBR descriptor
adapter, which binds only that snapshot's validated page resources at bindings
24, 25, 29 and 30. It never inserts placeholder resources. The shader uses table
bounds and the texture-array layer to compute local UVs. A missing page discards
the terrain fragment; it never samples a clamped global texture as a
compatibility fallback.
