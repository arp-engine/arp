// Test adapter for the engine's production material ABI and shared coverage.
// Lighting and production mesh/skin/wind vertex entrypoints stay external.
struct MaterialRecordVertex
{
    float4 position : SV_Position;
};
[shader("vertex")]
MaterialRecordVertex vertexMain(uint id : SV_VertexID)
{
    MaterialRecordVertex output;
    float2 p = id == 0u ? float2(-1.0, -1.0) : (id == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    output.position = float4(p, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 colorMain(MaterialRecordVertex input) : SV_Target0
{
    ArpMaterialGradients gradients = arpCaptureMaterialGradients(input.position.xy);
    uint selected = uint(input.position.x) % 2u;
    ArpBindlessRecord_MaterialRecords material;
    if (!arpLoadPbrMaterial(0u, selected, 1u, 2u, material)) discard;
    ArpBindlessPbrMaterialSample materialSample;
    if (!arpSamplePbrMaterial(material, gradients, float3(0.0, 0.0, 1.0),
        float4(1.0, 0.0, 0.0, 1.0), true, materialSample)) discard;
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    bool fault;
    MaterialResults_store(0u, pixel * 4u, asuint(materialSample.surface.baseColorAlpha.a), fault);
    MaterialResults_store(0u, pixel * 4u + 1u, asuint(materialSample.surface.roughness), fault);
    MaterialResults_store(0u, pixel * 4u + 2u, asuint(materialSample.surface.dielectricF0), fault);
    MaterialResults_store(0u, pixel * 4u + 3u, materialSample.surface.covered ? 1u : 0u, fault);
    if (!materialSample.surface.covered) discard;
    return float4(materialSample.surface.baseColorAlpha.rgb + materialSample.surface.emissive, 1.0);
}

void coverageMain(MaterialRecordVertex input, uint passIndex)
{
    ArpMaterialGradients gradients = arpCaptureMaterialGradients(input.position.xy);
    uint selected = uint(input.position.x) % 2u;
    ArpBindlessRecord_MaterialRecords material;
    if (!arpLoadPbrMaterial(0u, selected, 1u, 2u, material)) discard;
    float alpha;
    bool covered;
    if (!arpSamplePbrCoverage(material, gradients, alpha, covered)) discard;
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    bool fault;
    MaterialResults_store(0u, 16u + passIndex * 8u + pixel * 2u, asuint(alpha), fault);
    MaterialResults_store(0u, 17u + passIndex * 8u + pixel * 2u, covered ? 1u : 0u, fault);
    if (!covered) discard;
}
[shader("fragment")]
void depthMain(MaterialRecordVertex input) { coverageMain(input, 0u); }
[shader("fragment")]
void shadowMain(MaterialRecordVertex input) { coverageMain(input, 1u); }
