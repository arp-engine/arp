#include "common/volumetric_fog.slang"
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<float4> FogIntegrated;
[[vk::binding(2,0)]] Texture2D<float4> FogSceneColor;
[[vk::binding(3,0)]] ARP_SAMPLED_DEPTH(FogDepth);
struct VertexOutput { float4 position:SV_Position; float2 uv:TEXCOORD0; };
[shader("vertex")]
VertexOutput vertexMain(uint id:SV_VertexID)
{
    VertexOutput o;
    o.uv=float2((id<<1)&2,id&2);
    o.position=float4(o.uv*2.0-1.0,0,1);
    return o;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input):SV_Target
{
    FogConstants p=FogParams;
    uint2 pixel=min(uint2(input.position.xy),uint2(p.extentJitter.xy)-1);
    float depth=FogDepth.Load(int3(pixel,0));
    float2 uv=(float2(pixel)+0.5)/p.extentJitter.xy;
    float3 world=fogUnproject(uv,depth,p.inverseViewProjection);
    float viewDepth=abs(mul(p.view,float4(world,1)).z);
    float4 fog=sampleVolumetricFog(FogIntegrated,uv,viewDepth,p);
    float4 color=FogSceneColor.Load(int3(pixel,0));
    return float4(color.xyz*fog.w+fog.xyz,color.w);
}
