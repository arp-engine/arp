#ifndef ARP_VOLUMETRIC_FOG_SLANG
#define ARP_VOLUMETRIC_FOG_SLANG
#include "renderer/volume/medium_integration.slang"

struct FogGpuVolume
{
    float4x4 worldToLocal;
    float4 scatteringDensity;
    float4 shapeEdgeScaleStrength;
    float4 velocity; // xyz flow, w density noise contrast exponent (>=1)
    uint4 seed;
};
// extentJitter.xy is the pixel extent, .zw the sub-froxel XY sample in [0,1).
// cameraPosition.w is the sub-froxel Z sample. The uniform ABI is 784 bytes.
struct FogConstants
{
    float4x4 inverseViewProjection;
    float4x4 view;
    float4x4 previousViewProjection;
    float4x4 previousView;
    float4x4 shadowMatrices[4];
    float4 shadowRects[4];
    float4 cascadeEnds;
    uint4 grid;
    float4 depthTime;
    float4 cameraPosition;
    float4 globalMedium;
    float4 globalColorAnisotropy;
    float4 lightDirectionIntensity;
    float4 lightColor;
    float4 temporal;
    float4 extentJitter;
    uint4 counts;
    float4 shadowSelection; // xyz origin, w: 0 view depth, 1 radial distance
    float4 ambientRadiance; // FogIsotropicAmbient/1: mean incident linear radiance
};
struct FogMedium { float4 coefficients; float4 velocity; };
// FogPhaseHistory/1: source.xyz is isotropic ambient, .w extinction;
// direct.xyz is sun scattering before the view-dependent phase function.
struct FogScatteringRecord { float4 source; float4 direct; };
struct FogHistory { float4 source; float4 metadata; float4 direct; };
uint fogIndex(uint3 cell, uint3 grid) { return (cell.z * grid.y + cell.y) * grid.x + cell.x; }
float2 fogSampleUv(uint2 cell, FogConstants p)
{ return (float2(cell)+p.extentJitter.zw)/float2(p.grid.xy); }
float fogSliceDepth(float slice, FogConstants p)
{ return p.depthTime.x * pow(p.depthTime.y / p.depthTime.x, slice / float(p.grid.z)); }
float fogDepthSlice(float depth, FogConstants p)
{ return log(max(depth,p.depthTime.x) / p.depthTime.x) / log(p.depthTime.y / p.depthTime.x) * float(p.grid.z); }
float3 fogUnproject(float2 uv, float depth, float4x4 inv)
{
    float4 world = mul(inv, float4(uv * 2.0 - 1.0, depth, 1.0));
    return world.xyz / world.w;
}
float3 fogWorldAt(float2 uv, float depth, FogConstants p)
{
    float3 a = fogUnproject(uv, 0.0, p.inverseViewProjection);
    float3 b = fogUnproject(uv, 1.0, p.inverseViewProjection);
    float az = abs(mul(p.view, float4(a,1)).z);
    float bz = abs(mul(p.view, float4(b,1)).z);
    return lerp(a,b,(depth-az)/(bz-az));
}
float fogSegmentLength(float extinction, float distance)
{
    return arpVolumeSegmentLength(extinction, distance);
}
float fogPhase(float cosine, float g)
{
    return arpVolumePhase(cosine, g);
}

// Integrated records live on slice boundaries: entry z is the end of slice z.
// Transparent surfaces pass THEIR view depth, before ordinary alpha blending.
float4 sampleVolumetricFog(StructuredBuffer<float4> integrated, float2 uv,
    float viewDepth, FogConstants p)
{
    if (viewDepth <= p.depthTime.x) return float4(0,0,0,1);
    float2 xy = clamp(uv * float2(p.grid.xy) - 0.5, 0.0, float2(p.grid.xy)-1.0);
    float slice = clamp(fogDepthSlice(viewDepth,p), 0.0, float(p.grid.z));
    uint z = min(uint(slice), p.grid.z-1);
    float a = fogSliceDepth(float(z),p);
    float b = fogSliceDepth(float(z+1),p);
    float t = saturate((min(viewDepth,p.depthTime.y)-a)/(b-a));
    uint2 base = uint2(xy);
    float2 f = frac(xy);
    float4 result = 0;
    for (uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
        uint2 cell = min(base+uint2(x,y),p.grid.xy-1);
        float w = (x==0 ? 1-f.x : f.x)*(y==0 ? 1-f.y : f.y);
        float4 lower = z==0 ? float4(0,0,0,1) : integrated[fogIndex(uint3(cell,z-1),p.grid.xyz)];
        float4 upper = integrated[fogIndex(uint3(cell,z),p.grid.xyz)];
        float tr = upper.w / max(lower.w,1e-20);
        float partialTr = pow(max(tr,1e-20),t);
        float fraction = abs(1.0-tr)<0.0001 ? t : (1.0-partialTr)/(1.0-tr);
        result += float4(lower.xyz+(upper.xyz-lower.xyz)*fraction,lower.w*partialTr)*w;
    }
    return result;
}
#endif
