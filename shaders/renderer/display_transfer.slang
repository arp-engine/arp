// SdrDisplayTransfer/1. Input is linear scene color; alpha is never encoded.
// Encoding occurs once, after all scene layers have blended in linear space.
struct DisplayTransferPushConstants
{
    float2 inputUvScaleBias;
    uint encodeSrgb;
    uint reserved;
};
[[vk::push_constant]] DisplayTransferPushConstants gDisplay;
[[vk::binding(0, 0)]] Texture2D<float4> LinearSceneColor;
[[vk::binding(1, 0)]] SamplerState LinearSceneSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    float2 corners[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    VertexOutput output;
    output.position = float4(corners[vertexId], 0, 1);
    output.uv = corners[vertexId] * 0.5 + 0.5;
    output.uv.y = output.uv.y * gDisplay.inputUvScaleBias.x + gDisplay.inputUvScaleBias.y;
    return output;
}

float3 encodeSrgb(float3 linearColor)
{
    float3 c = saturate(linearColor);
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, float3(1.0 / 2.4)) - 0.055);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float4 color = LinearSceneColor.Sample(LinearSceneSampler, input.uv);
    return float4(gDisplay.encodeSrgb != 0 ? encodeSrgb(color.rgb) : saturate(color.rgb), color.a);
}
