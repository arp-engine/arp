#include "samples/fog_scene_geometry.slang"
struct DemoConstants { float4x4 viewProjection; float4 lightDirection; };
[[vk::binding(0,0)]] ConstantBuffer<DemoConstants> DemoParams;
struct VertexOutput {float4 position:SV_Position;float3 normal:NORMAL;float3 color:COLOR0;};
[shader("vertex")]
VertexOutput vertexMain(uint id:SV_VertexID)
{
    float3 world,normal,color;fogDemoVertex(id,world,normal,color,DemoParams.lightDirection.w>0.5);
    VertexOutput o;o.position=mul(DemoParams.viewProjection,float4(world,1));o.normal=normal;o.color=color;return o;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input):SV_Target
{return float4(input.color*(0.25+2.0*saturate(dot(normalize(input.normal),-DemoParams.lightDirection.xyz))),1);}
