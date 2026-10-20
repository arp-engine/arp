# NamedVariantRootSpace/1

Status: Contract Complete; native consumer verification pending.

Named GLB variant import accepts an explicit geometry-space intent. `Scene`
retains existing behavior: bake the complete node world transform into geometry.
`RootRelative` bakes the same authored rotation and scale, but subtracts the
world-space position of the mesh-bearing node's local origin. It preserves the
node root as (0,0,0), including for nested nodes. It does not infer a root from
bounds, lowest vertices, filenames or empirical offsets. `NamedVariant` retains
the complete source `worldTransform` for provenance in either mode.

PlantAssetSet and RockAssetSet are independent instance templates whose contracts
declare a root-contact / explicit embed anchor. They explicitly request
RootRelative on initial and incremental LOD loads. Generic scene import and
NamedMeshVariantSet retain Scene space. Authored offsets inside mesh positions
are untouched; there is no guessed recentering or ground-height adjustment.
Invalid geometry-space intent fails with `ASSET-NAMED-VARIANT-SPACE-INVALID`.

This is an allocation-neutral CPU import conversion, Equivalent on Null, Vulkan,
Metal and WebGPU; no RHI lowering, matrix upload ABI, native state or lifetime
changes. Existing importer-private GLB column-major parsing remains internal and
is never used as a shader-facing matrix representation.

Tests compare actual translated multi-variant plant geometry in both modes:
every vertex delta equals the explicit node origin; indices, normal/tangent/UV,
wind bytes and source transforms are unchanged. Native WoCC smoke/capture must
confirm roots no longer inherit display-layout translations before acceptance.
