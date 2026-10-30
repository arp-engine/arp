struct FrameConstants { float4 value; };
[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> frameConstants;
struct NullSmokePushConstants { uint data[16]; };
[[vk::push_constant]] NullSmokePushConstants pc;
struct VertexOutput { float4 position : SV_Position; };
[shader("vertex")]
VertexOutput vsMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(frameConstants.value.x + float(pc.data[vertexId % 16]), 0, 0, 1);
    return output;
}
[shader("fragment")]
float4 fsMain(VertexOutput input) : SV_Target
{
    return frameConstants.value + float4(float(pc.data[0]), 0, 0, 0);
}
