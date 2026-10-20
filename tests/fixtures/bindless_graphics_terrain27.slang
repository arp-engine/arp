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
float4 terrainFragment(TerrainVertex input, bool injectFault)
{
    float2 uv = input.position.xy;
    float2 dx = ddx(uv), dy = ddy(uv);
    uint layer = uint(input.position.y);
    uint selected = Selection.x + uint(input.position.x) % Selection.y;
    TerrainProbeRecord m = Materials[selected];
    bool fault;
    float4 a = Weights_sampleGrad(0u, 0u, uv, layer, dx, dy, fault);
    float4 b = Weights_sampleGrad(1u, 0u, uv, layer, dx, dy, fault);
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
        albedo += Surfaces_sampleGrad(base, 0u, uv, dx, dy, fault).rgb * w[i];
        normal += (Surfaces_sampleGrad(base + 1u, 0u, uv, dx, dy, fault).xyz * 2.0 - 1.0) * w[i];
        roughness += Surfaces_sampleGrad(base + 2u, 0u, uv, dx, dy, fault).r * w[i];
        height += Surfaces_sampleGrad(base + 3u, 0u, uv, dx, dy, fault).r * w[i];
    }
    float macro = Surfaces_sampleGrad(24u, 0u, uv, dx, dy, fault).r;
    float4 lanes[8] = {a, b, float4(w[0], w[1], w[2], w[3]),
        float4(w[4], w[5], snow, wet), float4(albedo, macro),
        float4(normal, roughness), float4(height, input.vertexMaterial.xyz),
        float4(float(selected), float(layer), dx.x, dy.y)};
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    for (uint v = 0u; v < 8u; ++v)
        for (uint c = 0u; c < 4u; ++c)
            Results_store(0u, pixel * 32u + v * 4u + c, asuint(lanes[v][c]), fault);
    if (injectFault) {
        float4 rejected = Surfaces_sampleGrad(25u, 0u, uv, dx, dy, fault);
        bool refused = fault && all(rejected == 0.0);
        rejected = Weights_sampleGrad(1u, 0u, uv, 2u, dx, dy, fault);
        refused = refused && fault && all(rejected == 0.0);
        Results_store(0u, pixel * 32u + 31u, asuint(refused ? 1.0 : -1.0), fault);
        Results_store(1u, 0u, 0xffffffffu, fault);
        Results_store(0u, 128u, 0xffffffffu, fault);
    }
    return float4(0.25, 0.5, 0.75, 1.0);
}
[shader("fragment")]
float4 cleanFragment(TerrainVertex input) : SV_Target0 { return terrainFragment(input, false); }
[shader("fragment")]
float4 faultFragment(TerrainVertex input) : SV_Target0 { return terrainFragment(input, true); }
