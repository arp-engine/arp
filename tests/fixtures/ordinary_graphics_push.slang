// Complete block is consumed independently by both selected stages.
struct GraphicsPushConstants
{
    float4 positionScaleBiasDepth;
    float4 color;
};
[[vk::push_constant]] GraphicsPushConstants Push;

struct PushVertex
{
    float4 position : SV_Position;
    nointerpolation float4 color : TEXCOORD0;
};

[shader("vertex")]
PushVertex vertexMain(uint vertex : SV_VertexID)
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    PushVertex output;
    output.position = float4(p * Push.positionScaleBiasDepth.x + Push.positionScaleBiasDepth.yz,
        Push.positionScaleBiasDepth.w, 1);
    output.color = Push.color * 0.5;
    return output;
}

[shader("fragment")]
float4 fragmentMain(PushVertex input) : SV_Target0
{
    return input.color + Push.color * 0.5;
}
