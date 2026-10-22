[[vk::binding(0,0)]] Texture2D<float4> DemoColor;
struct VertexOutput {float4 position:SV_Position;};
[shader("vertex")]
VertexOutput vertexMain(uint id:SV_VertexID)
{VertexOutput o;o.position=float4(float2((id<<1)&2,id&2)*2.0-1.0,0,1);return o;}
[shader("fragment")]
float4 fragmentMain(VertexOutput input):SV_Target
{float3 color=DemoColor.Load(int3(int2(input.position.xy),0)).xyz;return float4(1.0-exp(-color),1);}
