VertexOutput cloudCompositeVertex(uint id : SV_VertexID)
{
    VertexOutput output;
    float2 uv = float2((id << 1) & 2, id & 2);
    output.position = float4(uv * 2.0 - 1.0, 0, 1);
    return output;
}

float4 cloudCompositeFragment(VertexOutput input)
{
    CloudGpuConstants p = CloudParams;
    uint2 pixel = min(uint2(input.position.xy), p.dimensions.zw - 1u);
    float2 uv = (float2(pixel) + 0.5) / float2(p.dimensions.zw);
    float depth = CloudDepth.Load(int3(pixel, 0));
    float3 nearPoint = cloudUnproject(uv, 0, p.inverseViewProjection);
    float3 farPoint = cloudUnproject(uv, 1, p.inverseViewProjection);
    float3 origin = p.jitter.w > 0 ? nearPoint : p.cameraTime.xyz;
    float opaqueDistance = depth >= 1 ? p.temporal.z
        : max(0.0, dot(cloudUnproject(uv, depth, p.inverseViewProjection) - origin,
            normalize(farPoint - nearPoint)));
    float4 scene = CloudSceneColor.Load(int3(pixel, 0));
    float4 intervals;
    bool groundBlocked;
    // A foreground pixel whose exact ray never reaches the shell is vacuum,
    // even if a low-resolution neighbour contains distant clouds. This is an
    // exact geometric identity, independent of reconstruction depth weights.
    if (cloudIntervals(origin - p.cameraTime.xyz, normalize(farPoint - nearPoint),
        min(opaqueDistance, p.temporal.z), p, intervals, groundBlocked) == 0)
        return scene;
#if defined(ARP_CLOUD_FULL_RESOLVED)
    float4 integral = CloudResolved[pixel.y * p.dimensions.z + pixel.x].radianceTransmittance;
#else
    float2 grid = uv * float2(p.dimensions.xy) - 0.5;
    int2 base = int2(floor(grid));
    float2 f = frac(grid);
    float4 integral = 0;
    float total = 0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            int2 cell = clamp(base + int2(x,y), int2(0,0), int2(p.dimensions.xy) - 1);
            CloudGpuSample sample = CloudResolved[uint(cell.y) * p.dimensions.x + uint(cell.x)];
            float depthWeight = 1.0 / (1.0 + abs(sample.metadata.y - opaqueDistance) / p.temporal.y);
            float weight = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y) * depthWeight;
            integral += weight * sample.radianceTransmittance;
            total += weight;
        }
    integral /= total;
#endif
    return float4(integral.xyz + integral.w * scene.xyz, scene.w);
}
