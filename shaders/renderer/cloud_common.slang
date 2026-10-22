#ifndef ARP_CLOUD_COMMON_SLANG
#define ARP_CLOUD_COMMON_SLANG
#include "renderer/volume/medium_integration.slang"

// CloudGpuConstants/1. Camera-relative quadratic evaluation preserves low
// altitude precision at planet radii. All matrices use the default Slang layout.
struct CloudGpuConstants
{
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    float4 cameraTime;
    float4 cameraRadial;
    float4 shell;
    float4 albedoAnisotropy;
    float4 periodsCoverage;
    float4 windErosion;
    float4 sunDirection;
    float4 sunRadiance;
    float4 ambientRadiance;
    uint4 dimensions;
    uint4 sampling;
    uint4 assetLayout;
    float4 temporal;
    float4 previousCamera;
    float4 jitter;
};

struct CloudGpuSample
{
    float4 radianceTransmittance;
    float4 metadata;
};

float3 cloudUnproject(float2 uv, float depth, float4x4 inverseViewProjection)
{
    float4 p = mul(inverseViewProjection, float4(uv * 2.0 - 1.0, depth, 1.0));
    return p.xyz / p.w;
}

float cloudGroundC(float3 relativePoint, CloudGpuConstants p)
{
    return p.cameraRadial.w + 2.0 * dot(p.cameraRadial.xyz, relativePoint)
        + dot(relativePoint, relativePoint);
}

float cloudAltitude(float3 relativePoint, CloudGpuConstants p)
{
    float c = cloudGroundC(relativePoint, p);
    return c / (sqrt(max(p.shell.z * p.shell.z + c, 0.0)) + p.shell.z);
}

bool cloudSphere(float b, float c, out float2 interval)
{
    float discriminant = b * b - c;
    interval = 0;
    if (discriminant <= 0) return false;
    float root = sqrt(discriminant);
    float q = -b - (b < 0 ? -root : root);
    float other = c / q;
    interval = float2(min(q, other), max(q, other));
    return true;
}

// Returns up to two ordered shell segments. Planet occlusion is reported
// separately so sun visibility becomes zero when its ray intersects ground.
uint cloudIntervals(float3 origin, float3 direction, float limit, CloudGpuConstants p,
    out float4 intervals, out bool groundBlocked)
{
    intervals = 0;
    float c = cloudGroundC(origin, p);
    // Orthographic near-plane origins can enter the opaque planet even when
    // the camera itself is outside. Such pixels start fully occluded.
    if (c < 0) {
        groundBlocked = true;
        return 0;
    }
    float b = dot(p.cameraRadial.xyz + origin, direction);
    float2 ground;
    groundBlocked = cloudSphere(b, c, ground) && ground.x >= 0;
    if (groundBlocked) limit = min(limit, ground.x);
    float base = p.shell.x;
    float top = base + p.shell.y;
    float2 outer;
    if (!cloudSphere(b, c - top * (2.0 * p.shell.z + top), outer)) return 0;
    float first = max(outer.x, 0.0);
    float last = min(outer.y, limit);
    if (last <= first) return 0;
    float2 inner;
    if (!cloudSphere(b, c - base * (2.0 * p.shell.z + base), inner)
        || inner.y <= first || inner.x >= last) {
        intervals.xy = float2(first, last);
        return 1;
    }
    uint count = 0;
    if (inner.x > first) {
        intervals.xy = float2(first, min(inner.x, last));
        count = 1;
    }
    if (inner.y < last) {
        if (count == 0) intervals.xy = float2(max(first, inner.y), last);
        else intervals.zw = float2(max(first, inner.y), last);
        ++count;
    }
    return count;
}

uint cloudWrap(int value, uint dimension)
{
    int remainder = value % int(dimension);
    return uint(remainder < 0 ? remainder + int(dimension) : remainder);
}

float4 cloudSampleCells(StructuredBuffer<float4> cells, float3 uvw,
    uint offset, uint dimension, bool planar)
{
    float3 grid = frac(uvw) * float(dimension) - 0.5;
    if (planar) grid.z = 0;
    int3 base = int3(floor(grid));
    float3 f = frac(grid);
    float4 result = 0;
    for (uint z = 0; z < (planar ? 1u : 2u); ++z)
        for (uint y = 0; y < 2; ++y)
            for (uint x = 0; x < 2; ++x) {
                uint ix = cloudWrap(base.x + int(x), dimension);
                uint iy = cloudWrap(base.y + int(y), dimension);
                uint iz = planar ? 0u : cloudWrap(base.z + int(z), dimension);
                float weight = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y)
                    * (planar ? 1.0 : (z == 0 ? 1.0 - f.z : f.z));
                result += weight * cells[offset + (iz * dimension + iy) * dimension + ix];
            }
    return result;
}

float cloudDensity(StructuredBuffer<float4> cells, float3 relativePoint, CloudGpuConstants p)
{
    float h = (cloudAltitude(relativePoint, p) - p.shell.x) / p.shell.y;
    if (h <= 0 || h >= 1 || p.periodsCoverage.w <= 0) return 0;
    float3 world = p.cameraTime.xyz + relativePoint - p.windErosion.xyz * p.cameraTime.w;
    float3 weather = cloudSampleCells(cells, float3(world.xz / p.periodsCoverage.x, 0),
        3u, p.sampling.z, true).xyz;
    float cloudType = saturate(weather.y) * 2.0;
    uint profileIndex = min(uint(cloudType), 1u);
    float4 profile = lerp(cells[profileIndex], cells[profileIndex + 1u], cloudType - float(profileIndex));
    float height = smoothstep(profile.x, profile.y, h) * (1.0 - smoothstep(profile.z, profile.w, h));
    float coverage = saturate(weather.x * p.periodsCoverage.w);
    float shape = cloudSampleCells(cells, world / p.periodsCoverage.y,
        p.assetLayout.z, p.sampling.w, false).x;
    // Threshold remapping is defined by CloudDensity/1. Coverage zero is exact
    // vacuum; erosion removes high-frequency detail from the remaining shape.
    float shapeDensity = coverage > 0 ? saturate((shape - (1.0 - coverage)) / coverage) : 0;
    float detail = cloudSampleCells(cells, world / p.periodsCoverage.z,
        p.assetLayout.w, p.assetLayout.x, false).x;
    return saturate(shapeDensity - p.windErosion.w * detail * (1.0 - shapeDensity))
        * height * weather.z;
}

#endif
