// WOCC shadow-shaped push ABI, consumed independently in both graphics stages.
struct MatrixPushConstants
{
    float4x4 lightViewProjection[3];
    float4 depthBiasAndFlags;
    uint cascadeIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};
[[vk::push_constant]] MatrixPushConstants Push;

struct MatrixVertex
{
    float4 position : SV_Position;
    nointerpolation float4 value : TEXCOORD0;
};

[shader("vertex")]
MatrixVertex vertexMain(uint vertex : SV_VertexID)
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    MatrixVertex output;
    output.position = mul(Push.lightViewProjection[min(Push.cascadeIndex, 2u)], float4(p, 0, 1));
    output.value = Push.depthBiasAndFlags;
    return output;
}

[shader("fragment")]
float4 fragmentMain(MatrixVertex input) : SV_Target0
{
    return mul(Push.lightViewProjection[min(Push.cascadeIndex, 2u)], input.value) + Push.depthBiasAndFlags;
}
