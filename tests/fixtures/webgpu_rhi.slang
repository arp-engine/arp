struct PushConstants { uint data[16]; };
[[vk::push_constant]] PushConstants pc;
struct VertexOutput { float4 position : SV_Position; float4 color : COLOR; };
VertexOutput vsMain(uint vertexId : SV_VertexID) { VertexOutput output; output.position = float4(0, 0, 0, 1); output.color = float4(1, 1, 1, 1); return output; }
float4 fsMain(VertexOutput input) : SV_Target { return input.color; }
[numthreads(1, 1, 1)] void csMain(uint3 dispatchId : SV_DispatchThreadID) {}
