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

float4 materialFragment(MaterialVertex input, bool injectFault)
{
    // These must dominate all non-uniform resource/material dispatch and discard.
    float2 uv = input.position.xy;
    float2 gradientX = ddx(uv);
    float2 gradientY = ddy(uv);
    float2 probeUV = 0.5 + input.position.xy * 0.125;
    float2 probeGradientX = ddx(probeUV);
    float2 probeGradientY = ddy(probeUV);
    bool fault;
    float4 nearestProbe = Images_sampleGrad(0u, 0u, probeUV, probeGradientX, probeGradientY, fault);
    float4 linearProbe = Images_sampleGrad(0u, 1u, probeUV, probeGradientX, probeGradientY, fault);
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    uint selected = Selection.x + uint(input.position.x) % Selection.y;
    MaterialProbeRecord material = Materials[selected];
    uint sampler = material.occlusionSampler.y;
    float4 base = Images_sampleGrad(material.textures.x, sampler, uv, gradientX, gradientY, fault);
    float4 normal = Images_sampleGrad(material.textures.y, sampler, uv, gradientX, gradientY, fault);
    float4 mr = Images_sampleGrad(material.textures.z, sampler, uv, gradientX, gradientY, fault);
    float4 emissive = Images_sampleGrad(material.textures.w, sampler, uv, gradientX, gradientY, fault);
    float4 ao = Images_sampleGrad(material.occlusionSampler.x, sampler, uv, gradientX, gradientY, fault);
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
            Results_store(0u, pixel * 32u + v * 4u + c, asuint(lanes[v][c]), fault);
    if (injectFault) {
        float4 rejected = Images_sampleGrad(10u, sampler, uv, gradientX, gradientY, fault);
        float rejectedCheck = (fault ? 1.0 : -1.0) + dot(abs(rejected), float4(1.0));
        Results_store(0u, pixel * 32u + 31u, asuint(rejectedCheck), fault);
        Results_store(1u, 0u, 0xffffffffu, fault);
        Results_store(0u, 128u, 0xffffffffu, fault);
    }
    if (Selection.z != 0u && !surface.covered) discard;
    return float4(0.25, 0.5, 0.75, 1.0);
}
[shader("fragment")]
float4 cleanFragment(MaterialVertex input) : SV_Target0 { return materialFragment(input, false); }
[shader("fragment")]
float4 faultFragment(MaterialVertex input) : SV_Target0 { return materialFragment(input, true); }
