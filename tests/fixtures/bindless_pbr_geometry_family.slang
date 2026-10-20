// Numeric workload composed with the shared production geometry family.
[shader("fragment")]
float4 familyColorMain(FamilyVertex input, bool frontFacing : SV_IsFrontFace) : SV_Target0
{
    ArpMaterialGradients gradients = arpCaptureMaterialGradients(input.uv);
    ArpBindlessRecord_MaterialRecords material;
    if (!arpLoadPbrMaterial(0u, input.material, 1u, 2u, material)) discard;
    ArpBindlessPbrMaterialSample sampled;
    if (!arpSamplePbrMaterial(material, gradients, input.normal, input.tangent, frontFacing, sampled)) discard;
    if (!sampled.surface.covered) discard;
    bool fault;
    uint available = MaterialResults_query(0u, fault);
    uint4 extent = GeometryFrame.outputExtent;
    if (fault || extent.x == 0u || extent.y == 0u || extent.z != 23u || extent.w != 1u
        || extent.x > 0xffffffffu / extent.y || extent.x * extent.y > available / 23u
        || any(input.position.xy < 0.0) || any(input.position.xy >= float2(extent.xy))) {
        arpBindlessReportFault(); discard;
    }
    uint pixel = uint(input.position.y) * extent.x + uint(input.position.x);
    uint first = pixel * 23u;
    // Independent lanes per pixel retain actual deformation/temporal/metadata.
    for (uint lane = 0u; lane < 3u; ++lane) {
        MaterialResults_store(0u, first + lane, asuint(input.world[lane]), fault);
        MaterialResults_store(0u, first + 3u + lane, asuint(sampled.normal[lane]), fault);
    }
    for (uint lane = 0u; lane < 4u; ++lane) {
        MaterialResults_store(0u, first + 6u + lane, asuint(input.currentClip[lane]), fault);
        MaterialResults_store(0u, first + 10u + lane, asuint(input.previousClip[lane]), fault);
        MaterialResults_store(0u, first + 14u + lane, asuint(input.shadowClip[lane]), fault);
        MaterialResults_store(0u, first + 18u + lane, input.metadata[lane], fault);
    }
    MaterialResults_store(0u, first + 22u, asuint(input.lodTime), fault);
    return float4(sampled.surface.baseColorAlpha.rgb + sampled.surface.emissive, 1.0);
}
void familyCoverage(FamilyVertex input)
{
    ArpMaterialGradients gradients = arpCaptureMaterialGradients(input.uv);
    ArpBindlessRecord_MaterialRecords material;
    if (!arpLoadPbrMaterial(0u, input.material, 1u, 2u, material)) discard;
    float alpha;
    bool covered;
    if (!arpSamplePbrCoverage(material, gradients, alpha, covered) || !covered) discard;
}
[shader("fragment")]
void familyDepthMain(FamilyVertex input) { familyCoverage(input); }
[shader("fragment")]
void familyShadowMain(FamilyVertex input) { familyCoverage(input); }
