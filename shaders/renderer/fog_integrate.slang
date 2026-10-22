#include "common/volumetric_fog.slang"
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<FogHistory> FogResolved;
[[vk::binding(2,0)]] RWStructuredBuffer<float4> FogIntegrated;
[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id:SV_DispatchThreadID)
{
    FogConstants p=FogParams;
    if(any(id.xy>=p.grid.xy)) return;
    float2 uv=(float2(id.xy)+0.5)/float2(p.grid.xy);
    float rayScale=length(fogWorldAt(uv,p.depthTime.y,p)-fogWorldAt(uv,p.depthTime.x,p))/(p.depthTime.y-p.depthTime.x);
    float3 light=0;
    float transmittance=1;
    for(uint z=0;z<p.grid.z;++z) {
        uint index=fogIndex(uint3(id.xy,z),p.grid.xyz);
        float4 cell=FogResolved[index].source;
        float distance=(fogSliceDepth(float(z+1),p)-fogSliceDepth(float(z),p))*rayScale;
        light+=transmittance*cell.xyz*fogSegmentLength(cell.w,distance);
        transmittance*=exp(-cell.w*distance);
        FogIntegrated[index]=float4(light,transmittance);
    }
}
