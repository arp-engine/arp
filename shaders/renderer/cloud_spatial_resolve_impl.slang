// Shared Reconstruction/1,2 classifier, integration and history arithmetic.
void cloudResolveAt(uint3 id)
{
    CloudGpuConstants p = CloudParams;
    if (any(id.xy >= p.dimensions.zw)) return;
    uint index = id.y * p.dimensions.z + id.x;
    float2 uv = (float2(id.xy) + 0.5) / float2(p.dimensions.zw);
    float depth = CloudDepth.Load(int3(id.xy, 0));
    float3 nearPoint = cloudUnproject(uv, 0, p.inverseViewProjection);
    float3 farPoint = cloudUnproject(uv, 1, p.inverseViewProjection);
    float3 direction = normalize(farPoint - nearPoint);
    float3 origin = p.jitter.w > 0 ? nearPoint : p.cameraTime.xyz;
    float opaqueDistance = depth >= 1 ? p.temporal.z
        : max(0.0, dot(cloudUnproject(uv, depth, p.inverseViewProjection) - origin, direction));
    CloudGpuSample current;
    current.radianceTransmittance = float4(0, 0, 0, 1);
    current.metadata = float4(p.temporal.z, opaqueDistance, 0, 0);
    float4 intervals;
    bool blocked;
    if (cloudIntervals(origin - p.cameraTime.xyz, direction,
        min(opaqueDistance, p.temporal.z), p, intervals, blocked) == 0) {
        CloudResolved[index] = cloudStoreSample(current);
        return;
    }
    float2 grid = uv * float2(p.dimensions.xy) - 0.5;
    int2 base = int2(floor(grid));
    float2 f = frac(grid);
    float4 lower = float4(3.402823466e+38);
    float4 upper = -lower;
    bool refine = false;
    for (int y = -1; y <= 2; ++y)
        for (int x = -1; x <= 2; ++x) {
            int2 cell = clamp(base + int2(x,y), int2(0,0), int2(p.dimensions.xy) - 1);
            CloudGpuSample sample = CloudRaw[uint(cell.y) * p.dimensions.x + uint(cell.x)];
            lower = min(lower, sample.radianceTransmittance);
            upper = max(upper, sample.radianceTransmittance);
            refine = refine || abs(sample.metadata.y - opaqueDistance) > p.temporal.y;
        }
    float4 range = upper - lower;
    refine = refine || any(range.xyz > CloudReconstruction.maxRadianceRange)
        || range.w > CloudReconstruction.maxTransmittanceRange;
    if (refine) {
        // Exactly one extra primary integration, with the same bounded physical
        // integrator as raymarch. Refined samples never read temporal history.
        CloudResolved[index] = cloudStoreSample(cloudIntegrateRay(uv, depth, p, CloudDensityCells));
        return;
    }
    float4 integral = 0;
    float total = 0, moment = 0, extinction = 0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            int2 cell = clamp(base + int2(x,y), int2(0,0), int2(p.dimensions.xy) - 1);
            CloudGpuSample sample = CloudRaw[uint(cell.y) * p.dimensions.x + uint(cell.x)];
            float depthWeight = 1.0 / (1.0 + abs(sample.metadata.y - opaqueDistance) / p.temporal.y);
            float weight = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y) * depthWeight;
            integral += weight * sample.radianceTransmittance;
            total += weight;
            float absorbed = weight * (1.0 - sample.radianceTransmittance.w);
            moment += absorbed * sample.metadata.x;
            extinction += absorbed;
        }
    current.radianceTransmittance = integral / total;
    current.metadata.x = extinction > 0 ? moment / extinction : p.temporal.z;
    if (p.previousCamera.w > 0) {
        float3 previousPosition = origin + direction * current.metadata.x
            - p.windErosion.xyz * (p.cameraTime.w - p.temporal.w);
        float4 clip = mul(p.previousViewProjection, float4(previousPosition, 1));
        float2 previousUv = clip.xy / clip.w * 0.5 + 0.5;
        if (clip.w > 0 && all(previousUv >= 0) && all(previousUv < 1)) {
            uint2 pixel = uint2(previousUv * float2(p.dimensions.zw));
            CloudGpuSample old = cloudLoadStored(CloudPrevious[pixel.y * p.dimensions.z + pixel.x]);
            float distance = cloudPreviousDistance(previousPosition, p);
            bool sameMedium = abs(old.metadata.x - distance) <= p.temporal.y;
            bool currentSky = opaqueDistance >= p.temporal.z;
            bool previousSky = old.metadata.y >= p.temporal.z;
            float oldOpaque = cloudPreviousDistance(origin + direction * opaqueDistance, p);
            bool sameOpaque = currentSky && previousSky
                || (!currentSky && !previousSky && oldOpaque >= 0
                    && abs(old.metadata.y - oldOpaque) <= p.temporal.y);
            if (sameMedium && sameOpaque)
                current.radianceTransmittance = lerp(current.radianceTransmittance,
                    clamp(old.radianceTransmittance,
                        min(lower, current.radianceTransmittance),
                        max(upper, current.radianceTransmittance)), p.temporal.x);
        }
    }
    CloudResolved[index] = cloudStoreSample(current);
}
