#ifndef ARP_RENDERER_DDGI_SAMPLING_SLANG
#define ARP_RENDERER_DDGI_SAMPLING_SLANG

#include "renderer/ddgi_visibility.slang"
#include "renderer/ddgi_depth_cache.slang"

float4 sampleDdgiScreenCache(
    Texture2D<float4> cache,
    SamplerState cacheSampler,
    Texture2D<float4> sceneNormalTexture,
    float2 historyUv,
    float2 currentFullResolutionPixel,
    float2 inverseFullResolutionExtent,
    float historyDepth,
    float3 geometryNormal,
    float historyDepthFootprint)
{
    uint width;
    uint height;
    cache.GetDimensions(width, height);
    float2 cachePosition = historyUv * float2(width, height) - 0.5;
    int2 base = int2(floor(cachePosition));
    float2 fraction = frac(cachePosition);
    float3 irradiance = 0.0.xxx;
    float weightSum = 0.0;
    // The caller computes this before any potentially non-uniform material,
    // LOD, or diagnostic branch. Keeping derivative evaluation at the
    // fragment entry preserves adaptive depth rejection while remaining valid
    // for WebGPU's uniform-control-flow rules. This is the previous-frame
    // depth footprint because both the cached texel and its depth are history.
    const float depthTolerance = clamp(
        0.00005 + historyDepthFootprint * 2.0, 0.0001, 0.01);
    uint normalWidth;
    uint normalHeight;
    sceneNormalTexture.GetDimensions(normalWidth, normalHeight);
    // Validate the current surface against its current depth-prepass normal.
    // The cache samples are in the previous frame, so current normals cannot
    // be used as per-texel ownership data for those history coordinates.
    const float3 normalizedGeometryNormal = normalize(geometryNormal);
    int2 currentNormalCoordinate = int2(clamp(
        currentFullResolutionPixel * abs(inverseFullResolutionExtent)
            * float2(normalWidth, normalHeight),
        0.0.xx,
        float2(normalWidth - 1u, normalHeight - 1u)));
    float4 currentNormal = sceneNormalTexture.Load(
        int3(currentNormalCoordinate, 0));
    float normalAgreement = currentNormal.a > 0.5
        ? saturate((dot(normalize(currentNormal.xyz), normalizedGeometryNormal)
            + 0.25) / 1.25)
        : 0.0;
    normalAgreement *= normalAgreement;

    // A rejected current surface contributes zero weight to every cache tap.
    // Avoid four history reads before the caller uses the probe volume.
    if (normalAgreement == 0.0) return float4(0.0, 0.0, 0.0, 0.0);

    [unroll]
    for (int y = 0; y < 2; ++y) {
        [unroll]
        for (int x = 0; x < 2; ++x) {
            int2 coordinate = clamp(base + int2(x, y), 0,
                int2(int(width) - 1, int(height) - 1));
            float4 sampleValue = (x == 0 && y == 0)
                ? cache.SampleLevel(cacheSampler,
                    (float2(coordinate) + 0.5) / float2(width, height), 0.0)
                : cache.Load(int3(coordinate, 0));
            float2 axisWeight = x == 0
                ? float2(1.0 - fraction.x, 0.0)
                : float2(fraction.x, 0.0);
            float bilinearX = axisWeight.x;
            float bilinearY = y == 0 ? 1.0 - fraction.y : fraction.y;
            float depthWeight = saturate(
                1.0 - ddgiCacheDepthDistance(sampleValue.a, historyDepth) / depthTolerance);
            depthWeight *= depthWeight;
            float weight = bilinearX * bilinearY * depthWeight * normalAgreement;
            irradiance += sampleValue.rgb * weight;
            weightSum += weight;
        }
    }
    // Return zero confidence with zero radiance as one atomic validity state.
    // Previously RGB used a 1e-4 threshold while alpha exposed any positive
    // weight. Callers therefore accepted black RGB in the (0, 1e-4] interval
    // instead of invoking the probe fallback, producing one-pixel GI cracks.
    const float minimumAcceptedWeight = 0.0001;
    const float fullConfidenceWeight = 0.05;
    const bool valid = weightSum > minimumAcceptedWeight;
    return float4(
        valid ? irradiance / weightSum : 0.0.xxx,
        valid ? saturate(weightSum / fullConfidenceWeight) : 0.0);
}

float4 blendDdgiScreenCacheWithProbe(float4 screenCacheGi, float4 probeGi)
{
    if (screenCacheGi.a <= 0.0) return probeGi;
    if (probeGi.a <= 0.0) return screenCacheGi;
    const float screenConfidence = saturate(screenCacheGi.a);
    return float4(
        lerp(probeGi.rgb, screenCacheGi.rgb, screenConfidence),
        max(screenCacheGi.a, probeGi.a));
}

struct DdgiSamplingProbeState
{
    float3 offset;
    uint status;
    int3 logicalCell;
    uint classification;
    uint age;
    float confidence;
    uint lastUpdateFrame;
    uint lightingRevision;
    float4 reserved;
};

struct DdgiSamplingConstants
{
    float4 originNormalBias;
    float4 spacingViewBias;
    uint4 probeCount;
    int4 ringOffset;
};

float2 ddgiOctEncode(float3 direction)
{
    direction /= max(abs(direction.x) + abs(direction.y) + abs(direction.z), 0.0001);
    float2 encoded = direction.xy;
    if (direction.z < 0.0) {
        encoded = (1.0 - abs(encoded.yx))
            * float2(encoded.x >= 0.0 ? 1.0 : -1.0,
                encoded.y >= 0.0 ? 1.0 : -1.0);
    }
    return encoded * 0.5 + 0.5;
}

uint3 ddgiLogicalToPhysical(uint3 logical, DdgiSamplingConstants constants)
{
    int3 count = int3(constants.probeCount.xyz);
    int3 physical = (int3(logical) + constants.ringOffset.xyz) % count;
    physical = (physical + count) % count;
    return uint3(physical);
}

uint ddgiProbeStateIndex(uint3 logical, DdgiSamplingConstants constants)
{
    uint3 physical = ddgiLogicalToPhysical(logical, constants);
    return physical.x + constants.probeCount.x
        * (physical.y + constants.probeCount.y * physical.z);
}

float2 ddgiPhysicalProbeAtlasUv(
    uint3 physical,
    float2 oct,
    DdgiSamplingConstants constants,
    uint interiorTexels,
    uint physicalTexels)
{
    float2 interior = 1.5 + oct * float(interiorTexels - 1u);
    float2 atlasTexel = float2(
        physical.x * physicalTexels,
        (physical.y + physical.z * constants.probeCount.y) * physicalTexels)
        + interior;
    float2 atlasExtent = float2(
        constants.probeCount.x * physicalTexels,
        constants.probeCount.y * constants.probeCount.z * physicalTexels);
    return atlasTexel / atlasExtent;
}

float2 ddgiProbeAtlasUv(
    uint3 logical,
    float3 direction,
    DdgiSamplingConstants constants,
    uint interiorTexels,
    uint physicalTexels)
{
    return ddgiPhysicalProbeAtlasUv(
        ddgiLogicalToPhysical(logical, constants),
        ddgiOctEncode(normalize(direction)), constants,
        interiorTexels, physicalTexels);
}

float ddgiChebyshevVisibility(float meanDepth, float meanDepthSq, float distance)
{
    if (distance <= meanDepth) return 1.0;
    float variance = max(meanDepthSq - meanDepth * meanDepth, 0.0001);
    float delta = distance - meanDepth;
    float visibility = variance / (variance + delta * delta);
    return ddgiReduceVisibilityLeak(visibility);
}

#ifndef DDGI_PROBE_STATE_BUFFER
#define DDGI_PROBE_STATE_BUFFER StructuredBuffer<DdgiSamplingProbeState>
#endif

float4 sampleDdgiProbeVolume(
    Texture2D<float4> irradianceAtlas,
    Texture2D<float4> distanceAtlas,
    SamplerState atlasSampler,
    DDGI_PROBE_STATE_BUFFER probeStates,
    DdgiSamplingConstants constants,
    float3 worldPosition,
    float3 worldNormal,
    float3 surfaceToCamera)
{
    float3 normal = normalize(worldNormal);
    float3 biasedPosition = worldPosition
        + normal * constants.originNormalBias.w
        + normalize(surfaceToCamera) * constants.spacingViewBias.w;
    float3 grid = (biasedPosition - constants.originNormalBias.xyz)
        / constants.spacingViewBias.xyz;
    int3 base = int3(floor(grid));
    float3 fraction = frac(grid);
    float3 irradiance = 0.0.xxx;
    float weightSum = 0.0;

    // Adjacent logical corners are also adjacent in the toroidal atlas.
    // Wrap the base once, then each corner needs at most one subtraction.
    uint3 physicalBase = ddgiLogicalToPhysical(uint3(base), constants);
    float2 irradianceOct = ddgiOctEncode(normalize(normal));

    // Keep one probe's temporaries live at a time in foliage fragments.
    // Bit order is the original z/y/x traversal; all eight probes and the
    // floating-point accumulation order are preserved.
    [loop]
    for (uint corner = 0u; corner < 8u; ++corner) {
        uint x = corner & 1u;
        uint y = (corner >> 1u) & 1u;
        uint z = corner >> 2u;
        int3 cell = base + int3(x, y, z);
        if (any(cell < 0) || any(uint3(cell) >= constants.probeCount.xyz)) continue;
        uint3 logical = uint3(cell);
        uint3 physical = physicalBase + uint3(x, y, z);
        physical = uint3(
            physical.x >= constants.probeCount.x ? physical.x - constants.probeCount.x : physical.x,
            physical.y >= constants.probeCount.y ? physical.y - constants.probeCount.y : physical.y,
            physical.z >= constants.probeCount.z ? physical.z - constants.probeCount.z : physical.z);
        uint stateIndex = physical.x + constants.probeCount.x
            * (physical.y + constants.probeCount.y * physical.z);
        DdgiSamplingProbeState state = probeStates[stateIndex];
        if (state.classification != 1u || state.confidence <= 0.0) continue;
        float3 probePosition = constants.originNormalBias.xyz
            + float3(logical) * constants.spacingViewBias.xyz
            + state.offset;
        float3 probeToSurface = biasedPosition - probePosition;
        float distance = length(probeToSurface);
        float3 direction = probeToSurface / max(distance, 0.0001);
        float3 sampleIrradiance = irradianceAtlas.SampleLevel(
            atlasSampler,
            ddgiPhysicalProbeAtlasUv(physical, irradianceOct, constants, 6u, 8u), 0.0).rgb;
        float2 moments = distanceAtlas.SampleLevel(
            atlasSampler,
            ddgiPhysicalProbeAtlasUv(physical, ddgiOctEncode(normalize(direction)),
                constants, 14u, 16u), 0.0).xy;
        float visibility = ddgiChebyshevVisibility(
            moments.x, moments.y, distance);
        float wrap = saturate((dot(-direction, normal) + 1.0) * 0.5);
        wrap = wrap * wrap + 0.2;
        float weight = (x == 0u ? 1.0 - fraction.x : fraction.x)
            * (y == 0u ? 1.0 - fraction.y : fraction.y)
            * (z == 0u ? 1.0 - fraction.z : fraction.z)
            * wrap * visibility * state.confidence;
        irradiance += sampleIrradiance * weight;
        weightSum += weight;
    }
    const bool valid = weightSum > 0.0001;
    return float4(
        valid ? irradiance / weightSum : 0.0.xxx,
        valid ? saturate(weightSum) : 0.0);
}

#endif
