#include "common/volumetric_fog.slang"
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<FogMedium> FogMedia;
[[vk::binding(2,0)]] StructuredBuffer<FogScatteringRecord> FogScattering;
[[vk::binding(3,0)]] StructuredBuffer<FogHistory> FogPrevious;
[[vk::binding(4,0)]] ARP_SAMPLED_DEPTH(FogDepth);
[[vk::binding(5,0)]] RWStructuredBuffer<FogHistory> FogResolved;
[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID)
{
    FogConstants p=FogParams;
    if(any(id>=p.grid.xyz)) return;
    uint index=fogIndex(id,p.grid.xyz);
    float2 uv=(float2(id.xy)+0.5)/float2(p.grid.xy);
    float3 world=fogWorldAt(uv,fogSliceDepth(float(id.z)+0.5,p),p);
    uint2 pixel=min(uint2(uv*p.extentJitter.xy),uint2(p.extentJitter.xy)-1);
    float depth=FogDepth.Load(int3(pixel,0));
    float3 surface=fogUnproject(uv,depth,p.inverseViewProjection);
    float opaqueDepth=abs(mul(p.view,float4(surface,1)).z);
    FogMedium medium=FogMedia[index];
    FogScatteringRecord current=FogScattering[index];
    float4 source=current.source;
    float3 direct=current.direct.xyz;
    if(p.counts.w!=0) {
        float3 previousWorld=world-medium.velocity.xyz*p.depthTime.w;
        float4 previousClip=mul(p.previousViewProjection,float4(previousWorld,1));
        float3 ndc=previousClip.xyz/previousClip.w;
        float2 previousUv=ndc.xy*0.5+0.5;
        float previousZ=abs(mul(p.previousView,float4(previousWorld,1)).z);
        float3 coord=float3(previousUv*float2(p.grid.xy)-0.5,fogDepthSlice(previousZ,p)-0.5);
        if(previousClip.w>0 && all(coord>=0) && all(coord<=float3(p.grid.xyz)-1)) {
            uint3 base=uint3(coord);
            float3 f=frac(coord);
            FogHistory old;
            old.source=0;old.metadata=0;old.direct=0;
            for(uint z=0;z<2;++z) for(uint y=0;y<2;++y) for(uint x=0;x<2;++x) {
                uint3 at=min(base+uint3(x,y,z),p.grid.xyz-1);
                float weight=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y)*(z==0?1-f.z:f.z);
                FogHistory h=FogPrevious[fogIndex(at,p.grid.xyz)];
                old.source+=h.source*weight;old.metadata+=h.metadata*weight;
                old.direct+=h.direct*weight;
            }
            float previousSurfaceDepth=abs(mul(p.previousView,float4(surface,1)).z);
            bool mediumValid=all(abs(old.metadata.xyz-medium.coefficients.xyz)<=p.temporal.y*max(medium.coefficients.xyz,0.00001))
                && abs(old.source.w-source.w)<=p.temporal.y*max(source.w,0.00001);
            bool depthValid=abs(old.metadata.w-previousSurfaceDepth)<=p.temporal.z*max(previousSurfaceDepth,0.001);
            float3 lo=source.xyz,hi=source.xyz;
            float3 directLo=direct,directHi=direct;
            for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
                uint3 at=uint3(clamp(int2(id.xy)+int2(x,y),int2(0,0),int2(p.grid.xy)-1),id.z);
                FogScatteringRecord n=FogScattering[fogIndex(at,p.grid.xyz)];
                float3 neighbor=n.source.xyz;
                lo=min(lo,neighbor);hi=max(hi,neighbor);
                directLo=min(directLo,n.direct.xyz);directHi=max(directHi,n.direct.xyz);
            }
            if(mediumValid&&depthValid) {
                source.xyz=lerp(source.xyz,clamp(old.source.xyz,lo,hi),p.temporal.x);
                direct=lerp(direct,clamp(old.direct.xyz,directLo,directHi),p.temporal.x);
            }
        }
    }
    FogHistory result;
    result.source=source;
    result.metadata=float4(medium.coefficients.xyz,opaqueDepth);
    result.direct=float4(direct,0);
    FogResolved[index]=result;
}
