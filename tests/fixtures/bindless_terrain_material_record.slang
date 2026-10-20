// Adapter exercises the actual engine TerrainMaterial/1 library. Production
// geometry and frame lighting are separate families, not inferred from this.
struct TerrainRecordVertex
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
};
[shader("vertex")]
TerrainRecordVertex vertexMain(uint id : SV_VertexID)
{
    TerrainRecordVertex output;
    float2 p = id == 0u ? float2(-1.0, -1.0) : id == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0);
    output.position = float4(p, 0.0, 1.0);
    output.worldPosition = float3(p.x, 0.25, p.y);
    return output;
}

bool terrainCoverage(TerrainRecordVertex input, ArpTerrainGradients gradients,
    out ArpBindlessRecord_TerrainRecords material, out float4 pageA, out float4 pageB)
{
    pageA = 0.0;
    pageB = 0.0;
    bool covered;
    // A two-material non-uniform selector exercises record indirection.
    if (!arpLoadTerrainMaterial(0u, uint(input.position.x) % 2u, 1u, 2u, 3u, material)) return false;
    if (!arpSampleTerrainWeightPages(material, 0u, 2u, 2u, gradients, pageA, pageB, covered)) return false;
    return covered;
}

[shader("fragment")]
float4 colorMain(TerrainRecordVertex input) : SV_Target0
{
    ArpTerrainGradients gradients = arpCaptureTerrainGradients(input.worldPosition);
    ArpBindlessRecord_TerrainRecords material;
    float4 pageA, pageB;
    if (!terrainCoverage(input, gradients, material, pageA, pageB)) discard;
    float shoreWetness;
    if (!arpTerrainShoreWetness(0u, float4(0.5, 0.5, 0.5, 0.5),
        float4(0.0, 0.75, 1.0, 2.0), input.worldPosition, shoreWetness)) discard;
    ArpBindlessTerrainSurface surface;
    if (!arpSampleTerrainSurface(material, gradients, float3(0.8, 0.6, 0.0),
        float4(0.6, -0.8, 0.0, 1.0), pageA, pageB, 0.3, shoreWetness, surface)) discard;
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    bool fault;
    TerrainResults_store(0u, pixel * 8u, asuint(surface.roughness), fault);
    TerrainResults_store(0u, pixel * 8u + 1u, asuint(surface.occlusion), fault);
    TerrainResults_store(0u, pixel * 8u + 2u, asuint(surface.normal.x), fault);
    TerrainResults_store(0u, pixel * 8u + 3u, asuint(surface.normal.y), fault);
    TerrainResults_store(0u, pixel * 8u + 4u, asuint(surface.normal.z), fault);
    TerrainResults_store(0u, pixel * 8u + 5u, asuint(surface.emissive.x), fault);
    TerrainResults_store(0u, pixel * 8u + 6u, asuint(surface.emissive.y), fault);
    TerrainResults_store(0u, pixel * 8u + 7u, asuint(surface.emissive.z), fault);
    return float4(surface.baseColor + surface.emissive, 1.0);
}

void terrainDepthCoverage(TerrainRecordVertex input, uint passIndex)
{
    ArpTerrainGradients gradients = arpCaptureTerrainGradients(input.worldPosition);
    ArpBindlessRecord_TerrainRecords material;
    float4 pageA, pageB;
    if (!terrainCoverage(input, gradients, material, pageA, pageB)) discard;
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    bool fault;
    TerrainResults_store(0u, 32u + passIndex * 4u + pixel, 1u, fault);
}
[shader("fragment")]
void depthMain(TerrainRecordVertex input) { terrainDepthCoverage(input, 0u); }
[shader("fragment")]
void shadowMain(TerrainRecordVertex input) { terrainDepthCoverage(input, 1u); }
