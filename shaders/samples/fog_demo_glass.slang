#include "common/volumetric_fog.slang"
[[vk::binding(0,0)]] ConstantBuffer<FogConstants> FogParams;
[[vk::binding(1,0)]] StructuredBuffer<float4> FogIntegrated;
#include "samples/fog_glass_geometry.slang"
[[vk::binding(2,0)]] ConstantBuffer<DemoConstants> DemoParams;
[shader("vertex")]
VertexOutput vertexMain(uint id:SV_VertexID) { return fogGlassVertex(id); }
[shader("fragment")]
float4 fragmentMain(VertexOutput input):SV_Target
{
    float2 uv=input.position.xy/FogParams.extentJitter.xy;
    float depth=abs(mul(FogParams.view,float4(input.world,1)).z);
    float4 fog=sampleVolumetricFog(FogIntegrated,uv,depth,FogParams);
    return float4(float3(0.85,0.2,0.08)*fog.w+fog.xyz,0.45);
}
