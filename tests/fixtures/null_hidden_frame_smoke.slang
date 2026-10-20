struct FrameConstants { float4 value; };
[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> frameConstants;
struct VertexOutput { float4 position : SV_Position; };
VertexOutput vsMain(uint vertexId : SV_VertexID) { VertexOutput output; output.position = float4(0, 0, 0, 1); return output; }
float4 fsMain(VertexOutput input) : SV_Target { return frameConstants.value; }
