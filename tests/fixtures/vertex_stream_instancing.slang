struct VertexInput
{
    float2 position : POSITION0;
};

struct InstanceInput
{
    float2 offset : POSITION1;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput vertex, InstanceInput instance)
{
    VertexOutput output;
    output.position = float4(vertex.position + instance.offset, 0.0, 1.0);
    output.color = instance.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return input.color;
}
