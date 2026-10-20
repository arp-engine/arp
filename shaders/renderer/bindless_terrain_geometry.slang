// TerrainGeometry/1 composes GeometryFamily/1 and TerrainMaterial/1.
// The authored terrain mesh already contains world positions. Color/depth
// use exactly the same projection; shadow selects the declared light matrix.
struct ArpTerrainGeometryParameters
{
    float4 foamOriginExtent;
    float4 foamParameters;
    float4 weather; // rain, reserved zero
    uint4 pages; // page-buffer index, count, visit budget, foam-buffer index
};
[[vk::binding(3, 0)]] ConstantBuffer<ArpTerrainGeometryParameters> TerrainGeometryParameters;

FamilyVertex arpTerrainProject(FamilyInput input, bool shadow)
{
    return familyProject(arpPbrWorldGeometry(input.position, input.normal, input.tangent), input.uv, shadow);
}
[shader("vertex")]
FamilyVertex terrainColorVertex(FamilyInput input) { return arpTerrainProject(input, false); }
[shader("vertex")]
FamilyVertex terrainDepthVertex(FamilyInput input) { return arpTerrainProject(input, false); }
[shader("vertex")]
FamilyVertex terrainShadowVertex(FamilyInput input) { return arpTerrainProject(input, true); }

bool arpTerrainGeometryCoverage(FamilyVertex input, ArpTerrainGradients gradients,
    out ArpBindlessRecord_TerrainRecords material, out float4 pageA, out float4 pageB)
{
    pageA = 0.0;
    pageB = 0.0;
    material = (ArpBindlessRecord_TerrainRecords)0;
    if (!arpLoadTerrainMaterial(0u, input.material, 1u, 2u, 3u, material)) return false;
    bool covered;
    uint4 pages = TerrainGeometryParameters.pages;
    if (!arpSampleTerrainWeightPages(material, pages.x, pages.y, pages.z, gradients, pageA, pageB, covered)) return false;
    return covered;
}

bool arpTerrainGeometrySurface(FamilyVertex input, ArpTerrainGradients gradients,
    out ArpBindlessTerrainSurface surface)
{
    surface = (ArpBindlessTerrainSurface)0;
    ArpBindlessRecord_TerrainRecords material;
    float4 pageA, pageB;
    if (!arpTerrainGeometryCoverage(input, gradients, material, pageA, pageB)) return false;
    float shoreWetness;
    if (!arpTerrainShoreWetness(TerrainGeometryParameters.pages.w,
        TerrainGeometryParameters.foamOriginExtent, TerrainGeometryParameters.foamParameters,
        input.world, shoreWetness)) return false;
    return arpSampleTerrainSurface(material, gradients, input.normal, input.tangent,
        pageA, pageB, TerrainGeometryParameters.weather.x, shoreWetness, surface);
}
void arpTerrainGeometryDepth(FamilyVertex input)
{
    // Derivatives are evaluated before the material/page selectors and discard.
    ArpTerrainGradients gradients = arpCaptureTerrainGradients(input.world);
    ArpBindlessRecord_TerrainRecords material;
    float4 pageA, pageB;
    if (!arpTerrainGeometryCoverage(input, gradients, material, pageA, pageB)) discard;
}
[shader("fragment")]
void terrainDepthMain(FamilyVertex input) { arpTerrainGeometryDepth(input); }
[shader("fragment")]
void terrainShadowMain(FamilyVertex input) { arpTerrainGeometryDepth(input); }
