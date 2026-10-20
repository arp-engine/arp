// Numeric surface oracle only; scene lighting/GI is composed by renderer.
[shader("fragment")]
float4 terrainColorMain(FamilyVertex input) : SV_Target0
{
    ArpTerrainGradients gradients = arpCaptureTerrainGradients(input.world);
    ArpBindlessTerrainSurface surface;
    if (!arpTerrainGeometrySurface(input, gradients, surface)) discard;
    uint4 extent = GeometryFrame.outputExtent;
    bool fault;
    uint count = TerrainResults_query(0u, fault);
    if (fault || any(extent.xy == 0u) || extent.z != 23u || extent.w != 1u
        || extent.x > 0xffffffffu / extent.y || extent.x * extent.y > count / 23u
        || any(input.position.xy < 0.0) || any(input.position.xy >= float2(extent.xy))) {
        arpBindlessReportFault(); discard;
    }
    uint first = (uint(input.position.y) * extent.x + uint(input.position.x)) * 23u;
    for (uint i = 0u; i < 3u; ++i) {
        TerrainResults_store(0u, first + i, asuint(input.world[i]), fault);
        TerrainResults_store(0u, first + 3u + i, asuint(surface.normal[i]), fault);
        TerrainResults_store(0u, first + 6u + i, asuint(surface.emissive[i]), fault);
    }
    TerrainResults_store(0u, first + 9u, asuint(surface.roughness), fault);
    TerrainResults_store(0u, first + 10u, asuint(surface.occlusion), fault);
    for (uint i = 0u; i < 4u; ++i) {
        TerrainResults_store(0u, first + 11u + i, asuint(input.currentClip[i]), fault);
        TerrainResults_store(0u, first + 15u + i, asuint(input.previousClip[i]), fault);
        TerrainResults_store(0u, first + 19u + i, asuint(input.shadowClip[i]), fault);
    }
    return float4(surface.baseColor + surface.emissive, 1.0);
}
