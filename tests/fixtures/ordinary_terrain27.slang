// Independent Traditional implicit sampling reference; no bindless helpers.
[[vk::binding(0, 1)]] Texture2D<float4> Image0;
[[vk::binding(1, 1)]] Texture2D<float4> Image1;
[[vk::binding(2, 1)]] Texture2D<float4> Image2;
[[vk::binding(3, 1)]] Texture2D<float4> Image3;
[[vk::binding(4, 1)]] Texture2D<float4> Image4;
[[vk::binding(5, 1)]] Texture2D<float4> Image5;
[[vk::binding(6, 1)]] Texture2D<float4> Image6;
[[vk::binding(7, 1)]] Texture2D<float4> Image7;
[[vk::binding(8, 1)]] Texture2D<float4> Image8;
[[vk::binding(9, 1)]] Texture2D<float4> Image9;
[[vk::binding(10, 1)]] Texture2D<float4> Image10;
[[vk::binding(11, 1)]] Texture2D<float4> Image11;
[[vk::binding(12, 1)]] Texture2D<float4> Image12;
[[vk::binding(13, 1)]] Texture2D<float4> Image13;
[[vk::binding(14, 1)]] Texture2D<float4> Image14;
[[vk::binding(15, 1)]] Texture2D<float4> Image15;
[[vk::binding(16, 1)]] Texture2D<float4> Image16;
[[vk::binding(17, 1)]] Texture2D<float4> Image17;
[[vk::binding(18, 1)]] Texture2D<float4> Image18;
[[vk::binding(19, 1)]] Texture2D<float4> Image19;
[[vk::binding(20, 1)]] Texture2D<float4> Image20;
[[vk::binding(21, 1)]] Texture2D<float4> Image21;
[[vk::binding(22, 1)]] Texture2D<float4> Image22;
[[vk::binding(23, 1)]] Texture2D<float4> Image23;
[[vk::binding(24, 1)]] Texture2D<float4> Image24;
[[vk::binding(25, 1)]] Texture2DArray<float4> WeightA;
[[vk::binding(26, 1)]] Texture2DArray<float4> WeightB;
[[vk::binding(27, 1)]] SamplerState Filter;
[[vk::binding(28, 1)]] RWStructuredBuffer<uint> ProbeResults;
// Test-only resource/weight substrate. Complete terrain lighting is separate.
struct TerrainProbeRecord
{
    float4 heightSnowWet;
    uint4 selectors;
};
[[vk::binding(0, 0)]] StructuredBuffer<TerrainProbeRecord> Materials;
[[vk::binding(1, 0)]] ConstantBuffer<uint4> Selection;

struct TerrainVertex
{
    float4 position : SV_Position;
    nointerpolation float4 vertexMaterial : TEXCOORD0;
};
[shader("vertex")]
TerrainVertex vertexMain(uint vertexId : SV_VertexID)
{
    TerrainVertex output;
    float2 p = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    output.position = float4(p, 0.0, 1.0);
    output.vertexMaterial = Materials[Selection.x].heightSnowWet;
    return output;
}
[shader("fragment")]
float4 fragmentMain(TerrainVertex input) : SV_Target0
{
    float2 uv = input.position.xy;
    float2 dx = ddx(uv), dy = ddy(uv);
    uint layer = uint(input.position.y);
    uint selected = Selection.x + uint(input.position.x) % Selection.y;
    TerrainProbeRecord m = Materials[selected];
    // Every implicit sample dominates non-uniform material dispatch. The
    // reference samples all direct bindings before selecting sampled values.
    float4 sample0 = Image0.Sample(Filter, uv);
    float4 sample1 = Image1.Sample(Filter, uv);
    float4 sample2 = Image2.Sample(Filter, uv);
    float4 sample3 = Image3.Sample(Filter, uv);
    float4 sample4 = Image4.Sample(Filter, uv);
    float4 sample5 = Image5.Sample(Filter, uv);
    float4 sample6 = Image6.Sample(Filter, uv);
    float4 sample7 = Image7.Sample(Filter, uv);
    float4 sample8 = Image8.Sample(Filter, uv);
    float4 sample9 = Image9.Sample(Filter, uv);
    float4 sample10 = Image10.Sample(Filter, uv);
    float4 sample11 = Image11.Sample(Filter, uv);
    float4 sample12 = Image12.Sample(Filter, uv);
    float4 sample13 = Image13.Sample(Filter, uv);
    float4 sample14 = Image14.Sample(Filter, uv);
    float4 sample15 = Image15.Sample(Filter, uv);
    float4 sample16 = Image16.Sample(Filter, uv);
    float4 sample17 = Image17.Sample(Filter, uv);
    float4 sample18 = Image18.Sample(Filter, uv);
    float4 sample19 = Image19.Sample(Filter, uv);
    float4 sample20 = Image20.Sample(Filter, uv);
    float4 sample21 = Image21.Sample(Filter, uv);
    float4 sample22 = Image22.Sample(Filter, uv);
    float4 sample23 = Image23.Sample(Filter, uv);
    float4 sample24 = Image24.Sample(Filter, uv);
    float4 a = WeightA.Sample(Filter, float3(uv, layer));
    float4 b = WeightB.Sample(Filter, float3(uv, layer));
    float4 direct[25] = {sample0, sample1, sample2, sample3, sample4, sample5, sample6, sample7, sample8, sample9, sample10, sample11, sample12, sample13, sample14, sample15, sample16, sample17, sample18, sample19, sample20, sample21, sample22, sample23, sample24};
    float w[6] = {a.r, a.g, a.b, a.a, b.r, b.g};
    float total = 0.0;
    for (uint i = 0u; i < 6u; ++i) total += w[i];
    for (uint i = 0u; i < 6u; ++i) w[i] /= max(total, 0.001);
    float snow = b.b * saturate((m.heightSnowWet.x - m.heightSnowWet.y)
        / max(m.heightSnowWet.z, 0.001));
    float pull = snow * 0.7;
    w[5] += pull; w[0] *= 1.0 - pull; w[4] *= 1.0 - pull * 0.8;
    float wet = b.a * 0.3;
    w[2] += wet; w[0] *= 1.0 - wet * 0.5;
    total = 0.0;
    for (uint i = 0u; i < 6u; ++i) total += w[i];
    for (uint i = 0u; i < 6u; ++i) w[i] /= max(total, 0.001);
    float3 albedo = float3(0.0), normal = float3(0.0);
    float roughness = 0.0, height = 0.0;
    for (uint i = 0u; i < 6u; ++i) {
        uint base = (i * 4u + m.selectors.x) % 24u;
        albedo += direct[base].rgb * w[i];
        normal += (direct[base + 1u].xyz * 2.0 - 1.0) * w[i];
        roughness += direct[base + 2u].r * w[i];
        height += direct[base + 3u].r * w[i];
    }
    float macro = direct[24u].r;
    float4 lanes[8] = {a, b, float4(w[0], w[1], w[2], w[3]),
        float4(w[4], w[5], snow, wet), float4(albedo, macro),
        float4(normal, roughness), float4(height, input.vertexMaterial.xyz),
        float4(float(selected), float(layer), dx.x, dy.y)};
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    for (uint v = 0u; v < 8u; ++v)
        for (uint c = 0u; c < 4u; ++c)
            ProbeResults[pixel * 32u + v * 4u + c] = asuint(lanes[v][c]);
    return float4(0.25, 0.5, 0.75, 1.0);
}
