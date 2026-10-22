#include "common/volumetric_fog.slang"
#include "core/noise3.slang"
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<FogGpuVolume> FogVolumes;
[[vk::binding(2,0)]] RWStructuredBuffer<FogMedium> FogMedia;
[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    FogConstants p = FogParams;
    if (any(id>=p.grid.xyz)) return;
    float2 uv = fogSampleUv(id.xy,p);
    float3 world = fogWorldAt(uv,fogSliceDepth(float(id.z)+p.cameraPosition.w,p),p);
    float sigma = p.counts.y==0 ? 0 : p.globalMedium.x * exp(-p.globalMedium.y * max(world.y-p.globalMedium.z,0.0));
    float3 scatter = sigma * p.globalColorAnisotropy.xyz;
    float3 velocity = 0;
    for(uint i=0;i<p.counts.x;++i) {
        FogGpuVolume v = FogVolumes[i];
        float3 local = mul(v.worldToLocal,float4(world,1)).xyz;
        float radius = v.shapeEdgeScaleStrength.x==0 ? max(abs(local.x),max(abs(local.y),abs(local.z))) : length(local);
        float edge = v.shapeEdgeScaleStrength.y==0 ? (radius<1 ? 1.0 : 0.0)
            : smoothstep(0.0,v.shapeEdgeScaleStrength.y,1.0-radius);
        float noise = arpNoise3((world-v.velocity.xyz*p.depthTime.z)*v.shapeEdgeScaleStrength.z,v.seed.x);
        noise = pow(noise, v.velocity.w);
        float density = v.scatteringDensity.w * edge * lerp(1.0,noise,v.shapeEdgeScaleStrength.w);
        scatter += density*v.scatteringDensity.xyz;
        sigma += density;
        velocity += density*v.velocity.xyz;
    }
    FogMedium m;
    m.coefficients=float4(scatter,sigma);
    m.velocity=float4(sigma>0 ? velocity/sigma : float3(0,0,0),0);
    FogMedia[fogIndex(id,p.grid.xyz)] = m;
}
