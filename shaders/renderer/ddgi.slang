// Engine Dynamic Diffuse Global Illumination shader.
#define DDGI_PROBE_STATE_BUFFER RWStructuredBuffer<DdgiSamplingProbeState>
#include "renderer/ddgi_sampling.slang"
#include "renderer/ddgi_depth_cache.slang"
//
// Industrial-grade probe-grid GI with:
//   - Octahedral-encoded directional irradiance per probe (8x8 map per probe)
//   - Per-direction depth moments (mean + mean^2) in a dedicated Rgba16Float atlas
//     -> Chebyshev upper-tail ambient occlusion with valid variance E[d^2]-E[d]^2
//   - Temporal hysteresis (new trace blended with previous frame via ping-pong)
//   - Single-bounce lighting (terrain hit samples previous-frame probe irradiance)
//   - Baked TerrainSurface height-texture ray-march
//   - Analytic sky radiance (replicates sky.slang fragmentMain)
//   - Half-resolution screen-space resolve (depth + inverse view-projection)
//
// Two compute entry points:
//   giProbeUpdateComputeMain  - per probe-texel: trace a bounded ray set, update irradiance + moments
//   giIrradianceResolveComputeMain - per half-res pixel: trilinear-blend 8 probes + AO
//
// Matrix layout contract (AGENTS.md hard constraint):
//   invViewProjection is a DEFAULT float4x4 (no row_major / column_major keyword).
//   Slang default float4x4 -> SPIR-V RowMajor decoration, which matches the
//   row-major C++ data filled by core::inverse(core::multiply(proj, view)).
//   Declaring `row_major` here would emit SPIR-V ColMajor and re-transpose the
//   data, producing a wrong inverse - forbidden by the matrix layout contract.
//   The explanatory comment lives ABOVE the struct so reflectedPushConstantSize
//   (which parses struct members off ';'-terminated statements and does not
//   skip // comments) does not absorb it into a field's type token.

// Phase 4: scene BVH parameters packed into sceneBvhParams below.
//   xy = static leaf/node counts; zw = dynamic leaf/node counts. Flat geometry
//   occupies the static tree with an identity instance table. Frame index and
//   proxy statistics are not traversal counts. See DdgiSharedBvhInput/1.
struct DdgiPushConstants
{
    float4x4 invViewProjection;
    float4 probeGridOriginHysteresis;
    float4 probeGridSpacingStrength;
    float4 probeCountRayCount;
    float4 probeTextureSizeBounceMaxRay;
    float4 sunDirectionSkyAmbient;
    float4 diagnosticParams;
    float4 sceneBvhParams;
    float4 directionalLightRadiance;
    float4 scrollOffsetFrame;
    float4 updateParams;
};

[[vk::push_constant]]
DdgiPushConstants gDdgi;

// Shared scene-sky contract. The sky/lighting system owns these values and
// uploads them in pre-exposed HDR space; DDGI only samples the contract.
struct DdgiSkyEnvironmentData
{
    float4 horizonRadiance;
    float4 zenithRadiance;
    float4 sunRadiance;
    float4 sunDirection;
    float4 angularParams;
    float4 preExposure;
};

[[vk::binding(19, 0)]]
ConstantBuffer<DdgiSkyEnvironmentData> DdgiSkyEnvironment;

// Main directional-light cascaded shadow map. Probe hits select the first
// cascade whose projected coordinates are valid; when no cascade covers the
// point, the caller retains the software BVH shadow path as the deterministic
// outside-range fallback.
struct DdgiCsmShadowData
{
    float4x4 lightViewProjection[3];
    float4 cascadeSplits;
    float4 atlasUvTransforms[3];
    float4 shadowParams;
};

[[vk::binding(28, 0)]]
ConstantBuffer<DdgiCsmShadowData> DdgiCsmShadow;

#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name

[[vk::binding(29, 0)]]
ARP_SAMPLED_DEPTH(DdgiShadowMap);

[[vk::binding(30, 0)]]
SamplerState DdgiShadowSampler;

struct DdgiLocalLightData
{
    float4 positionAndRange;
    float4 radianceAndIntensity;
};

[[vk::binding(20, 0)]]
StructuredBuffer<DdgiLocalLightData> DdgiLocalLights;

[[vk::binding(21, 0)]]
StructuredBuffer<uint> ProbeLocalLightInfluences;

// Emissive radiance is indexed by the serialized BVH leaf index. It is kept
// separate from GpuBvhLeaf so the existing triangle ABI remains stable.
[[vk::binding(22, 0)]]
StructuredBuffer<float4> SceneBvhEmissive;

[[vk::binding(32, 0)]]
StructuredBuffer<uint> DdgiSceneMaterials;

[[vk::binding(0, 0)]]
RWTexture2D<float4> ProbeIrradianceOut;

[[vk::binding(1, 0)]]
Texture2D<float4> ProbeIrradianceHistory;

[[vk::binding(2, 0)]]
SamplerState ProbeSampler;

// Phase 2 depth moments atlas (Rgba16Float): r = mean depth, g = mean depth
// squared. The extra lanes are padding for the portable storage format.
// Separated from the irradiance atlas so variance = E[d^2] - E[d]^2 yields a
// mathematically valid Chebyshev upper-tail bound.
[[vk::binding(3, 0)]]
RWTexture2D<float4> ProbeDepthMomentsOut;

[[vk::binding(8, 0)]]
Texture2D<float4> ProbeDepthMomentsHistory;

[[vk::binding(4, 0)]]
RWTexture2D<float4> GiIrradianceOutput;

[[vk::binding(5, 0)]]
ARP_SAMPLED_DEPTH(SceneDepth);

// Depth32Float is a depth resource on WebGPU. Keep this read integer-texel
// based so the generated WGSL uses texture_depth_2d/textureLoad rather than
// pairing a filtering sampler with a regular float texture.
float sampleSceneDepth(float2 uv)
{
    uint width, height;
    SceneDepth.GetDimensions(width, height);
    int2 pixel = int2(clamp(
        uv * float2(float(width), float(height)),
        float2(0.0, 0.0),
        float2(float(width - 1u), float(height - 1u))));
    return SceneDepth.Load(int3(pixel, 0)).r;
}

// ---------------------------------------------------------------------------
// Phase 3: Per-probe relocation state (ProbeStateBuffer, binding 9).
// Authored by the giProbeRelocateComputeMain compute pass and read by
// probe-update + resolve via probeWorldPos(). Replaces the Prototype
// terrainHeight+1.5 in-shader lift - relocation decisions live in a dedicated
// pass, consumer shaders never mutate probe positions.
//
// Layout matches C++ renderer::ProbeState in ddgi_renderer.h:
//   offset.xyz = world-space relocation offset added to the grid-aligned base
//                position (0,0,0 = grid-aligned, no relocation)
//   status     = packed: bits 0-3 = relocReason (see ProbeRelocReason below),
//                bits 4-7 = backoffSteps (escape iterations used), bits 8+ flags
//                (bit 8 = ScrollNewCell, forces history reset in update pass).
// ---------------------------------------------------------------------------

// Relocation reason codes (packed into ProbeState.status bits 0-3).
static const uint kProbeRelocReason_None           = 0u;
static const uint kProbeRelocReason_InsideTerrain  = 1u;
static const uint kProbeRelocReason_ScrollNewCell  = 2u;
static const uint kProbeRelocReason_SteepSlope     = 3u;
static const uint kProbeRelocReason_MaxBackoff     = 4u;

static const uint kProbeScrollNewCellFlag = 0x100u;

typedef DdgiSamplingProbeState ProbeState;

[[vk::binding(9, 0)]]
RWStructuredBuffer<ProbeState> ProbeStateBuffer;

// ---------------------------------------------------------------------------
// Phase 4: Scene geometry ray query via software BVH (bindings 10, 11).
// CPU-built flat AABB tree uploaded as two SSBOs and traversed in the probe
// update pass with M枚ller-Trumbore ray-triangle intersection. Replaces the
// baked-terrain-only marchTerrain() with full-scene coverage (terrain +
// static meshes). Water is excluded (transparent, not a GI occluder).
//
// GpuBvhNode (32 bytes, std430):
//   float3 aabbMin; uint leftChild;   // internal: left child index
//                                       leaf:     kInvalidLeafMarker (0xFFFFFFFF)
//   float3 aabbMax; uint rightChild;  // internal: right child index
//                                       leaf:     leafStart in SceneBvhLeaves
// The current BVH builder (bvh.cpp buildRecursive) always splits down to
// count <= 1, so every leaf node references exactly one triangle. The GPU
// traversal exploits this and does not need a separate leaf-count array.
//
// GpuBvhLeaf (96 bytes, std430): triangle v0/v1/v2 + face
// normal + materialIdx + three UVs. Aligned to 16 bytes per component for
// std430; the UV lanes are consumed by texture-backed material albedo.
// ---------------------------------------------------------------------------

static const uint kBvhInvalidLeafMarker = 0xFFFFFFFFu;

struct GpuBvhNode {
    float3 aabbMin;
    uint   leftChild;
    float3 aabbMax;
    uint   rightChild;
};

struct GpuBvhLeaf {
    float3 v0;
    float  _pad0;
    float3 v1;
    float  _pad1;
    float3 v2;
    float  _pad2;
    float3 normal;
    uint   materialIdx;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    float2 _uvPad;
};

[[vk::binding(10, 0)]]
StructuredBuffer<GpuBvhNode> SceneBvhNodes;

[[vk::binding(11, 0)]]
StructuredBuffer<GpuBvhLeaf> SceneBvhLeaves;

// Explicit split path: binding 10/11 are the persistent static BLAS, while
// 24/25 are the dynamic proxy TLAS. Emissive records stay leaf-indexed in
// separate buffers and binding 27 carries the instance table contract.
[[vk::binding(24, 0)]]
StructuredBuffer<GpuBvhNode> DynamicTlasNodes;

[[vk::binding(25, 0)]]
StructuredBuffer<GpuBvhLeaf> DynamicTlasLeaves;

[[vk::binding(26, 0)]]
StructuredBuffer<float4> DynamicTlasEmissive;

[[vk::binding(27, 0)]]
StructuredBuffer<float4> SceneBvhInstances;

// Split probe stages use a bounded storage-buffer transient rather than
// reading a storage texture. WebGPU storage textures are write-only in WGSL;
// this keeps the same ABI executable on all backends.
[[vk::binding(31, 0)]]
RWStructuredBuffer<float4> ProbeTraceRecords;

// CPU-authored, hard-budgeted logical probe indices. The scheduler preserves
// physical-slot age across ring scrolling and orders new/invalid, starvation,
// near-camera, revision-changed and oldest probes deterministically.
[[vk::binding(17, 0)]]
StructuredBuffer<uint> ProbeUpdateList;

[[vk::binding(18, 0)]]
Texture2D<float4> SceneNormal;

float4 sampleSceneNormal(float2 uv)
{
    uint width, height;
    SceneNormal.GetDimensions(width, height);
    int2 pixel = int2(clamp(
        uv * float2(float(width), float(height)),
        float2(0.0, 0.0),
        float2(float(width - 1u), float(height - 1u))));
    return SceneNormal.Load(int3(pixel, 0));
}

// DDGI material contract: fixed semantic categories are bound explicitly by
// the C++ pipeline. This keeps backend differences in RHI descriptor lowering
// and avoids descriptor arrays, which have stricter WebGPU rules.
[[vk::binding(12, 0)]]
Texture2D<float4> DdgiTerrainAlbedo;

[[vk::binding(13, 0)]]
Texture2D<float4> DdgiStaticAlbedo;

[[vk::binding(14, 0)]]
Texture2D<float4> DdgiPlayerAlbedo;

[[vk::binding(15, 0)]]
Texture2D<float4> DdgiRoadAlbedo;

[[vk::binding(16, 0)]]
SamplerState DdgiMaterialSampler;

// ---------------------------------------------------------------------------
// Baked TerrainSurface height. The CPU uploads one immutable R32_SFLOAT grid
// and records its exact rectangular bounds and dimensions in the hierarchy.
// ---------------------------------------------------------------------------

[[vk::binding(7, 0)]]
Texture2D<float> BakedTerrainHeight;

// Records 0/1 contain rectangular world bounds and source dimensions;
// remaining records are the conservative min/max hierarchy.
[[vk::binding(23, 0)]]
StructuredBuffer<float4> TerrainHeightMinMax;

bool terrainContains(float x, float z)
{
    const float4 bounds = TerrainHeightMinMax[0];
    return x >= bounds.x && x <= bounds.z && z >= bounds.y && z <= bounds.w;
}

float terrainSample(float x, float z)
{
    const float4 bounds = TerrainHeightMinMax[0];
    const uint samplesX = asuint(TerrainHeightMinMax[1].x);
    const uint samplesZ = asuint(TerrainHeightMinMax[1].y);
    const float2 source = float2(
        (x - bounds.x) / (bounds.z - bounds.x) * float(samplesX - 1u),
        (z - bounds.y) / (bounds.w - bounds.y) * float(samplesZ - 1u));
    const uint2 lower = min(uint2(floor(source)), uint2(samplesX - 2u, samplesZ - 2u));
    const float2 fraction = source - float2(lower);
    const float h00 = BakedTerrainHeight.Load(int3(lower, 0));
    const float h10 = BakedTerrainHeight.Load(int3(lower + uint2(1u, 0u), 0));
    const float h01 = BakedTerrainHeight.Load(int3(lower + uint2(0u, 1u), 0));
    const float h11 = BakedTerrainHeight.Load(int3(lower + uint2(1u, 1u), 0));
    return lerp(lerp(h00, h10, fraction.x), lerp(h01, h11, fraction.x), fraction.y);
}

// Terrain albedo by height remains a prototype material response until DDGI
// consumes the TerrainAsset weight pages and material resources directly.
float3 terrainAlbedoByHeight(float y)
{
    if (y < 0.1)  return float3(0.76, 0.70, 0.50); // sand/water edge
    if (y < 2.0)  return float3(0.35, 0.55, 0.25); // grass
    if (y < 8.0)  return float3(0.45, 0.42, 0.38); // rock
    return float3(0.85, 0.85, 0.88);               // snow
}

// ---------------------------------------------------------------------------
// Shared sky radiance contract. All colors, angular falloff, and pre-exposure
// arrive through DdgiSkyEnvironment; this shader contains no sky tuning data.
// ---------------------------------------------------------------------------

float3 skyRadiance(float3 dir)
{
    float3 sunDir = normalize(DdgiSkyEnvironment.sunDirection.xyz);
    float3 horizonColor = DdgiSkyEnvironment.horizonRadiance.rgb;
    float3 zenithColor = DdgiSkyEnvironment.zenithRadiance.rgb;
    float3 sunColor = DdgiSkyEnvironment.sunRadiance.rgb;
    float glowPower = max(DdgiSkyEnvironment.angularParams.x, 0.0001);
    float glowStrength = DdgiSkyEnvironment.angularParams.y;
    float corePower = max(DdgiSkyEnvironment.angularParams.z, 0.0001);
    float coreStrength = DdgiSkyEnvironment.angularParams.w;
    float preExposure = max(DdgiSkyEnvironment.preExposure.x, 0.0001);
    float horizonPower = max(DdgiSkyEnvironment.preExposure.y, 0.0001);

    float heightFactor = saturate(dir.y);
    heightFactor = pow(heightFactor, horizonPower);

    float3 skyColor = lerp(horizonColor, zenithColor, heightFactor);

    float d = max(dot(dir, sunDir), 0.0);
    float sunAmt = pow(d, glowPower);
    float3 warmGlow = sunColor * sunAmt * glowStrength;

    float sunCore = pow(d, corePower);
    float3 coreGlow = sunColor * sunCore * coreStrength;

    return (skyColor + warmGlow + coreGlow) * preExposure;
}

uint terrainHierarchyBaseResolution()
{
    return 64u;
}

uint terrainHierarchyLevelCount()
{
    return asuint(TerrainHeightMinMax[1].z);
}

uint terrainHierarchyLevelResolution(uint level)
{
    uint baseResolution = terrainHierarchyBaseResolution();
    return max(1u, baseResolution >> level);
}

uint terrainHierarchyLevelOffset(uint level)
{
    uint offset = 2u;
    uint resolution = terrainHierarchyBaseResolution();
    for (uint current = 0u; current < level; ++current) {
        offset += resolution * resolution;
        resolution = max(1u, resolution >> 1u);
    }
    return offset;
}

bool terrainHeightMinMaxAt(
    float x, float z, uint level, out float minHeight, out float maxHeight)
{
    const float worldMinX = TerrainHeightMinMax[0].x;
    const float worldMinZ = TerrainHeightMinMax[0].y;
    const float worldMaxX = TerrainHeightMinMax[0].z;
    const float worldMaxZ = TerrainHeightMinMax[0].w;
    if (level >= terrainHierarchyLevelCount()
        || x < worldMinX || z < worldMinZ
        || x > worldMaxX || z > worldMaxZ) {
        minHeight = 0.0;
        maxHeight = 0.0;
        return false;
    }
    const uint resolution = terrainHierarchyLevelResolution(level);
    const float2 normalized = saturate((float2(x, z) - float2(worldMinX, worldMinZ))
        / float2(worldMaxX - worldMinX, worldMaxZ - worldMinZ));
    const uint2 cell = min(
        uint2(normalized * float(resolution)), uint2(resolution - 1u));
    const float4 record = TerrainHeightMinMax[
        terrainHierarchyLevelOffset(level) + cell.y * resolution + cell.x];
    minHeight = record.x;
    maxHeight = record.y;
    return true;
}

uint terrainHierarchyLevelForSpan(float span)
{
    const float worldExtent = max(TerrainHeightMinMax[0].z - TerrainHeightMinMax[0].x,
                                  TerrainHeightMinMax[0].w - TerrainHeightMinMax[0].y);
    const uint baseResolution = terrainHierarchyBaseResolution();
    const uint levelCount = terrainHierarchyLevelCount();
    float cellSpan = worldExtent / float(baseResolution);
    uint level = 0u;
    while (level + 1u < levelCount && span > cellSpan * 2.0) {
        cellSpan *= 2.0;
        ++level;
    }
    return level;
}

// ---------------------------------------------------------------------------
// Octahedral encoding - maps a unit direction to [0,1]^2 and back.
// Compact, hardware-bilinear-friendly direction parameterization for probe
// irradiance maps. 8x8 texels per probe cover the full sphere.
// ---------------------------------------------------------------------------

float2 octEncode(float3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 0.0001);
    float2 projected;
    if (n.z >= 0.0) {
        projected = n.xy;
    } else {
        float2 s = select(n.xy >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
        projected = (1.0 - abs(n.yx)) * s;
    }
    return projected * 0.5 + 0.5;
}

float3 octDecode(float2 f)
{
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        float2 s = select(n.xy >= 0.0, float2(1.0, 1.0), float2(-1.0, -1.0));
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

// ---------------------------------------------------------------------------
// Cosine-weighted hemisphere sampling - Fibonacci spiral + Malley's method.
// Deterministic (no random): the same (index, count, normal) always yields the
// same ray, so probe irradiance is temporally stable without per-frame jitter.
// ---------------------------------------------------------------------------

float3 cosineWeightedHemisphereSample(
    uint index, uint count, float3 normal, uint probeIndex, uint frameIndex)
{
    // Fibonacci spiral on the unit disk.
    float golden = 1.6180339;
    float r = sqrt(float(index + 1u) / float(count));
    uint rotationHash = probeIndex * 747796405u + frameIndex * 2891336453u;
    float rotation = float(rotationHash & 0xffffu) / 65536.0;
    float theta = 6.2831853 * (float(index) * golden + rotation);
    float2 disk = float2(r * cos(theta), r * sin(theta));
    // Malley: lift disk point onto the hemisphere above `normal`.
    float z = sqrt(max(1.0 - r * r, 0.0));
    // Build tangent basis around `normal`.
    float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(disk.x * tangent + disk.y * bitangent + z * normal);
}

float3 sphericalFibonacciSample(
    uint index, uint count, uint probeIndex, uint frameIndex)
{
    float sampleIndex = float(index) + 0.5;
    float z = 1.0 - 2.0 * sampleIndex / float(max(count, 1u));
    float radius = sqrt(max(1.0 - z * z, 0.0));
    uint rotationHash = probeIndex * 747796405u + frameIndex * 2891336453u;
    float rotation = float(rotationHash & 0xffffu) / 65536.0;
    float azimuth = 6.2831853 * (sampleIndex * 0.61803398875 + rotation);
    return float3(radius * cos(azimuth), z, radius * sin(azimuth));
}

// ---------------------------------------------------------------------------
// Chebyshev upper-tail ambient occlusion.
// Given the probe's mean depth and mean depth squared in a direction, plus the
// fragment's distance to that probe, returns a visibility weight in [0,1]. If
// the fragment is closer than the mean, the probe sees past it (no occlusion,
// weight=1). If farther, the surface between probe and fragment occludes
// (weight < 1). This is the canonical DDGI soft-shadow approximation.
// Phase 2: variance is now computed from the dedicated Rgba16Float moments atlas
// (E[d^2] - E[d]^2) instead of the previous Prototype that guessed variance
// from mean depth alone.
// ---------------------------------------------------------------------------

float chebyshevWeight(float meanDepth, float meanDepthSq, float fragDist)
{
    if (fragDist <= meanDepth) {
        return 1.0;
    }
    float variance = max(meanDepthSq - meanDepth * meanDepth, 0.0001);
    float diff = fragDist - meanDepth;
    return variance / (variance + diff * diff);
}

// ---------------------------------------------------------------------------
// Probe grid helpers.
// Probe grids are packed into independent 2D atlases. Irradiance uses an 8x8
// physical tile with a 6x6 interior; distance uses a 16x16 physical tile with
// a 14x14 interior. The irradiance atlas (RGBA16F) stores rgb
// irradiance; its alpha channel is unused (Phase 2 moved depth moments to a
// dedicated Rgba16Float atlas so variance is mathematically valid). The moments
// atlas packs (mean depth, mean depth^2) per direction, consumed by the
// Chebyshev AO in the resolve.
// ---------------------------------------------------------------------------

static const uint kIrradiancePhysicalTexels = 8u;
static const uint kIrradianceInteriorTexels = 6u;
static const uint kDistancePhysicalTexels = 16u;
static const uint kDistanceInteriorTexels = 14u;
static const uint kMaximumRaysPerProbe = 256u;
static const uint kMaximumLocalLights = 128u;
static const uint kMaximumLocalLightsPerProbe = 8u;

uint3 logicalToPhysicalProbe(uint3 gridCoord)
{
    uint3 count = uint3(gDdgi.probeCountRayCount.xyz);
    uint3 ringOffset = uint3(gDdgi.scrollOffsetFrame.xyz);
    return (gridCoord + ringOffset) % count;
}

uint3 probeIndexToGrid(uint linearIndex)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    // countZ = probeCountRayCount.z
    uint z = linearIndex / (countX * countY);
    uint rem = linearIndex - z * (countX * countY);
    uint y = rem / countX;
    uint x = rem - y * countX;
    return uint3(x, y, z);
}

// Inverse of probeIndexToGrid: pack a 3D grid coordinate into the linear
// probe index used by ProbeStateBuffer. Shared by probeWorldPos (read offset)
// and giProbeRelocateComputeMain (write offset) so both passes agree on the
// buffer layout.
uint probeGridToIndex(uint3 gridCoord)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint3 physical = logicalToPhysicalProbe(gridCoord);
    return physical.x
         + physical.y * countX
         + physical.z * countX * countY;
}

float3 probeWorldPos(uint3 gridCoord)
{
    float3 position = gDdgi.probeGridOriginHysteresis.xyz
                    + float3(gridCoord) * gDdgi.probeGridSpacingStrength.xyz;
    // Phase 3: relocation offset is authored by the giProbeRelocateComputeMain
    // pass into ProbeStateBuffer (binding 9). Consumer shaders (update +
    // resolve) read the offset here and NEVER mutate the probe position -
    // the relocation decision lives entirely in the dedicated pass. This
    // replaces the Prototype terrainHeight+1.5 in-shader lift.
    uint linearIdx = probeGridToIndex(gridCoord);
    return position + ProbeStateBuffer[linearIdx].offset;
}

float2 probeAtlasUv(
    uint3 gridCoord,
    float3 dir,
    uint physicalTexels,
    uint interiorTexels,
    uint2 atlasSize)
{
    uint countY = uint(gDdgi.probeCountRayCount.y);
    float2 octUv = octEncode(dir);
    uint3 physical = logicalToPhysicalProbe(gridCoord);
    float2 tileOrigin = float2(
        physical.x * physicalTexels,
        (physical.y + physical.z * countY) * physicalTexels);
    float2 interiorTexel = 1.5 + octUv * float(interiorTexels - 1u);
    return (tileOrigin + interiorTexel) / float2(atlasSize);
}

uint2 octahedralBorderSource(uint2 coord, uint physicalTexels, uint interiorTexels)
{
    uint last = physicalTexels - 1u;
    if (all(coord == 0u.xx)) return interiorTexels.xx;
    if (coord.x == last && coord.y == 0u) return uint2(1u, interiorTexels);
    if (coord.x == 0u && coord.y == last) return uint2(interiorTexels, 1u);
    if (all(coord == last.xx)) return 1u.xx;
    if (coord.y == 0u) return uint2(last - coord.x, 1u);
    if (coord.y == last) return uint2(last - coord.x, interiorTexels);
    if (coord.x == 0u) return uint2(1u, last - coord.y);
    if (coord.x == last) return uint2(interiorTexels, last - coord.y);
    return coord;
}

float2 probeIrradianceUv(uint3 gridCoord, float3 dir)
{
    uint width, height;
    ProbeIrradianceHistory.GetDimensions(width, height);
    return probeAtlasUv(gridCoord, dir, kIrradiancePhysicalTexels,
        kIrradianceInteriorTexels, uint2(width, height));
}

float2 probeDistanceUv(uint3 gridCoord, float3 dir)
{
    uint width, height;
    ProbeDepthMomentsHistory.GetDimensions(width, height);
    return probeAtlasUv(gridCoord, dir, kDistancePhysicalTexels,
        kDistanceInteriorTexels, uint2(width, height));
}

// Sample the probe irradiance history at a world position + surface normal.
// Used by the probe update for single-bounce lighting at terrain hit points.
float3 sampleProbeIrradiance(float3 worldPos, float3 normal)
{
    DdgiSamplingConstants constants;
    constants.originNormalBias = float4(gDdgi.probeGridOriginHysteresis.xyz, gDdgi.diagnosticParams.z);
    constants.spacingViewBias = float4(gDdgi.probeGridSpacingStrength.xyz, gDdgi.diagnosticParams.w);
    constants.probeCount = uint4(gDdgi.probeCountRayCount.xyz, 0u);
    constants.ringOffset = int4(gDdgi.scrollOffsetFrame.xyz, 0);
    return sampleDdgiProbeVolume(ProbeIrradianceHistory, ProbeDepthMomentsHistory,
        ProbeSampler, ProbeStateBuffer, constants, worldPos, normal, normal).rgb;
}

float3 sampleLocalLightRadiance(
    uint probeLinearIndex, float3 hitPos, float3 hitNormal)
{
    float3 radiance = 0.0.xxx;
    uint influenceBase = probeLinearIndex * kMaximumLocalLightsPerProbe;
    for (uint slot = 0u; slot < kMaximumLocalLightsPerProbe; ++slot) {
        uint lightIndex = ProbeLocalLightInfluences[influenceBase + slot];
        if (lightIndex >= kMaximumLocalLights) continue;
        DdgiLocalLightData light = DdgiLocalLights[lightIndex];
        float3 toLight = light.positionAndRange.xyz - hitPos;
        float distanceSquared = dot(toLight, toLight);
        float range = light.positionAndRange.w;
        if (distanceSquared <= 0.0001 || distanceSquared >= range * range) continue;
        float distance = sqrt(distanceSquared);
        float3 lightDirection = toLight / distance;
        float attenuation = saturate(1.0 - distance / range);
        attenuation *= attenuation;
        radiance += light.radianceAndIntensity.rgb
            * light.radianceAndIntensity.w
            * max(dot(hitNormal, lightDirection), 0.0)
            * attenuation;
    }
    return radiance;
}

// ---------------------------------------------------------------------------
// Terrain ray-march. Steps along the ray and tests whether the analytic
// terrain heightfield rises to meet the ray (ray penetrates below surface).
// Returns hit distance (or -1 if sky). Covers the full [-50,+50]^2 world.
// ---------------------------------------------------------------------------

float marchTerrain(float3 origin, float3 dir, float maxDist)
{
    const uint kSteps = 40u;
    float stepLen = maxDist / float(kSteps);
    float previousT = 0.0;
    for (uint i = 1u; i <= kSteps; ++i) {
        float t = stepLen * float(i);
        float3 p = origin + dir * t;
        // Only test within the world extent.
        const float hierarchyWorldMinX = TerrainHeightMinMax[0].x;
        const float hierarchyWorldMinZ = TerrainHeightMinMax[0].y;
        const float hierarchyWorldMaxX = TerrainHeightMinMax[0].z;
        const float hierarchyWorldMaxZ = TerrainHeightMinMax[0].w;
        if (p.x < hierarchyWorldMinX
            || p.x > hierarchyWorldMaxX
            || p.z < hierarchyWorldMinZ
            || p.z > hierarchyWorldMaxZ) {
            return -1.0; // outside world = sky
        }
        const float segmentSpan = max(abs(dir.x), abs(dir.z)) * (t - previousT);
        const uint hierarchyLevel = terrainHierarchyLevelForSpan(segmentSpan);
        float minHeight = 0.0;
        float maxHeight = 0.0;
        const bool hasBounds = terrainHeightMinMaxAt(
            (origin.x + p.x) * 0.5, (origin.z + p.z) * 0.5,
            hierarchyLevel, minHeight, maxHeight);
        const float segmentMinY = min(
            origin.y + dir.y * previousT, p.y);
        const float segmentMaxY = max(
            origin.y + dir.y * previousT, p.y);
        if (hasBounds && (segmentMaxY < minHeight || segmentMinY > maxHeight)) {
            previousT = t;
            continue;
        }

        // A hierarchy candidate is refined with a bounded binary search. The
        // midpoint check catches a terrain peak between coarse march samples.
        float hitT = -1.0;
        if (p.y <= terrainSample(p.x, p.z)) {
            hitT = t;
        } else {
            const float midpointT = (previousT + t) * 0.5;
            const float3 midpoint = origin + dir * midpointT;
            if (midpoint.y <= terrainSample(midpoint.x, midpoint.z)) {
                hitT = midpointT;
            }
        }
        if (hitT >= 0.0) {
            float lo = previousT;
            float hi = hitT;
            for (uint iteration = 0u; iteration < 6u; ++iteration) {
                const float mid = (lo + hi) * 0.5;
                const float3 samplePoint = origin + dir * mid;
                if (samplePoint.y <= terrainSample(samplePoint.x, samplePoint.z)) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            return hi;
        }
        previousT = t;
    }
    return -1.0;
}

// ---------------------------------------------------------------------------
// Phase 4: Scene geometry ray query via software BVH.
// M枚ller-Trumbore ray-triangle intersection + slab-method ray-AABB test +
// iterative stackless (well, fixed-stack) BVH traversal. Replaces the
// baked-terrain-only marchTerrain() with full-scene coverage when the
// BVH SSBOs are populated (gDdgi.sceneBvhParams.x > 0). When the BVH is
// empty (first frame, capability lowering, or no static geometry), the
// update pass falls back to marchTerrain() so DDGI never goes dark.
// ---------------------------------------------------------------------------

// Material albedo lookup. The fixed table uses:
//   materialIdx == 0          -> terrain (procedural height response)
//   materialIdx == 1          -> static/vegetation authored albedo
//   materialIdx == 2          -> player authored albedo
//   materialIdx == 3          -> road authored albedo
//   0x80000000                 -> vegetation proxy AABB
//   0x80000001                 -> dynamic player proxy AABB (data fallback)
//   0x80000002                 -> generic dynamic creature proxy AABB
#include "renderer/ddgi_scene_materials.slang"
float3 materialAlbedo(uint materialIdx, float3 hitPos, float2 uv)
{
    if (ddgiUsesSceneMaterial(materialIdx)) return ddgiSceneMaterialColor(materialIdx, uv).rgb;
    if (materialIdx == 0x80000000u) {
        // P6-B: vegetation proxy AABB. The proxy does not bind the material
        // system; use a fixed canopy albedo so bounce radiance has a green
        // tint instead of neutral grey.
        return float3(0.18, 0.28, 0.12);
    }
    if (materialIdx == 0x80000001u) {
        // Dynamic player proxy used only when authored skin data is unavailable.
        // Keep the bounce contribution skin/warm-cloth colored and stable.
        return float3(0.46, 0.31, 0.22);
    }
    if (materialIdx == 0x80000002u) {
        return float3(0.34, 0.25, 0.18);
    }
    if (materialIdx == 0u) {
        const float3 authored = DdgiTerrainAlbedo.SampleLevel(
            DdgiMaterialSampler, uv, 0.0).rgb;
        return lerp(authored, terrainAlbedoByHeight(hitPos.y), 0.65);
    } else if (materialIdx == 1u) {
        return DdgiStaticAlbedo.SampleLevel(DdgiMaterialSampler, uv, 0.0).rgb;
    } else if (materialIdx == 2u) {
        return DdgiPlayerAlbedo.SampleLevel(DdgiMaterialSampler, uv, 0.0).rgb;
    } else if (materialIdx == 3u) {
        return DdgiRoadAlbedo.SampleLevel(DdgiMaterialSampler, uv, 0.0).rgb;
    }
    return float3(0.50, 0.50, 0.50); // neutral fallback
}

// Slab-method ray-AABB test. Returns true and sets tNear to the entry point
// (clamped to >= 0) when the ray intersects the AABB within [0, maxDist].
// invDir is 1.0/dir (precomputed by the caller) to avoid per-node division.
#include "renderer/ddgi_scene_bvh_trace.slang"

float sampleDdgiDirectionalCsm(float3 worldPosition)
{
    if (DdgiCsmShadow.shadowParams.x < 0.5) {
        return 1.0;
    }
    const float shadowStrength = saturate(DdgiCsmShadow.shadowParams.z);
    const float depthBias = max(gDdgi.diagnosticParams.z, 0.0001);
    for (uint cascade = 0u; cascade < 3u; ++cascade) {
        float4 clip = mul(
            DdgiCsmShadow.lightViewProjection[cascade],
            float4(worldPosition, 1.0));
        if (clip.w <= 0.0001) continue;
        float3 ndc = clip.xyz / clip.w;
        if (ndc.x < -1.0 || ndc.x > 1.0
            || ndc.y < -1.0 || ndc.y > 1.0
            || ndc.z < 0.0 || ndc.z > 1.0) {
            continue;
        }
        const float4 atlas = DdgiCsmShadow.atlasUvTransforms[cascade];
        float2 uv = (ndc.xy * 0.5 + 0.5) * atlas.xy + atlas.zw;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            continue;
        }
        const float shadowDepth = DdgiShadowMap.SampleLevel(
            DdgiShadowSampler, uv, 0.0).r;
        const float visible = ndc.z - depthBias <= shadowDepth ? 1.0 : 0.0;
        return lerp(1.0, visible, shadowStrength);
    }
    // No cascade covers this probe hit: the software BVH query remains the
    // authoritative outside-range fallback in the caller.
    return 1.0;
}

bool traceSceneBvhShadow(float3 origin, float3 direction, float maxDist)
{
    if (!hasSceneBvhGeometry()) return false;
    const float selfHitThreshold = max(gDdgi.diagnosticParams.z, 0.0001);
    const SceneRayHit blocker = traceSceneBvh(origin, direction, maxDist);
    return blocker.hitDist > selfHitThreshold;
}

float4 traceDdgiRay(
    float3 probePos, uint localIndex, uint rayCount, uint linearIdx,
    uint frameIndex, float maxRayDist, float bounceFactor,
    float skyAmbientStrength)
{
    const float3 rayDir = sphericalFibonacciSample(
        localIndex, rayCount, linearIdx, frameIndex);
    float3 hitNormal = float3(0.0, 1.0, 0.0);
    float3 albedo = terrainAlbedoByHeight(probePos.y);
    float3 emissive = float3(0.0, 0.0, 0.0);
    float hitDist = marchTerrain(probePos, rayDir, maxRayDist);
    bool sceneHitSelected = false;
    if (hasSceneBvhGeometry()) {
        SceneRayHit sceneHit = traceSceneBvh(probePos, rayDir, maxRayDist);
        if (sceneHit.hitDist > 0.0
            && (hitDist <= 0.0 || sceneHit.hitDist < hitDist)) {
            hitDist = sceneHit.hitDist;
            hitNormal = sceneHit.hitNormal;
            albedo = sceneHit.albedo;
            emissive = sceneHit.emissive;
            sceneHitSelected = true;
        }
    }
    if (hitDist > 0.0 && !sceneHitSelected) {
        const float3 hitPos = probePos + rayDir * hitDist;
        albedo = terrainAlbedoByHeight(hitPos.y);
    }
    if (hitDist <= 0.0) {
        return float4(skyRadiance(rayDir), maxRayDist);
    }

    const float3 hitPos = probePos + rayDir * hitDist;
    const float3 sunDir = gDdgi.sunDirectionSkyAmbient.xyz;
    const float sunNdotL = max(dot(hitNormal, sunDir), 0.0);
    const float shadowBias = max(gDdgi.diagnosticParams.z, 0.0001);
    const bool softwareShadowed = sunNdotL > 0.0
        && traceSceneBvhShadow(hitPos + hitNormal * shadowBias, sunDir, maxRayDist);
    const float csmVisibility = sampleDdgiDirectionalCsm(
        hitPos + hitNormal * shadowBias);
    const float3 directSun = albedo * gDdgi.directionalLightRadiance.rgb
        * gDdgi.directionalLightRadiance.w * sunNdotL
        * (softwareShadowed ? 0.0 : 1.0) * csmVisibility;
    const float3 hitIrradiance = bounceFactor > 0.0
        ? sampleProbeIrradiance(hitPos, hitNormal)
        : float3(0.0, 0.0, 0.0);
    const float3 bounceLight = albedo * hitIrradiance * bounceFactor;
    const float3 localLight = albedo * sampleLocalLightRadiance(
        linearIdx, hitPos, hitNormal);
    return float4(
        emissive + directSun + localLight + bounceLight, hitDist);
}

// ---------------------------------------------------------------------------
// Compute entry point 1: Probe trace and atlas convolution.
// One 16x16 workgroup owns one probe. The first raysPerProbe lanes trace a
// frame-rotated spherical Fibonacci set once into workgroup memory. All lanes
// then convolve that shared result into the 6x6 irradiance interior and 14x14
// distance interior. Border lanes duplicate the nearest interior direction.
// ---------------------------------------------------------------------------

groupshared float4 DdgiRayRadianceDistance[kMaximumRaysPerProbe];
groupshared float4 DdgiRayDirectionDistanceSq[kMaximumRaysPerProbe];

uint ddgiTraceRecordIndex(uint linearIdx, uint rayIndex)
{
    return linearIdx * kMaximumRaysPerProbe + rayIndex;
}

// ---------------------------------------------------------------------------
// Compute entry point 1a: ProbeTrace.
// Writes one float4 (radiance.rgb, hit distance) per traced ray into the
// current moments texture as a bounded transient between the split stages.
// ---------------------------------------------------------------------------

[shader("compute")]
[numthreads(16, 16, 1)]
void giProbeTraceComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint countZ = uint(gDdgi.probeCountRayCount.z);
    uint updateCount = min(uint(gDdgi.updateParams.x), countX * countY * countZ);
    if (groupId.x >= updateCount || groupId.y != 0u || groupId.z != 0u) return;

    uint logicalIndex = ProbeUpdateList[groupId.x];
    uint3 gridCoord;
    gridCoord.x = logicalIndex % countX;
    logicalIndex /= countX;
    gridCoord.y = logicalIndex % countY;
    gridCoord.z = logicalIndex / countY;
    if (gridCoord.z >= countZ) return;
    const uint linearIdx = probeGridToIndex(gridCoord);
    const ProbeState probeState = ProbeStateBuffer[linearIdx];
    if (probeState.classification != 1u) return;

    const uint rayCount = min(uint(gDdgi.probeCountRayCount.w), kMaximumRaysPerProbe);
    const uint localIndex = groupThreadId.y * 16u + groupThreadId.x;
    if (localIndex >= rayCount) return;
    const uint frameIndex = uint(gDdgi.scrollOffsetFrame.w);
    const float3 probePos = probeWorldPos(gridCoord);
    const float4 traced = traceDdgiRay(
        probePos, localIndex, rayCount, linearIdx, frameIndex,
        gDdgi.probeTextureSizeBounceMaxRay.w,
        gDdgi.probeTextureSizeBounceMaxRay.z,
        gDdgi.sunDirectionSkyAmbient.w);
    ProbeTraceRecords[ddgiTraceRecordIndex(linearIdx, localIndex)] = traced;
}

// ---------------------------------------------------------------------------
// Compute entry point 1b: BlendIrradiance.
// Reads the ProbeTrace transient and writes only the irradiance interior.
// ---------------------------------------------------------------------------

[shader("compute")]
[numthreads(16, 16, 1)]
void giProbeBlendIrradianceComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint countZ = uint(gDdgi.probeCountRayCount.z);
    uint updateCount = min(uint(gDdgi.updateParams.x), countX * countY * countZ);
    if (groupId.x >= updateCount || groupId.y != 0u || groupId.z != 0u) return;

    uint logicalIndex = ProbeUpdateList[groupId.x];
    uint3 gridCoord;
    gridCoord.x = logicalIndex % countX;
    logicalIndex /= countX;
    gridCoord.y = logicalIndex % countY;
    gridCoord.z = logicalIndex / countY;
    if (gridCoord.z >= countZ) return;
    const uint linearIdx = probeGridToIndex(gridCoord);
    const ProbeState probeState = ProbeStateBuffer[linearIdx];
    if (probeState.classification != 1u) return;
    const uint rayCount = min(uint(gDdgi.probeCountRayCount.w), kMaximumRaysPerProbe);
    const uint3 physicalCoord = logicalToPhysicalProbe(gridCoord);
    const uint frameIndex = uint(gDdgi.scrollOffsetFrame.w);
    float hysteresis = gDdgi.updateParams.y;
    if ((probeState.status & kProbeScrollNewCellFlag) != 0u) hysteresis = 0.0;
    else if (probeState.lightingRevision != uint(gDdgi.updateParams.w)) hysteresis = gDdgi.updateParams.z;

    if (groupThreadId.x < 1u || groupThreadId.x > kIrradianceInteriorTexels
        || groupThreadId.y < 1u || groupThreadId.y > kIrradianceInteriorTexels) return;
    const uint2 interiorCoord = groupThreadId.xy;
    const float2 texelUv = float2(interiorCoord - 1u)
        / float(kIrradianceInteriorTexels - 1u);
    const float3 texelDir = octDecode(texelUv);
    float3 irradiance = 0.0.xxx;
    float weightSum = 0.0;
    for (uint rayIndex = 0u; rayIndex < rayCount; ++rayIndex) {
        const float3 rayDir = sphericalFibonacciSample(
            rayIndex, rayCount, linearIdx, frameIndex);
        const float weight = max(dot(texelDir, rayDir), 0.0);
        const float4 traced = ProbeTraceRecords[
            ddgiTraceRecordIndex(linearIdx, rayIndex)];
        irradiance += traced.rgb * weight;
        weightSum += weight;
    }
    irradiance = weightSum > 0.0 ? irradiance / weightSum : 0.0.xxx;
    const float2 historyUv = probeIrradianceUv(gridCoord, texelDir);
    float3 history = hysteresis > 0.0
        ? ProbeIrradianceHistory.SampleLevel(ProbeSampler, historyUv, 0.0).rgb
        : irradiance;
    const float targetLuminance = dot(irradiance, float3(0.2126, 0.7152, 0.0722));
    const float historyLimit = max(targetLuminance * 2.0, 0.1);
    history = clamp(history, irradiance - historyLimit, irradiance + historyLimit);
    const uint2 outCoord = uint2(
        physicalCoord.x * kIrradiancePhysicalTexels + groupThreadId.x,
        (physicalCoord.y + physicalCoord.z * countY)
            * kIrradiancePhysicalTexels + groupThreadId.y);
    ProbeIrradianceOut[outCoord] = float4(
        lerp(irradiance, history, hysteresis), 1.0);
}

// ---------------------------------------------------------------------------
// Compute entry point 1c: BlendDistance.
// Reads the same ProbeTrace transient and commits mean/depth-squared moments.
// ---------------------------------------------------------------------------

[shader("compute")]
[numthreads(16, 16, 1)]
void giProbeBlendDistanceComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint countZ = uint(gDdgi.probeCountRayCount.z);
    uint updateCount = min(uint(gDdgi.updateParams.x), countX * countY * countZ);
    if (groupId.x >= updateCount || groupId.y != 0u || groupId.z != 0u) return;

    uint logicalIndex = ProbeUpdateList[groupId.x];
    uint3 gridCoord;
    gridCoord.x = logicalIndex % countX;
    logicalIndex /= countX;
    gridCoord.y = logicalIndex % countY;
    gridCoord.z = logicalIndex / countY;
    if (gridCoord.z >= countZ) return;
    const uint linearIdx = probeGridToIndex(gridCoord);
    const ProbeState probeState = ProbeStateBuffer[linearIdx];
    if (probeState.classification != 1u) return;
    const uint rayCount = min(uint(gDdgi.probeCountRayCount.w), kMaximumRaysPerProbe);
    const uint3 physicalCoord = logicalToPhysicalProbe(gridCoord);
    const uint frameIndex = uint(gDdgi.scrollOffsetFrame.w);
    float hysteresis = gDdgi.updateParams.y;
    const bool newlyExposed = (probeState.status & kProbeScrollNewCellFlag) != 0u;
    if (newlyExposed) hysteresis = 0.0;
    else if (probeState.lightingRevision != uint(gDdgi.updateParams.w)) hysteresis = gDdgi.updateParams.z;

    const uint2 distanceInteriorCoord = clamp(
        groupThreadId.xy, 1u.xx, kDistanceInteriorTexels.xx);
    const float2 distanceTexelUv = float2(distanceInteriorCoord - 1u)
        / float(kDistanceInteriorTexels - 1u);
    const float3 distanceDir = octDecode(distanceTexelUv);
    float meanDepth = 0.0;
    float meanDepthSq = 0.0;
    float distanceWeightSum = 0.0;
    for (uint rayIndex = 0u; rayIndex < rayCount; ++rayIndex) {
        const float3 rayDir = sphericalFibonacciSample(
            rayIndex, rayCount, linearIdx, frameIndex);
        const float cosine = max(dot(distanceDir, rayDir), 0.0);
        const float weight = pow(cosine, 32.0);
        const float4 traced = ProbeTraceRecords[
            ddgiTraceRecordIndex(linearIdx, rayIndex)];
        meanDepth += traced.w * weight;
        meanDepthSq += traced.w * traced.w * weight;
        distanceWeightSum += weight;
    }
    if (distanceWeightSum > 0.0) {
        meanDepth /= distanceWeightSum;
        meanDepthSq /= distanceWeightSum;
    } else {
        meanDepth = gDdgi.probeTextureSizeBounceMaxRay.w;
        meanDepthSq = meanDepth * meanDepth;
    }
    const float2 distanceHistoryUv = probeDistanceUv(gridCoord, distanceDir);
    const float2 historyMoments = hysteresis > 0.0
        ? ProbeDepthMomentsHistory.SampleLevel(
            ProbeSampler, distanceHistoryUv, 0.0).xy
        : float2(meanDepth, meanDepthSq);
    if (groupThreadId.x >= 1u && groupThreadId.x <= kDistanceInteriorTexels
        && groupThreadId.y >= 1u && groupThreadId.y <= kDistanceInteriorTexels) {
        const uint2 distanceOutCoord = uint2(
            physicalCoord.x * kDistancePhysicalTexels + groupThreadId.x,
            (physicalCoord.y + physicalCoord.z * countY)
                * kDistancePhysicalTexels + groupThreadId.y);
        ProbeDepthMomentsOut[distanceOutCoord] = float4(
            lerp(meanDepth, historyMoments.x, hysteresis),
            lerp(meanDepthSq, historyMoments.y, hysteresis), 0.0, 0.0);
    }
    // BlendDistance consumes the persistent trace records written by the
    // preceding pass; no workgroup-shared values are read here. Keeping this
    // pass barrier-free also preserves WGSL uniform-control-flow validation.
    if (all(groupThreadId.xy == 0u.xx)) {
        ProbeState state = ProbeStateBuffer[linearIdx];
        state.age = 0u;
        state.confidence = newlyExposed ? 0.25 : min(state.confidence + 0.03125, 1.0);
        state.lastUpdateFrame = frameIndex;
        state.lightingRevision = uint(gDdgi.updateParams.w);
        state.status &= ~kProbeScrollNewCellFlag;
        ProbeStateBuffer[linearIdx] = state;
    }
}

[shader("compute")]
[numthreads(16, 16, 1)]
void giProbeUpdateComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint countZ = uint(gDdgi.probeCountRayCount.z);
    uint updateCount = min(uint(gDdgi.updateParams.x), countX * countY * countZ);
    if (groupId.x >= updateCount || groupId.y != 0u || groupId.z != 0u) {
        return;
    }

    uint logicalIndex = ProbeUpdateList[groupId.x];
    uint3 gridCoord;
    gridCoord.x = logicalIndex % countX;
    logicalIndex /= countX;
    gridCoord.y = logicalIndex % countY;
    gridCoord.z = logicalIndex / countY;
    if (gridCoord.z >= countZ) {
        return;
    }

    uint linearIdx = probeGridToIndex(gridCoord);
    uint frameIndex = uint(gDdgi.scrollOffsetFrame.w);
    ProbeState probeState = ProbeStateBuffer[linearIdx];
    bool newlyExposed = (probeState.status & kProbeScrollNewCellFlag) != 0u;
    if (probeState.classification != 1u) {
        return;
    }

    float3 probePos = probeWorldPos(gridCoord);
    uint rayCount = min(uint(gDdgi.probeCountRayCount.w), kMaximumRaysPerProbe);
    float bounceFactor = gDdgi.probeTextureSizeBounceMaxRay.z;
    float maxRayDist = gDdgi.probeTextureSizeBounceMaxRay.w;
    uint localIndex = groupThreadId.y * 16u + groupThreadId.x;

    if (localIndex < rayCount) {
        float3 rayDir = sphericalFibonacciSample(
            localIndex, rayCount, linearIdx, frameIndex);
        // Terrain remains analytic; the BVH contains static meshes and proxy
        // AABBs only. Comparing both hits keeps the nearest occluder correct
        // while avoiding a dense terrain triangle BVH.
        float3 hitNormal = float3(0.0, 1.0, 0.0);
        float3 albedo = terrainAlbedoByHeight(probePos.y);
        float3 emissive = float3(0.0, 0.0, 0.0);
        float hitDist = marchTerrain(probePos, rayDir, maxRayDist);
        bool sceneHitSelected = false;
        if (hasSceneBvhGeometry()) {
            SceneRayHit sceneHit = traceSceneBvh(probePos, rayDir, maxRayDist);
            if (sceneHit.hitDist > 0.0
                && (hitDist <= 0.0 || sceneHit.hitDist < hitDist)) {
                hitDist = sceneHit.hitDist;
                hitNormal = sceneHit.hitNormal;
                albedo = sceneHit.albedo;
                emissive = sceneHit.emissive;
                sceneHitSelected = true;
            }
        }
        if (hitDist > 0.0 && !sceneHitSelected) {
            float3 hitPos = probePos + rayDir * hitDist;
            albedo = terrainAlbedoByHeight(hitPos.y);
        }
        if (hitDist > 0.0) {
            float3 hitPos = probePos + rayDir * hitDist;
            // Direct sun on this hit surface (matches PBR diffuse: albedo * NdotL * intensity).
            float3 sunDir = gDdgi.sunDirectionSkyAmbient.xyz;
            float sunNdotL = max(dot(hitNormal, sunDir), 0.0);
            const float shadowBias = max(gDdgi.diagnosticParams.z, 0.0001);
            const bool softwareShadowed = sunNdotL > 0.0
                && traceSceneBvhShadow(
                    hitPos + hitNormal * shadowBias, sunDir, maxRayDist);
            const float csmVisibility = sampleDdgiDirectionalCsm(
                hitPos + hitNormal * shadowBias);
            float3 directSun = albedo * gDdgi.directionalLightRadiance.rgb
                * gDdgi.directionalLightRadiance.w * sunNdotL
                * (softwareShadowed ? 0.0 : 1.0) * csmVisibility;
            // Sky enters through ray misses; surface hits receive shadowed
            // direct light and prior probe bounce, never unoccluded sky fill.
            // Single bounce: sample previous-frame probe irradiance at the hit.
            float3 hitIrradiance = bounceFactor > 0.0
                ? sampleProbeIrradiance(hitPos, hitNormal)
                : float3(0.0, 0.0, 0.0);
            float3 bounceLight = albedo * hitIrradiance * bounceFactor;
            float3 localLight = albedo * sampleLocalLightRadiance(
                linearIdx, hitPos, hitNormal);
            DdgiRayRadianceDistance[localIndex] = float4(
                emissive + directSun + localLight + bounceLight, hitDist);
        } else {
            hitDist = maxRayDist;
            DdgiRayRadianceDistance[localIndex] = float4(
                skyRadiance(rayDir), hitDist);
        }
        DdgiRayDirectionDistanceSq[localIndex] = float4(
            rayDir, hitDist * hitDist);
    }
    // The legacy combined entry point is retained for ABI compatibility only;
    // production dispatch uses the split trace/blend passes. Its shared data
    // is no longer consumed after the split, so no group barrier is needed.

    float hysteresis = gDdgi.updateParams.y;
    if (newlyExposed) {
        hysteresis = 0.0;
    } else if (probeState.lightingRevision != uint(gDdgi.updateParams.w)) {
        hysteresis = gDdgi.updateParams.z;
    }
    uint3 physicalCoord = logicalToPhysicalProbe(gridCoord);

    if (groupThreadId.x >= 1u && groupThreadId.x <= kIrradianceInteriorTexels
        && groupThreadId.y >= 1u && groupThreadId.y <= kIrradianceInteriorTexels) {
        uint2 interiorCoord = groupThreadId.xy;
        float2 texelUv = float2(interiorCoord - 1u)
            / float(kIrradianceInteriorTexels - 1u);
        float3 texelDir = octDecode(texelUv);
        float3 irradiance = 0.0.xxx;
        float weightSum = 0.0;
        for (uint rayIndex = 0u; rayIndex < rayCount; ++rayIndex) {
            float weight = max(dot(texelDir,
                DdgiRayDirectionDistanceSq[rayIndex].xyz), 0.0);
            irradiance += DdgiRayRadianceDistance[rayIndex].rgb * weight;
            weightSum += weight;
        }
        irradiance = weightSum > 0.0 ? irradiance / weightSum : 0.0.xxx;
        float2 historyUv = probeIrradianceUv(gridCoord, texelDir);
        float3 history = hysteresis > 0.0
            ? ProbeIrradianceHistory.SampleLevel(ProbeSampler, historyUv, 0.0).rgb
            : irradiance;
        float targetLuminance = dot(irradiance, float3(0.2126, 0.7152, 0.0722));
        float historyLimit = max(targetLuminance * 2.0, 0.1);
        history = clamp(history, irradiance - historyLimit, irradiance + historyLimit);
        uint2 outCoord = uint2(
            physicalCoord.x * kIrradiancePhysicalTexels + groupThreadId.x,
            (physicalCoord.y + physicalCoord.z * countY)
                * kIrradiancePhysicalTexels + groupThreadId.y);
        ProbeIrradianceOut[outCoord] = float4(
            lerp(irradiance, history, hysteresis), 1.0);
    }

    uint2 distanceInteriorCoord = clamp(
        groupThreadId.xy, 1u.xx, kDistanceInteriorTexels.xx);
    float2 distanceTexelUv = float2(distanceInteriorCoord - 1u)
        / float(kDistanceInteriorTexels - 1u);
    float3 distanceDir = octDecode(distanceTexelUv);
    float meanDepth = 0.0;
    float meanDepthSq = 0.0;
    float distanceWeightSum = 0.0;
    for (uint rayIndex = 0u; rayIndex < rayCount; ++rayIndex) {
        float cosine = max(dot(distanceDir,
            DdgiRayDirectionDistanceSq[rayIndex].xyz), 0.0);
        float weight = pow(cosine, 32.0);
        meanDepth += DdgiRayRadianceDistance[rayIndex].w * weight;
        meanDepthSq += DdgiRayDirectionDistanceSq[rayIndex].w * weight;
        distanceWeightSum += weight;
    }
    if (distanceWeightSum > 0.0) {
        meanDepth /= distanceWeightSum;
        meanDepthSq /= distanceWeightSum;
    } else {
        meanDepth = maxRayDist;
        meanDepthSq = maxRayDist * maxRayDist;
    }
    float2 distanceHistoryUv = probeDistanceUv(gridCoord, distanceDir);
    float2 historyMoments = hysteresis > 0.0
        ? ProbeDepthMomentsHistory.SampleLevel(
            ProbeSampler, distanceHistoryUv, 0.0).xy
        : float2(meanDepth, meanDepthSq);
    if (groupThreadId.x >= 1u && groupThreadId.x <= kDistanceInteriorTexels
        && groupThreadId.y >= 1u && groupThreadId.y <= kDistanceInteriorTexels) {
        uint2 distanceOutCoord = uint2(
            physicalCoord.x * kDistancePhysicalTexels + groupThreadId.x,
            (physicalCoord.y + physicalCoord.z * countY)
                * kDistancePhysicalTexels + groupThreadId.y);
        ProbeDepthMomentsOut[distanceOutCoord] = float4(
            lerp(meanDepth, historyMoments.x, hysteresis),
            lerp(meanDepthSq, historyMoments.y, hysteresis), 0.0, 0.0);
    }

    if (all(groupThreadId.xy == 0u.xx)) {
        ProbeState state = ProbeStateBuffer[linearIdx];
        state.age = 0u;
        state.confidence = newlyExposed ? 0.25 : min(state.confidence + 0.03125, 1.0);
        state.lastUpdateFrame = frameIndex;
        state.lightingRevision = uint(gDdgi.updateParams.w);
        state.status &= ~kProbeScrollNewCellFlag;
        ProbeStateBuffer[linearIdx] = state;
    }
}

// ---------------------------------------------------------------------------
// Compute entry point 2: BorderUpdate.
// Copies only the one-texel atlas borders after the interior blend has
// completed. Keeping this write in its own dispatch makes the resource hazard
// and timing scope independently visible without changing the atlas ABI.
// ---------------------------------------------------------------------------

[shader("compute")]
[numthreads(16, 16, 1)]
void giProbeBorderUpdateComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint countZ = uint(gDdgi.probeCountRayCount.z);
    uint updateCount = min(uint(gDdgi.updateParams.x), countX * countY * countZ);
    if (groupId.x >= updateCount || groupId.y != 0u || groupId.z != 0u) {
        return;
    }

    uint logicalIndex = ProbeUpdateList[groupId.x];
    uint3 gridCoord;
    gridCoord.x = logicalIndex % countX;
    logicalIndex /= countX;
    gridCoord.y = logicalIndex % countY;
    gridCoord.z = logicalIndex / countY;
    if (gridCoord.z >= countZ) return;

    uint3 physicalCoord = logicalToPhysicalProbe(gridCoord);
    if (groupThreadId.x < kIrradiancePhysicalTexels
        && groupThreadId.y < kIrradiancePhysicalTexels
        && (groupThreadId.x == 0u || groupThreadId.y == 0u
            || groupThreadId.x == kIrradiancePhysicalTexels - 1u
            || groupThreadId.y == kIrradiancePhysicalTexels - 1u)) {
        uint2 tileOrigin = uint2(
            physicalCoord.x * kIrradiancePhysicalTexels,
            (physicalCoord.y + physicalCoord.z * countY) * kIrradiancePhysicalTexels);
        uint2 source = octahedralBorderSource(
            groupThreadId.xy, kIrradiancePhysicalTexels, kIrradianceInteriorTexels);
        // WebGPU storage textures are write-only for this format. The graph
        // copies the completed atlas into the opposite history slot before
        // this pass, so border reads stay on a sampled texture.
        ProbeIrradianceOut[tileOrigin + groupThreadId.xy]
            = ProbeIrradianceHistory.Load(int3(tileOrigin + source, 0));
    }
    if (groupThreadId.x < kDistancePhysicalTexels
        && groupThreadId.y < kDistancePhysicalTexels
        && (groupThreadId.x == 0u || groupThreadId.y == 0u
            || groupThreadId.x == kDistancePhysicalTexels - 1u
            || groupThreadId.y == kDistancePhysicalTexels - 1u)) {
        uint2 tileOrigin = uint2(
            physicalCoord.x * kDistancePhysicalTexels,
            (physicalCoord.y + physicalCoord.z * countY) * kDistancePhysicalTexels);
        uint2 source = octahedralBorderSource(
            groupThreadId.xy, kDistancePhysicalTexels, kDistanceInteriorTexels);
        ProbeDepthMomentsOut[tileOrigin + groupThreadId.xy]
            = ProbeDepthMomentsHistory.Load(int3(tileOrigin + source, 0));
    }
}

// ---------------------------------------------------------------------------
// Compute entry point 3: Irradiance Resolve.
// [numthreads(8,8,1)] - each thread is one half-resolution pixel.
// Reconstructs world position from depth + inverse view-projection, finds the
// 8 surrounding probes, trilinear-blends their octahedral irradiance samples
// (keyed by the surface normal), and applies Chebyshev AO per probe.
// ---------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void giIrradianceResolveComputeMain(uint3 dtid : SV_DispatchThreadID)
{
    uint2 texSize;
    GiIrradianceOutput.GetDimensions(texSize.x, texSize.y);
    if (dtid.x >= texSize.x || dtid.y >= texSize.y) {
        return;
    }

    // Sample scene depth and reconstruct world position.
    float2 uv = (float2(dtid.xy) + 0.5) / float2(texSize);
    float depth = sampleSceneDepth(uv);
    // D32Float in Vulkan maps to [0,1] NDC z.
    float2 ndc = uv * 2.0 - 1.0;
    float4 worldHomo = mul(gDdgi.invViewProjection, float4(ndc, depth, 1.0));
    float3 worldPos = worldHomo.xyz / max(worldHomo.w, 0.0001);

    float4 normalSample = sampleSceneNormal(uv);
    float3 normal = normalize(normalSample.xyz);
    float4 nearH = mul(gDdgi.invViewProjection, float4(ndc, 0.0, 1.0));
    float3 nearPos = nearH.xyz / max(nearH.w, 0.0001);
    float3 cameraToSurface = worldPos - nearPos;
    if (dot(normal, cameraToSurface) > 0.0) normal = -normal;

    float3 surfaceToCamera = normalize(nearPos - worldPos);
    float3 biasedWorldPos = worldPos
        + normal * gDdgi.diagnosticParams.z
        + surfaceToCamera * gDdgi.diagnosticParams.w;

    // Trilinear blend of 8 surrounding probes.
    float3 gridF = (biasedWorldPos - gDdgi.probeGridOriginHysteresis.xyz)
        / gDdgi.probeGridSpacingStrength.xyz;
    int3 base = int3(floor(gridF));
    float3 frac_ = gridF - float3(base);
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint countZ = uint(gDdgi.probeCountRayCount.z);

    float3 totalIrradiance = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    float totalTrilinearWeight = 0.0;
    float geometricCoverage = 0.0;
    float totalMeanDepth = 0.0;
    float totalMeanDepthSq = 0.0;

    for (uint dz = 0u; dz < 2u; ++dz) {
        for (uint dy = 0u; dy < 2u; ++dy) {
            for (uint dx = 0u; dx < 2u; ++dx) {
                int3 coord = base + int3(dx, dy, dz);
                if (coord.x < 0 || coord.y < 0 || coord.z < 0) continue;
                if (uint(coord.x) >= countX || uint(coord.y) >= countY || uint(coord.z) >= countZ) continue;
                ProbeState sampleState = ProbeStateBuffer[
                    probeGridToIndex(uint3(coord))];
                if (sampleState.classification != 1u || sampleState.confidence <= 0.0) {
                    continue;
                }
                float3 probePos = probeWorldPos(uint3(coord));
                float3 probeToSurface = biasedWorldPos - probePos;
                float fragDist = length(probeToSurface);
                float2 irradianceUv = probeIrradianceUv(uint3(coord), normal);
                float2 visibilityUv = probeDistanceUv(
                    uint3(coord), probeToSurface / max(fragDist, 0.0001));
                float3 probeIrradiance = ProbeIrradianceHistory.SampleLevel(
                    ProbeSampler, irradianceUv, 0.0).rgb;
                // Phase 2: read (mean, mean^2) from the dedicated Rgba16Float
                // moments atlas instead of the irradiance alpha channel. This
                // gives a mathematically valid Chebyshev bound via
                // variance = E[d^2] - E[d]^2.
                float2 moments = ProbeDepthMomentsHistory.SampleLevel(
                    ProbeSampler, visibilityUv, 0.0).xy;
                float meanDepth = moments.x;
                float meanDepthSq = moments.y;
                float ao = ddgiReduceVisibilityLeak(
                    chebyshevWeight(meanDepth, meanDepthSq, fragDist));
                float3 surfaceToProbe = -probeToSurface / max(fragDist, 0.0001);
                float wrapWeight = saturate((dot(surfaceToProbe, normal) + 1.0) * 0.5);
                wrapWeight = wrapWeight * wrapWeight + 0.2;
                float trilinearWeight = (dx == 0u ? 1.0 - frac_.x : frac_.x)
                    * (dy == 0u ? 1.0 - frac_.y : frac_.y)
                    * (dz == 0u ? 1.0 - frac_.z : frac_.z);
                float validWeight = trilinearWeight * sampleState.confidence;
                float w = validWeight * wrapWeight;
                totalIrradiance += probeIrradiance * w * ao;
                totalWeight += w * ao;
                totalTrilinearWeight += w;
                geometricCoverage += trilinearWeight;
                totalMeanDepth += meanDepth * w;
                totalMeanDepthSq += meanDepthSq * w;
            }
        }
    }

    // DDGI is an additive indirect-light term. The material shaders always
    // provide the shared environment ambient, so injecting sky radiance when
    // no probe is available double-lights pixels outside the finite volume.
    // Confidence chooses the relative contribution of valid probes, but must
    // not modulate the normalized result itself. Otherwise the staggered probe
    // update order appears as brightness bands while confidence converges.
    // Pure trilinear coverage still fades the DDGI increment over the outer
    // probe cell as bounded neighbors disappear.
    float3 resolved = totalWeight > 0.0001
        ? totalIrradiance / totalWeight
        : float3(0.0, 0.0, 0.0);
    resolved *= saturate(geometricCoverage);
    const uint diagnosticMode = uint(gDdgi.diagnosticParams.x);
    if (diagnosticMode == 2u) {
        resolved = float3(saturate((totalMeanDepth / max(totalTrilinearWeight, 0.0001))
            / max(gDdgi.probeTextureSizeBounceMaxRay.w, 0.0001)));
    } else if (diagnosticMode == 3u) {
        resolved = float3(saturate(totalWeight / max(totalTrilinearWeight, 0.0001)));
    } else if (diagnosticMode == 4u) {
        resolved = float3(base) / max(gDdgi.probeCountRayCount.xyz - 1.0, 1.0);
    } else if (diagnosticMode == 5u) {
        resolved = float3(saturate(geometricCoverage));
    } else if (diagnosticMode == 6u) {
        // Phase 2 second-moment diagnostic: visualize mean depth^2 (the
        // g channel of the Rgba16Float moments atlas), normalized by maxRayDist^2.
        // Useful for verifying the moments atlas is populated and the variance
        // E[d^2]-E[d]^2 is non-degenerate.
        float maxRayDist = gDdgi.probeTextureSizeBounceMaxRay.w;
        resolved = float3(saturate((totalMeanDepthSq / max(totalTrilinearWeight, 0.0001))
            / max(maxRayDist * maxRayDist, 0.0001)));
    } else if (diagnosticMode == 7u || diagnosticMode == 8u) {
        // Parity fixture: diagnosticParams.yzw carries one logical probe
        // coordinate. Mode 7 exercises the shared linear index helper;
        // mode 8 packs the independently computed scroll-mapped physical
        // coordinate through the same row-major layout.
        uint3 parityLogical = uint3(max(round(gDdgi.diagnosticParams.yzw), 0.0));
        parityLogical = min(parityLogical, uint3(countX, countY, countZ) - 1u);
        uint3 parityPhysical = logicalToPhysicalProbe(parityLogical);
        uint parityValue = diagnosticMode == 7u
            ? probeGridToIndex(parityLogical)
            : parityPhysical.x
                + parityPhysical.y * countX
                + parityPhysical.z * countX * countY;
        uint parityCount = max(countX * countY * countZ, 1u);
        resolved = float3(parityValue) / float3(max(parityCount - 1u, 1u));
    } else if (diagnosticMode == 9u) {
        // Parity fixture: diagnosticParams.yzw is a fixed world position.
        // Recompute the bounded trilinear sum without history/state terms so
        // it can be compared directly with DdgiProbeWeightReference.
        float3 parityGridF = (gDdgi.diagnosticParams.yzw
            - gDdgi.probeGridOriginHysteresis.xyz)
            / gDdgi.probeGridSpacingStrength.xyz;
        int3 parityBase = int3(floor(parityGridF));
        float3 parityFrac = parityGridF - float3(parityBase);
        float parityWeight = 0.0;
        for (uint pz = 0u; pz < 2u; ++pz) {
            for (uint py = 0u; py < 2u; ++py) {
                for (uint px = 0u; px < 2u; ++px) {
                    int3 coord = parityBase + int3(px, py, pz);
                    if (coord.x < 0 || coord.y < 0 || coord.z < 0
                        || uint(coord.x) >= countX || uint(coord.y) >= countY
                        || uint(coord.z) >= countZ) continue;
                    parityWeight += (px == 0u ? 1.0 - parityFrac.x : parityFrac.x)
                        * (py == 0u ? 1.0 - parityFrac.y : parityFrac.y)
                        * (pz == 0u ? 1.0 - parityFrac.z : parityFrac.z);
                }
            }
        }
        resolved = float3(parityWeight);
    } else if (diagnosticMode == 10u) {
        // Parity fixture: y = mean depth, z = mean depth squared, w =
        // fragment distance. This is the exact visibility equation used by
        // resolve, independent of sampled atlas contents.
        resolved = float3(chebyshevWeight(
            gDdgi.diagnosticParams.y,
            gDdgi.diagnosticParams.z,
            gDdgi.diagnosticParams.w));
    }
    // Alpha carries the scene depth that authored this screen-space GI texel.
    // The next frame compares it with the reprojected surface depth before
    // accepting history, preventing disoccluded surfaces from inheriting the
    // previous occluder's indirect light.
    GiIrradianceOutput[dtid.xy] = float4(resolved, ddgiEncodeCacheDepth(depth));
}

// ---------------------------------------------------------------------------
// Compute entry point 3: Probe Relocate (Phase 3).
// [numthreads(8,1,8)] - one thread per probe, dispatched as a 2D XZ slab over
// the grid (Y is the group's Z axis -> mapped to gridCoord.z; the dispatch
// covers countX * countY * countZ probes total via a 3D dispatch).
//
// For each probe this pass:
//   1. Computes the grid-aligned base position.
//   2. Samples terrain height at (x, z).
//   3. If the probe is inside terrain (y < terrainHeight + margin), moves it
//      up to terrainHeight + margin (escape direction +Y preferred).
//   4. If the probe was already relocated and the grid scrolled (new cell),
//      forces a re-evaluation (ScrollNewCell flag) so the update pass resets
//      history.
//   5. Writes the resulting offset + status into ProbeStateBuffer.
//
// The margin avoids probes resting exactly on the surface (which produces
// degenerate mean depth ~0). Steep-slope detection is a stub for now: a probe
// whose terrain gradient exceeds a threshold is flagged but not yet moved
// laterally (TODO: lateral escape along steepest-descent azimuth). The
// contract is that consumer shaders (update/resolve) read offsets from the
// buffer and never mutate positions themselves.
// ---------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 1, 8)]
void giProbeRelocateComputeMain(uint3 gid : SV_DispatchThreadID)
{
    uint countX = uint(gDdgi.probeCountRayCount.x);
    uint countY = uint(gDdgi.probeCountRayCount.y);
    uint countZ = uint(gDdgi.probeCountRayCount.z);
    if (gid.x >= countX || gid.y >= countY || gid.z >= countZ) {
        return;
    }
    uint3 gridCoord = gid;
    uint linearIdx = probeGridToIndex(gridCoord);

    float3 basePos = gDdgi.probeGridOriginHysteresis.xyz
                   + float3(gridCoord) * gDdgi.probeGridSpacingStrength.xyz;

    // Read previous state to preserve offset / backoff across frames.
    ProbeState prev = ProbeStateBuffer[linearIdx];
    int3 originCell = int3(round(gDdgi.probeGridOriginHysteresis.xyz
        / gDdgi.probeGridSpacingStrength.xyz));
    int3 logicalCell = originCell + int3(gridCoord);
    bool scrollNewCell = prev.classification == 0u
        || any(prev.logicalCell != logicalCell);

    const bool hasTerrain = terrainContains(basePos.x, basePos.z);
    float terrainH = hasTerrain ? terrainSample(basePos.x, basePos.z) : 0.0;
    // Margin: probes must sit above the surface so their rays see non-zero
    // mean depth. 1.5 matches the former Prototype constant.
    float surfaceClearance = 1.5;
    float safeY = terrainH + surfaceClearance;

    ProbeState state;
    state.offset = prev.offset;
    // A staggered update may not select this cell on the frame it appears.
    // Only BlendDistance (or the combined update) may acknowledge its first
    // completed sample and clear the pending initialization flag.
    state.status = prev.status & kProbeScrollNewCellFlag;
    state.logicalCell = logicalCell;
    state.classification = 1u;
    state.age = scrollNewCell ? 0u : min(prev.age + 1u, 0xfffffffeu);
    state.confidence = scrollNewCell ? 0.0 : prev.confidence;
    state.lastUpdateFrame = prev.lastUpdateFrame;
    state.lightingRevision = prev.lightingRevision;
    state.reserved = scrollNewCell ? 0.0.xxxx : prev.reserved;

    if (hasTerrain && basePos.y < safeY) {
        // Probe is inside (or too close to) terrain: lift it to the safe height.
        // Offset is applied on top of the grid-aligned basePos, so offset.y
        // must close the gap to safeY.
        state.offset.y = safeY - basePos.y;
        state.status |= kProbeRelocReason_InsideTerrain;
    } else if (hasTerrain) {
        // Probe is above terrain: no relocation needed. Preserve any existing
        // lateral offset from a previous frame (none currently, but the field
        // is reserved for future steep-slope lateral escape).
        state.offset.y = 0.0;
    }

    // Geometry relocation /2: fixed 32 direction set, at most four escape
    // attempts per cell, <= 0.45 spacing ellipsoid. Inactive cells are not
    // sampled or traced. A new cell clears confidence/history before reuse.
    const uint frame = uint(gDdgi.scrollOffsetFrame.w);
    const bool firstGeometryCheck = scrollNewCell || prev.reserved.w == 0.0;
    const bool geometryCheckScheduled = firstGeometryCheck
        ? (linearIdx % 4u == frame % 4u) : (linearIdx % 128u == frame % 128u);
    if (hasSceneBvhGeometry() && geometryCheckScheduled) {
        const float3 spacing = gDdgi.probeGridSpacingStrength.xyz;
        const float clearance = min(0.3, min(spacing.x, min(spacing.y, spacing.z)) * 0.45);
        float3 offset = scrollNewCell ? 0.0.xxx : prev.offset;
        bool validPlacement = false;
        uint attempt = 0u;
        for (; attempt < 4u; ++attempt) {
            uint backCount = 0u;
            float nearestBack = 1e20;
            float nearestFront = 1e20;
            float3 escape = 0.0.xxx;
            float3 repel = 0.0.xxx;
            for (uint r = 0u; r < 32u; ++r) {
                float3 d = sphericalFibonacciSample(r, 32u, linearIdx, 0u);
                SceneRayHit hit = traceSceneBvh(basePos + offset, d,
                    gDdgi.probeTextureSizeBounceMaxRay.w);
                if (hit.hitDist <= 0.0) continue;
                if (hit.backFacing) {
                    backCount++;
                    if (hit.hitDist < nearestBack) { nearestBack = hit.hitDist; escape = d; }
                } else if (hit.hitDist < nearestFront) {
                    nearestFront = hit.hitDist; repel = -d;
                }
            }
            bool inside = float(backCount) / 32.0 > 0.25;
            if (!inside && nearestFront >= clearance) { validPlacement = true; break; }
            float3 candidate = offset + (inside ? escape * (nearestBack + clearance)
                : repel * (clearance - nearestFront));
            if (length(candidate / spacing) > 0.45) break;
            offset = candidate;
        }
        state.offset = offset;
        state.classification = validPlacement ? 1u : 2u;
        state.status |= (validPlacement ? 0u : kProbeRelocReason_MaxBackoff) | (attempt << 4u);
        state.reserved.w = 1.0;
        if (length(prev.offset - offset) > 0.0001 || scrollNewCell
            || prev.classification != state.classification) {
            state.status |= kProbeScrollNewCellFlag;
            state.confidence = 0.0;
        }
    } else if (hasSceneBvhGeometry()) {
        // Unchecked cells cannot seed light while the bounded startup batches run.
        state.classification = firstGeometryCheck ? 0u : prev.classification;
    }

    // Geometry-only cells follow the same initialization lifecycle as terrain
    // cells. Keep the flag until an update actually commits fresh radiance.
    if (scrollNewCell) {
        state.status |= kProbeScrollNewCellFlag;
    }

    ProbeStateBuffer[linearIdx] = state;
}
