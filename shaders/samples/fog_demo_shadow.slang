#include "samples/fog_scene_geometry.slang"
struct DemoConstants { float4x4 viewProjection; float4 lightDirection; };
[[vk::binding(0,0)]] ConstantBuffer<DemoConstants> DemoParams;
[shader("vertex")]
float4 vertexMain(uint id:SV_VertexID):SV_Position
{float3 world,normal,color;fogDemoVertex(id,world,normal,color);return mul(DemoParams.viewProjection,float4(world,1));}
[shader("fragment")]
void fragmentMain() {}
