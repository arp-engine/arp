// OriginalGeometryStreams/1 consumes independent immutable vertex and instance
// streams plus the existing readonly index-buffer descriptor. No procedural
// vertex position or color can substitute for the declared inputs.
[[vk::binding(3, 0)]] StructuredBuffer<uint> Input;

struct VertexInput { float2 position : POSITION0; };
struct InstanceInput { float2 offset : POSITION1; float4 color : COLOR0; };
struct VertexOutput { float4 position : SV_Position; float4 color : COLOR0; };

[shader("vertex")]
VertexOutput vertexMain(VertexInput vertex, InstanceInput instance)
{
    VertexOutput result;
    result.position = float4(vertex.position + instance.offset, 0, 1);
    result.color = instance.color;
    return result;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return select(Input[0] == 65536u, input.color, float4(1, 0, 0, 1));
}
