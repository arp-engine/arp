#include "samples/fog_glass_geometry.slang"
[[vk::binding(2,0)]] ConstantBuffer<DemoConstants> DemoParams;
[shader("vertex")]
VertexOutput vertexMain(uint id:SV_VertexID) { return fogGlassVertex(id); }
[shader("fragment")]
float4 fragmentMain(VertexOutput input):SV_Target
{ return float4(0.85,0.2,0.08,0.45); }
