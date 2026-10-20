// Independent terrain-weight-page reference; no generated bindless helpers.
[[vk::binding(0, 0)]] Texture2DArray<float4> Image0;
[[vk::binding(1, 0)]] Texture2DArray<float4> Image1;
[[vk::binding(2, 0)]] SamplerState Nearest;
[[vk::binding(3, 0)]] SamplerState Linear;

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position {
    float2 p = vertex == 0u ? float2(-1, -1) : vertex == 1u ? float2(3, -1) : float2(-1, 3);
    return float4(p, 0, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0 {
    uint layer = uint(position.y);
    float4 implicit0 = Image0.Sample(Linear, float3(position.xy, layer));
    float4 implicit1 = Image1.Sample(Nearest, float3(position.xy, layer));
    float4 color0 = float4(Image0.Load(int4(0, 0, layer, 0)).r,
        Image0.SampleLevel(Linear, float3(0.5, 0.5, layer), 0.0).r, implicit0.r, 1);
    float4 color1 = float4(Image1.Load(int4(0, 0, layer, 0)).r,
        Image1.SampleLevel(Nearest, float3(0.5, 0.5, layer), 0.0).r, implicit1.r, 1);
    if (uint(position.x) == 1u && layer == 0u) discard;
    return uint(position.x) % 2u == 0u ? color1 : color0;
}
