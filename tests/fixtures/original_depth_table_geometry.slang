// OriginalDepthTableGeometry/1: the immutable table and vertex/instance streams
// contribute independently to the output. No procedural position substitute.
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(Inputs)[3];

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
    uint element = (uint(input.position.x) + uint(input.color.x)) % 3;
    float depth = Inputs[element].Load(int3(0, 0, 0));
    return float4(depth, input.color.y, input.color.z, 1);
}
