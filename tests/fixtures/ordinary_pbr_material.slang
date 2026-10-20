#include "core/leaf_surface.slang"
#include "core/pbr_material.slang"
// Independent direct-bound Traditional sampling oracle; no bindless helpers.
// Explicit libraries: core/leaf_surface.slang, core/pbr_material.slang.
// Test transport of existing material lanes, not a production GPU table ABI.
struct MaterialProbeRecord
{
    float4 baseColorAlpha;
    float4 pbrParams;
    float4 pbrExtraParams;
    float4 environmentParams;
    uint4 textures;
    uint4 occlusionSampler;
};
[[vk::binding(0, 0)]] StructuredBuffer<MaterialProbeRecord> Materials;
[[vk::binding(1, 0)]] ConstantBuffer<uint4> Selection;

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
[[vk::binding(10, 1)]] SamplerState Nearest;
[[vk::binding(11, 1)]] SamplerState Linear;
[[vk::binding(12, 1)]] RWStructuredBuffer<uint> ProbeResults;

struct MaterialVertex
{
    float4 position : SV_Position;
    nointerpolation float4 vertexMaterial : TEXCOORD0;
};
[shader("vertex")]
MaterialVertex vertexMain(uint vertexId : SV_VertexID)
{
    MaterialVertex result;
    float2 p = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    result.position = float4(p, 0.0, 1.0);
    result.vertexMaterial = Materials[Selection.x].baseColorAlpha;
    return result;
}

[shader("fragment")]
float4 fragmentMain(MaterialVertex input) : SV_Target0
{
    // These must dominate all non-uniform resource/material dispatch and discard.
    float2 uv = input.position.xy;
    float2 gradientX = ddx(uv);
    float2 gradientY = ddy(uv);
    float2 probeUV = 0.5 + input.position.xy * 0.125;
    float2 probeGradientX = ddx(probeUV);
    float2 probeGradientY = ddy(probeUV);
    float4 nearestProbe = Image0.Sample(Nearest, probeUV);
    float4 linearProbe = Image0.Sample(Linear, probeUV);
    float4 direct[10] = {
        Image0.Sample(Nearest, uv),
        Image1.Sample(Nearest, uv),
        Image2.Sample(Nearest, uv),
        Image3.Sample(Nearest, uv),
        Image4.Sample(Nearest, uv),
        Image5.Sample(Linear, uv),
        Image6.Sample(Linear, uv),
        Image7.Sample(Linear, uv),
        Image8.Sample(Linear, uv),
        Image9.Sample(Linear, uv)
    };
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    uint selected = Selection.x + uint(input.position.x) % Selection.y;
    MaterialProbeRecord material = Materials[selected];
    uint sampler = material.occlusionSampler.y;
    float4 base = direct[material.textures.x];
    float4 normal = direct[material.textures.y];
    float4 mr = direct[material.textures.z];
    float4 emissive = direct[material.textures.w];
    float4 ao = direct[material.occlusionSampler.x];
    ArpPbrMaterialSurface surface = arpEvaluatePbrMaterial(material.baseColorAlpha,
        material.pbrParams, material.pbrExtraParams, material.environmentParams,
        base, mr, emissive, ao);
    bool frontFacing = input.position.y < 1.0;
    float3 mappedNormal = arpPbrMaterialNormal(float3(0.0, 0.0, 2.0),
        float4(2.0, 0.0, 1.0, -1.0), frontFacing, material.pbrExtraParams.z, normal.xyz);
    float4 lanes[8] = {
        input.vertexMaterial,
        surface.baseColorAlpha,
        float4(surface.metallic, surface.roughness, surface.occlusion, surface.covered ? 1.0 : 0.0),
        float4(surface.emissive, surface.dielectricF0),
        float4(surface.translucentColor, surface.leafWeight),
        float4(mappedNormal, frontFacing ? 1.0 : 0.0),
        float4(nearestProbe.r, linearProbe.r, nearestProbe.g, linearProbe.g),
        float4(float(selected), float(sampler), gradientX.x, gradientY.y)
    };
    for (uint v = 0u; v < 8u; ++v)
        for (uint c = 0u; c < 4u; ++c)
            ProbeResults[pixel * 32u + v * 4u + c] = asuint(lanes[v][c]);
    if (Selection.z != 0u && !surface.covered) discard;
    return float4(0.25, 0.5, 0.75, 1.0);
}
